#include "abcurves/planner.hpp"
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>
#include <thread>
#include <vector>
namespace {
std::atomic<bool> track = false;
std::atomic<std::size_t> allocations = 0;
} // namespace
void *operator new(std::size_t n) {
  if (track)
    ++allocations;
  if (auto *p = std::malloc(n ? n : 1))
    return p;
  throw std::bad_alloc();
}
void operator delete(void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { std::free(p); }
void *operator new[](std::size_t n) { return ::operator new(n); }
void operator delete[](void *p) noexcept { ::operator delete(p); }
void operator delete[](void *p, std::size_t) noexcept { ::operator delete(p); }
void require(bool b, const char *s) {
  if (!b)
    throw std::runtime_error(s);
}
template <class F> void rejects(F f) {
  bool threw = false;
  try {
    f();
  } catch (const std::exception &) {
    threw = true;
  }
  require(threw, "invalid operation accepted");
}
int main(int argc, char **argv) try {
  if (argc != 2)
    return 2;
  std::vector<std::shared_ptr<const abc::Model>> models;
  std::vector<std::unique_ptr<abc::ContinuousPlanner>> planners;
  for (int c : {32, 16, 8}) {
    auto m = abc::Model::load(std::filesystem::path(argv[1]) /
                              (std::to_string(c) + "ms"));
    models.push_back(m);
    planners.push_back(std::make_unique<abc::ContinuousPlanner>(m));
  }
  for (auto &p : planners) {
    p->update_target({320, 120}, 0);
    p->advance(500, nullptr, 0);
    rejects([&] { p->advance(400, nullptr, 0); });
    abc::Sample first;
    p->advance(1000, &first, 1);
    require(p->diagnostics().decision_count == 1,
            "first decision must be at zero");
    p->update_target({-70, 20}, 1001);
    int c = p->planning_interval_ms();
    std::vector<abc::Sample> out(c);
    p->advance(c * 1000, out.data(), out.size());
    require(p->diagnostics().decision_count == 1,
            "commitment boundary changed");
    p->advance(c * 1000 + 1000, &first, 1);
    require(p->diagnostics().decision_count == 2, "missing fresh decision");
  }
  std::array<std::vector<abc::Sample>, 3> expected, concurrent;
  for (int k = 0; k < 3; ++k) {
    auto &p = *planners[k];
    p.reset(11);
    p.update_target({250, 90}, 0);
    p.update_target({-180, 60}, 250123);
    expected[k].resize(1000);
    p.advance(1000000, expected[k].data(), 1000);
    p.reset(11);
    p.update_target({250, 90}, 0);
    p.update_target({-180, 60}, 250123);
    concurrent[k].resize(1000);
  }
  std::vector<std::thread> threads;
  for (int k = 0; k < 3; ++k)
    threads.emplace_back(
        [&, k] { planners[k]->advance(1000000, concurrent[k].data(), 1000); });
  for (auto &t : threads)
    t.join();
  for (int k = 0; k < 3; ++k)
    for (int i = 0; i < 1000; ++i)
      require(expected[k][i].position.x == concurrent[k][i].position.x &&
                  expected[k][i].position.y == concurrent[k][i].position.y,
              "concurrent state leaked");
  std::size_t measured = 0;
  for (auto &p : planners) {
    p->reset(13);
    p->update_target({350, 90}, 0);
    std::vector<abc::Sample> out(1000);
    allocations = 0;
    track = true;
    p->advance(1000000, out.data(), 1000);
    track = false;
    measured += allocations;
  }
  require(measured == 0, "heap allocation in steady-state generation");
  for (auto &p : planners) {
    p->reset();
    std::vector<abc::Sample> out(1000);
    p->advance(1000000, out.data(), 1000);
    require(p->diagnostics().decision_count == 0,
            "uncommanded plan consumed RNG");
    p->update_target({100, 20}, 1000123);
    abc::Sample s;
    p->advance(1001000, &s, 1);
    require(p->diagnostics().decision_count == 0,
            "receipt exposed before next decision");
    p->advance(1002000, &s, 1);
    require(p->diagnostics().decision_count == 1,
            "late acquisition not planned");
    rejects([&] { p->update_target({1, 1}, 1001000); });
    rejects([&] { p->advance(1005000, &s, 1); });
    p->advance(1003000, &s, 1);
  }
  std::cout << "scheduling, reset, simultaneous variants: pass; steady-state "
               "allocations="
            << measured << "\n";
  return 0;
} catch (const std::exception &e) {
  track = false;
  std::cerr << e.what() << '\n';
  return 1;
}
