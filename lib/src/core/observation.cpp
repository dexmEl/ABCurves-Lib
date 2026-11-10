#include "abcurves/observation.hpp"
#include <cmath>

namespace abc {
namespace {
bool finite(Vec2 v) { return std::isfinite(v.x) && std::isfinite(v.y); }
}

ObservationInput::ObservationInput(ContinuousPlanner& p, ObservationOptions o)
    : planner_(p), lifetime_(p.lifetime_), options_(o), revision_(p.reset_revision()) {
    if (static_cast<unsigned>(o.ordering) > 2 || o.max_age_us < -1)
        throw std::invalid_argument("invalid observation options");
    p.require_direct_access();
}
void ObservationInput::synchronize() {
    if (lifetime_.expired()) throw std::logic_error("observation parent planner was destroyed");
    if (revision_ != planner_.reset_revision()) {
        revision_ = planner_.reset_revision();
        diagnostics_ = {};
        recent_count_ = cursor_ = 0;
    }
}
[[noreturn]] void ObservationInput::reject(ObservationRejection r, const char* message) {
    ++diagnostics_.rejected;
    diagnostics_.last_rejection = r;
    if (r == ObservationRejection::duplicate) ++diagnostics_.duplicates;
    if (r == ObservationRejection::out_of_order) ++diagnostics_.out_of_order;
    if (r == ObservationRejection::stale) ++diagnostics_.stale;
    throw ObservationError(r, message);
}
Vec2 ObservationInput::submit(const TargetObservation& o) {
    synchronize();
    planner_.require_direct_access();
    if (static_cast<unsigned>(o.kind) > 1 || static_cast<unsigned>(o.availability) > 2 || !finite(o.value))
        reject(ObservationRejection::invalid, "invalid observation kind, availability or coordinates");
    const auto now = planner_.diagnostics().numerical_time_us;
    if (o.capture_time_us < 0 || o.receipt_time_us < o.capture_time_us ||
        o.receipt_time_us < planner_.requested_time_us() || o.receipt_time_us < now ||
        o.receipt_time_us - now >= SAMPLE_US)
        reject(ObservationRejection::timestamp,
               "advance and collect through receipt first; capture <= receipt in current sample interval required");
    if (options_.max_age_us >= 0 && o.receipt_time_us - o.capture_time_us > options_.max_age_us)
        reject(ObservationRejection::stale, "observation exceeds maximum age");
    for (std::size_t i = 0; i < recent_count_; ++i) {
        const auto& old = recent_[i];
        if (old.capture_time_us == o.capture_time_us && old.kind == o.kind &&
            old.availability == o.availability &&
            (o.availability != TargetAvailability::available ||
             (old.value.x == o.value.x && old.value.y == o.value.y)))
            reject(ObservationRejection::duplicate, "duplicate observation in 64-entry window");
    }
    if (o.receipt_time_us < diagnostics_.last_receipt_us)
        reject(ObservationRejection::out_of_order, "receipt times must be chronological");
    if (options_.ordering != ObservationOrdering::caller &&
        o.capture_time_us < diagnostics_.last_capture_us)
        reject(ObservationRejection::out_of_order, "capture time precedes last accepted capture");
    if (options_.ordering == ObservationOrdering::strict &&
        o.capture_time_us == diagnostics_.last_capture_us)
        reject(ObservationRejection::duplicate, "strict ordering requires increasing captures");

    auto reference = planner_.position();
    auto target = o.kind == ObservationKind::relative ? reference + o.value : o.value;
    if (!finite(target)) reject(ObservationRejection::invalid, "relative target sum exceeds finite range");
    // Planner validation/enqueue occurs before updating accepted metadata.
    if (o.availability == TargetAvailability::available)
        planner_.update_target(target, o.receipt_time_us);
    else
        planner_.clear_target(o.receipt_time_us);
    recent_[cursor_] = o;
    cursor_ = (cursor_ + 1) % recent_.size();
    if (recent_count_ < recent_.size()) ++recent_count_;
    diagnostics_.last_capture_us = o.capture_time_us;
    diagnostics_.last_receipt_us = o.receipt_time_us;
    diagnostics_.last_age_us = o.receipt_time_us - o.capture_time_us;
    diagnostics_.availability = o.availability;
    diagnostics_.last_rejection = ObservationRejection::none;
    diagnostics_.reference_position = reference;
    diagnostics_.resolved_target = o.availability == TargetAvailability::available ? target : Vec2{};
    ++diagnostics_.accepted;
    return diagnostics_.resolved_target;
}
ObservationDiagnostics ObservationInput::diagnostics() { synchronize(); return diagnostics_; }

PreparedHistory prepare_history(std::span<const Vec2> raw, Vec2 current) {
    if (raw.size() != 164 || !finite(current))
        throw std::invalid_argument("164 raw displacements and finite current position required");
    for (auto v : raw) if (!finite(v)) throw std::invalid_argument("nonfinite raw history");
    PreparedHistory result;
    result.observed_position = current;
    constexpr double weights[5]{1./9., 2./9., 3./9., 2./9., 1./9.};
    for (std::size_t i = 0; i < 160; ++i) {
        Vec2 v{};
        for (std::size_t k = 0; k < 5; ++k) v = v + raw[i+k] * weights[k];
        if (!finite(v)) throw std::invalid_argument("history filtering exceeds finite range");
        result.history[i] = v;
    }
    auto lag = (raw[163]*8. + raw[162]*6. + raw[161]*3. + raw[160])/9.;
    result.initial_position = current - lag;
    if (!finite(result.initial_position)) throw std::invalid_argument("history anchor exceeds finite range");
    return result;
}
} // namespace abc
