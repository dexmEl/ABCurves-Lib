#pragma once

// Header-only Windows wrapper over the stable C ABI. No native engine is linked.
#ifndef _WIN32
#error "abcurves/dll.hpp requires Windows"
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <abcurves.h>
#include <memory>
#include <array>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace abcurves_dll {
class Error : public std::runtime_error {
    abc_status status_;
public:
    Error(abc_status status, const char* message) : std::runtime_error(message ? message : "ABCurves error"), status_(status) {}
    abc_status status() const noexcept { return status_; }
};
class OutputError : public Error {
    uint64_t written_;
public:
    OutputError(abc_status status, const char* message, uint64_t written)
        : Error(status, message), written_(written) {}
    uint64_t written() const noexcept { return written_; }
};
class Library {
    HMODULE module_{};
    template<class T> T symbol(const char* name) {
        auto address = GetProcAddress(module_, name);
        if (!address) throw std::runtime_error(std::string("ABCurves DLL missing export: ") + name);
        return reinterpret_cast<T>(address);
    }
public:
    template<class T> T resolve(const char* name) { return symbol<T>(name); }
#define ABC_DLL_MEMBER(name) decltype(&abc_##name) name{}
    ABC_DLL_MEMBER(last_error); ABC_DLL_MEMBER(check_abi);
    ABC_DLL_MEMBER(model_load_embedded); ABC_DLL_MEMBER(model_load); ABC_DLL_MEMBER(model_release);
    ABC_DLL_MEMBER(planner_options_init); ABC_DLL_MEMBER(planner_create); ABC_DLL_MEMBER(planner_destroy);
    ABC_DLL_MEMBER(planner_reset_session); ABC_DLL_MEMBER(segment_options_init); ABC_DLL_MEMBER(segment_create);
    ABC_DLL_MEMBER(segment_destroy); ABC_DLL_MEMBER(segment_submit_observation);
    ABC_DLL_MEMBER(segment_required_capacity); ABC_DLL_MEMBER(segment_next); ABC_DLL_MEMBER(segment_get_diagnostics);
    ABC_DLL_MEMBER(segment_reset_session);
    ABC_DLL_MEMBER(model_get_info); ABC_DLL_MEMBER(model_get_tensor_info);
    ABC_DLL_MEMBER(planner_get_state); ABC_DLL_MEMBER(planner_reinitialize); ABC_DLL_MEMBER(prepare_history);
    ABC_DLL_MEMBER(planner_reset); ABC_DLL_MEMBER(planner_set_position); ABC_DLL_MEMBER(planner_update_target);
    ABC_DLL_MEMBER(planner_clear_target); ABC_DLL_MEMBER(planner_get_model_info);
    ABC_DLL_MEMBER(planner_required_capacity); ABC_DLL_MEMBER(planner_advance); ABC_DLL_MEMBER(planner_generate_sample);
    ABC_DLL_MEMBER(observation_options_init); ABC_DLL_MEMBER(observation_create); ABC_DLL_MEMBER(observation_destroy);
    ABC_DLL_MEMBER(observation_submit); ABC_DLL_MEMBER(observation_get_diagnostics);
    ABC_DLL_MEMBER(segment_get_metadata); ABC_DLL_MEMBER(segment_get_state);
    ABC_DLL_MEMBER(segment_get_recovery_diagnostics); ABC_DLL_MEMBER(segment_clear_pending_observations);
    ABC_DLL_MEMBER(segment_reset_diagnostics);
    ABC_DLL_MEMBER(renderer_create); ABC_DLL_MEMBER(renderer_create_embedded); ABC_DLL_MEMBER(renderer_destroy);
    ABC_DLL_MEMBER(renderer_set_profile); ABC_DLL_MEMBER(renderer_reset); ABC_DLL_MEMBER(renderer_step);
    ABC_DLL_MEMBER(renderer_step_batch); ABC_DLL_MEMBER(renderer_render_samples); ABC_DLL_MEMBER(segment_render);
#undef ABC_DLL_MEMBER
    explicit Library(const wchar_t* path = L"abcurves.dll") {
        module_ = LoadLibraryW(path);
        if (!module_) throw std::runtime_error("Cannot load abcurves.dll; provide its full path or place it beside the executable");
        try {
#define ABC_DLL_LOAD(name) name = symbol<decltype(name)>("abc_" #name)
            ABC_DLL_LOAD(last_error); ABC_DLL_LOAD(check_abi);
            ABC_DLL_LOAD(model_load_embedded); ABC_DLL_LOAD(model_load); ABC_DLL_LOAD(model_release);
            ABC_DLL_LOAD(planner_options_init); ABC_DLL_LOAD(planner_create); ABC_DLL_LOAD(planner_destroy);
            ABC_DLL_LOAD(planner_reset_session); ABC_DLL_LOAD(segment_options_init); ABC_DLL_LOAD(segment_create);
            ABC_DLL_LOAD(segment_destroy); ABC_DLL_LOAD(segment_submit_observation);
            ABC_DLL_LOAD(segment_required_capacity); ABC_DLL_LOAD(segment_next); ABC_DLL_LOAD(segment_get_diagnostics);
            ABC_DLL_LOAD(segment_reset_session);
            ABC_DLL_LOAD(model_get_info); ABC_DLL_LOAD(model_get_tensor_info);
            ABC_DLL_LOAD(planner_get_state); ABC_DLL_LOAD(planner_reinitialize); ABC_DLL_LOAD(prepare_history);
            ABC_DLL_LOAD(planner_reset); ABC_DLL_LOAD(planner_set_position); ABC_DLL_LOAD(planner_update_target);
            ABC_DLL_LOAD(planner_clear_target); ABC_DLL_LOAD(planner_get_model_info);
            ABC_DLL_LOAD(planner_required_capacity); ABC_DLL_LOAD(planner_advance); ABC_DLL_LOAD(planner_generate_sample);
            ABC_DLL_LOAD(observation_options_init); ABC_DLL_LOAD(observation_create); ABC_DLL_LOAD(observation_destroy);
            ABC_DLL_LOAD(observation_submit); ABC_DLL_LOAD(observation_get_diagnostics);
            ABC_DLL_LOAD(segment_get_metadata); ABC_DLL_LOAD(segment_get_state);
            ABC_DLL_LOAD(segment_get_recovery_diagnostics); ABC_DLL_LOAD(segment_clear_pending_observations);
            ABC_DLL_LOAD(segment_reset_diagnostics);
            ABC_DLL_LOAD(renderer_create); ABC_DLL_LOAD(renderer_create_embedded); ABC_DLL_LOAD(renderer_destroy);
            ABC_DLL_LOAD(renderer_set_profile); ABC_DLL_LOAD(renderer_reset); ABC_DLL_LOAD(renderer_step);
            ABC_DLL_LOAD(renderer_step_batch); ABC_DLL_LOAD(renderer_render_samples); ABC_DLL_LOAD(segment_render);
#undef ABC_DLL_LOAD
            check(check_abi(1, 5));
        } catch (...) { FreeLibrary(module_); module_ = nullptr; throw; }
    }
    ~Library() { if (module_) FreeLibrary(module_); }
    Library(const Library&) = delete;
    Library& operator=(const Library&) = delete;
    void check(abc_status status) const { if (status != ABC_OK) throw Error(status, last_error()); }
    void check_output(abc_status status, uint64_t written) const {
        if (status != ABC_OK) throw OutputError(status, last_error(), written);
    }
};

