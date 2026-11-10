#include "abcurves/native.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <vector>
int main(int argc, char **argv) try {
  if (argc != 5) {
    std::cerr
        << "usage: planner_renderer <planner-model> <renderer-model> "
           "<256-report-hardware-profile.txt> <radians-per-native-count>\n";
    return 2;
  }
  double gain = std::stod(argv[4]),
         scale = gain / abc::COMMON_RADIANS_PER_COUNT;
  if (!std::isfinite(scale) || scale <= 0)
    throw std::invalid_argument("gain must be positive and finite");
  std::ifstream input(argv[3]);
  std::vector<abc::Report> profile;
  int x, y;
  while (input >> x >> y) {
    if (x < -32768 || x > 32767 || y < -32767 || y > 32767)
      throw std::invalid_argument(
          "profile report outside canonical int16 range");
    profile.push_back({std::int16_t(x), std::int16_t(-y)});
  }
  if (!input.eof() || profile.size() != 256)
    throw std::invalid_argument(
        "provide exactly 256 genuine chronological hardware reports");
  auto planner = abc::ContinuousPlanner::load(argv[1]);
  abc::Renderer renderer(argv[2]);
  renderer.observe_profile(profile);
  renderer.begin(7);
  planner->update_target({320, 120}, 0);
  abc::Vec2 previous = planner->position();
  for (int i = 0; i < 1000; ++i) {
    auto sample = planner->generate_sample((i + 1LL) * 1000);
    auto report = renderer.step((sample.position - previous) / scale);
    previous = sample.position;
    std::cout << sample.timestamp_us << ',' << report.dx << ','
              << -int(report.dy) << '\n';
  }
  return 0;
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
