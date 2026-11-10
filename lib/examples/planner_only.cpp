#include "abcurves/planner.hpp"

#include <iostream>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 2) { std::cerr << "usage: planner_only <model-directory>\n"; return 2; }
    auto planner = abc::ContinuousPlanner::load(argv[1], {.initial_position = {}, .seed = 7});
    planner->update_target({320.0, 120.0}, 0);
    std::vector<abc::Sample> samples(1000);
    planner->advance(1000000, samples.data(), samples.size());
    for (const auto& sample : samples) std::cout << sample.timestamp_us << ',' << sample.position.x << ',' << sample.position.y << '\n';
}
