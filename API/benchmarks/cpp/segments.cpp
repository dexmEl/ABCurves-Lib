#include "abcurves/native.hpp"
#include "abcurves.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <ctime>
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
static std::atomic<uint64_t> allocations{0};static bool track=false;
void* operator new(std::size_t n){if(track)++allocations;if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void*p)noexcept{std::free(p);}void operator delete(void*p,std::size_t)noexcept{std::free(p);}
void*operator new[](std::size_t n){return ::operator new(n);}void operator delete[](void*p)noexcept{::operator delete(p);}void operator delete[](void*p,std::size_t)noexcept{::operator delete(p);}
struct Record{const char*stage;double us;uint64_t allocations;bool fresh,active;};
using Clock=std::chrono::steady_clock;
static double cpu_us(){
#ifdef _WIN32
 FILETIME created,exited,kernel,user;
 if(!GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user))throw std::runtime_error("GetProcessTimes failed");
 auto value=[](FILETIME t){return (uint64_t(t.dwHighDateTime)<<32)|t.dwLowDateTime;};
 return double(value(kernel)+value(user))*.1;
#else
 return double(std::clock())/CLOCKS_PER_SEC*1e6;
#endif
}
int main(int argc,char**argv)try{
 if(argc!=4)throw std::runtime_error("model mode output");std::string mode=argv[2];bool capi=mode=="cabi_single"||mode=="cabi_batch";
 auto model=abc::Model::load(argv[1]);abc::ContinuousPlanner planner(model);abc::SegmentController ctl(planner);
 auto interval=model->commitment_ms();std::array<abc::Sample,32> samples;std::array<abc_sample,32> csamples;std::array<abc::Report,32> reports;abc::SegmentMetadata metadata;
 std::vector<Record> records;records.reserve(200000);uint64_t written=0;abc_model_handle mh=0;abc_planner_handle ph=0;
 auto check=[](int status){if(status)throw std::runtime_error(abc_last_error());};
 if(capi){check(abc_model_load(argv[1],&mh));check(abc_planner_create(mh,nullptr,&ph));}
 abc::Renderer renderer(std::filesystem::path(argv[1])/"../../../Native/models/renderer_global_h80.bin");std::vector<abc::Report> profile(256);
 for(int i=0;i<256;++i)profile[i]={static_cast<int16_t>(i%9-4),static_cast<int16_t>(i%7-3)};renderer.observe_profile(profile);renderer.begin(11);
 auto timed=[&](const char*stage,auto fn,bool fresh=false,bool active=false){auto a=allocations.load();track=true;auto start=Clock::now();fn();auto end=Clock::now();track=false;records.push_back({stage,std::chrono::duration<double,std::micro>(end-start).count(),allocations.load()-a,fresh,active});};
 auto start_cpu=cpu_us();auto start_wall=Clock::now();
 for(int i=0;i<2528;++i){int64_t t=int64_t(i)*interval*1000;abc::Vec2 target{420.+200.*std::sin(i*.09),170.+90.*std::cos(i*.071)};
  if(capi)check(abc_planner_update_target(ph,{target.x,target.y},t));else if(mode=="single"||mode=="batch")planner.update_target(target,t);
  else if(mode!="gaps"||i%7==0)timed("observation",[&]{ctl.submit_observation({target,t,t});});
  if(mode=="multiple")for(int n=1;n<4;++n)timed("observation",[&]{ctl.submit_observation({{target.x+n,target.y},t,t});});
  uint64_t d0,m0; if(capi){abc_planner_state s{};s.struct_size=sizeof s;check(abc_planner_get_state(ph,&s));d0=s.decision_count;m0=s.motor_evaluations;}else{auto s=planner.diagnostics();d0=s.decision_count;m0=s.motor_evaluations;}
  const auto at=records.size();
  if(mode=="single"||mode=="cabi_single"){
   for(int n=1;n<=interval;++n)timed(n==1?"fresh_single":"buffered_sample",[&]{if(capi)check(abc_planner_generate_sample(ph,t+n*1000,&csamples[n-1]));else samples[n-1]=planner.generate_sample(t+n*1000);});
  }else if(mode=="batch"||mode=="cabi_batch")timed("legacy_batch",[&]{if(capi)check(abc_planner_advance(ph,t+interval*1000,csamples.data(),32,&written));else planner.advance(t+interval*1000,samples.data(),32);});
  else {
   timed("segment",[&]{ctl.next_segment(samples,metadata);});
   timed("metadata",[&]{metadata=ctl.metadata();});
   if(mode=="renderer")timed("renderer",[&]{abc::SegmentController::render_segment(renderer,std::span(samples.data(),metadata.sample_count),metadata,abc::COMMON_RADIANS_PER_COUNT*8.,reports);});
  }
  uint64_t d1,m1;if(capi){abc_planner_state s{};s.struct_size=sizeof s;check(abc_planner_get_state(ph,&s));d1=s.decision_count;m1=s.motor_evaluations;}else{auto s=planner.diagnostics();d1=s.decision_count;m1=s.motor_evaluations;}
  records[at].fresh=d1>d0;records[at].active=m1>m0;
  if(i==127){records.clear();start_cpu=cpu_us();start_wall=Clock::now();}
 }
 auto cpu=cpu_us()-start_cpu;auto wall=std::chrono::duration<double,std::micro>(Clock::now()-start_wall).count();uint64_t memory=0;
#ifdef _WIN32
 PROCESS_MEMORY_COUNTERS_EX counters{};counters.cb=sizeof counters;if(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),sizeof counters))memory=counters.PrivateUsage;
#endif
 std::ofstream out(std::string(argv[3])+".csv");out<<std::setprecision(17)<<"stage,us,allocations,fresh,active\n";for(size_t i=0;i<records.size();++i)out<<records[i].stage<<','<<records[i].us<<','<<records[i].allocations<<','<<records[i].fresh<<','<<records[i].active<<'\n';
 std::ofstream meta(std::string(argv[3])+".json");meta<<"{\"cpu_us\":"<<cpu<<",\"wall_us\":"<<wall<<",\"private_bytes\":"<<memory<<",\"controller_bytes\":"<<sizeof(abc::SegmentController)<<",\"allocation_scope\":\"executable scalar/array new; DLL allocations excluded\"}";
 if(capi){check(abc_planner_destroy(ph));check(abc_model_release(mh));}return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
