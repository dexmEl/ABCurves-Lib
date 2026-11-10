#include "abcurves/planner.hpp"
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>
#ifdef ABC_EXTENDED_DIAGNOSTICS
#include "matvec.hpp"
#endif
#ifdef ABC_PROFILE
#include "numerical.hpp"
#endif
#ifdef _WIN32
#include <windows.h>

#include <psapi.h>
#else
#include <ctime>
#endif
using Clock = std::chrono::steady_clock;
double cpu_us() {
#ifdef _WIN32
  FILETIME created, exited, kernel, user;
  GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user);
  ULARGE_INTEGER k{}, u{}; k.LowPart=kernel.dwLowDateTime; k.HighPart=kernel.dwHighDateTime;
  u.LowPart=user.dwLowDateTime; u.HighPart=user.dwHighDateTime;
  return double(k.QuadPart+u.QuadPart)/10.;
#else
  return double(std::clock())*1e6/CLOCKS_PER_SEC;
#endif
}
double us(Clock::duration d) {
  return std::chrono::duration<double, std::micro>(d).count();
}
int main(int argc, char **argv) try {
  if (argc != 4)
    return 2;
  std::ifstream in(argv[2]);
  int duration, np, ne;
  unsigned long long seed;
  abc::Vec2 initial;
  in >> duration >> seed >> initial.x >> initial.y >> np >> ne;
  abc::PlannerOptions o;
  o.seed = seed;
  o.initial_position = initial;
  o.initial_history.resize(np);
  for (auto &v : o.initial_history)
    in >> v.x >> v.y;
  struct Event {
    long long t;
    abc::Vec2 p;
  };
  std::vector<Event> events(ne);
  for (auto &e : events)
    in >> e.t >> e.p.x >> e.p.y;
  if (!in)
    throw std::runtime_error("bad fixture");
  auto start = Clock::now();
  auto model = abc::Model::load(argv[1]);
  double load = us(Clock::now() - start);
  start = Clock::now();
  abc::ContinuousPlanner p(model, o);
  double init = us(Clock::now() - start);
  for (auto &e : events)
    p.update_target(e.p, e.t);
  std::vector<abc::Sample> samples(duration);
  // Warm every planner path using an independent throwaway instance.
  {
    abc::ContinuousPlanner w(model, o);
    w.update_target({320, 120}, 0);
    w.advance(1000000, samples.data(), samples.size());
  }
  struct Row {
    double time;
    abc::Diagnostics d;
  };
#ifdef ABC_PROFILE
  for(auto& v:abc::detail::profile().samples)v.clear();
#endif
  std::vector<Row> rows(duration);
  for (int i = 0; i < duration; ++i) {
    start = Clock::now();
    p.advance((i + 1LL) * 1000, &samples[i], 1);
    rows[i] = {us(Clock::now() - start), p.diagnostics()};
  }
  abc::ContinuousPlanner b(model, o);
  for (auto &e : events)
    b.update_target(e.p, e.t);
  std::vector<abc::Sample> batch(duration);
  double cpu_start=cpu_us();
  start = Clock::now();
  b.advance(duration * 1000LL, batch.data(), batch.size());
  double total = us(Clock::now() - start);
  double cpu_total=cpu_us()-cpu_start;
  double memory = 0;
#ifdef _WIN32
  PROCESS_MEMORY_COUNTERS_EX mem{};
  mem.cb = sizeof(mem);
  GetProcessMemoryInfo(GetCurrentProcess(),
                       reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&mem),
                       sizeof(mem));
  memory = double(mem.PrivateUsage);
#endif
  std::ofstream out(std::string(argv[3]) + ".csv");
  out << std::setprecision(17) << "t,x,y,dx,dy,us,decisions,motors,mode,head";
#ifdef ABC_EXTENDED_DIAGNOSTICS
  out << ",decision_us,receipt_us,pending";
#endif
  out << '\n';
  for (int i = 0; i < duration; ++i) {
    auto &s = samples[i];
    auto &r = rows[i];
    out << s.timestamp_us << ',' << s.position.x << ',' << s.position.y << ','
        << s.displacement.x << ',' << s.displacement.y << ',' << r.time << ','
        << r.d.decision_count << ',' << r.d.motor_evaluations << ','
        << int(r.d.mode) << ',' << r.d.selected_head;
#ifdef ABC_EXTENDED_DIAGNOSTICS
    out << ',' << r.d.last_decision_us << ',' << r.d.last_target_receipt_us
        << ',' << r.d.pending_samples;
#endif
    out << '\n';
  }
  std::ofstream meta(std::string(argv[3]) + ".json");
  meta << std::setprecision(12) << "{\"model_load_us\":" << load
       << ",\"initialize_us\":" << init << ",\"batch_trace_us\":" << total
       << ",\"process_cpu_us\":" << cpu_total
       << ",\"private_bytes\":" << memory;
#ifdef ABC_EXTENDED_DIAGNOSTICS
  meta<<",\"kernel\":\""<<abc::detail::kernel_name()<<"\"";
#endif
  meta<<"}\n";
#ifdef ABC_PROFILE
  std::ofstream prof(std::string(argv[3]) + "_profile.csv");
  prof << "stage,us\n";
  auto &stages = abc::detail::profile().samples;
  for (int s = 0; s < abc::detail::stage_count; ++s)
    for (auto t : stages[s])
      prof << s << ',' << t << '\n';
#endif
  return 0;
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
