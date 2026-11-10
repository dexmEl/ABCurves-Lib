#include <abcurves/dll.hpp>
#include <iostream>
#include <array>
#include <atomic>
#include <thread>
#include <string>
#include <cstring>

static void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

static void direct_tests(const std::shared_ptr<abcurves_dll::Library>& library, const char* models, const char* renderer_path) {
    for (auto variant : {8u, 16u, 32u}) {
        auto embedded = abcurves_dll::Model::embedded(library, variant);
        auto external = abcurves_dll::Model::load(library, (std::string(models) + "/" + std::to_string(variant) + "ms").c_str());
        require(std::strcmp(embedded.info().sha256, external.info().sha256) == 0, "model hashes differ");
        abcurves_dll::Planner a(embedded), b(external);
        a.update_target({120, -7}, 0); b.update_target({120, -7}, 0);
        auto aa = a.advance(32000), bb = b.advance(32000);
        require(aa.size() == bb.size() && !aa.empty(), "direct output size mismatch");
        for (size_t i = 0; i < aa.size(); ++i)
            require(aa[i].timestamp_us == bb[i].timestamp_us && aa[i].position.x == bb[i].position.x &&
                    aa[i].position.y == bb[i].position.y && aa[i].displacement.x == bb[i].displacement.x &&
                    aa[i].displacement.y == bb[i].displacement.y, "embedded trajectory mismatch");
        auto as = a.state(), bs = b.state();
        require(as.selected_head == bs.selected_head && as.mode == bs.mode && as.decision_count == bs.decision_count, "planner state mismatch");
        require(a.model_info().commitment_ms == variant, "planner model information mismatch");
    }
    auto model = abcurves_dll::Model::embedded(library, 16);
    {
        abcurves_dll::Planner wrapped(model);
        abc_planner_handle raw{};
        auto options = abcurves_dll::Planner::options(library);
        library->check(library->planner_create(model.native_handle(), &options, &raw));
        try {
            wrapped.update_target({123, -17}, 0);
            library->check(library->planner_update_target(raw, {123, -17}, 0));
            auto samples = wrapped.advance(32000);
            std::array<abc_sample, 32> native{}; uint64_t count{};
            library->check(library->planner_advance(raw, 32000, native.data(), native.size(), &count));
            require(samples.size() == count, "C/C++ sample count mismatch");
            for (size_t i = 0; i < count; ++i)
                require(samples[i].timestamp_us == native[i].timestamp_us && samples[i].position.x == native[i].position.x &&
                        samples[i].position.y == native[i].position.y && samples[i].displacement.x == native[i].displacement.x &&
                        samples[i].displacement.y == native[i].displacement.y, "C/C++ trajectory mismatch");
        } catch (...) { library->planner_destroy(raw); throw; }
        library->check(library->planner_destroy(raw));
    }
    abcurves_dll::Planner planner(model);
    planner.set_position({100, 50});
    auto config = abcurves_dll::ObservationInput::options(library);
    abcurves_dll::ObservationInput input(planner, config);
    auto resolved = input.relative({12, -7}, 0, 0);
    require(resolved.x == 112 && resolved.y == 43, "relative observation mismatch");
    require(input.diagnostics().accepted == 1, "observation diagnostics mismatch");
    require(planner.required_capacity(32000) == 32, "direct required capacity mismatch");
    std::array<abc_sample, 32> output{};
    try { planner.advance_into(32000, std::span(output).first(1)); throw std::runtime_error("short buffer accepted"); }
    catch (const abcurves_dll::OutputError& e) { require(e.status() == ABC_INSUFFICIENT_CAPACITY && e.written() == 0, "wrong output error"); }
    require(planner.advance_into(32000, output) == 32, "direct advance failed");
    input.invalidate(32000, 32000);
    require(input.diagnostics().availability == ABC_TARGET_INVALIDATED, "invalidation failed");
    planner.reset(42);
    planner.update_target({5, 6}, 0);
    require(planner.generate_sample(1000).timestamp_us == 1000, "single sample failed");
    planner.clear_target(1000);
    auto options = abcurves_dll::Planner::options(library);
    planner.reinitialize(options);
    require(planner.state().samples_emitted == 0, "reinitialization failed");
    auto moved_input = std::move(input);
    moved_input.absolute({1, 2}, 0, 0);
    auto embedded_renderer = abcurves_dll::Renderer::embedded(library);
    auto external_renderer = abcurves_dll::Renderer::load(library, renderer_path);
    std::array<abc_report, 256> profile{};
    embedded_renderer.set_profile(profile); external_renderer.set_profile(profile);
    embedded_renderer.reset(7); external_renderer.reset(7);
    std::array<abc_report, 32> a{}, b{};
    abc_vec2 pa{100, 50}, pb = pa;
    auto na = embedded_renderer.render_samples_into(output, pa, ABC_COMMON_RADIANS_PER_COUNT, a);
    auto nb = external_renderer.render_samples_into(output, pb, ABC_COMMON_RADIANS_PER_COUNT, b);
    require(na == nb && pa.x == pb.x && pa.y == pb.y, "Renderer output size or anchor mismatch");
    for (size_t i = 0; i < na; ++i) require(a[i].dx == b[i].dx && a[i].dy == b[i].dy, "Renderer reports differ");
    auto moved_renderer = std::move(embedded_renderer);
    moved_renderer = std::move(external_renderer);
    auto child = [&] { abcurves_dll::Planner parent(model); return abcurves_dll::ObservationInput(parent); }();
    child.relative({1, 2}, 0, 0);
    abcurves_dll::Planner p1(model), p2(model);
    abcurves_dll::SegmentController c1(p1), c2(p2);
    c1 = std::move(c2);
    c1.relative({1, 2}, 0, 0);
    require(!c1.next_segment().samples.empty(), "moved controller failed");
    try { abcurves_dll::Library missing(L"Z:\\abcurves-no-such-file.dll"); throw std::runtime_error("missing DLL accepted"); }
    catch (const abcurves_dll::Error&) { throw; }
    catch (const std::runtime_error& e) { require(std::string(e.what()).find("Cannot load") != std::string::npos, "missing DLL error incorrect"); }
}

