#include "abcurves/renderer.hpp"

extern "C" {
#include "abc_online.h"
}

#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace abc {

struct Renderer::Impl {
    std::vector<std::uint8_t> blob;
    abc_online_model_t model{};
    abc_online_renderer_t renderer{};
    bool begun{false};
};

Renderer::Renderer(const std::filesystem::path& model_path) : impl_(std::make_unique<Impl>()) {
    std::ifstream stream(model_path, std::ios::binary | std::ios::ate);
    if (!stream) throw std::runtime_error("cannot open Renderer model: " + model_path.string());
    const auto size = stream.tellg();
    if (size <= 0) throw std::runtime_error("empty Renderer model");
    impl_->blob.resize(static_cast<std::size_t>(size)); stream.seekg(0);
    stream.read(reinterpret_cast<char*>(impl_->blob.data()), size);
    if (!stream || abc_online_model_init(&impl_->model, impl_->blob.data(), impl_->blob.size()) != ABC_FIXED_OK)
        throw std::runtime_error("Renderer model failed validation");
}

Renderer::Renderer(std::span<const std::uint8_t> model_bytes) : impl_(std::make_unique<Impl>()) {
    impl_->blob.assign(model_bytes.begin(), model_bytes.end());
    if (impl_->blob.empty() || abc_online_model_init(&impl_->model, impl_->blob.data(), impl_->blob.size()) != ABC_FIXED_OK)
        throw std::runtime_error("Renderer model failed validation");
}

Renderer::~Renderer() = default;
Renderer::Renderer(Renderer&&) noexcept = default;
Renderer& Renderer::operator=(Renderer&&) noexcept = default;

void Renderer::observe_profile(const std::vector<Report>& reports) {
    if (reports.size() != 256) throw std::invalid_argument("Renderer profile requires 256 reports");
    if (abc_online_reset(&impl_->renderer, &impl_->model) != ABC_FIXED_OK) throw std::runtime_error("Renderer reset failed");
    for (const auto& report : reports)
        if (abc_online_observe_raw(&impl_->renderer, report.dx, report.dy) != ABC_FIXED_OK)
            throw std::runtime_error("Renderer profile observation failed");
    ready_ = true; impl_->begun = false;
}

void Renderer::begin(std::uint64_t seed) {
    if (!ready_) throw std::logic_error("Renderer profile has not been prepared");
    if (abc_online_begin(&impl_->renderer, seed) != ABC_FIXED_OK) throw std::runtime_error("Renderer begin failed");
    impl_->begun = true;
}

Report Renderer::step(Vec2 smooth_counts) {
    if (!impl_->begun) throw std::logic_error("Renderer has not begun an event");
    if (!std::isfinite(smooth_counts.x) || !std::isfinite(smooth_counts.y)) throw std::invalid_argument("smooth counts must be finite");
    const auto to_q16 = [](double value) -> std::int32_t {
        // Python portable_renderer stages intent at float32 before Q16.
        const double intent = static_cast<double>(static_cast<float>(value)) * 65536.0;
        if (!std::isfinite(intent)) throw std::out_of_range("smooth count exceeds Renderer Q16 range");
        const double base = std::floor(intent), fraction = intent - base;
        const double scaled = base + (fraction > .5 || (fraction == .5 && std::fmod(base, 2.0) != 0.0) ? 1.0 : 0.0);
        if (scaled < static_cast<double>(std::numeric_limits<std::int32_t>::min()) || scaled > static_cast<double>(std::numeric_limits<std::int32_t>::max()))
            throw std::out_of_range("smooth count exceeds Renderer Q16 range");
        return static_cast<std::int32_t>(scaled);
    };
    abc_fixed_report_t report{};
    if (abc_online_step(&impl_->renderer, to_q16(smooth_counts.x), to_q16(smooth_counts.y), &report) != ABC_FIXED_OK)
        throw std::runtime_error("Renderer step failed");
    return {report.dx, report.dy};
}

} // namespace abc
