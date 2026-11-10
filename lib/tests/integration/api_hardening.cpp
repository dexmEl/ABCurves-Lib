#include "abcurves/native.hpp"
#include "failure.hpp"
#include "rng.hpp"
#include <array>
#include <cmath>
#include <cstring>
#include <future>
#include <iostream>
#include <limits>
#include <stdexcept>
#define CHECK(x) do {if(!(x))throw std::runtime_error(#x);}while(0)
template<class F> void blocked(F f) {try{f();CHECK(false);}catch(const std::logic_error&) {}}
static void same(const abc::Sample& a,const abc::Sample& b) {
 CHECK(a.timestamp_us==b.timestamp_us && a.position.x==b.position.x && a.position.y==b.position.y &&
       a.displacement.x==b.displacement.x && a.displacement.y==b.displacement.y);
}
static void run(const std::shared_ptr<const abc::Model>& model) {
 std::array<abc::Sample,32> out{},ref{};abc::SegmentMetadata m;
 abc::ContinuousPlanner p(model); abc::ObservationInput input(p);
 {
  abc::SegmentController s(p);blocked([&]{abc::SegmentController second(p);});
  blocked([&]{abc::ObservationInput other(p);});
  blocked([&]{input.submit({{320,120},0,0});});
  CHECK(input.diagnostics().accepted==0);
  auto reject_direct=[&] {
   auto pos=p.position();auto time=p.requested_time_us();
   blocked([&]{p.set_position({99,22});});blocked([&]{p.update_target({5,5},time);});
   blocked([&]{p.clear_target(time);});blocked([&]{p.advance(time+1000,out.data(),32);});
   blocked([&]{p.generate_sample(time+1000);});
   CHECK(pos.x==p.position().x && pos.y==p.position().y && time==p.requested_time_us());
  };
  reject_direct();s.submit_observation({{320,120},0,0});s.next_segment(out,m);reject_direct();
  auto first=m;s.next_segment(out,m);auto window=s.state();
  CHECK(window.relative_origin_earliest_us==m.start_timestamp_us && window.relative_origin_latest_us==m.end_timestamp_us);
  auto d=p.diagnostics();
  try{s.submit_observation({{5,6},first.start_timestamp_us,first.start_timestamp_us,abc::ObservationKind::relative});CHECK(false);}
  catch(const abc::ObservationError& e){CHECK(e.reason==abc::ObservationRejection::timestamp);}
  CHECK(s.state().last_origin_error_receipt_us==first.start_timestamp_us);
  CHECK(p.diagnostics().decision_count==d.decision_count);
  // Earliest boundary, interior, current boundary and future are all truthful.
  for(auto t:{m.start_timestamp_us,m.start_timestamp_us+1,m.end_timestamp_us,m.end_timestamp_us+1000})
   s.submit_observation({{double(t),6},t,t,abc::ObservationKind::relative});
  CHECK(s.diagnostics().pending_observations==4);s.clear_pending_observations();
  p.reset(11);CHECK(s.state().generation_attempts==0 && s.diagnostics().total_segments==0);
  abc::PlannerOptions options; options.seed=11; options.initial_history.assign(160,{.01,-.02});
  p.reinitialize(options);CHECK(s.state().numerical_time_us==0);
  s.submit_observation({{1.3e308,1.3e308},0,0});
  try{s.next_segment(out,m);CHECK(false);}catch(const std::runtime_error&){}
  auto f=s.state();CHECK(p.failed() && f.failure.reset_required && f.failure.reason==abc::FailureReason::nonfinite_policy);
  CHECK(f.requested_time_us>f.numerical_time_us && s.diagnostics().segment_generation_errors==1);
  CHECK(s.recovery_diagnostics().recoverable_observation_errors==0);
  auto original=p.failure();blocked([&]{s.next_segment(out,m);});blocked([&]{s.submit_observation({{1,1},0,0});});
  CHECK(std::strcmp(original.message,s.state().failure.message)==0);
  s.reset_segment_session();CHECK(!p.failed() && !s.state().failure.reset_required);
 }
 CHECK(!p.has_segment_controller());p.reset();p.update_target({320,120},0);p.generate_sample(1000);
 {abc::SegmentController s(p);CHECK(s.next_segment(out,m)==size_t(model->commitment_ms()-1));}
 // Stored seed, low32 mapping, optional prefix, session and deliberate resets.
 for(bool prefix:{false,true}) for(std::uint64_t seed:{7ULL,11ULL,11ULL+(1ULL<<32)}) {
  abc::PlannerOptions o;o.seed=seed;if(prefix)o.initial_history.assign(160,{.03,-.01});
  abc::ContinuousPlanner a(model,o),b(model,o);abc::SegmentController s(a);
  for(int round=0;round<3;++round) {
   s.submit_observation({{320,120},0,0});b.update_target({320,120},0);
   for(int j=0;j<8;++j){auto n=s.next_segment(out,m);CHECK(b.advance(m.end_timestamp_us,ref.data(),32)==n);for(size_t k=0;k<n;++k)same(out[k],ref[k]);}
   if(round==0){s.reset_segment_session();b.reset();}else {a.reset(7);b.reset(7);}
  }
 }
 // A distinct numeric failure with enormous finite history (no model change).
 abc::PlannerOptions o;o.diagnostics=true;o.initial_history.assign(160,{1e20,1e20});
 abc::ContinuousPlanner primitive(model,o);abc::SegmentController ps(primitive);ps.submit_observation({{1e5,1e5},0,0});
 try{ps.next_segment(out,m);CHECK(false);}catch(const std::runtime_error&){}
 CHECK(ps.state().failure.reason==abc::FailureReason::invalid_primitive);
 // Both borrowed native handlers reject expired parents, including after reuse.
 auto parent=std::make_unique<abc::ContinuousPlanner>(model);
 auto handler=std::make_unique<abc::ObservationInput>(*parent);
 auto controller=std::make_unique<abc::SegmentController>(*parent);parent.reset();
 parent=std::make_unique<abc::ContinuousPlanner>(model);
 blocked([&]{handler->diagnostics();});blocked([&]{handler->submit({});});
 blocked([&]{controller->state();});controller.reset();handler.reset();
 abc::SegmentController replacement(*parent);
}
int main(int argc,char**argv)try {
 CHECK(argc==2);
 for(int c:{32,16,8}) {
  auto model=abc::Model::load(std::filesystem::path(argv[1])/(std::to_string(c)+"ms"));run(model);
  auto task=[model]{abc::ContinuousPlanner p(model);abc::SegmentController s(p);s.submit_observation({{320,120},0,0});std::array<abc::Sample,32> out;abc::SegmentMetadata m;for(int i=0;i<64;++i)s.next_segment(out,m);return p.position();};
  auto a=std::async(std::launch::async,task),b=std::async(std::launch::async,task);auto x=a.get(),y=b.get();CHECK(x.x==y.x && x.y==y.y);
 }
 // Actual inference softmax failure remains typed, never an observation error.
 std::array<float,16> logits,prob;logits.fill(std::numeric_limits<float>::quiet_NaN());
 try{abc::detail::softmax(logits.data(),prob);CHECK(false);}catch(const abc::detail::GenerationError& e){CHECK(e.category==abc::FailureCategory::inference && e.reason==abc::FailureReason::invalid_probabilities);}
 std::cout<<"API hardening passed all variants, exclusivity, resets, failures, windows, lifetimes and independent threads\n";
 return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