class Model {
    friend class Planner;
    struct State {
        std::shared_ptr<Library> library;
        abc_model_handle handle{};
        ~State() { if (handle) library->model_release(handle); }
    };
    std::shared_ptr<State> state_;
    explicit Model(std::shared_ptr<State> state) : state_(std::move(state)) {}
public:
    static Model embedded(uint32_t milliseconds) { return embedded(std::make_shared<Library>(), milliseconds); }
    static Model embedded(std::shared_ptr<Library> library, uint32_t milliseconds) {
        auto state = std::make_shared<State>(); state->library = std::move(library);
        state->library->check(state->library->model_load_embedded(milliseconds, &state->handle));
        return Model(std::move(state));
    }
    static Model load(std::shared_ptr<Library> library, const char* directory) {
        auto state = std::make_shared<State>(); state->library = std::move(library);
        state->library->check(state->library->model_load(directory, &state->handle));
        return Model(std::move(state));
    }
    abc_model_info info() const {
        abc_model_info result{}; result.struct_size = sizeof(result);
        state_->library->check(state_->library->model_get_info(state_->handle, &result)); return result;
    }
    abc_tensor_info tensor_info(const char* name) const {
        abc_tensor_info result{}; result.struct_size = sizeof(result);
        state_->library->check(state_->library->model_get_tensor_info(state_->handle, name, &result)); return result;
    }
    abc_model_handle native_handle() const noexcept { return state_->handle; }
};

