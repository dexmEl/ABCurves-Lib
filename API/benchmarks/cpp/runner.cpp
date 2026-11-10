#include "abcurves.h"
#include <abcurves/native.hpp>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>

#include <psapi.h>
#endif
using Clock = std::chrono::steady_clock;
double us(Clock::duration d) { return std::chrono::duration<double, std::micro>(d).count(); }
void check(abc_status s) {
  if (s)
    throw std::runtime_error(abc_last_error());
}
struct Event {
  int64_t t;
  abc::Vec2 p;
};
int main(int argc, char **argv) try {
  if (argc != 8) {
    std::cerr
        << "runner direct|cabi model fixture output-stem renderer-model renderer-fixture batch\n";
    return 2;
  }
  bool direct = std::string(argv[1]) == "direct";
  int batch = std::stoi(argv[7]);
  if (batch < 1 || batch > 8000)
    return 2;
  std::ifstream in(argv[3]);
  int duration, np, ne;
  uint64_t seed;
  abc::PlannerOptions o;
  in >> duration >> seed >> o.initial_position.x >> o.initial_position.y >> np >> ne;
  o.seed = seed;
  o.initial_history.resize(np);
  for (auto &v : o.initial_history)
    in >> v.x >> v.y;
  std::vector<Event> events(ne);
  for (auto &e : events)
    in >> e.t >> e.p.x >> e.p.y;
  if (!in)
    throw std::runtime_error("bad fixture");
  std::ifstream rf(argv[6]);
  size_t rn;
  double scale;
  uint64_t rs;
  rf >> rn >> scale >> rs;
  std::vector<abc::Report> profile(256);
  std::vector<abc_report> cprofile(256);
  for (size_t i = 0; i < 256; i++) {
    int x, y;
    rf >> x >> y;
    profile[i] = {int16_t(x), int16_t(y)};
    cprofile[i] = {int16_t(x), int16_t(y)};
  }
  if (!rf)
    throw std::runtime_error("bad Renderer fixture");
  std::shared_ptr<const abc::Model> m;
  std::unique_ptr<abc::ContinuousPlanner> p;
  std::unique_ptr<abc::Renderer> r;
  abc_model_handle cm = 0;
  abc_planner_handle cp = 0;
  abc_renderer_handle cr = 0;
  auto start = Clock::now();
  if (direct)
    m = abc::Model::load(argv[2]);
  else
    check(abc_model_load(argv[2], &cm));
  double load = us(Clock::now() - start);
  start = Clock::now();
  if (direct)
    p = std::make_unique<abc::ContinuousPlanner>(m, o);
  else {
    abc_planner_options options;
    abc_planner_options_init(&options, sizeof(options));
    options.seed = seed;
    options.initial_position = {o.initial_position.x, o.initial_position.y};
    std::vector<abc_vec2> h;
    for (auto v : o.initial_history)
      h.push_back({v.x, v.y});
    options.history = h.data();
    options.history_count = h.size();
    check(abc_planner_create(cm, &options, &cp));
  }
  double initialize = us(Clock::now() - start);
  if (direct) {
    r = std::make_unique<abc::Renderer>(argv[5]);
    r->observe_profile(profile);
  } else {
    check(abc_renderer_create(argv[5], &cr));
    check(abc_renderer_set_profile(cr, cprofile.data(), 256));
  }
  auto reset = [&] {
    if (direct) {
      p->reset(seed);
      r->observe_profile(profile);
      r->begin(seed);
      for (auto e : events)
        p->update_target(e.p, e.t);
    } else {
      check(abc_planner_reset(cp, seed));
      check(abc_renderer_reset(cr, seed));
      for (auto e : events)
        check(abc_planner_update_target(cp, {e.p.x, e.p.y}, e.t));
    }
  };
  std::vector<abc::Sample> a(duration);
  std::vector<abc_sample> b(duration);
  std::vector<abc_report> reports(duration);
  std::vector<abc_planner_state> states(duration);
  auto state = [&] {
    abc_planner_state s{};
    s.struct_size = sizeof(s);
    if (direct) {
      auto d = p->diagnostics();
      s.mode = uint32_t(p->mode());
      s.numerical_time_us = d.numerical_time_us;
      s.decision_count = d.decision_count;
      s.motor_evaluations = d.motor_evaluations;
      s.selected_head = d.selected_head;
      s.selected_brake_head = d.selected_brake_head;
      s.last_decision_us = d.last_decision_us;
      s.last_target_receipt_us = d.last_target_receipt_us;
      s.pending_samples = d.pending_samples;
      s.position = {p->position().x, p->position().y};
    } else
      check(abc_planner_get_state(cp, &s));
    return s;
  };
  reset();
  if (direct)
    p->advance(duration * 1000LL, a.data(), a.size());
  else {
    uint64_t n;
    check(abc_planner_advance(cp, duration * 1000LL, b.data(), b.size(), &n));
  }
  reset();
  struct Timing {
    int64_t t;
    int n;
    double us, renderer;
    uint64_t decisions, motors;
  };
  std::vector<Timing> timings;
  abc_vec2 previous{o.initial_position.x, o.initial_position.y};
  for (int i = 0; i < duration;) {
    int n = std::min(batch, duration - i);
    auto before = state();
    start = Clock::now();
    if (direct)
      p->advance((i + n) * 1000LL, a.data() + i, n);
    else {
      uint64_t written;
      check(abc_planner_advance(cp, (i + n) * 1000LL, b.data() + i, n, &written));
      if (written != uint64_t(n))
        throw std::runtime_error("wrong count");
    }
    double elapsed = us(Clock::now() - start);
    auto after = state();
    if (direct)
      for (int j = i; j < i + n; j++)
        b[j] = {a[j].timestamp_us,
                {a[j].position.x, a[j].position.y},
                {a[j].displacement.x, a[j].displacement.y}};
    start = Clock::now();
    if (direct) {
      for (int j = i; j < i + n; j++) {
        auto v = r->step(
            {(b[j].position.x - previous.x) / scale, (b[j].position.y - previous.y) / scale});
        reports[j] = {v.dx, v.dy};
        previous = b[j].position;
      }
    } else {
      uint64_t written;
      check(abc_renderer_render_samples(cr, b.data() + i, n, &previous,
                                        scale * abc::COMMON_RADIANS_PER_COUNT, reports.data() + i,
                                        n, &written));
    }
    double render = us(Clock::now() - start);
    for (int j = i; j < i + n; j++)
      states[j] = after;
    timings.push_back({(i + n) * 1000LL, n, elapsed, render,
                       after.decision_count - before.decision_count,
                       after.motor_evaluations - before.motor_evaluations});
    i += n;
  }
  // Untimed output IO; one additional reset batch measures total planner compute.
  reset();
  start = Clock::now();
  if (direct)
    p->advance(duration * 1000LL, a.data(), a.size());
  else {
    uint64_t n;
    check(abc_planner_advance(cp, duration * 1000LL, b.data(), b.size(), &n));
  }
  double trace = us(Clock::now() - start);
  std::ofstream samples(std::string(argv[4]) + ".samples.csv");
  samples << "t,x,y,dx,dy,mode,head,decisions,motors,decision_us,receipt_us,pending,rdx,rdy\n"
          << std::setprecision(17);
  for (int i = 0; i < duration; i++) {
    auto &s = b[i];
    auto &d = states[i];
    samples << s.timestamp_us << ',' << s.position.x << ',' << s.position.y << ','
            << s.displacement.x << ',' << s.displacement.y << ',' << d.mode << ','
            << d.selected_head << ',' << d.decision_count << ',' << d.motor_evaluations << ','
            << d.last_decision_us << ',' << d.last_target_receipt_us << ',' << d.pending_samples
            << ',' << reports[i].dx << ',' << reports[i].dy << '\n';
  }
  std::ofstream timing(std::string(argv[4]) + ".timings.csv");
  timing << "t,n,us,renderer_us,decisions,motors\n" << std::setprecision(17);
  for (auto row : timings)
    timing << row.t << ',' << row.n << ',' << row.us << ',' << row.renderer << ',' << row.decisions
           << ',' << row.motors << '\n';
  std::ofstream overhead(std::string(argv[4]) + ".overhead.csv");
  overhead << "calls,us\n" << std::setprecision(17);
  uint64_t sum = 0;
  for (int rep = 0; rep < 50; rep++) {
    start = Clock::now();
    for (int i = 0; i < 1000; i++)
      sum += state().numerical_time_us;
    overhead << 1000 << ',' << us(Clock::now() - start) << '\n';
  }
  size_t memory = 0;
#ifdef _WIN32
  PROCESS_MEMORY_COUNTERS_EX mem{};
  mem.cb = sizeof(mem);
  GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&mem),
                       sizeof(mem));
  memory = mem.PrivateUsage;
#endif
  abc_model_info info{};
  info.struct_size = sizeof(info);
  if (!direct)
    check(abc_model_get_info(cm, &info));
  std::ofstream meta(std::string(argv[4]) + ".json");
  meta << std::setprecision(17) << "{\"load_us\":" << load << ",\"initialize_us\":" << initialize
       << ",\"batch_trace_us\":" << trace << ",\"private_bytes\":" << memory
       << ",\"model_sha256\":\"" << (direct ? m->sha256() : info.sha256)
       << "\",\"state_checksum\":" << sum << "}\n";
  if (!direct) {
    check(abc_renderer_destroy(cr));
    check(abc_planner_destroy(cp));
    check(abc_model_release(cm));
  }
  return 0;
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
