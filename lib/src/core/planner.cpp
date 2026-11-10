#include "abcurves/planner.hpp"
#include "numerical.hpp"
#include "failure.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
namespace abc {
namespace {
bool finite(Vec2 p) { return std::isfinite(p.x) && std::isfinite(p.y); }
double norm(Vec2 p) { return std::sqrt(p.x * p.x + p.y * p.y); }
std::shared_ptr<const detail::Weights>
shared_weights(const std::shared_ptr<const Model> &m) {
  static std::mutex mutex;
  static std::unordered_map<const Model *, std::weak_ptr<const detail::Weights>>
      cache;
  std::lock_guard lock(mutex);
  for (auto it = cache.begin(); it != cache.end();)
    if (it->second.expired())
      it = cache.erase(it);
    else
      ++it;
  auto &w = cache[m.get()];
  auto p = w.lock();
  if (!p) {
    p = std::make_shared<detail::Weights>(*m);
    w = p;
  }
  return p;
}
} // namespace
struct ContinuousPlanner::Impl {
  struct Event {
    std::int64_t t;
    Vec2 p;
    bool valid;
  };
  PlannerOptions options;
  int interval;
  detail::Neural neural;
  detail::RandomStream motor_rng, selector_rng, brake_rng;
  std::array<Vec2, 1280> history{}, targets{};
  std::array<std::int64_t, 1280> available{};
  std::array<std::uint8_t, 1280> valid{}, motion{};
  std::array<Vec2, 32> actions{}, points{};
  int cursor = 0, pending = 0;
  std::deque<Event> events;
  std::int64_t now = 0, requested = 0, last_receipt = -1, receipt = 0;
  Vec2 target{}, position{};
  bool has_target = false, observed = false, failed = false;
  PlannerFailure failure{};
  int mode = -1;
  double age = 160, duration = 64;
  Vec2 brake_v{}, brake_a{}, hold_goal{}, hold_position{};
  std::array<Vec2, 5> coeff{};
  double hold_error = 0,
         innovation_at = std::numeric_limits<double>::quiet_NaN(),
         resume_integral = 0, stop_integral = 0;
  float resume_budget = 0, stop_budget = 0;
  bool changed = false, previous_valid = false;
  std::array<float, 32> previous{};
  Vec2 origin{};
  bool has_origin = false;
  Diagnostics diagnostics{};
  Impl(std::shared_ptr<const Model> m, PlannerOptions o)
      : options(std::move(o)), interval(m->commitment_ms()),
        neural(shared_weights(m)) {
    if (!finite(options.initial_position))
      throw std::invalid_argument("initial position must be finite");
    if (!options.initial_history.empty() &&
        options.initial_history.size() != 160)
      throw std::invalid_argument(
          "initial history must contain exactly 160 samples");
    for (auto p : options.initial_history)
      if (!finite(p))
        throw std::invalid_argument("history must be finite");
    reset();
  }
  void reset() {
    motor_rng.seed(std::uint32_t(options.seed));
    selector_rng.seed(std::uint32_t(options.seed) ^ 0x5397A1u);
    brake_rng.seed(std::uint32_t(options.seed) ^ 0x1763B4u);
    mode = -1;
    previous_valid = false;
    has_origin = false;
    previous.fill(0);
    diagnostics = {};
    events.clear();
    now = requested = receipt = 0;
    last_receipt = -1;
    position = options.initial_position;
    has_target = observed = failed = false;
    failure = {};
    target = {};
    cursor = 0;
    pending = interval;
    history.fill({});
    targets.fill({});
    available.fill(0);
    valid.fill(0);
    motion.fill(0);
    for (int i = 480; i < 640; ++i) {
      history[i] = history[i + 640] = options.initial_history.empty()
                                          ? Vec2{}
                                          : options.initial_history[i - 480];
      motion[i] = motion[i + 640] = 1;
    }
  }
  void arrive() {
    ABC_TIME(target_events_stage);
    if (events.empty() || events.front().t > now)
      return;
    while (!events.empty() && events.front().t <= now) {
      auto e = events.front();
      events.pop_front();
      receipt = e.t;
      target = e.p;
      has_target = e.valid;
    }
    int at = (cursor + 639) % 640;
    targets[at] = targets[at + 640] = target;
    available[at] = available[at + 640] = receipt;
    valid[at] = valid[at + 640] = has_target;
  }
  void append(Vec2 delta) {
    ABC_TIME(history_write_stage);
    int at = cursor, mirror = at + 640;
    history[at] = history[mirror] = delta;
    targets[at] = targets[mirror] = has_target ? target : Vec2{};
    available[at] = available[mirror] = has_target ? receipt : 0;
    valid[at] = valid[mirror] = has_target;
    motion[at] = motion[mirror] = 1;
    cursor = (at + 1) % 640;
  }
  void initialize(const detail::Observation &o, Vec2 goal) {
    mode = 2;
    for (int i = 608; i < 640; ++i)
      if (std::max(std::abs(o.h[i].x), std::abs(o.h[i].y)) > 1e-12)
        mode = 0;
    age = 160;
    duration = 64;
    brake_v = brake_a = {};
    coeff.fill({});
    hold_goal = goal;
    hold_position = position;
    hold_error = norm(goal - position);
    changed = hold_error > 1e-7;
    innovation_at =
        changed ? now / 1000. : std::numeric_limits<double>::quiet_NaN();
    resume_integral = stop_integral = 0;
    resume_budget = -std::log(std::max(selector_rng.uniform(), 1e-8f));
    stop_budget = -std::log(std::max(brake_rng.uniform(), 1e-8f));
  }
  void motor(const detail::Observation &o) {
    auto f = detail::motor_features(o);
    neural.motor(f);
    neural.geometry(o.h[639]);
    neural.selector(previous, previous_valid,
                    has_origin ? position - origin : Vec2{}, interval);
    int chosen;
    {
      ABC_TIME(rng_stage);
      chosen = motor_rng.categorical(neural.probabilities);
    }
    std::copy_n(neural.heads.data() + chosen * 32, 32, previous.begin());
    origin = position;
    has_origin = previous_valid = true;
    neural.decode(chosen, o.h[639], interval, actions.data());
    diagnostics.selected_head = chosen;
    ++diagnostics.motor_evaluations;
  }
  void plan() {
    ABC_TIME(policy_stage);
    if (!observed) {
      for (int i = 0; i < 480; ++i) {
        int at = (cursor + i) % 640;
        history[at] = history[at + 640] = {};
        targets[at] = targets[at + 640] = {};
        available[at] = available[at + 640] = 0;
        valid[at] = valid[at + 640] = motion[at] = motion[at + 640] = 0;
      }
      observed = true;
    }
    detail::Observation o{history.data() + cursor,
                          targets.data() + cursor,
                          available.data() + cursor,
                          valid.data() + cursor,
                          motion.data() + cursor,
                          position,
                          now};
    bool known = o.v[639] && o.a[639] <= now;
    auto goal = known ? o.g[639] : position;
    if (mode < 0)
      initialize(o, goal);
    int old = mode;
    if (old != 0) {
      previous_valid = false;
      for (int i = 640 - interval; i < 640; ++i)
        if (o.v[i] && o.a[i] <= now + (i - 639LL) * 1000 &&
            norm(o.g[i] - hold_goal) > 1e-7) {
          changed = true;
          if (std::isnan(innovation_at))
            innovation_at = now / 1000. - interval + 1 + (i - (640 - interval));
          break;
        }
    }
    double innovation_age = std::isnan(innovation_at)
                                ? 0
                                : std::max(0., now / 1000. - innovation_at);
    auto f = detail::event_features(
        o, {age, hold_error, innovation_age, hold_goal, hold_position, old});
    bool innovation =
        old == 2 &&
        (changed || norm(position - hold_position) > 1e-5 || f.raw[0] > 10);
    std::array<float, 2> probabilities{};
    bool full = options.diagnostics, need_hazard = old == 0 || innovation;
    if (full)
      neural.brake(f.context);
    if (full || need_hazard) {
      neural.hazards(f.context);
      for (int i = 0; i < 2; ++i)
        probabilities[i] =
            1.f / (1.f + std::exp(-std::clamp(neural.hazard[i], -40.f, 40.f)));
    }
    if (!std::isfinite(probabilities[0]) || !std::isfinite(probabilities[1]))
      throw detail::GenerationError(FailureCategory::numerical, FailureReason::nonfinite_policy,
                                    "nonfinite event probability");
    float draw0 = selector_rng.uniform(), draw1 = selector_rng.uniform();
    int brake_head = -1;
    std::array<float, 16> brake_prob;
    if (full) {
      detail::softmax(neural.frequency.data(), brake_prob);
      brake_head = brake_rng.categorical(brake_prob);
    }
    if (old == 0) {
      if (known)
        stop_integral +=
            -std::log(std::max(1e-8, 1 - double(probabilities[0]))) *
            (interval / 32.);
      if (stop_integral >= stop_budget && known) {
        if (!full) {
          neural.brake(f.context);
          detail::softmax(neural.frequency.data(), brake_prob);
          brake_head = brake_rng.categorical(brake_prob);
        }
        mode = 1;
        age = 0;
        brake_v = o.h[639];
        Vec2 a{}, b{};
        for (int i = 632; i < 640; ++i)
          a = a + o.h[i];
        for (int i = 624; i < 632; ++i)
          b = b + o.h[i];
        brake_a = (a / 8. - b / 8.) / 8.;
        duration = neural.duration[brake_head];
        for (int j = 0; j < 5; ++j) {
          double x = neural.brake_coeff[brake_head * 10 + j * 2],
                 y = neural.brake_coeff[brake_head * 10 + j * 2 + 1];
          coeff[j] = {x * f.axis.x - y * f.axis.y, x * f.axis.y + y * f.axis.x};
        }
        hold_goal = goal;
        hold_error = f.raw[0];
        changed = false;
        innovation_at = std::numeric_limits<double>::quiet_NaN();
      }
    } else if (old == 2) {
      if (innovation && known)
        resume_integral +=
            -std::log(std::max(1e-8, 1 - double(probabilities[1]))) *
            (interval / 32.);
      if (innovation && resume_integral >= resume_budget && known) {
        mode = 0;
        age = 0;
        changed = false;
        innovation_at = std::numeric_limits<double>::quiet_NaN();
        stop_integral = 0;
        stop_budget = float(-std::log(std::max(double(draw0), 1e-8)));
      }
    }
    if (!full && brake_head < 0)
      brake_rng.discard();
    diagnostics.selected_head = -1;
    diagnostics.selected_brake_head = brake_head;
    if (mode == 0 || full)
      motor(o);
    else
      motor_rng.discard();
    if (mode == 1) {
      ABC_TIME(brake_path_stage);
      auto prior = detail::c2_path(brake_v, brake_a, duration, coeff, age);
      Vec2 sum{};
      for (int i = 0; i < interval; ++i) {
        auto next =
            detail::c2_path(brake_v, brake_a, duration, coeff, age + i + 1);
        actions[i] = next - prior;
        sum = sum + actions[i];
        prior = next;
      }
      age += interval;
      if (age >= duration) {
        mode = 2;
        age = std::max(0., age - duration);
        hold_position = position + sum;
        hold_goal = goal;
        hold_error = norm(goal - hold_position);
        changed = false;
        innovation_at = std::numeric_limits<double>::quiet_NaN();
        resume_integral = 0;
        resume_budget = float(-std::log(std::max(double(draw1), 1e-8)));
      }
    } else if (mode == 2) {
      std::fill_n(actions.begin(), interval, Vec2{});
      age += interval;
    } else
      age += interval;
    if (mode != 0)
      previous_valid = false;
    for (int i = 0; i < interval; ++i)
      if (!finite(actions[i]) ||
          std::max(std::abs(actions[i].x), std::abs(actions[i].y)) > 1e5)
        throw detail::GenerationError(FailureCategory::numerical, FailureReason::invalid_primitive,
                                      "invalid finite primitive");
    {
      ABC_TIME(buffer_stage);
      Vec2 block_origin = position;
      for (int offset = 0; offset < interval; offset += 8) {
        Vec2 sum{};
        for (int i = offset; i < offset + 8; ++i) {
          sum = sum + actions[i];
          points[i] = sum + block_origin;
        }
        block_origin = points[offset + 7];
      }
    }
    pending = 0;
    ++diagnostics.decision_count;
    diagnostics.last_decision_us = now;
    diagnostics.mode = static_cast<PlannerMode>(mode);
  }
};
std::shared_ptr<ContinuousPlanner>
ContinuousPlanner::load(const std::filesystem::path &p, PlannerOptions o) {
  return std::make_shared<ContinuousPlanner>(Model::load(p), std::move(o));
}
ContinuousPlanner::ContinuousPlanner(std::shared_ptr<const Model> m,
                                     PlannerOptions o)
    : model_(std::move(m)),
      impl_(model_ ? std::make_unique<Impl>(model_, std::move(o))
                   : throw std::invalid_argument("model is required")),
      commitment_ms_(model_->commitment_ms()) {}
ContinuousPlanner::~ContinuousPlanner() = default;
void ContinuousPlanner::reset() { impl_->reset(); ++revision_; }
void ContinuousPlanner::reset(std::uint64_t seed) {
  impl_->options.seed = seed;
  impl_->reset();
  ++revision_;
}
void ContinuousPlanner::reinitialize(PlannerOptions options) {
  auto replacement = std::make_unique<Impl>(model_, std::move(options));
  impl_ = std::move(replacement);
  ++revision_;
}
std::int64_t ContinuousPlanner::requested_time_us() const noexcept {
  return impl_->requested;
}
void ContinuousPlanner::set_position(Vec2 p) {
  require_direct_access();
  if (!finite(p))
    throw std::invalid_argument("position must be finite");
  impl_->position = p;
}
void ContinuousPlanner::update_target(Vec2 p, std::int64_t t) {
  require_direct_access();
  auto &i = *impl_;
  if (!finite(p) || t < 0 || t < i.now || t < i.last_receipt)
    throw std::invalid_argument("invalid target or receipt time");
  i.events.push_back({t, p, true});
  i.last_receipt = t;
}
void ContinuousPlanner::clear_target(std::int64_t t) {
  require_direct_access();
  auto &i = *impl_;
  if (t < 0 || t < i.now || t < i.last_receipt)
    throw std::invalid_argument("invalid receipt time");
  i.events.push_back({t, {}, false});
  i.last_receipt = t;
}
bool ContinuousPlanner::has_queued_targets() const noexcept { return !impl_->events.empty(); }
bool ContinuousPlanner::target_available() const noexcept { return impl_->has_target; }
Vec2 ContinuousPlanner::segment_position_at(std::int64_t t, std::int64_t start,
                                            Vec2 initial) const {
  const auto& i = *impl_;
  if (t < start || t > i.now)
    throw std::invalid_argument("relative receipt is outside the last segment");
  auto closed = t / SAMPLE_US * SAMPLE_US;
  if (closed == i.now) return i.position;
  if (closed == start) return initial;
  // A segment either drains one commitment or is one stationary idle sample.
  // Read the original absolute points; do not reverse-sum movement history.
  auto decision = i.diagnostics.last_decision_us;
  if (decision >= 0 && closed > decision && closed - decision <= i.interval * SAMPLE_US)
    return i.points[static_cast<std::size_t>((closed - decision) / SAMPLE_US - 1)];
  return initial;
}
void ContinuousPlanner::apply_segment_target(Vec2 p, bool valid_target,
                                              std::int64_t receipt_t) {
  auto& i = *impl_;
  if (!finite(p) || receipt_t < 0 || receipt_t > i.now ||
      i.now - receipt_t >= static_cast<std::int64_t>(HISTORY_SAMPLES) * SAMPLE_US ||
      !i.events.empty())
    throw std::invalid_argument("invalid deferred target or mixed legacy events");
  // Equivalent to legacy arrive at ceil(receipt/1000), followed by append
  // through now. Only target history is changed, never generated motion.
  // With unavailable input, arrive stores receipt at its slot; subsequent
  // append slots store zero availability, matching the original implementation.
  const auto steps = (i.now - receipt_t) / SAMPLE_US;
  for (std::int64_t n = steps; n >= 0; --n) {
    const auto at = static_cast<int>((i.cursor + 639 - n) % 640);
    i.targets[at] = i.targets[at + 640] = valid_target ? p : Vec2{};
    i.available[at] = i.available[at + 640] = valid_target || n == steps ? receipt_t : 0;
    i.valid[at] = i.valid[at + 640] = valid_target;
  }
  i.target = valid_target ? p : Vec2{};
  i.has_target = valid_target;
  i.receipt = receipt_t;
  i.last_receipt = receipt_t;
}
std::size_t ContinuousPlanner::advance(std::int64_t t, Sample *out,
                                       std::size_t capacity) {
  require_direct_access();
  return advance_impl(t, out, capacity);
}
void ContinuousPlanner::require_direct_access() const {
  if (segment_owner_)
    throw std::logic_error("planner input/output is owned by SegmentController; destroy it before direct mutations");
  if (impl_->failed) throw std::logic_error("planner failed; explicit reset required");
}
bool ContinuousPlanner::failed() const noexcept { return impl_->failed; }
PlannerFailure ContinuousPlanner::failure() const noexcept { return impl_->failure; }
void ContinuousPlanner::latch_failure(PlannerFailure failure) noexcept {
  if (!impl_->failed) impl_->failure=failure;
  impl_->failed=true;
}
std::size_t ContinuousPlanner::advance_impl(std::int64_t t, Sample *out,
                                            std::size_t capacity) {
  auto &i = *impl_;
  if (i.failed)
    throw std::runtime_error("planner failed; reset required");
  if (t < 0 || t < i.requested)
    throw std::invalid_argument("advance clock must be monotonic");
  auto count = std::size_t((t - i.now) / 1000);
  if (count > capacity || (count && out == nullptr))
    throw std::invalid_argument("output buffer too small");
  i.requested = t;
  try {
  i.arrive();
  for (std::size_t n = 0; n < count; ++n) {
    if (i.pending == i.interval) {
      if (!i.has_target) {
        i.append({});
        i.now += 1000;
        i.arrive();
        out[n] = {i.now, i.position, {}};
        ++i.diagnostics.buffered_samples;
        continue;
      }
      i.plan();
    }
    i.position = i.points[i.pending];
    auto delta = i.actions[i.pending++];
    i.append(delta);
    i.now += 1000;
    i.arrive();
    out[n] = {i.now, i.position, delta};
    ++i.diagnostics.buffered_samples;
  }
  } catch (...) {
    if (!i.failed) i.failure = detail::current_failure();
    i.failed = true;
    detail::rethrow_generation_failure();
  }
  return count;
}
Sample ContinuousPlanner::generate_sample(std::int64_t t) {
  Sample s;
  if (advance(t, &s, 1) != 1)
    throw std::invalid_argument("one closed sample required");
  return s;
}
Diagnostics ContinuousPlanner::diagnostics() const noexcept {
  auto result = impl_->diagnostics;
  result.numerical_time_us = impl_->now;
  result.last_target_receipt_us = impl_->has_target ? impl_->receipt : -1;
  result.pending_samples =
      static_cast<std::uint32_t>(impl_->interval - impl_->pending);
  return result;
}
Vec2 ContinuousPlanner::position() const noexcept { return impl_->position; }
PlannerMode ContinuousPlanner::mode() const noexcept {
  return impl_->mode < 0 ? PlannerMode::holding
                         : static_cast<PlannerMode>(impl_->mode);
}
} // namespace abc
