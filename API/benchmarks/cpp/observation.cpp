#include "abcurves/native.hpp"
#include "abcurves.h"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <new>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#endif
static std::atomic<uint64_t> allocations=0;
static bool track=false;
void* operator new(std::size_t n){if(track)++allocations;if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void*p) noexcept{std::free(p);}
void operator delete(void*p,std::size_t) noexcept{std::free(p);}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete[](void*p) noexcept{::operator delete(p);}
void operator delete[](void*p,std::size_t) noexcept{::operator delete(p);}
using Clock=std::chrono::steady_clock;
struct Timing{const char* stage;double us;uint64_t allocations;bool fresh,active;};
int main(int argc,char**argv)try{
 if(argc!=4)throw std::runtime_error("model interface output");
 std::string mode=argv[2];bool capi=mode=="cabi";bool legacy=mode=="legacy";
 auto m=abc::Model::load(argv[1]);abc::ContinuousPlanner planner(m);abc::ObservationInput input(planner,{abc::ObservationOrdering::strict,50000});
 abc_model_handle mh=0;abc_planner_handle ph=0;abc_observation_handle ih=0;
 auto check=[](int code){if(code)throw std::runtime_error(abc_last_error());};
 if(capi){check(abc_model_load(argv[1],&mh));check(abc_planner_create(mh,nullptr,&ph));abc_observation_options io{sizeof io,0,50000};check(abc_observation_create(ph,&io,&ih));}
 std::vector<Timing> records;records.reserve(50000);double trace=0;
 auto timed=[&](const char*stage,auto fn,bool collect){auto count=allocations.load();track=true;auto start=Clock::now();fn();auto end=Clock::now();track=false;if(collect)records.push_back({stage,std::chrono::duration<double,std::micro>(end-start).count(),allocations.load()-count,false,false});};
 auto run=[&](bool collect,bool instrument){
  planner.reset(7);if(capi)check(abc_planner_reset(ph,7));
  uint64_t decisions=0,motors=0;
  auto start=Clock::now();
  for(int i=0;i<8000;++i){
   int64_t t=i*1000LL;abc::Vec2 target{320.+double((i/800)%3)*100.,120.};
   if(i%8==0){
    auto submit=[&]{if(capi){abc_target_observation o{sizeof o,0,0,0,t,t,{target.x,target.y}};abc_vec2 resolved;check(abc_observation_submit(ih,&o,&resolved));}else if(legacy)planner.update_target(target,t);else input.submit({target,t,t});};
    if(instrument)timed("absolute_submit",submit,collect);else submit();
   }
   auto next=[&]{if(capi){abc_sample s;check(abc_planner_generate_sample(ph,t+1000,&s));}else planner.generate_sample(t+1000);};
   if(instrument){timed("sample",next,collect);if(collect){uint64_t d,n;if(capi){abc_planner_state s{};s.struct_size=sizeof s;check(abc_planner_get_state(ph,&s));d=s.decision_count;n=s.motor_evaluations;}else{auto s=planner.diagnostics();d=s.decision_count;n=s.motor_evaluations;}records.back().fresh=d>decisions;records.back().active=n>motors;decisions=d;motors=n;}}
   else next();
  }
  return std::chrono::duration<double,std::micro>(Clock::now()-start).count();
 };
 run(false,false);run(true,true);trace=run(false,false);
 // Observe operations separately from the matched absolute performance trace.
 if(!legacy){
  planner.reset(7);if(capi)check(abc_planner_reset(ph,7));
  for(int i=0;i<8000;++i){int64_t t=i*1000LL;
   auto advance=[&]{if(capi){abc_sample s;check(abc_planner_generate_sample(ph,t+1000,&s));}else planner.generate_sample(t+1000);};advance();t+=1000;
   auto submit=[&](int64_t capture){if(capi){abc_target_observation o{sizeof o,1,0,0,capture,t,{12,-7}};abc_vec2 v;return abc_observation_submit(ih,&o,&v);}try{input.submit({{12,-7},capture,t,abc::ObservationKind::relative});return 0;}catch(const abc::ObservationError&e){return e.reason==abc::ObservationRejection::stale?15:16;}};
   timed("relative_submit",[&]{check(submit(t));},true);
   timed("duplicate_reject",[&]{if(submit(t)!=16)throw std::runtime_error("expected duplicate");},true);
   if(t>50000)timed("stale_reject",[&]{if(submit(0)!=15)throw std::runtime_error("expected stale");},true);
  }
 }
 std::ofstream out(std::string(argv[3])+".csv");out<<std::setprecision(12)<<"stage,us,allocations,fresh,active\n";
 for(auto r:records)out<<r.stage<<','<<r.us<<','<<r.allocations<<','<<r.fresh<<','<<r.active<<'\n';
 uint64_t memory=0;
#ifdef _WIN32
 PROCESS_MEMORY_COUNTERS_EX counters{};counters.cb=sizeof counters;if(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),sizeof counters))memory=counters.PrivateUsage;
#endif
 std::ofstream meta(std::string(argv[3])+".json");meta<<"{\"trace_us\":"<<trace<<",\"private_bytes\":"<<memory<<",\"input_object_bytes\":"<<sizeof(abc::ObservationInput)<<",\"allocation_scope\":\"executable scalar/array new, DLL allocator excluded\"}";
 if(capi){check(abc_observation_destroy(ih));check(abc_planner_destroy(ph));check(abc_model_release(mh));}
 return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
