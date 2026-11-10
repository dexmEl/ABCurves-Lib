#include "abcurves/renderer.hpp"
#include <fstream>
#include <iostream>
#include <vector>
int main(int argc, char **argv) try {
  if (argc != 4)
    return 2;
  std::ifstream in(argv[2]);
  std::size_t n;
  double scale;
  unsigned long long seed;
  in >> n >> scale >> seed;
  std::vector<abc::Report> profile(256);
  for (auto &r : profile) {
    int x, y;
    in >> x >> y;
    r = {std::int16_t(x), std::int16_t(y)};
  }
  abc::Renderer r(argv[1]);
  r.observe_profile(profile);
  r.begin(seed);
  std::ofstream out(argv[3]);
  out << "dx,dy\n";
  for (std::size_t i = 0; i < n; ++i) {
    abc::Vec2 d;
    in >> d.x >> d.y;
    auto report = r.step(d / scale);
    out << report.dx << ',' << report.dy << '\n';
  }
  if (!in)
    throw std::runtime_error("invalid renderer fixture");
  return 0;
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
