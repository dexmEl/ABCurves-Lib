#include "abcurves/native.hpp"
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>
using Clock = std::chrono::steady_clock;
double us(Clock::duration d) {
  return std::chrono::duration<double, std::micro>(d).count();
}
int main(int argc, char **argv) try {
  if (argc != 6)
    return 2;
  std::ifstream in(argv[2]), profile_in(argv[3]);
  int duration, np, ne;
  unsigned long long seed;
  abc::Vec2 initial;
  in >> duration >> seed >> initial.x >> initial.y >> np >> ne;
  abc::PlannerOptions options;
  options.seed = seed;
  options.initial_position = initial;
  options.initial_history.resize(np);
  for (auto &h : options.initial_history)
    in >> h.x >> h.y;
  struct E {
    long long t;
    abc::Vec2 p;
  };
  std::vector<E> events(ne);
  for (auto &e : events)
    in >> e.t >> e.p.x >> e.p.y;
  std::size_t n;
  double scale;
  unsigned long long ignored;
  profile_in >> n >> scale >> ignored;
  std::vector<abc::Report> profile(256);
  for (auto &r : profile) {
    int x, y;
    profile_in >> x >> y;
    r = {std::int16_t(x), std::int16_t(y)};
  }
  if (!in || !profile_in)
    throw std::runtime_error("invalid pipeline fixture");
  auto model = abc::Model::load(argv[1]);
  abc::ContinuousPlanner planner(model, options);
  abc::Renderer renderer(argv[4]);
  renderer.observe_profile(profile);
  renderer.begin(seed);
  auto run = [&](bool collect) {
    planner.reset(seed);
    renderer.observe_profile(profile);
    renderer.begin(seed);
    for (auto &e : events)
      planner.update_target(e.p, e.t);
    abc::Vec2 previous = initial;
    std::vector<double> timings;
    timings.reserve(duration);
    auto start = Clock::now();
    for (int i = 0; i < duration; ++i) {
      abc::Sample sample;
      auto call = Clock::now();
      planner.advance((i + 1LL) * 1000, &sample, 1);
      renderer.step((sample.position - previous) / scale);
      previous = sample.position;
      if (collect)
        timings.push_back(us(Clock::now() - call));
    }
    double total = us(Clock::now() - start);
    return std::make_pair(total, timings);
  };
  run(false);
  auto result = run(true);
  auto batch = run(false);
  std::ofstream out(argv[5]);
  out << std::setprecision(12) << "total_us,sample_us\n";
  for (auto t : result.second)
    out << batch.first << ',' << t << '\n';
  return 0;
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