struct PreparedHistory { std::array<abc_vec2, 160> samples{}; abc_vec2 filtered_anchor{}; };
inline PreparedHistory prepare_history(const std::shared_ptr<Library>& library,
                                       std::span<const abc_vec2> raw, abc_vec2 current) {
    PreparedHistory result;
    library->check(library->prepare_history(raw.data(), raw.size(), current, result.samples.data(),
                                            result.samples.size(), &result.filtered_anchor));
    return result;
}

class Planner {
    friend class SegmentController;
    friend class ObservationInput;
    struct State {
        std::shared_ptr<Library> library;
        abc_planner_handle handle{};
        ~State() { if (handle) library->planner_destroy(handle); }
    };
    std::shared_ptr<State> state_;
public:
    explicit Planner(const Model& model, uint64_t seed = 7) : state_(std::make_shared<State>()) {
        state_->library = model.state_->library;
        abc_planner_options options{};
        state_->library->check(state_->library->planner_options_init(&options, sizeof(options)));
        options.seed = seed;
        state_->library->check(state_->library->planner_create(model.state_->handle, &options, &state_->handle));
    }
    Planner(const Model& model, const abc_planner_options& options) : state_(std::make_shared<State>()) {
        state_->library = model.state_->library;
        state_->library->check(state_->library->planner_create(model.state_->handle, &options, &state_->handle));
    }
    static abc_planner_options options(const std::shared_ptr<Library>& library) {
        abc_planner_options result{};
        library->check(library->planner_options_init(&result, sizeof(result))); return result;
    }
    void reset() { state_->library->check(state_->library->planner_reset_session(state_->handle)); }
    void reset(uint64_t seed) { state_->library->check(state_->library->planner_reset(state_->handle, seed)); }
    void reinitialize(const abc_planner_options& options) { state_->library->check(state_->library->planner_reinitialize(state_->handle, &options)); }
    void set_position(abc_vec2 position) { state_->library->check(state_->library->planner_set_position(state_->handle, position)); }
    void update_target(abc_vec2 target, int64_t receipt_us) { state_->library->check(state_->library->planner_update_target(state_->handle, target, receipt_us)); }
    void clear_target(int64_t receipt_us) { state_->library->check(state_->library->planner_clear_target(state_->handle, receipt_us)); }
    abc_model_info model_info() const {
        abc_model_info result{}; result.struct_size = sizeof(result);
        state_->library->check(state_->library->planner_get_model_info(state_->handle, &result)); return result;
    }
    uint64_t required_capacity(int64_t timestamp_us) const {
        uint64_t result{}; state_->library->check(state_->library->planner_required_capacity(state_->handle, timestamp_us, &result)); return result;
    }
    uint64_t advance_into(int64_t timestamp_us, std::span<abc_sample> output) {
        uint64_t written{};
        auto status = state_->library->planner_advance(state_->handle, timestamp_us, output.data(), output.size(), &written);
        state_->library->check_output(status, written); return written;
    }
    std::vector<abc_sample> advance(int64_t timestamp_us) {
        std::vector<abc_sample> result(static_cast<std::size_t>(required_capacity(timestamp_us)));
        result.resize(static_cast<std::size_t>(advance_into(timestamp_us, result))); return result;
    }
    abc_sample generate_sample(int64_t timestamp_us) {
        abc_sample result{}; state_->library->check(state_->library->planner_generate_sample(state_->handle, timestamp_us, &result)); return result;
    }
    abc_planner_state state() const {
        abc_planner_state result{}; result.struct_size = sizeof(result);
        state_->library->check(state_->library->planner_get_state(state_->handle, &result)); return result;
    }
    abc_planner_handle native_handle() const noexcept { return state_->handle; }
};

