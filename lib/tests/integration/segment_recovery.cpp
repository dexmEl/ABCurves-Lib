#include "abcurves/native.hpp"
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#define CHECK(x) do {if(!(x)) throw std::runtime_error(#x);} while(0)
static void same(const abc::Sample& a,const abc::Sample& b) {
 CHECK(a.timestamp_us==b.timestamp_us && a.position.x==b.position.x && a.position.y==b.position.y);
 CHECK(a.displacement.x==b.displacement.x && a.displacement.y==b.displacement.y);
}
int main(int argc,char**argv) try {
 CHECK(argc==3); constexpr double big=1.3e308;
 for(int c:{32,16,8}) {
  auto model=abc::Model::load(std::filesystem::path(argv[1])/(std::to_string(c)+"ms"));
  abc::PlannerOptions options;options.initial_position={big,big};
  abc::ContinuousPlanner p(model,options),reference(model,options);
  abc::SegmentController s(p),r(reference); std::array<abc::Sample,32> a,b;
  abc::SegmentMetadata ma,mb;
  // All three records are queued before their exact virtual origins exist.
  s.submit_observation({{big,big},1000,1000});r.submit_observation({{big,big},1000,1000});
  s.submit_observation({{big,big},2000,2000,abc::ObservationKind::relative});
  s.submit_observation({{big,big},3000,3000});r.submit_observation({{big,big},3000,3000});
  for(int i=0;i<6;++i) {
   auto n=s.next_segment(a,ma);CHECK(n==r.next_segment(b,mb));
   for(std::size_t j=0;j<n;++j)same(a[j],b[j]);
   CHECK(ma.mode==mb.mode && ma.selected_head==mb.selected_head && ma.selected_brake_head==mb.selected_brake_head);
  }
  auto d=s.recovery_diagnostics();CHECK(d.invalid_pending_observations==1 && d.nonfinite_relative_resolutions==1);
  CHECK(d.last_error_capture_us==2000 && d.last_error_receipt_us==2000);
  CHECK(d.last_recoverable_error==abc::ObservationRejection::invalid && d.pending_observations==0);
  CHECK(s.diagnostics().segment_generation_errors==0 && s.diagnostics().resolved_target.x==big);
  // Repeated invalid records: one bounded pass consumes every bad record.
  for(int round=0;round<4;++round) {
   auto t=ma.end_timestamp_us;
   for(int j=1;j<=64;++j)s.submit_observation({{big,big},t+j,t+j,abc::ObservationKind::relative});
   auto n=s.next_segment(a,ma);CHECK(n==r.next_segment(b,mb));
   for(std::size_t j=0;j<n;++j)same(a[j],b[j]);
   n=s.next_segment(a,ma);CHECK(n==r.next_segment(b,mb));
   for(std::size_t j=0;j<n;++j)same(a[j],b[j]);
   CHECK(s.recovery_diagnostics().pending_observations==0);
  }
  CHECK(s.recovery_diagnostics().nonfinite_relative_resolutions==257);
  // Removed records do not poison capture/receipt watermarks or fingerprints.
  auto t=ma.end_timestamp_us;
  s.submit_observation({{big,big},t+1000000,t+1000000,abc::ObservationKind::relative});
  auto before=p.diagnostics();auto position=p.position();s.clear_pending_observations();s.clear_pending_observations();
  CHECK(p.diagnostics().numerical_time_us==before.numerical_time_us && p.position().x==position.x);
  CHECK(s.diagnostics().resolved_target.x==big && s.recovery_diagnostics().explicit_pending_clears==2);
  s.submit_observation({{big,big},t,t});r.submit_observation({{big,big},t,t});
  auto n=s.next_segment(a,ma);CHECK(n==r.next_segment(b,mb));for(std::size_t j=0;j<n;++j)same(a[j],b[j]);
  s.reset_diagnostics();CHECK(s.recovery_diagnostics().recoverable_observation_errors==0);
  CHECK(s.recovery_diagnostics().last_error_receipt_us>=0);
  s.reset_segment_session();CHECK(s.recovery_diagnostics().last_error_receipt_us==-1);
  // Ordinary moving/braking/holding streams: clear preserves RNG/history and
  // persistent Renderer, not just the stationary overflow fixture.
  abc::ContinuousPlanner normal(model),old(model);abc::SegmentController ctl(normal),ref(old);
  abc::Renderer renderer(argv[2]),expected(argv[2]);std::vector<abc::Report> profile(256);
  for(int i=0;i<256;++i)profile[i]={static_cast<std::int16_t>(i%9-4),static_cast<std::int16_t>(i%7-3)};
  renderer.observe_profile(profile);expected.observe_profile(profile);renderer.begin(11);expected.begin(11);
  ctl.submit_observation({{420,170},0,0});ref.submit_observation({{420,170},0,0});
  auto initial_count=ctl.next_segment(a,ma);CHECK(initial_count==ref.next_segment(b,mb));
  std::array<abc::Report,32> reports,old_reports;
  abc::SegmentController::render_segment(renderer,std::span(a.data(),initial_count),ma,abc::COMMON_RADIANS_PER_COUNT*8,reports);
  abc::SegmentController::render_segment(expected,std::span(b.data(),initial_count),mb,abc::COMMON_RADIANS_PER_COUNT*8,old_reports);
  for(int i=0;i<420;++i) {
   auto state=normal.diagnostics();auto at=state.numerical_time_us;
   for(int j=0;j<64;++j)ctl.submit_observation({{double(j),1},at+1000000+j,at+1000000+j});
   ctl.clear_pending_observations();CHECK(normal.diagnostics().numerical_time_us==at);
   CHECK(normal.mode()==old.mode() && normal.position().x==old.position().x);
   if(i==200 || i==205) {
    auto availability=i==200?abc::TargetAvailability::unavailable:abc::TargetAvailability::available;
    ctl.submit_observation({{420,170},at,at,abc::ObservationKind::absolute,availability});
    ref.submit_observation({{420,170},at,at,abc::ObservationKind::absolute,availability});
   }
   auto count=ctl.next_segment(a,ma);CHECK(count==ref.next_segment(b,mb));
   abc::SegmentController::render_segment(renderer,std::span(a.data(),count),ma,abc::COMMON_RADIANS_PER_COUNT*8,reports);
   abc::SegmentController::render_segment(expected,std::span(b.data(),count),mb,abc::COMMON_RADIANS_PER_COUNT*8,old_reports);
   for(std::size_t j=0;j<count;++j) {same(a[j],b[j]);CHECK(reports[j].dx==old_reports[j].dx && reports[j].dy==old_reports[j].dy);}
  }
  auto parent=std::make_unique<abc::ContinuousPlanner>(model);abc::SegmentController orphan(*parent);parent.reset();
  try{orphan.clear_pending_observations();CHECK(false);}catch(const std::logic_error&){}
  try{orphan.recovery_diagnostics();CHECK(false);}catch(const std::logic_error&){}
 }
 // Accepted late absolute input may expire while a legacy remainder drains.
 auto model=abc::Model::load(std::filesystem::path(argv[1])/"32ms");abc::ContinuousPlanner p(model);
 p.update_target({420,170},0);std::array<abc::Sample,32> out;for(int i=1;i<=641;++i)p.generate_sample(i*1000);
 abc::SegmentController s(p);s.submit_observation({{430,171},2000,2000});abc::SegmentMetadata m;
 s.next_segment(out,m);s.next_segment(out,m);CHECK(s.recovery_diagnostics().stale_pending_observations==1);
 std::cout<<"Native recovery contract passed all variants, selective rejection, repeated errors, clear/state/RNG/Renderer/lifetime/history retention\n";
 return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
