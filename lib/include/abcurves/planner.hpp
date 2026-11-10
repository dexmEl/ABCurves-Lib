#pragma once

#include "model.hpp"
#include "types.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <vector>

namespace abc {

class SegmentController;
class ObservationInput;

struct PlannerOptions {
    Vec2 initial_position{};
    std::vector<Vec2> initial_history; // optional 160 one-ms displacements
    std::uint64_t seed{7};
    bool diagnostics{false};
};

class ContinuousPlanner final {
public:
    static std::shared_ptr<ContinuousPlanner> load(const std::filesystem::path& model_directory,
                                                   PlannerOptions options = {});
    explicit ContinuousPlanner(std::shared_ptr<const Model> model, PlannerOptions options = {});
    ~ContinuousPlanner();

    const Model& model() const noexcept { return *model_; }
    int planning_interval_ms() const noexcept { return commitment_ms_; }
    void reset(); // stored seed/prefix; explicit zero-time session reset
    void reset(std::uint64_t seed); // replace seed; low 32 bits seed legacy RNG
    // Explicit initialization boundary; strong guarantee on invalid options.
    // Reset/reinitialize remain allowed with a controller, which synchronizes
    // its pending input/metadata on the next operation. Other direct mutations
    // require destroying the controller first. Serialize all same-parent calls.
    void reinitialize(PlannerOptions options);
    std::uint64_t reset_revision() const noexcept { return revision_; }
    std::int64_t requested_time_us() const noexcept;
    void set_position(Vec2 position);
    void update_target(Vec2 target, std::int64_t receipt_us);
    void clear_target(std::int64_t receipt_us);

    // Advances numerical time and writes at most capacity closed 1 ms samples.
    // The return value is the number of samples written.
    std::size_t advance(std::int64_t timestamp_us, Sample* output, std::size_t capacity);
    Sample generate_sample(std::int64_t timestamp_us);
    Diagnostics diagnostics() const noexcept;
    Vec2 position() const noexcept;
    PlannerMode mode() const noexcept;
    bool failed() const noexcept;
    PlannerFailure failure() const noexcept;
    bool has_segment_controller() const noexcept { return segment_owner_ != nullptr; }

private:
    friend class SegmentController;
    friend class ObservationInput;
    void require_direct_access() const;
    void latch_failure(PlannerFailure failure) noexcept;
    std::size_t advance_impl(std::int64_t timestamp_us, Sample* output, std::size_t capacity);
    bool has_queued_targets() const noexcept;
    bool target_available() const noexcept;
    Vec2 segment_position_at(std::int64_t receipt_us, std::int64_t start_us, Vec2 initial) const;
    void apply_segment_target(Vec2 target, bool available, std::int64_t receipt_us);
    struct Impl;
    // Allows a borrowing controller to detect parent destruction before access.
    std::shared_ptr<const int> lifetime_{std::make_shared<const int>(0)};
    std::shared_ptr<const Model> model_;
    std::unique_ptr<Impl> impl_;
    int commitment_ms_{32};
    std::uint64_t revision_{0};
    const SegmentController* segment_owner_{nullptr};
};

} // namespace abc
