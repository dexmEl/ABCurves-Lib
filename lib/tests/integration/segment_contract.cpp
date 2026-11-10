#include "abcurves/native.hpp"
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(0)
static void same(const abc::Sample&a,const abc::Sample&b){
 CHECK(a.timestamp_us==b.timestamp_us);CHECK(a.position.x==b.position.x);CHECK(a.position.y==b.position.y);
 CHECK(a.displacement.x==b.displacement.x);CHECK(a.displacement.y==b.displacement.y);
}
int main(int argc,char**argv)try{
 CHECK(argc==3);
 const int rounds=std::getenv("ABC_SEGMENT_TEST_ROUNDS")?std::stoi(std::getenv("ABC_SEGMENT_TEST_ROUNDS")):1100;
 CHECK(rounds>=200 && rounds<=1100);
 for(int c:{32,16,8})for(int prefix:{0,160,164})for(int rate:{60,120,144,240,360,777}){
  auto model=abc::Model::load(std::filesystem::path(argv[1])/(std::to_string(c)+"ms"));
  abc::PlannerOptions opts;opts.seed=11;opts.initial_position={100,50};
  std::array<abc::Vec2,164> raw;for(size_t j=0;j<raw.size();++j)raw[j]={std::sin(double(j)*.12)*.2,std::cos(double(j)*.11)*.1};
  if(prefix==160)opts.initial_history.assign(raw.begin(),raw.begin()+160);
  if(prefix==164){auto h=abc::prepare_history(raw,opts.initial_position);opts.initial_position=h.initial_position;opts.initial_history.assign(h.history.begin(),h.history.end());}
  abc::ContinuousPlanner planner(model,opts),legacy(model,opts);
  abc::SegmentController controller(planner);abc::ObservationInput direct(legacy,{abc::ObservationOrdering::latest,250000});
  abc::Renderer renderer(argv[2]),old_renderer(argv[2]);std::vector<abc::Report> profile(256);
  for(size_t n=0;n<profile.size();++n)profile[n]={static_cast<int16_t>((n%9)-4),static_cast<int16_t>((n%7)-3)};
  renderer.observe_profile(profile);old_renderer.observe_profile(profile);renderer.begin(7);old_renderer.begin(7);
  abc::TargetObservation first{{420,170},0,0};controller.submit_observation(first);direct.submit(first);
  int64_t next=rate==777?7000:1000000/rate;int index=1;abc::Vec2 previous=opts.initial_position;
  std::array<abc::Sample,32> samples{},reference{};std::array<abc::Report,32> reports{};
  for(int segment=0;segment<rounds;++segment){
   abc::SegmentMetadata m;auto n=controller.next_segment(samples,m);
   CHECK(n==m.sample_count);CHECK(n>=1 && n<=size_t(c));CHECK(m.initial_position.x==previous.x && m.initial_position.y==previous.y);
   CHECK(m.end_timestamp_us-m.start_timestamp_us==int64_t(n)*1000);CHECK(m.first_sample_timestamp_us==m.start_timestamp_us+1000);
   size_t written=0;
   while(next<=m.end_timestamp_us){
    written+=legacy.advance(next,reference.data()+written,reference.size()-written);
    abc::TargetObservation o{{420.+double(index%29)*8.,170.+double(index%11)},std::max<int64_t>(0,next-3500),next};
    o.kind=index%3==0?abc::ObservationKind::relative:abc::ObservationKind::absolute;
    if(o.kind==abc::ObservationKind::relative)o.value=o.value-legacy.position();
    if(index%97>=90)o.availability=abc::TargetAvailability::unavailable;
    // Omit observations for a long gap, while preserving movement/retention.
    if(index%211<180){direct.submit(o);controller.submit_observation(o);}
    ++index;next+=rate==777?(index%3==0?1:index%3==1?6999:7000):1000000/rate;
   }
   written+=legacy.advance(m.end_timestamp_us,reference.data()+written,reference.size()-written);CHECK(written==n);
   abc::SegmentController::render_segment(renderer,std::span(samples.data(),n),m,abc::COMMON_RADIANS_PER_COUNT*8.,reports);
   for(size_t j=0;j<n;++j){same(samples[j],reference[j]);auto r=old_renderer.step((reference[j].position-previous)/8.);
    CHECK(r.dx==reports[j].dx && r.dy==reports[j].dy);previous=reference[j].position;}
   auto a=planner.diagnostics(),b=legacy.diagnostics();
   CHECK(a.decision_count==b.decision_count && a.motor_evaluations==b.motor_evaluations);
   CHECK(a.selected_head==b.selected_head && a.selected_brake_head==b.selected_brake_head && planner.mode()==legacy.mode());
  }
 }
 // Long consecutive retention runs without resetting or feeding output back.
 for(int c:{32,16,8}) {
  auto model=abc::Model::load(std::filesystem::path(argv[1])/(std::to_string(c)+"ms"));
  abc::ContinuousPlanner p(model),legacy(model);abc::SegmentController controller(p);
  controller.submit_observation({{420,170},0,0});legacy.update_target({420,170},0);
  std::array<abc::Sample,32> samples,reference;abc::SegmentMetadata m;
  for(int segment=0;segment<4096;++segment) {
   auto n=controller.next_segment(samples,m);auto count=legacy.advance(m.end_timestamp_us,reference.data(),32);
   CHECK(n==count && n==size_t(c));for(size_t i=0;i<n;++i)same(samples[i],reference[i]);
  }
  CHECK(p.diagnostics().decision_count==legacy.diagnostics().decision_count);
 }
 auto model=abc::Model::load(std::filesystem::path(argv[1])/"16ms");abc::ContinuousPlanner p(model);abc::SegmentController s(p);
 std::array<abc::Sample,32> out;abc::SegmentMetadata m;
 CHECK(s.next_segment(out,m)==1 && !m.fresh_planning_decision);
 s.submit_observation({{320,120},0,0});CHECK(s.next_segment(out,m)==16);
 s.submit_observation({{330,130},7000,7000});s.submit_observation({{340,140},14000,14000});
 CHECK(s.diagnostics().pending_observations==2);s.next_segment(out,m);CHECK(s.diagnostics().last_apply_us==17000);
 CHECK(s.diagnostics().last_observation_receipt_us==14000);auto d=s.diagnostics();s.reset_diagnostics();
 CHECK(s.diagnostics().total_segments==0 && s.diagnostics().last_observation_receipt_us==14000);
 try{s.submit_observation({{340,140},14000,14000});CHECK(false);}catch(const abc::ObservationError&e){CHECK(e.reason==abc::ObservationRejection::duplicate);}
 s.reset_segment_session();CHECK(s.diagnostics().accepted_observations==0);CHECK(p.position().x==0);
 // Future receipts cannot influence earlier decisions. Fixed bounded storage.
 s.submit_observation({{320,120},100000,100000});CHECK(s.next_segment(out,m)==1 && !m.fresh_planning_decision);
 for(int i=1;i<64;++i)s.submit_observation({{double(i),1},100000+i,100000+i});
 try{s.submit_observation({{1,1},200000,200000});CHECK(false);}catch(const abc::SegmentCapacityError&){}
 CHECK(s.diagnostics().pending_observations==64);s.reset_segment_session();
 // Parent destruction is checked without dereferencing the expired reference.
 auto parent=std::make_unique<abc::ContinuousPlanner>(model);abc::SegmentController orphan(*parent);parent.reset();
 try{orphan.next_segment(out,m);CHECK(false);}catch(const std::logic_error&){}
 // Mid-commitment attachment drains only its remainder.
 abc::ContinuousPlanner partial(model);partial.update_target({320,120},0);partial.generate_sample(1000);abc::SegmentController remainder(partial);
 CHECK(remainder.next_segment(out,m)==15 && !m.fresh_planning_decision);
 // Timeout uses elapsed microseconds, is opt-in, and never resets numerical time.
 abc::ContinuousPlanner tp(model);abc::SegmentOptions so;so.inactivity_timeout_us=10000;abc::SegmentController timeout(tp,so);
 timeout.submit_observation({{320,120},0,0});timeout.next_segment(out,m);timeout.next_segment(out,m);
 CHECK(timeout.diagnostics().target_timeout_events==1 && tp.diagnostics().numerical_time_us==17000);
 std::cout<<"Segment contract passed: 54 scenarios x "<<rounds<<" + 3 x 4096 segments, exact samples/heads/RNG/modes/Renderer, prefixes, gaps, future, reset, bounds\n";
 return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