int main(int argc, char** argv) {
    try {
        require(argc == 3, "model and Renderer paths required");
        auto library = std::make_shared<abcurves_dll::Library>();
        direct_tests(library, argv[1], argv[2]);
        bool missing_export_rejected = false;
        try { (void)library->resolve<decltype(&abc_check_abi)>("abc_missing_test_export"); }
        catch (const std::runtime_error&) { missing_export_rejected = true; }
        if (!missing_export_rejected) throw std::runtime_error("missing export accepted");
        if (library->check_abi(99, 0) == ABC_OK) throw std::runtime_error("incompatible ABI accepted");
        for (auto variant : {8u, 16u, 32u}) {
            auto candidate = abcurves_dll::Model::embedded(library, variant);
            if (candidate.info().commitment_ms != variant) throw std::runtime_error("wrong model variant");
        }
        bool invalid_rejected = false;
        try { (void)abcurves_dll::Model::embedded(library, 12); }
        catch (const abcurves_dll::Error& error) { invalid_rejected = error.status() == ABC_INVALID_ARGUMENT; }
        if (!invalid_rejected) throw std::runtime_error("invalid model variant accepted");
        std::atomic<bool> thread_failed{false};
        auto independent = [&] {
            try {
                auto m = abcurves_dll::Model::embedded(library, 8);
                abcurves_dll::Planner p(m);
                abcurves_dll::SegmentController c(p);
                c.relative({10, 5}, 0, 0);
                if (c.next_segment().samples.empty()) thread_failed = true;
            } catch (...) { thread_failed = true; }
        };
        std::thread first(independent), second(independent);
        first.join(); second.join();
        if (thread_failed) throw std::runtime_error("independent planner test failed");
        auto model = abcurves_dll::Model::embedded(library, 16);
        auto options = abcurves_dll::Planner::options(library);
        options.seed = 7;
        std::array<abc_vec2, 164> raw{};
        auto history = abcurves_dll::prepare_history(library, raw, {});
        options.history = history.samples.data(); options.history_count = history.samples.size();
        abcurves_dll::Planner planner(model, options);
        auto config = abcurves_dll::SegmentController::options(library);
        abcurves_dll::SegmentController controller(planner, config);
        controller.relative({120.0, -7.0}, 0, 0);
        std::array<abc_sample, 32> samples{};
        abc_segment_metadata metadata{};
        auto written = controller.next_segment_into(samples, metadata);
        if (!written || written > samples.size()) throw std::runtime_error("invalid segment length");
        for (const auto& sample : std::span(samples).first(written))
            std::cout << sample.timestamp_us << ' ' << sample.displacement.x << ' ' << sample.displacement.y << '\n';
        auto renderer = abcurves_dll::Renderer::embedded(library);
        std::array<abc_report, 256> profile{};
        renderer.set_profile(profile); renderer.reset(7);
        std::array<abc_report, 32> reports{};
        if (renderer.render_into(std::span(samples).first(written), metadata, ABC_COMMON_RADIANS_PER_COUNT, reports) != written)
            throw std::runtime_error("invalid Renderer report count");
        controller.unavailable(metadata.end_timestamp_us, metadata.end_timestamp_us);
        (void)controller.diagnostics(); (void)controller.recovery_diagnostics(); (void)controller.state();
        controller.clear_pending(); controller.reset_diagnostics();
        controller.reset();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
