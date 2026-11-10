#pragma once
#include "abcurves/model.hpp"
#include "rng.hpp"
#include <array>
#include <chrono>
#include <memory>
#include <vector>
namespace abc::detail {
struct Observation {
  const Vec2 *h, *g;
  const std::int64_t *a;
  const std::uint8_t *v, *m;
  Vec2 p;
  std::int64_t now;
};
struct Features {
  std::array<float, 630> coarse{};
  std::array<float, 54> fine{};
  std::array<float, 8> dynamics{};
};
struct EventFeatures {
  std::array<float, 22> raw{};
  std::array<float, 32> context{};
  Vec2 axis;
};
struct EventState {
  double age = 160, initial_error = 0, innovation_age = 0;
  Vec2 target{}, position{};
  int mode = 2;
};
Features motor_features(const Observation &);
EventFeatures event_features(const Observation &, const EventState &);
Vec2 c2_path(Vec2 v, Vec2 a, double duration, const std::array<Vec2, 5> &coeff,
             double time);
struct Linear {
  int in = 0, out = 0;
  std::vector<float> w, b, transpose;
  Linear() = default;
  Linear(const Model &, const char *weight, const char *bias, int input,
         int output);
  void run(const float *x, float *y, bool activation = false) const;
};
struct Weights {
  Linear input, ih, hh, trunk, output, unary, unary_out, context, pair,
      pair_out, hazard0, hazard2, hazard4, brake0, brake2, brake_out, frequency;
  std::array<double, 128 * 21> velocity{};
  std::array<double, 128> carry{};
  std::array<double, 16 * 21> mean_basis{};
  std::array<double, 16> mean_carry{};
  explicit Weights(const Model &);
};
// Test/profiling hooks are compiled out of the ordinary library.
enum Stage {
  features_stage,
  input_stage,
  projection_stage,
  recurrence_stage,
  trunk_stage,
  geometry_stage,
  selector_stage,
  decode_stage,
  event_features_stage,
  hazard_stage,
  brake_stage,
  policy_stage,
  buffer_stage,
  target_events_stage,
  history_write_stage,
  rng_stage,
  brake_path_stage,
  selector_features_stage,
  selector_network_stage,
  stage_count
};
#ifdef ABC_PROFILE
struct Profile {
  std::array<std::vector<double>, stage_count> samples;
};
Profile &profile();
struct Timer {
  Stage stage;
  std::chrono::steady_clock::time_point start;
  explicit Timer(Stage s) : stage(s), start(std::chrono::steady_clock::now()) {}
  ~Timer() {
    profile().samples[stage].push_back(
        std::chrono::duration<double, std::micro>(
            std::chrono::steady_clock::now() - start)
            .count());
  }
};
#define ABC_TIME(s) abc::detail::Timer timer_##s(abc::detail::s)
#else
#define ABC_TIME(s) ((void)0)
#endif
struct Neural {
  std::shared_ptr<const Weights> w;
  std::array<float, 70 * 96> tokens{};
  std::array<float, 70 * 288> projected{};
  std::array<float, 96> hidden{}, encoded{};
  std::array<float, 288> recurrent{};
  std::array<float, 672> coefficients{};
  std::array<float, 512> heads{};
  std::array<float, 256> unary_geometry{};
  std::array<float, 320> pairs{};
  std::array<float, 16> logits{}, probabilities{}, duration{}, frequency{};
  std::array<float, 160> brake_coeff{};
  std::array<float, 2> hazard{};
  explicit Neural(std::shared_ptr<const Weights> weights)
      : w(std::move(weights)) {}
  float *state_trace = nullptr; // Optional test-only borrowed [70,96] buffer.
  void motor(const Features &);
  void geometry(Vec2 incoming);
  void selector(const std::array<float, 32> &previous, bool valid, Vec2 actual,
                int interval);
  void hazards(const std::array<float, 32> &);
  void brake(const std::array<float, 32> &);
  void decode(int chosen, Vec2 incoming, int count, Vec2 *action) const;
};
float tanh_pade9(float);
float sigmoid_exact(float);
} // namespace abc::detail
