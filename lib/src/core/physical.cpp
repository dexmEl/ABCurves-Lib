#include "numerical.hpp"
#include <algorithm>
#include <cmath>
namespace abc::detail {
namespace {
constexpr double vs = 1.1982087601807259, ps = 81.73350722609987;
double norm(Vec2 x) { return std::sqrt(x.x * x.x + x.y * x.y); }
Vec2 mean(const Vec2 *h, int a, int b) {
  Vec2 r{};
  for (int i = a; i < b; ++i)
    r = r + h[i];
  return r / double(b - a);
}
} // namespace
Features motor_features(const Observation &o) {
  ABC_TIME(features_stage);
  Features f;
  std::array<bool, 640> known{};
  Vec2 total{};
  int coverage = 0;
  for (int i = 0; i < 640; ++i) {
    known[i] = o.v[i] && o.a[i] <= o.now + (i - 639LL) * 1000;
    if (o.m[i]) {
      total = total + o.h[i];
      ++coverage;
    }
  }
  Vec2 cumulative{};
  int end = 0;
  for (int token = 0; token < 70; ++token) {
    int width = token < 30 ? 16 : 4, begin = end;
    end += width;
    Vec2 sum{};
    int count = 0;
    for (int j = begin; j < end; ++j)
      if (o.m[j]) {
        sum = sum + o.h[j];
        cumulative = cumulative + o.h[j];
        ++count;
      }
    int i = end - 1;
    Vec2 rel{}, tv{};
    if (known[i] && o.m[i]) {
      auto point = o.p + cumulative - total;
      rel = {std::asinh((o.g[i].x - point.x) / ps),
             std::asinh((o.g[i].y - point.y) / ps)};
    }
    if (i >= 32 && known[i] && known[i - 32])
      tv = (o.g[i] - o.g[i - 32]) / 32.;
    auto *c = f.coarse.data() + 9 * token;
    c[0] = float(sum.x / width / vs);
    c[1] = float(sum.y / width / vs);
    c[2] = float(rel.x);
    c[3] = float(rel.y);
    c[4] = float(tv.x / vs);
    c[5] = float(tv.y / vs);
    c[6] = float(known[i]);
    c[7] = float(double(count) / width);
    c[8] = token < 30 ? 1.f : .25f;
  }
  for (int i = 0; i < 16; ++i) {
    f.fine[2 * i] = o.m[624 + i] ? float(o.h[624 + i].x / vs) : 0;
    f.fine[2 * i + 1] = o.m[624 + i] ? float(o.h[624 + i].y / vs) : 0;
    f.fine[32 + i] = float(o.m[624 + i]);
  }
  Vec2 tv{};
  if (known[639] && known[607])
    tv = (o.g[639] - o.g[607]) / 32.;
  f.fine[48] = known[639] ? float(std::asinh((o.g[639].x - o.p.x) / ps)) : 0;
  f.fine[49] = known[639] ? float(std::asinh((o.g[639].y - o.p.y) / ps)) : 0;
  f.fine[50] = float(tv.x / vs);
  f.fine[51] = float(tv.y / vs);
  f.fine[52] = float(known[639]);
  f.fine[53] = float(coverage / 640.);
  bool valid_acc = true;
  for (int i = 40; i < 48; ++i)
    valid_acc = valid_acc && f.fine[i] > .5f;
  for (int d = 0; d < 2; ++d) {
    float a = 0, b = 0;
    for (int i = 0; i < 4; ++i) {
      a = a + f.fine[(12 + i) * 2 + d];
      b = b + f.fine[(8 + i) * 2 + d];
    }
    a = a / 4.f;
    b = b / 4.f;
    float acc = a - b;
    acc = acc * float(vs);
    acc = acc / 4.f;
    acc = valid_acc ? acc / .25f : 0;
    f.dynamics[d] = f.fine[30 + d];
    f.dynamics[2 + d] = acc;
  }
  double ex = std::sinh(double(f.fine[48])) * ps,
         ey = std::sinh(double(f.fine[49])) * ps,
         mag = std::sqrt(ex * ex + ey * ey + 100.);
  float dx = f.fine[52] > .5f ? float(ex / mag) : 0,
        dy = f.fine[52] > .5f ? float(ey / mag) : 0,
        vx = f.fine[30] - f.fine[50], vy = f.fine[31] - f.fine[51],
        ax = f.dynamics[2], ay = f.dynamics[3];
  f.dynamics[4] = vx * dx + vy * dy;
  f.dynamics[5] = vx * (-dy) + vy * dx;
  f.dynamics[6] = ax * dx + ay * dy;
  f.dynamics[7] = ax * (-dy) + ay * dx;
  return f;
}
EventFeatures event_features(const Observation &o, const EventState &s) {
  ABC_TIME(event_features_stage);
  EventFeatures f;
  std::array<bool, 640> known{}, kc{};
  int mc = 0;
  for (int i = 0; i < 640; ++i) {
    kc[i] = o.v[i] && o.a[i] <= o.now + (i - 639LL) * 1000;
    known[i] = kc[i] && std::isfinite(o.g[i].x) && std::isfinite(o.g[i].y);
    mc += o.m[i] != 0;
  }
  auto goal = known[639] ? o.g[639] : o.p;
  auto e = goal - o.p;
  double distance = norm(e);
  auto axis = e / std::max(distance, 1e-8);
  auto v1 = o.h[639], v8 = mean(o.h, 632, 640), v32 = mean(o.h, 608, 640),
       acc = (v8 - mean(o.h, 624, 632)) / 8.;
  double s1 = norm(v1), s8 = norm(v8), s32 = norm(v32);
  std::array<Vec2, 3> tv{};
  int periods[] = {8, 32, 128};
  for (int j = 0; j < 3; ++j)
    if (known[639] && known[639 - periods[j]])
      tv[j] = (goal - o.g[639 - periods[j]]) / double(periods[j]);
  double ts8 = norm(tv[0]), ts32 = norm(tv[1]), ts128 = norm(tv[2]), growth = 0;
  if (known[639] && known[607])
    growth = (distance - norm(o.g[607] - (o.p - v32 * 32.))) / 32.;
  int last_change = -1, tc = 0;
  double excursion = 0;
  for (int i = 512; i < 640; ++i) {
    double d = norm(o.g[i] - goal);
    if (d > 1e-8 || !known[i])
      last_change = i - 512;
    if (known[i]) {
      excursion = std::max(excursion, d);
      ++tc;
    }
  }
  f.raw = {float(distance),
           float(s1),
           float(s8),
           float(s32),
           float(s8 / std::max(s32, .005)),
           float(ts32),
           float(ts128),
           float(v8.x * axis.x + v8.y * axis.y),
           float(std::abs(v8.x * axis.y - v8.y * axis.x)),
           float(growth),
           float(-(acc.x * v8.x + acc.y * v8.y) / std::max(s8, .005)),
           float(excursion),
           float(127 - last_change),
           float(tc / 128.),
           float(known[639]),
           float(mc / 640.),
           float(std::min(s.age, 4096.)),
           float(s.mode != 0 ? norm(goal - s.target) : 0),
           float(s.mode != 0 ? norm(o.p - s.position) : 0),
           float(ts8),
           float((v8.x * tv[1].x + v8.y * tv[1].y) / std::max(s8 * ts32, 1e-8)),
           float(s.mode != 0 ? s.initial_error : 0)};
  auto cg = kc[639] ? o.g[639] : o.p;
  auto ce = cg - o.p;
  double cd = norm(ce);
  axis = cd > 1e-6   ? ce / std::max(cd, 1e-9)
         : s8 > 1e-6 ? v8 / std::max(s8, 1e-9)
                     : Vec2{1, 0};
  f.axis = axis;
  std::array<double, 10> extra{v1.x * axis.x + v1.y * axis.y,
                               -v1.x * axis.y + v1.y * axis.x,
                               (acc.x * axis.x + acc.y * axis.y) * 8.,
                               (-acc.x * axis.y + acc.y * axis.x) * 8.};
  for (int j = 0; j < 2; ++j) {
    int k = j == 0 ? 32 : 128;
    Vec2 t{};
    if (kc[639] && kc[639 - k])
      t = (cg - o.g[639 - k]) / double(k);
    extra[4 + j * 2] = t.x * axis.x + t.y * axis.y;
    extra[5 + j * 2] = -t.x * axis.y + t.y * axis.x;
  }
  extra[8] = -v8.x * axis.y + v8.y * axis.x;
  extra[9] = std::min(2048., s.innovation_age) / 128.;
  constexpr double scales[] = {10, .1,  .1, .1, 1, .1,  .1, .1, .1, .1, .01,
                               10, 128, 1,  1,  1, 128, 10, 10, .1, 1,  10};
  for (int j = 0; j < 22; ++j)
    f.context[j] = float(std::asinh(
        (j == 16 ? std::min(double(f.raw[j]), 256.) : double(f.raw[j])) /
        scales[j]));
  for (int j = 0; j < 10; ++j)
    f.context[22 + j] = float(std::asinh(extra[j]));
  return f;
}
Vec2 c2_path(Vec2 v, Vec2 a, double duration, const std::array<Vec2, 5> &c,
             double t) {
  double s = std::min(1., std::max(0., t / duration)), z = 2 * s - 1,
         s2 = s * s, s3 = s2 * s, s4 = s2 * s2, s5 = s4 * s, om = 1 - s,
         om3 = om * om * om;
  double e = 64 * s3 * om3, carry = s - 6 * s3 + 8 * s4 - 3 * s5,
         acc = .5 * s * s * om3, goal = 10 * s3 - 15 * s4 + 6 * s5;
  double r0 = e, r1 = e * z, r2 = e * (3 * z * z - 1) / 2,
         r3 = e * (5 * z * z * z - 3 * z) / 2;
  auto res = c[1] * r0 + c[2] * r1 + c[3] * r2 + c[4] * r3;
  return v * (carry * duration) + a * (acc * duration * duration) +
         c[0] * goal + res;
}
} // namespace abc::detail
