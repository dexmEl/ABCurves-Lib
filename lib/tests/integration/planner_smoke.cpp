#include "abcurves/planner.hpp"

#include <cmath>
#include <iostream>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    try {
        for (int commit : {32, 16, 8}) {
            auto path = std::filesystem::path(argv[1]).parent_path() / (std::to_string(commit) + "ms");
            auto planner = abc::ContinuousPlanner::load(path, {.initial_position = {}, .seed = 7});
            if (planner->planning_interval_ms() != commit) return 3;
            planner->update_target({120.0, -40.0}, 0);
            std::vector<abc::Sample> samples(1000);
            const auto n = planner->advance(1000000, samples.data(), samples.size());
            if (n != samples.size()) return 4;
            for (const auto& sample : samples) if (!std::isfinite(sample.position.x) || !std::isfinite(sample.position.y)) return 5;
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 6;
    }
    return 0;
}
