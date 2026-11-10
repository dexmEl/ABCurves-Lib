#include "abcurves/segments.hpp"
#include "failure.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

namespace abc {
namespace {
bool finite(Vec2 v) { return std::isfinite(v.x) && std::isfinite(v.y); }
bool duplicate(const TargetObservation& a, const TargetObservation& b) {
    return a.capture_time_us==b.capture_time_us && a.kind==b.kind && a.availability==b.availability &&
        (a.availability!=TargetAvailability::available || (a.value.x==b.value.x && a.value.y==b.value.y));
}
}
void SegmentController::record_recoverable(const Pending& p, ObservationRejection reason) {
    ++recovery_.recoverable_observation_errors;
    ++recovery_.invalid_pending_observations;
    ++diagnostics_.rejected_observations; diagnostics_.last_rejection=reason;
    if(reason==ObservationRejection::stale) ++diagnostics_.stale_observations;
    recovery_.last_error_capture_us=p.observation.capture_time_us;
    recovery_.last_error_receipt_us=p.observation.receipt_time_us;
    recovery_.last_recoverable_error=reason;
}
void SegmentController::remove_recent(const TargetObservation& observation) {
    std::array<TargetObservation,64> kept{}; std::size_t count=0;
    for(std::size_t n=0;n<recent_count_;++n) {
        const auto at=(recent_count_==recent_.size()?recent_cursor_+n:n)%recent_.size();
        if (duplicate(recent_[at],observation)) continue;
        kept[count++]=recent_[at];
    }
    recent_=kept; recent_count_=count; recent_cursor_=count%recent_.size();
}
void SegmentController::recompute_watermarks() {
    last_capture_=applied_capture_; last_receipt_=diagnostics_.last_observation_receipt_us;
    for(std::size_t n=0;n<pending_count_;++n) {
        last_capture_=pending_[n].observation.capture_time_us;
        last_receipt_=pending_[n].observation.receipt_time_us;
    }
    diagnostics_.last_capture_us=last_capture_; diagnostics_.last_receipt_us=last_receipt_;
}
SegmentController::SegmentController(ContinuousPlanner& p, SegmentOptions o)
    : planner_(p), lifetime_(p.lifetime_), options_(o), interval_(p.planning_interval_ms()),
      revision_(p.reset_revision()), expected_time_(p.diagnostics().numerical_time_us) {
    if (static_cast<unsigned>(o.ordering)>2 || o.max_age_us < -1 || o.inactivity_timeout_us < -1 ||
        o.observation_capacity==0 || o.observation_capacity>64)
        throw std::invalid_argument("invalid segment options");
    if (p.has_queued_targets() || p.requested_time_us()!=expected_time_)
        throw std::logic_error("controller requires a closed boundary without legacy queued targets");
    p.require_direct_access();
    diagnostics_.availability=p.target_available()?TargetAvailability::available:TargetAvailability::unavailable;
    p.segment_owner_=this;
}
SegmentController::~SegmentController() {
    if (!lifetime_.expired() && planner_.segment_owner_==this) planner_.segment_owner_=nullptr;
}
void SegmentController::check_parent() const {
    if (lifetime_.expired()) throw std::logic_error("segment parent planner was destroyed");
}
void SegmentController::synchronize(bool allow_failure) {
    check_parent();
    if (revision_!=planner_.reset_revision()) {
        revision_=planner_.reset_revision(); pending_count_=recent_count_=recent_cursor_=0;
        next_id_=1; current_={}; diagnostics_={}; last_capture_=last_receipt_=-1;
        recovery_={};
        failure_={}; generation_attempts_=0; last_attempt_start_=last_attempt_end_=-1;
        last_origin_error_receipt_=-1;
        applied_capture_=-1;
        timeout_active_=observations_since_decision_=false;
        expected_time_=planner_.diagnostics().numerical_time_us;
    }
    if (planner_.failed() || failure_.reset_required) {
        if (allow_failure) return;
        throw std::logic_error("segment generation failed; inspect state() and explicitly reset the session");
    }
    if (planner_.diagnostics().numerical_time_us!=expected_time_ || planner_.has_queued_targets() ||
        planner_.requested_time_us()!=expected_time_)
        throw std::logic_error("do not mix controller operations with legacy advancement/queued targets");
}
[[noreturn]] void SegmentController::reject(ObservationRejection r,const char* message) {
    ++diagnostics_.rejected_observations; diagnostics_.last_rejection=r;
    if(r==ObservationRejection::stale) ++diagnostics_.stale_observations;
    if(r==ObservationRejection::duplicate) ++diagnostics_.duplicate_observations;
    if(r==ObservationRejection::out_of_order) ++diagnostics_.out_of_order_observations;
    throw ObservationError(r,message);
}
void SegmentController::submit_observation(const TargetObservation& o) {
    synchronize();
    if(static_cast<unsigned>(o.kind)>1 || static_cast<unsigned>(o.availability)>2 || !finite(o.value))
        reject(ObservationRejection::invalid,"invalid observation enums or value");
    if(o.capture_time_us<0 || o.receipt_time_us<o.capture_time_us)
        reject(ObservationRejection::timestamp,"nonnegative capture <= receipt required");
    if(options_.max_age_us>=0 && o.receipt_time_us-o.capture_time_us>options_.max_age_us)
        reject(ObservationRejection::stale,"observation exceeds maximum capture-to-receipt age");
    if(o.receipt_time_us<=expected_time_ && expected_time_-o.receipt_time_us>=640*SAMPLE_US)
        reject(ObservationRejection::stale,"receipt is outside retained target history");
    for(std::size_t n=0;n<recent_count_;++n)
        if(duplicate(recent_[n],o)) reject(ObservationRejection::duplicate,"duplicate observation");
    if(o.receipt_time_us<last_receipt_ || (options_.ordering!=ObservationOrdering::caller && o.capture_time_us<last_capture_))
        reject(ObservationRejection::out_of_order,"observation receipt/capture is out of order");
    if(options_.ordering==ObservationOrdering::strict && o.capture_time_us==last_capture_)
        reject(ObservationRejection::duplicate,"strict capture order requires increasing timestamps");
    Pending pending{o};
    if(o.availability==TargetAvailability::available && o.receipt_time_us<=expected_time_) {
        pending.reference=planner_.position();
        if(o.kind==ObservationKind::relative) {
            auto start=current_.segment_id?current_.start_timestamp_us:expected_time_;
            if(o.receipt_time_us<start) {
                last_origin_error_receipt_=o.receipt_time_us;
                reject(ObservationRejection::timestamp,"relative receipt predates last returned segment");
            }
            pending.reference=planner_.segment_position_at(o.receipt_time_us,start,
                current_.segment_id?current_.initial_position:planner_.position());
        }
        pending.target=o.kind==ObservationKind::relative?pending.reference+o.value:o.value;
        if(!finite(pending.target)) reject(ObservationRejection::invalid,"relative target sum is not finite");
        pending.resolved=true;
    }
    if(pending_count_==options_.observation_capacity) {
        ++diagnostics_.rejected_observations; ++diagnostics_.queue_full_observations;
        throw SegmentCapacityError();
    }
    pending_[pending_count_++]=pending;
    recent_[recent_cursor_]=o; recent_cursor_=(recent_cursor_+1)%recent_.size();
    recent_count_=std::min(recent_count_+1,recent_.size());
    last_capture_=o.capture_time_us; last_receipt_=o.receipt_time_us;
    diagnostics_.last_capture_us=last_capture_; diagnostics_.last_receipt_us=last_receipt_;
    ++diagnostics_.accepted_observations; diagnostics_.last_rejection=ObservationRejection::none;
}
std::size_t SegmentController::apply_pending(std::int64_t boundary) {
    std::size_t count=0; bool rejected=false;
    // Validate every eligible record before writing target history. Compact
    // only on the exceptional input path; the valid path uses existing storage.
    while(count<pending_count_ && pending_[count].observation.receipt_time_us<=boundary) {
        auto& p=pending_[count]; const auto& o=p.observation;
        ObservationRejection reason=ObservationRejection::none;
        if(boundary-o.receipt_time_us>=640*SAMPLE_US) {
            reason=ObservationRejection::stale;
            ++recovery_.stale_pending_observations;
        } else if(o.availability==TargetAvailability::available && !p.resolved) {
            p.reference=planner_.position();
            if(o.kind==ObservationKind::relative) {
                auto start=current_.segment_id?current_.start_timestamp_us:boundary;
                if(o.receipt_time_us<start) {
                    reason=ObservationRejection::timestamp;
                    last_origin_error_receipt_=o.receipt_time_us;
                }
                else p.reference=planner_.segment_position_at(o.receipt_time_us,start,
                    current_.segment_id?current_.initial_position:planner_.position());
            }
            if(!finite(p.reference)) throw std::logic_error("nonfinite planner virtual origin");
            if(reason==ObservationRejection::none) {
                p.target=o.kind==ObservationKind::relative?p.reference+o.value:o.value;
                if(!finite(p.target)) {
                    if(o.kind!=ObservationKind::relative || !finite(o.value))
                        throw std::logic_error("invalid validated pending coordinates");
                    reason=ObservationRejection::invalid;
                    ++recovery_.nonfinite_relative_resolutions;
                } else p.resolved=true;
            }
        }
        if(reason!=ObservationRejection::none) {
            record_recoverable(p,reason); remove_recent(o);
            std::move(pending_.begin()+count+1,pending_.begin()+pending_count_,pending_.begin()+count);
            --pending_count_; rejected=true;
        } else ++count;
    }
    for(std::size_t n=0;n<count;++n) {
        const auto& p=pending_[n]; const auto& o=p.observation;
        planner_.apply_segment_target(p.target,o.availability==TargetAvailability::available,o.receipt_time_us);
        diagnostics_.availability=o.availability;
        diagnostics_.last_observation_receipt_us=o.receipt_time_us;
        applied_capture_=o.capture_time_us;
        diagnostics_.last_apply_us=boundary;
        diagnostics_.resolved_target=p.target; diagnostics_.reference_position=p.reference;
        timeout_active_=o.availability!=TargetAvailability::available;
        if(timeout_active_) ++diagnostics_.target_unavailable_events;
    }
    if(count) {
        std::move(pending_.begin()+count,pending_.begin()+pending_count_,pending_.begin());
        pending_count_-=count; observations_since_decision_=true;
    }
    if(rejected) recompute_watermarks();
    auto receipt=diagnostics_.last_observation_receipt_us;
    if(options_.inactivity_timeout_us>=0 && receipt>=0 && !timeout_active_ &&
        boundary-receipt>options_.inactivity_timeout_us) {
        // Expiry is an event at this decision boundary, never historical loss.
        planner_.apply_segment_target({},false,boundary);
        diagnostics_.availability=TargetAvailability::unavailable;
        ++diagnostics_.target_timeout_events; ++diagnostics_.target_unavailable_events;
        timeout_active_=true;
    }
    if(receipt>=0) diagnostics_.last_observation_age_us=boundary-applied_capture_;
    return count;
}
std::size_t SegmentController::next_segment(std::span<Sample> out,SegmentMetadata& metadata) {
    synchronize();
    if(out.size()<interval_) throw std::invalid_argument("segment capacity must cover model commitment");
    auto before=planner_.diagnostics();
    auto start=before.numerical_time_us; auto initial=planner_.position();
    // Determine range only after applying targets, so idle is exactly one
    // original stationary step and a remainder never spans another decision.
    auto maximum=static_cast<std::int64_t>(interval_)*SAMPLE_US;
    if(start>std::numeric_limits<std::int64_t>::max()-maximum)
        throw std::invalid_argument("segment timestamp would overflow");
    const auto clock=std::chrono::steady_clock::now();
    std::size_t written;
    ++generation_attempts_; last_attempt_start_=start; last_attempt_end_=-1;
    try {
        if(before.pending_samples==0) apply_pending(start);
        auto count=before.pending_samples?before.pending_samples:(planner_.target_available()?interval_:1u);
        last_attempt_end_=start+count*SAMPLE_US;
        written=planner_.advance_impl(last_attempt_end_,out.data(),out.size());
    } catch(...) {
        ++diagnostics_.segment_generation_errors;
        if (!failure_.reset_required) failure_=planner_.failed()?planner_.failure():detail::current_failure();
        planner_.latch_failure(failure_);
        detail::rethrow_generation_failure();
    }
    auto elapsed=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-clock).count();
    auto after=planner_.diagnostics(); expected_time_=after.numerical_time_us;
    SegmentMetadata m;
    m.segment_id=next_id_++; m.variant_ms=interval_; m.sample_count=static_cast<std::uint32_t>(written);
    m.start_timestamp_us=start; m.end_timestamp_us=expected_time_; m.first_sample_timestamp_us=out[0].timestamp_us;
    m.last_sample_timestamp_us=out[written-1].timestamp_us; m.duration_us=expected_time_-start;
    m.initial_position=initial; m.final_position=planner_.position(); m.mode=planner_.mode();
    m.fresh_planning_decision=after.decision_count>before.decision_count;
    m.motor_inference=after.motor_evaluations>before.motor_evaluations;
    m.selected_head=after.selected_head; m.selected_brake_head=after.selected_brake_head;
    ++diagnostics_.total_segments;
    if(m.fresh_planning_decision) {
        ++diagnostics_.fresh_planning_decisions;
        if(observations_since_decision_) ++diagnostics_.decisions_with_new_observation;
        else ++diagnostics_.decisions_without_new_observation;
        observations_since_decision_=false;
    }
    diagnostics_.last_planning_latency_us=elapsed; metadata=current_=m;
    return written;
}
SegmentMetadata SegmentController::metadata() {
    synchronize(true);
    if(!current_.segment_id) throw std::logic_error("no segment has been generated");
    return current_;
}
SegmentDiagnostics SegmentController::diagnostics() {
    synchronize(true); auto result=diagnostics_; result.pending_observations=static_cast<std::uint32_t>(pending_count_);
    return result;
}
SegmentRecoveryDiagnostics SegmentController::recovery_diagnostics() {
    synchronize(true); auto result=recovery_;
    result.pending_observations=static_cast<std::uint32_t>(pending_count_);
    return result;
}
SegmentState SegmentController::state() {
    synchronize(true);
    SegmentState s;
    s.failure=failure_.reset_required?failure_:planner_.failure();
    s.numerical_time_us=planner_.diagnostics().numerical_time_us;
    s.requested_time_us=planner_.requested_time_us();
    s.generation_attempts=generation_attempts_;
    s.last_attempt_start_us=last_attempt_start_; s.last_attempt_end_us=last_attempt_end_;
    s.last_segment_id=current_.segment_id;
    if(current_.segment_id) {
        s.last_segment_start_us=current_.start_timestamp_us;
        s.last_segment_end_us=current_.end_timestamp_us;
    }
    s.relative_origin_earliest_us=current_.segment_id?current_.start_timestamp_us:expected_time_;
    s.relative_origin_latest_us=s.numerical_time_us;
    s.last_origin_error_receipt_us=last_origin_error_receipt_;
    return s;
}
void SegmentController::clear_pending_observations() {
    synchronize();
    for(std::size_t n=0;n<pending_count_;++n) remove_recent(pending_[n].observation);
    pending_count_=0; recompute_watermarks();
    ++recovery_.explicit_pending_clears;
}
void SegmentController::reset_diagnostics() {
    synchronize(true);
    auto error=recovery_; recovery_={};
    recovery_.last_recoverable_error=error.last_recoverable_error;
    recovery_.last_error_capture_us=error.last_error_capture_us;
    recovery_.last_error_receipt_us=error.last_error_receipt_us;
    auto old=diagnostics_; diagnostics_={};
    diagnostics_.availability=old.availability; diagnostics_.last_rejection=old.last_rejection;
    diagnostics_.last_capture_us=old.last_capture_us; diagnostics_.last_receipt_us=old.last_receipt_us;
    diagnostics_.last_apply_us=old.last_apply_us; diagnostics_.last_observation_age_us=old.last_observation_age_us;
    diagnostics_.last_planning_latency_us=old.last_planning_latency_us;
    diagnostics_.last_observation_receipt_us=old.last_observation_receipt_us;
    diagnostics_.resolved_target=old.resolved_target; diagnostics_.reference_position=old.reference_position;
}
void SegmentController::reset_segment_session() {check_parent();planner_.reset();synchronize();}
void SegmentController::render_segment(Renderer& renderer,std::span<const Sample> samples,
    const SegmentMetadata& m,double gain,std::span<Report> output) {
    if(samples.size()!=m.sample_count || output.size()<samples.size() || !std::isfinite(gain) || gain<=0)
        throw std::invalid_argument("invalid segment Renderer buffers or calibration");
    const double scale=gain/COMMON_RADIANS_PER_COUNT;
    if(!std::isfinite(scale) || scale<=0) throw std::invalid_argument("invalid Renderer scale");
    auto prior=m.initial_position;
    // Validate the whole segment before changing Renderer state.
    for(const auto& sample:samples) {
        auto v=(sample.position-prior)/scale;
        for(double x:{v.x,v.y}) {
            double q=double(float(x))*65536.;
            if(!std::isfinite(q) || q<double(INT32_MIN)-.5 || q>=double(INT32_MAX)+.5)
                throw std::invalid_argument("Renderer intent exceeds Q16 range");
        }
        prior=sample.position;
    }
    prior=m.initial_position;
    for(std::size_t n=0;n<samples.size();++n) {
        output[n]=renderer.step((samples[n].position-prior)/scale);prior=samples[n].position;
    }
}
} // namespace abc
