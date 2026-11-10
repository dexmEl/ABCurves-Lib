#include <abcurves/dll.hpp>
#include <array>
#include <iostream>

int main() try {
    auto model = abcurves_dll::Model::embedded(16);
    abcurves_dll::Planner planner(model);
    abcurves_dll::ObservationInput input(planner);
    input.relative({120.0, -7.0}, 0, 0);
    std::array<abc_sample, 32> samples{};
    auto count = planner.advance_into(16000, samples);
    for (const auto& sample : std::span(samples).first(count))
        std::cout << sample.timestamp_us << ' ' << sample.displacement.x << ' ' << sample.displacement.y << '\n';
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
