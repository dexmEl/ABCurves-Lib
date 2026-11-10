#include "abcurves.h"
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#endif
using Clock=std::chrono::steady_clock;
struct Row{const char* stage;double us;};
void check(int s){if(s)throw std::runtime_error(abc_last_error());}
double cpu(){FILETIME c,e,k,u;GetProcessTimes(GetCurrentProcess(),&c,&e,&k,&u);auto v=[](FILETIME t){return (uint64_t(t.dwHighDateTime)<<32)|t.dwLowDateTime;};return double(v(k)+v(u))*.1;}
int main(int argc,char**argv)try{
 if(argc!=4)throw std::runtime_error("model mode output");std::string mode=argv[2];
 auto clear=reinterpret_cast<abc_status(ABC_CALL*)(abc_segment_handle)>(GetProcAddress(GetModuleHandleW(L"abcurves.dll"),"abc_segment_clear_pending_observations"));
 auto recovery=reinterpret_cast<abc_status(ABC_CALL*)(abc_segment_handle,abc_segment_recovery_diagnostics*)>(GetProcAddress(GetModuleHandleW(L"abcurves.dll"),"abc_segment_get_recovery_diagnostics"));
 abc_model_handle model;abc_planner_handle planner;abc_segment_handle ctl;check(abc_model_load(argv[1],&model));
 abc_planner_options options;check(abc_planner_options_init(&options,sizeof options));if(mode=="invalid")options.initial_position={1.3e308,1.3e308};
 check(abc_planner_create(model,&options,&planner));check(abc_segment_create(planner,nullptr,&ctl));
 std::array<abc_sample,32> samples;abc_segment_metadata m{};m.struct_size=sizeof m;abc_segment_diagnostics d{};d.struct_size=sizeof d;abc_segment_recovery_diagnostics rd{};rd.struct_size=sizeof rd;
 std::vector<Row> rows;rows.reserve(20000);auto timed=[&](const char*stage,auto fn){auto a=Clock::now();fn();auto b=Clock::now();rows.push_back({stage,std::chrono::duration<double,std::micro>(b-a).count()});};
 int64_t t=0;uint64_t n;double start_cpu=cpu();auto start_wall=Clock::now();
 if(mode=="invalid" && !clear) {
  abc_target_observation o{sizeof o,1,0,0,1000,1000,{1.3e308,1.3e308}};
  check(abc_segment_submit_observation(ctl,&o));check(abc_segment_next(ctl,samples.data(),32,&n,&m));
 }
 for(int i=0;i<2528;++i){
  abc_target_observation o{sizeof o,0,0,0,t,t,{420.+200.*std::sin(i*.09),170.+90.*std::cos(i*.071)}};
  if(mode=="apply"){o.availability=1;o.value={};}
  if(mode=="invalid"){
   if(!clear) {
    timed("blocked_generation_error",[&]{if(abc_segment_next(ctl,samples.data(),32,&n,&m)!=ABC_INVALID_ARGUMENT)throw std::runtime_error("expected old blocked failure");});
   } else {
    o.kind=1;o.value={1.3e308,1.3e308};o.capture_time_us=o.receipt_time_us=t+1000;
    check(abc_segment_submit_observation(ctl,&o));check(abc_segment_next(ctl,samples.data(),32,&n,&m));t=m.end_timestamp_us;
    timed("invalid_resolution_and_idle",[&]{check(abc_segment_next(ctl,samples.data(),32,&n,&m));});t=m.end_timestamp_us;
   }
  }else if(mode=="clear"){
   o.capture_time_us=o.receipt_time_us=t+1000000;check(abc_segment_submit_observation(ctl,&o));
   timed("clear_one",[&]{check(clear(ctl));});timed("clear_empty",[&]{check(clear(ctl));});
  }else{
   timed("valid_submission",[&]{check(abc_segment_submit_observation(ctl,&o));});
   timed(mode=="apply"?"application_and_idle":"complete_segment",[&]{check(abc_segment_next(ctl,samples.data(),32,&n,&m));});t=m.end_timestamp_us;
  }
  timed("diagnostics_1_2",[&]{check(abc_segment_get_diagnostics(ctl,&d));});
  if(recovery)timed("recovery_diagnostics",[&]{check(recovery(ctl,&rd));});
  if(i==127){rows.clear();start_cpu=cpu();start_wall=Clock::now();}
 }
 auto cpu_us=cpu()-start_cpu;auto wall=std::chrono::duration<double,std::micro>(Clock::now()-start_wall).count();PROCESS_MEMORY_COUNTERS_EX counters{};counters.cb=sizeof counters;GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),sizeof counters);
 abc_version_info version{};version.struct_size=sizeof version;check(abc_get_version(&version));
 std::ofstream out(std::string(argv[3])+".csv");out<<std::setprecision(17)<<"stage,us\n";for(auto r:rows)out<<r.stage<<','<<r.us<<'\n';
 std::ofstream meta(std::string(argv[3])+".json");meta<<"{\"abi_minor\":"<<version.abi_minor<<",\"cpu_us\":"<<cpu_us<<",\"wall_us\":"<<wall<<",\"private_bytes\":"<<counters.PrivateUsage<<",\"allocations\":\"DLL allocator not instrumented\"}";
 check(abc_segment_destroy(ctl));check(abc_planner_destroy(planner));check(abc_model_release(model));return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
