#pragma once

#include "planner.hpp"
#include <array>
#include <span>
#include <stdexcept>

namespace abc {

enum class ObservationOrdering : std::uint32_t { strict = 0, latest = 1, caller = 2 };
enum class TargetAvailability : std::uint32_t { available = 0, unavailable = 1, invalidated = 2 };
enum class ObservationKind : std::uint32_t { absolute = 0, relative = 1 };
enum class ObservationRejection : std::uint32_t {
    none = 0, invalid = 1, timestamp = 2, stale = 3, duplicate = 4, out_of_order = 5
};

class ObservationError : public std::invalid_argument {
public:
    ObservationError(ObservationRejection reason, const char* message)
        : std::invalid_argument(message), reason(reason) {}
    ObservationRejection reason;
};

struct ObservationOptions {
    ObservationOrdering ordering{ObservationOrdering::strict};
    std::int64_t max_age_us{250000}; // -1 disables age limit
};

struct TargetObservation {
    Vec2 value{};
    std::int64_t capture_time_us{0}, receipt_time_us{0};
    ObservationKind kind{ObservationKind::absolute};
    TargetAvailability availability{TargetAvailability::available};
};

struct ObservationDiagnostics {
    std::int64_t last_capture_us{-1}, last_receipt_us{-1}, last_age_us{-1};
    std::uint64_t accepted{0}, rejected{0}, duplicates{0}, out_of_order{0}, stale{0};
    TargetAvailability availability{TargetAvailability::unavailable};
    ObservationRejection last_rejection{ObservationRejection::none};
    Vec2 resolved_target{}, reference_position{};
};

// Borrows planner, checking its weak lifetime token before access. Serialize all
// access together; submission is prohibited while a SegmentController owns it.
// Do not queue legacy future
// targets while using this handler. Advance/collect through receipt before submit.
// No movement state, execution feedback, transport, or scheduling threads.
class ObservationInput final {
public:
    explicit ObservationInput(ContinuousPlanner& planner, ObservationOptions options = {});
    Vec2 submit(const TargetObservation& observation);
    ObservationDiagnostics diagnostics();
    const ObservationOptions& options() const noexcept { return options_; }
private:
    void synchronize();
    [[noreturn]] void reject(ObservationRejection reason, const char* message);
    ContinuousPlanner& planner_;
    std::weak_ptr<const int> lifetime_;
    ObservationOptions options_;
    ObservationDiagnostics diagnostics_{};
    std::uint64_t revision_{};
    std::array<TargetObservation, 64> recent_{};
    std::size_t recent_count_{0}, cursor_{0};
};

struct PreparedHistory {
    Vec2 initial_position{}, observed_position{};
    std::array<Vec2, 160> history{};
};
// Original causal W5 representation, including filtered position anchor.
PreparedHistory prepare_history(std::span<const Vec2> raw, Vec2 current_position);

} // namespace abc
