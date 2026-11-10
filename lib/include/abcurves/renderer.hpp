#pragma once

#include "types.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>

namespace abc {

struct Report { std::int16_t dx{0}; std::int16_t dy{0}; };

// Adapter for the existing C99 online Renderer. The profile is prepared once,
// then the renderer state remains persistent across every generated report.
class Renderer final {
public:
    explicit Renderer(const std::filesystem::path& model_path);
    explicit Renderer(std::span<const std::uint8_t> model_bytes);
    ~Renderer();
    Renderer(Renderer&&) noexcept;
    Renderer& operator=(Renderer&&) noexcept;
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void observe_profile(const std::vector<Report>& reports);
    void begin(std::uint64_t seed);
    Report step(Vec2 smooth_counts);
    bool ready() const noexcept { return ready_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool ready_{false};
};

} // namespace abc
