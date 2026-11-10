#include "numerical.hpp"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
template <class T, std::size_t N>
void write(std::ofstream &f, const std::array<T, N> &x) {
  f.write(reinterpret_cast<const char *>(x.data()), sizeof(T) * N);
}
int main(int argc, char **argv) try {
  if (argc == 3 && std::string(argv[1]) == "--load") {
    abc::Model::load(argv[2]);
    return 0;
  }
  if (argc == 4 && std::string(argv[1]) == "--rng") {
    auto seed = std::stoull(argv[2]);
    abc::detail::RandomStream r{std::uint32_t(seed)};
    std::ofstream out(argv[3], std::ios::binary);
    for (int i = 0; i < 2048; ++i) {
      auto x = r.word();
      out.write(reinterpret_cast<char *>(&x), 4);
    }
    r.seed(std::uint32_t(seed));
    for (int i = 0; i < 2048; ++i) {
      auto x = r.uniform();
      out.write(reinterpret_cast<char *>(&x), 4);
    }
    r.seed(std::uint32_t(seed));
    for (int i = 0; i < 2048; ++i) {
      auto x = r.exponential();
      out.write(reinterpret_cast<char *>(&x), 4);
    }
    r.seed(std::uint32_t(seed));
    std::array<float, 16> p;
    for (int i = 0; i < 16; ++i)
      p[i] = float(i + 1) / 136.f;
    for (int i = 0; i < 512; ++i) {
      if (i % 3 == 0)
        r.uniform();
      if (i % 7 == 0)
        r.discard();
      int x = r.categorical(p);
      out.write(reinterpret_cast<char *>(&x), 4);
    }
    return 0;
  }
  if (argc != 4)
    return 2;
  std::ifstream in(argv[2]);
  std::array<abc::Vec2, 640> h, g;
  std::array<std::int64_t, 640> a;
  std::array<std::uint8_t, 640> v, m;
  abc::Vec2 p, actual;
  long long now;
  int interval, prev_valid;
  in >> p.x >> p.y >> now >> interval >> prev_valid >> actual.x >> actual.y;
  abc::detail::EventState s;
  in >> s.age >> s.initial_error >> s.innovation_age >> s.target.x >>
      s.target.y >> s.position.x >> s.position.y >> s.mode;
  for (int i = 0; i < 640; ++i) {
    int vi, mi;
    in >> h[i].x >> h[i].y >> g[i].x >> g[i].y >> a[i] >> vi >> mi;
    v[i] = std::uint8_t(vi);
    m[i] = std::uint8_t(mi);
  }
  std::array<float, 32> previous;
  for (auto &x : previous)
    in >> x;
  if (!in)
    throw std::runtime_error("bad probe fixture");
  abc::detail::Observation o{h.data(), g.data(), a.data(), v.data(),
                             m.data(), p,        now};
  auto f = abc::detail::motor_features(o);
  auto e = abc::detail::event_features(o, s);
  auto weights =
      std::make_shared<abc::detail::Weights>(*abc::Model::load(argv[1]));
  abc::detail::Neural n(weights);
  std::array<float, 70 * 96> states;
  n.state_trace = states.data();
  n.motor(f);
  n.geometry(h.back());
  n.selector(previous, prev_valid != 0, actual, interval);
  n.hazards(e.context);
  n.brake(e.context);
  std::array<abc::Vec2, 32> action;
  for (int j = 0; j < 32; ++j)
    action[j] = {};
  n.decode(3, h.back(), interval, action.data());
  std::array<abc::Vec2, 5> coeff;
  for (int j = 0; j < 5; ++j) {
    double x = n.brake_coeff[2 * 10 + 2 * j],
           y = n.brake_coeff[2 * 10 + 2 * j + 1];
    coeff[j] = {x * e.axis.x - y * e.axis.y, x * e.axis.y + y * e.axis.x};
  }
  std::array<abc::Vec2, 33> curve;
  for (int i = 0; i < 33; ++i)
    curve[i] = abc::detail::c2_path(h.back(), {.04, -.02}, n.duration[2], coeff,
                                    i + 7.);
  std::array<float, 401> activation;
  for (int i = 0; i < 401; ++i)
    activation[i] = abc::detail::tanh_pade9(float(i - 200) * .05f);
  std::ofstream out(argv[3], std::ios::binary);
  write(out, f.coarse);
  write(out, f.fine);
  write(out, f.dynamics);
  write(out, e.raw);
  write(out, e.context);
  out.write(reinterpret_cast<char *>(&e.axis), 16);
  write(out, n.tokens);
  write(out, n.projected);
  write(out, states);
  write(out, n.encoded);
  write(out, n.coefficients);
  write(out, n.heads);
  write(out, n.unary_geometry);
  write(out, n.pairs);
  write(out, n.logits);
  write(out, n.probabilities);
  write(out, n.hazard);
  write(out, n.duration);
  write(out, n.brake_coeff);
  write(out, n.frequency);
  write(out, action);
  write(out, curve);
  write(out, activation);
  return 0;
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
