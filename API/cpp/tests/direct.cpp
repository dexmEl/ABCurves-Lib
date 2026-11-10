#include "abcurves.h"
#include <abcurves/native.hpp>
#include <cmath>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
void check(bool b) {
  if (!b)
    throw std::runtime_error(abc_last_error());
}
void run(const std::filesystem::path &dir) {
  auto m = abc::Model::load(dir);
  abc::PlannerOptions o;
  o.seed = 13;
  o.initial_history.resize(160);
  for (int i = 0; i < 160; i++)
    o.initial_history[i] = {.2 * std::sin(i * .02), .1 * std::cos(i * .03)};
  abc::ContinuousPlanner direct(m, o);
  abc_model_handle model;
  abc_planner_handle planner;
  check(!abc_model_load(dir.string().c_str(), &model));
  abc_planner_options options;
  abc_planner_options_init(&options, sizeof(options));
  options.seed = 13;
  std::vector<abc_vec2> h;
  for (auto p : o.initial_history)
    h.push_back({p.x, p.y});
  options.history = h.data();
  options.history_count = 160;
  check(!abc_planner_create(model, &options, &planner));
  check(!abc_model_release(model));
  for (int i = 0; i < 20; i++) {
    abc::Vec2 p{200 * std::cos(i * .5), 90 * std::sin(i * .7)};
    direct.update_target(p, i * 13123);
    check(!abc_planner_update_target(planner, {p.x, p.y}, i * 13123));
  }
  std::vector<abc::Sample> a(1000);
  std::vector<abc_sample> b(1000);
  direct.advance(1000000, a.data(), a.size());
  uint64_t written;
  check(!abc_planner_advance(planner, 1000000, b.data(), b.size(), &written));
  check(written == 1000);
  for (size_t i = 0; i < 1000; i++)
    check(a[i].position.x == b[i].position.x &&
          a[i].position.y == b[i].position.y &&
          a[i].displacement.x == b[i].displacement.x &&
          a[i].displacement.y == b[i].displacement.y &&
          a[i].timestamp_us == b[i].timestamp_us);
  direct.clear_target(1000000);
  check(!abc_planner_clear_target(planner, 1000000));
  direct.advance(1100000, a.data(), a.size());
  check(!abc_planner_advance(planner, 1100000, b.data(), b.size(), &written));
  check(written == 100);
  for (size_t i = 0; i < written; i++)
    check(a[i].timestamp_us == b[i].timestamp_us &&
          a[i].position.x == b[i].position.x &&
          a[i].position.y == b[i].position.y &&
          a[i].displacement.x == b[i].displacement.x &&
          a[i].displacement.y == b[i].displacement.y);
  check(b[99].displacement.x == 0 && b[99].displacement.y == 0);
  check(!abc_planner_destroy(planner));
}
int main(int argc, char **argv) try {
  if (argc != 2)
    return 2;
  std::vector<std::thread> t;
  for (auto c : {32, 16, 8})
    t.emplace_back([=] {
      run(std::filesystem::path(argv[1]) / (std::to_string(c) + "ms"));
    });
  for (auto &x : t)
    x.join();
  check(abc_check_abi(99, 0) == ABC_UNSUPPORTED_VERSION);
  const std::string main_error = abc_last_error();
  std::thread error_thread([] {
    check(abc_check_abi(1, 0) == ABC_OK);
    check(std::string(abc_last_error()).empty());
    check(abc_planner_reset(0, 7) == ABC_INVALID_HANDLE);
    check(std::string(abc_last_error()).find("handle") != std::string::npos);
  });
  error_thread.join();
  check(main_error == abc_last_error());
  std::cout << "Concurrent direct C++ / C ABI parity: pass\n";
} catch (const std::exception &e) {
  std::cerr << e.what();
  return 1;
}
