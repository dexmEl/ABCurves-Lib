#pragma once
#include "observation.hpp"
#include "renderer.hpp"
#include <array>
#include <span>

namespace abc {
struct SegmentOptions {
    ObservationOrdering ordering{ObservationOrdering::latest};
    std::int64_t max_age_us{250000}, inactivity_timeout_us{-1};
    std::uint32_t observation_capacity{64};
};
struct SegmentMetadata {
    std::uint64_t segment_id{0};
    std::uint32_t variant_ms{0}, sample_count{0};
    std::int64_t start_timestamp_us{0}, end_timestamp_us{0};
    std::int64_t first_sample_timestamp_us{-1}, last_sample_timestamp_us{-1}, duration_us{0};
    Vec2 initial_position{}, final_position{};
    PlannerMode mode{PlannerMode::holding};
    bool fresh_planning_decision{false}, motor_inference{false};
    int selected_head{-1}, selected_brake_head{-1};
};
struct SegmentDiagnostics {
    std::uint64_t total_segments{0}, fresh_planning_decisions{0};
    std::uint64_t decisions_with_new_observation{0}, decisions_without_new_observation{0};
    std::uint64_t stale_observations{0}, duplicate_observations{0}, out_of_order_observations{0};
    std::uint64_t target_unavailable_events{0}, target_timeout_events{0};
    std::uint64_t segment_generation_errors{0}, missed_generation_deadlines{0};
    std::uint64_t accepted_observations{0}, rejected_observations{0}, queue_full_observations{0};
    std::int64_t last_observation_age_us{-1}, last_observation_receipt_us{-1};
    std::int64_t last_capture_us{-1}, last_receipt_us{-1}, last_apply_us{-1};
    double last_planning_latency_us{0};
    std::uint32_t pending_observations{0};
    TargetAvailability availability{TargetAvailability::unavailable};
    ObservationRejection last_rejection{ObservationRejection::none};
    Vec2 resolved_target{}, reference_position{};
};
struct SegmentRecoveryDiagnostics {
    std::uint64_t recoverable_observation_errors{0};
    std::uint64_t invalid_pending_observations{0};
    std::uint64_t nonfinite_relative_resolutions{0};
    std::uint64_t stale_pending_observations{0};
    std::uint64_t explicit_pending_clears{0};
    std::int64_t last_error_capture_us{-1}, last_error_receipt_us{-1};
    std::uint32_t pending_observations{0};
    ObservationRejection last_recoverable_error{ObservationRejection::none};
};
struct SegmentState {
    PlannerFailure failure{};
    std::int64_t numerical_time_us{0}, requested_time_us{0};
    std::uint64_t generation_attempts{0};
    std::int64_t last_attempt_start_us{-1}, last_attempt_end_us{-1};
    std::uint64_t last_segment_id{0};
    std::int64_t last_segment_start_us{-1}, last_segment_end_us{-1};
    std::int64_t relative_origin_earliest_us{0}, relative_origin_latest_us{0};
    std::int64_t last_origin_error_receipt_us{-1};
};
class SegmentCapacityError : public std::length_error {
public: SegmentCapacityError() : std::length_error("bounded observation queue is full") {}
};
// Borrows a planner, detecting parent destruction. Enforces exclusive attachment
// and rejects direct input/output mutations until destruction. Explicit parent
// reset/reinitialize synchronize through revisions. Serialize all same-parent
// operations; attachment ownership is not a concurrency lock.
class SegmentController final {
public:
    explicit SegmentController(ContinuousPlanner& planner, SegmentOptions options = {});
    ~SegmentController();
    SegmentController(const SegmentController&) = delete;
    SegmentController& operator=(const SegmentController&) = delete;
    // Records the observation. Resolution/application occurs at the next boundary.
    void submit_observation(const TargetObservation& observation);
    std::size_t next_segment(std::span<Sample> output, SegmentMetadata& metadata);
    SegmentMetadata metadata(); // throws before first segment
    SegmentDiagnostics diagnostics();
    SegmentRecoveryDiagnostics recovery_diagnostics();
    SegmentState state(); // read-only failure/window snapshot, usable after failure
    std::size_t required_capacity() const noexcept { return interval_; }
    void reset_diagnostics();
    // Drops only deferred observations. Planner time, target/history, RNG and
    // already committed output are unchanged.
    void clear_pending_observations();
    void reset_segment_session(); // explicit planner reset, same seed/prefix
    // Persistent Renderer composition; gain is application calibrated radians
    // per native count, X right/Y up. Reports are already native counts.
    static void render_segment(Renderer& renderer, std::span<const Sample> samples,
                               const SegmentMetadata& metadata, double gain,
                               std::span<Report> output);
private:
    struct Pending { TargetObservation observation; Vec2 target{}, reference{}; bool resolved{false}; };
    void synchronize(bool allow_failure = false);
    void check_parent() const;
    [[noreturn]] void reject(ObservationRejection reason, const char* message);
    std::size_t apply_pending(std::int64_t boundary_us);
    void record_recoverable(const Pending& pending, ObservationRejection reason);
    void remove_recent(const TargetObservation& observation);
    void recompute_watermarks();
    ContinuousPlanner& planner_;
    std::weak_ptr<const int> lifetime_;
    SegmentOptions options_;
    std::uint32_t interval_;
    std::uint64_t revision_, next_id_{1};
    std::array<Pending,64> pending_{};
    std::size_t pending_count_{0};
    std::array<TargetObservation,64> recent_{};
    std::size_t recent_count_{0}, recent_cursor_{0};
    SegmentDiagnostics diagnostics_{};
    SegmentRecoveryDiagnostics recovery_{};
    SegmentMetadata current_{};
    PlannerFailure failure_{};
    std::uint64_t generation_attempts_{0};
    std::int64_t last_attempt_start_{-1}, last_attempt_end_{-1};
    std::int64_t last_origin_error_receipt_{-1};
    std::int64_t expected_time_{0}, last_capture_{-1}, last_receipt_{-1};
    std::int64_t applied_capture_{-1};
    bool timeout_active_{false}, observations_since_decision_{false};
};
} // namespace abc
