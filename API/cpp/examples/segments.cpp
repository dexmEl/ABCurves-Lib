#include <abcurves/native.hpp>
#include <array>
#include <iostream>
#include <span>
int main(int argc, char** argv) try {
    if (argc != 2) return 2; // Application supplies bundle directory.
    auto model = abc::Model::load(argv[1]);
    abc::PlannerOptions options; // seed7, origin(0,0), no prefix.
    // Optional prepared prefix: options.initial_history.assign(...160 pairs...);
    // For raw164: auto h=abc::prepare_history(raw, current_position);
    // options.initial_position=h.initial_position;
    // options.initial_history.assign(h.history.begin(),h.history.end());
    abc::ContinuousPlanner planner(model, options);
    abc::SegmentController controller(planner); // Borrows planner.
    std::array<abc::Sample,32> storage;
    abc::SegmentMetadata meta;
    controller.submit_observation({{120,-7},0,0,abc::ObservationKind::relative});
    auto n = controller.next_segment(storage, meta);
    for (const auto& s : std::span(storage.data(), n)) {
        std::cout << s.timestamp_us << " " << s.displacement.x << " " << s.displacement.y << "\n";
        if (s.timestamp_us <= meta.start_timestamp_us) return 3;
    }
    // Simulated newer input, recorded while the first commitment is consumed.
    const auto receipt = meta.end_timestamp_us - 1000;
    controller.submit_observation({{12,-6},receipt,receipt,abc::ObservationKind::relative});
    controller.next_segment(storage, meta); // Safe reuse after consumer finished.
    controller.next_segment(storage, meta); // No new input: retain prior target.
    const auto d = controller.diagnostics();
    std::cout << meta.sample_count << ' ' << d.total_segments << '\n';
    return d.total_segments == 3 ? 0 : 4;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
