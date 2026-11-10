#include "abcurves/native.hpp"
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <new>
static bool track=false;static std::atomic<unsigned long long> calls=0;
void*operator new(std::size_t n){if(track)++calls;if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void*p)noexcept{std::free(p);}void operator delete(void*p,std::size_t)noexcept{std::free(p);}
void*operator new[](std::size_t n){return ::operator new(n);}void operator delete[](void*p)noexcept{::operator delete(p);}void operator delete[](void*p,std::size_t)noexcept{::operator delete(p);}
int main(int argc,char**argv)try{
 if(argc!=2)return 1;auto model=abc::Model::load(argv[1]);abc::PlannerOptions options;options.initial_position={1.3e308,1.3e308};abc::ContinuousPlanner p(model,options);abc::SegmentController s(p);std::array<abc::Sample,32> out;abc::SegmentMetadata m;
 unsigned long long submit=0,generate=0,reject=0,clear=0,diagnostics=0;
 for(int i=0;i<2400;++i){auto t=p.diagnostics().numerical_time_us;
  auto measure=[&](auto f){auto a=calls.load();track=true;f();track=false;return calls.load()-a;};
  submit+=measure([&]{s.submit_observation({{1.3e308,1.3e308},t+1000,t+1000,abc::ObservationKind::relative});});
  generate+=measure([&]{s.next_segment(out,m);});reject+=measure([&]{s.next_segment(out,m);});
  t=p.diagnostics().numerical_time_us;s.submit_observation({{1,2},t+1000,t+1000});
  clear+=measure([&]{s.clear_pending_observations();});diagnostics+=measure([&]{auto d=s.recovery_diagnostics();if(d.pending_observations)throw std::runtime_error("pending");});
 }
 abc::ContinuousPlanner active(model);abc::SegmentController ctl(active);unsigned long long active_submit=0,active_generate=0;
 for(int i=0;i<2400;++i){auto t=active.diagnostics().numerical_time_us;auto a=calls.load();track=true;ctl.submit_observation({{420.+double(i%30),170},t,t});track=false;active_submit+=calls.load()-a;a=calls.load();track=true;ctl.next_segment(out,m);track=false;active_generate+=calls.load()-a;}
 std::cout<<"{\"controller_bytes\":"<<sizeof(abc::SegmentController)<<",\"submission_new\":"<<submit<<",\"idle_new\":"<<generate<<",\"rejection_new\":"<<reject<<",\"clear_new\":"<<clear<<",\"diagnostics_new\":"<<diagnostics<<",\"active_submission_new\":"<<active_submit<<",\"active_generation_new\":"<<active_generate<<"}\n";
 return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
