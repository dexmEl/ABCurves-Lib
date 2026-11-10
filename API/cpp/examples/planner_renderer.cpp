#include <abcurves/native.hpp>
#include <fstream>
#include <iostream>
#include <vector>
int main(int argc, char **argv) try {
  if (argc != 5) {
    std::cerr << "usage: example <planner-dir> <renderer-file> <256-report-Y-up-profile> "
                 "<radians-per-native-count>\n";
    return 2;
  }
  auto model = abc::Model::load(argv[1]);
  abc::ContinuousPlanner p(model);
  abc::Renderer r(argv[2]);
  std::ifstream f(argv[3]);
  std::vector<abc::Report> profile;
  int x, y;
  while (f >> x >> y) {
    if (x < -32768 || x > 32767 || y < -32768 || y > 32767)
      throw std::runtime_error("int16 profile required");
    profile.push_back({int16_t(x), int16_t(y)});
  }
  r.observe_profile(profile);
  r.begin(7);
  double scale = std::stod(argv[4]) / abc::COMMON_RADIANS_PER_COUNT;
  if (!(scale > 0))
    throw std::runtime_error("positive gain required");
  p.update_target({320, 120}, 0);
  std::vector<abc::Sample> samples(128);
  p.advance(128000, samples.data(), samples.size());
  abc::Vec2 previous{};
  for (auto s : samples) {
    auto report = r.step((s.position - previous) / scale);
    previous = s.position;
    std::cout << s.timestamp_us << ',' << report.dx << ',' << report.dy << '\n';
  }
  return 0;
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