class ObservationInput {
    std::shared_ptr<Planner::State> planner_;
    abc_observation_handle handle_{};
    void release() noexcept { if (handle_) planner_->library->observation_destroy(std::exchange(handle_, 0)); }
public:
    explicit ObservationInput(Planner& planner) : planner_(planner.state_) {
        planner_->library->check(planner_->library->observation_create(planner_->handle, nullptr, &handle_));
    }
    ObservationInput(Planner& planner, const abc_observation_options& options) : planner_(planner.state_) {
        planner_->library->check(planner_->library->observation_create(planner_->handle, &options, &handle_));
    }
    static abc_observation_options options(const std::shared_ptr<Library>& library) {
        abc_observation_options result{}; library->check(library->observation_options_init(&result, sizeof(result))); return result;
    }
    ~ObservationInput() { release(); }
    ObservationInput(const ObservationInput&) = delete;
    ObservationInput& operator=(const ObservationInput&) = delete;
    ObservationInput(ObservationInput&& other) noexcept : planner_(std::move(other.planner_)), handle_(std::exchange(other.handle_, 0)) {}
    ObservationInput& operator=(ObservationInput&& other) noexcept {
        if (this != &other) { release(); planner_ = std::move(other.planner_); handle_ = std::exchange(other.handle_, 0); } return *this;
    }
    abc_vec2 submit(const abc_target_observation& observation) {
        abc_vec2 result{}; planner_->library->check(planner_->library->observation_submit(handle_, &observation, &result)); return result;
    }
    abc_vec2 submit(abc_vec2 value, int64_t capture_us, int64_t receipt_us, uint32_t kind, uint32_t availability = ABC_TARGET_AVAILABLE) {
        abc_target_observation observation{}; observation.struct_size = sizeof(observation); observation.kind = kind;
        observation.availability = availability; observation.capture_time_us = capture_us; observation.receipt_time_us = receipt_us; observation.value = value;
        return submit(observation);
    }
    abc_vec2 relative(abc_vec2 value, int64_t capture_us, int64_t receipt_us) { return submit(value, capture_us, receipt_us, ABC_TARGET_RELATIVE); }
    abc_vec2 absolute(abc_vec2 value, int64_t capture_us, int64_t receipt_us) { return submit(value, capture_us, receipt_us, ABC_TARGET_ABSOLUTE); }
    abc_vec2 unavailable(int64_t capture_us, int64_t receipt_us, bool invalidate = false) {
        return submit({}, capture_us, receipt_us, ABC_TARGET_ABSOLUTE, invalidate ? ABC_TARGET_INVALIDATED : ABC_TARGET_UNAVAILABLE);
    }
    abc_vec2 invalidate(int64_t capture_us, int64_t receipt_us) { return unavailable(capture_us, receipt_us, true); }
    abc_observation_diagnostics diagnostics() const {
        abc_observation_diagnostics result{}; result.struct_size = sizeof(result);
        planner_->library->check(planner_->library->observation_get_diagnostics(handle_, &result)); return result;
    }
    abc_observation_handle native_handle() const noexcept { return handle_; }
};

