#include <abcurves/dll.hpp>
#include <array>
#include <iostream>

int main() try {
    auto library = std::make_shared<abcurves_dll::Library>();
    auto model = abcurves_dll::Model::embedded(library, 16);
    auto options = abcurves_dll::Planner::options(library);
    // Optional: initialize the planner from 164 raw movement history samples.
    std::array<abc_vec2, 164> raw_history{};
    auto history = abcurves_dll::prepare_history(library, raw_history, {});
    options.history = history.samples.data();
    options.history_count = history.samples.size();
    abcurves_dll::Planner planner(model, options);
    abcurves_dll::SegmentController controller(planner);
    controller.relative({120.0, -7.0}, 0, 0);

    std::array<abc_sample, 32> samples{};
    abc_segment_metadata metadata{};
    auto count = controller.next_segment_into(samples, metadata);
    for (const auto& sample : std::span(samples).first(count))
        std::cout << sample.timestamp_us << ' ' << sample.displacement.x << ' ' << sample.displacement.y << '\n';
    auto diagnostics = controller.diagnostics();
    (void)diagnostics;

    auto renderer = abcurves_dll::Renderer::embedded(library);
    std::array<abc_report, 256> profile{}; // Replace with your calibrated device profile.
    renderer.set_profile(profile);
    renderer.reset(7);
    std::array<abc_report, 32> reports{};
    renderer.render_into(std::span(samples).first(count), metadata, ABC_COMMON_RADIANS_PER_COUNT, reports);
    // Schedule the reports with your application's device interface.
} catch (const abcurves_dll::Error& error) {
    std::cerr << "ABCurves error " << error.status() << ": " << error.what() << '\n';
    return 1;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
