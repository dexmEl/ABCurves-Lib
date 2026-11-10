#include "abcurves/planner.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
double micros(Clock::duration d) { return std::chrono::duration<double, std::micro>(d).count(); }
double percentile(std::vector<double> values, double p) {
    std::sort(values.begin(), values.end());
    if (values.empty()) return 0.0;
    const double index = p * static_cast<double>(values.size() - 1);
    const auto lo = static_cast<std::size_t>(index); const auto hi = std::min(lo + 1, values.size() - 1);
    return values[lo] + (values[hi] - values[lo]) * (index - static_cast<double>(lo));
}
double median(const std::vector<double>& v) { return percentile(v, .5); }
void array_json(std::ostream& out, const std::vector<double>& values) {
    out << '[' << std::setprecision(12);
    for (std::size_t i = 0; i < values.size(); ++i) { if (i) out << ','; out << values[i]; }
    out << ']';
}
}

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    const std::filesystem::path model_root = argv[1];
    const std::filesystem::path output = argv[2];
    std::filesystem::create_directories(output.parent_path());
    std::ofstream json(output);
    if (!json) return 3;
    json << "{\n  \"schema\": \"abcurves.native_benchmark.v1\",\n  \"variants\": {\n";
    for (int variant_index = 0; variant_index < 3; ++variant_index) {
        const int commit = variant_index == 0 ? 32 : variant_index == 1 ? 16 : 8;
        const auto directory = model_root / (std::to_string(commit) + "ms");
        std::vector<double> startup, fresh, buffered;
        for (int rep = 0; rep < 25; ++rep) {
            const auto begin = Clock::now();
            auto planner = abc::ContinuousPlanner::load(directory, {.initial_position = {}, .seed = static_cast<std::uint64_t>(rep + 7)});
            startup.push_back(micros(Clock::now() - begin));
            planner->update_target({320.0, 120.0}, 0);
            std::vector<abc::Sample> warm(2000);
            planner->advance(2000000, warm.data(), warm.size());
            planner->reset(static_cast<std::uint64_t>(rep + 7));
            planner->update_target({320.0, 120.0}, 0);
            const auto fresh_begin = Clock::now();
            abc::Sample first{};
            planner->advance(1000, &first, 1);
            fresh.push_back(micros(Clock::now() - fresh_begin));
            abc::Sample sample{};
            for (int i = 0; i < 1000; ++i) {
                const auto b = Clock::now();
                planner->advance(static_cast<std::int64_t>(i + 2) * 1000, &sample, 1);
                buffered.push_back(micros(Clock::now() - b));
            }
        }
        auto trace = abc::ContinuousPlanner::load(directory, {.initial_position = {}, .seed = 7});
        for (int event = 0; event < 16; ++event) {
            const double phase = static_cast<double>(event) * 0.9;
            trace->update_target({320.0 * std::cos(phase), 220.0 * std::sin(phase)}, static_cast<std::int64_t>(event) * 500000);
        }
        std::vector<abc::Sample> samples(8000);
        const auto trace_begin = Clock::now(); trace->advance(8000000, samples.data(), samples.size());
        const double trace_ms = std::chrono::duration<double, std::milli>(Clock::now() - trace_begin).count();
        if (variant_index) json << ",\n";
        json << "    \"" << commit << "ms\": {\n"
             << "      \"startup_us\": {\"median\": " << median(startup) << ", \"p95\": " << percentile(startup, .95) << "},\n"
             << "      \"fresh_1ms_sample_us\": {\"median\": " << median(fresh) << ", \"p95\": " << percentile(fresh, .95) << "},\n"
             << "      \"buffered_1ms_us\": {\"median\": " << median(buffered) << ", \"p95\": " << percentile(buffered, .95) << "},\n"
             << "      \"cpu_time_per_8s_trace_ms\": " << trace_ms << ",\n"
             << "      \"raw\": {\"startup_us\": "; array_json(json, startup); json << ", \"fresh_1ms_sample_us\": "; array_json(json, fresh); json << ", \"buffered_1ms_us\": "; array_json(json, buffered); json << "}\n    }";
    }
    json << "\n  }\n}\n";
}