struct Segment { abc_segment_metadata metadata{}; std::vector<abc_sample> samples; };

class SegmentController {
    std::shared_ptr<Planner::State> planner_;
    abc_segment_handle handle_{};
public:
    explicit SegmentController(Planner& planner) : planner_(planner.state_) {
        abc_segment_options options{};
        planner_->library->check(planner_->library->segment_options_init(&options, sizeof(options)));
        planner_->library->check(planner_->library->segment_create(planner_->handle, &options, &handle_));
    }
    SegmentController(Planner& planner, const abc_segment_options& options) : planner_(planner.state_) {
        planner_->library->check(planner_->library->segment_create(planner_->handle, &options, &handle_));
    }
    static abc_segment_options options(const std::shared_ptr<Library>& library) {
        abc_segment_options result{};
        library->check(library->segment_options_init(&result, sizeof(result))); return result;
    }
    ~SegmentController() { if (handle_) planner_->library->segment_destroy(handle_); }
    SegmentController(const SegmentController&) = delete;
    SegmentController& operator=(const SegmentController&) = delete;
    SegmentController(SegmentController&& other) noexcept : planner_(std::move(other.planner_)), handle_(std::exchange(other.handle_, 0)) {}
    SegmentController& operator=(SegmentController&& other) noexcept {
        if (this != &other) { if (handle_) planner_->library->segment_destroy(handle_); planner_ = std::move(other.planner_); handle_ = std::exchange(other.handle_, 0); } return *this;
    }
    void submit(const abc_target_observation& observation) {
        planner_->library->check(planner_->library->segment_submit_observation(handle_, &observation));
    }
    void submit(abc_vec2 value, int64_t capture_us, int64_t receipt_us,
                uint32_t kind = ABC_TARGET_RELATIVE, uint32_t availability = ABC_TARGET_AVAILABLE) {
        abc_target_observation observation{};
        observation.struct_size = sizeof(observation); observation.kind = kind;
        observation.availability = availability; observation.capture_time_us = capture_us;
        observation.receipt_time_us = receipt_us; observation.value = value;
        submit(observation);
    }
    void relative(abc_vec2 error, int64_t capture_us, int64_t receipt_us) {
        submit(error, capture_us, receipt_us);
    }
    void absolute(abc_vec2 position, int64_t capture_us, int64_t receipt_us) {
        submit(position, capture_us, receipt_us, ABC_TARGET_ABSOLUTE);
    }
    void unavailable(int64_t capture_us, int64_t receipt_us, bool invalidate = false) {
        submit({}, capture_us, receipt_us, ABC_TARGET_ABSOLUTE, invalidate ? ABC_TARGET_INVALIDATED : ABC_TARGET_UNAVAILABLE);
    }
    uint64_t required_capacity() const {
        uint64_t result{}; planner_->library->check(planner_->library->segment_required_capacity(handle_, &result)); return result;
    }
    uint64_t next_segment_into(std::span<abc_sample> samples, abc_segment_metadata& metadata) {
        uint64_t written{}; metadata = {}; metadata.struct_size = sizeof(metadata);
        planner_->library->check(planner_->library->segment_next(handle_, samples.data(), samples.size(), &written, &metadata));
        return written;
    }
    Segment next_segment() {
        Segment result; uint64_t capacity{}, written{};
        planner_->library->check(planner_->library->segment_required_capacity(handle_, &capacity));
        result.samples.resize(static_cast<std::size_t>(capacity));
        result.metadata.struct_size = sizeof(result.metadata);
        planner_->library->check(planner_->library->segment_next(handle_, result.samples.data(), capacity, &written, &result.metadata));
        result.samples.resize(static_cast<std::size_t>(written)); return result;
    }
    abc_segment_diagnostics diagnostics() const {
        abc_segment_diagnostics result{}; result.struct_size = sizeof(result);
        planner_->library->check(planner_->library->segment_get_diagnostics(handle_, &result)); return result;
    }
    void reset() { planner_->library->check(planner_->library->segment_reset_session(handle_)); }
    void reset_diagnostics() { planner_->library->check(planner_->library->segment_reset_diagnostics(handle_)); }
    void clear_pending() { planner_->library->check(planner_->library->segment_clear_pending_observations(handle_)); }
    abc_segment_metadata metadata() const {
        abc_segment_metadata result{}; result.struct_size = sizeof(result);
        planner_->library->check(planner_->library->segment_get_metadata(handle_, &result)); return result;
    }
    abc_segment_state state() const {
        abc_segment_state result{}; result.struct_size = sizeof(result);
        planner_->library->check(planner_->library->segment_get_state(handle_, &result)); return result;
    }
    abc_segment_recovery_diagnostics recovery_diagnostics() const {
        abc_segment_recovery_diagnostics result{}; result.struct_size = sizeof(result);
        planner_->library->check(planner_->library->segment_get_recovery_diagnostics(handle_, &result)); return result;
    }
    abc_segment_handle native_handle() const noexcept { return handle_; }
};

class Renderer {
    std::shared_ptr<Library> library_;
    abc_renderer_handle handle_{};
    Renderer(std::shared_ptr<Library> library, const char* path) : library_(std::move(library)) {
        library_->check(path ? library_->renderer_create(path, &handle_) : library_->renderer_create_embedded(&handle_));
    }
public:
    static Renderer embedded(std::shared_ptr<Library> library) { return Renderer(std::move(library), nullptr); }
    static Renderer load(std::shared_ptr<Library> library, const char* path) { return Renderer(std::move(library), path); }
    ~Renderer() { if (handle_) library_->renderer_destroy(handle_); }
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&& other) noexcept : library_(std::move(other.library_)), handle_(std::exchange(other.handle_, 0)) {}
    Renderer& operator=(Renderer&& other) noexcept {
        if (this != &other) { if (handle_) library_->renderer_destroy(handle_); library_ = std::move(other.library_); handle_ = std::exchange(other.handle_, 0); } return *this;
    }
    void set_profile(std::span<const abc_report> profile) { library_->check(library_->renderer_set_profile(handle_, profile.data(), profile.size())); }
    void reset(uint64_t seed = 7) { library_->check(library_->renderer_reset(handle_, seed)); }
    abc_report step(abc_vec2 intent) { abc_report out{}; library_->check(library_->renderer_step(handle_, intent, &out)); return out; }
    uint64_t step_into(std::span<const abc_vec2> intents, std::span<abc_report> output) {
        uint64_t written{}; auto status = library_->renderer_step_batch(handle_, intents.data(), intents.size(), output.data(), output.size(), &written);
        library_->check_output(status, written); return written;
    }
    uint64_t render_samples_into(std::span<const abc_sample> samples, abc_vec2& previous_position,
                                 double gain, std::span<abc_report> output) {
        uint64_t written{}; auto status = library_->renderer_render_samples(handle_, samples.data(), samples.size(), &previous_position, gain, output.data(), output.size(), &written);
        library_->check_output(status, written); return written;
    }
    uint64_t render_into(std::span<const abc_sample> samples, const abc_segment_metadata& metadata,
                         double gain, std::span<abc_report> output) {
        uint64_t written{}; auto status = library_->segment_render(handle_, samples.data(), samples.size(), &metadata, gain, output.data(), output.size(), &written);
        library_->check_output(status, written); return written;
    }
    abc_renderer_handle native_handle() const noexcept { return handle_; }
};
} // namespace abcurves_dll
