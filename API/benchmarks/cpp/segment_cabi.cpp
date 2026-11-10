#include "abcurves.h"
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#endif
using Clock=std::chrono::steady_clock;
struct Record{const char*stage;double us;uint32_t fresh=0,active=0;};
static void check(int status){if(status)throw std::runtime_error(abc_last_error());}
static double cpu(){
#ifdef _WIN32
 FILETIME c,e,k,u;if(!GetProcessTimes(GetCurrentProcess(),&c,&e,&k,&u))throw std::runtime_error("GetProcessTimes");
 auto value=[](FILETIME t){return (uint64_t(t.dwHighDateTime)<<32)|t.dwLowDateTime;};return double(value(k)+value(u))*.1;
#else
 return 0; // This Windows benchmark does not claim non-Windows CPU timing.
#endif
}
int main(int argc,char**argv)try{
 if(argc!=3)throw std::runtime_error("model output");abc_model_handle model;abc_planner_handle planner;abc_segment_handle controller;
 check(abc_model_load(argv[1],&model));check(abc_planner_create(model,nullptr,&planner));check(abc_segment_create(planner,nullptr,&controller));
 abc_model_info info{};info.struct_size=sizeof info;check(abc_model_get_info(model,&info));
 abc_version_info version{};version.struct_size=sizeof version;check(abc_get_version(&version));
 std::array<abc_sample,32> output;abc_segment_metadata metadata{};metadata.struct_size=sizeof metadata;std::vector<Record> records;records.reserve(10000);
 auto timed=[&](const char*stage,auto fn){auto start=Clock::now();fn();auto end=Clock::now();records.push_back({stage,std::chrono::duration<double,std::micro>(end-start).count()});};
 auto cpu_start=cpu();auto wall_start=Clock::now();
 for(int i=0;i<2528;++i){int64_t t=int64_t(i)*info.commitment_ms*1000;abc_target_observation o{sizeof o,0,0,0,t,t,{420.+200.*std::sin(i*.09),170.+90.*std::cos(i*.071)}};
  timed("observation",[&]{check(abc_segment_submit_observation(controller,&o));});uint64_t n;
  timed("segment",[&]{check(abc_segment_next(controller,output.data(),32,&n,&metadata));});records.back().fresh=metadata.fresh_planning_decision;records.back().active=metadata.motor_inference;
  timed("metadata",[&]{check(abc_segment_get_metadata(controller,&metadata));});if(i==127){records.clear();cpu_start=cpu();wall_start=Clock::now();}
 }
 auto cpu_us=cpu()-cpu_start;auto wall_us=std::chrono::duration<double,std::micro>(Clock::now()-wall_start).count();uint64_t memory=0;
#ifdef _WIN32
 PROCESS_MEMORY_COUNTERS_EX counters{};counters.cb=sizeof counters;if(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),sizeof counters))memory=counters.PrivateUsage;
#endif
 std::ofstream out(std::string(argv[2])+".csv");out<<std::setprecision(17)<<"stage,us,fresh,active\n";for(auto r:records)out<<r.stage<<','<<r.us<<','<<r.fresh<<','<<r.active<<'\n';
 std::ofstream meta(std::string(argv[2])+".json");meta<<"{\"cpu_us\":"<<cpu_us<<",\"wall_us\":"<<wall_us<<",\"private_bytes\":"<<memory<<",\"abi_minor\":"<<version.abi_minor<<",\"allocations\":\"DLL allocator not intercepted\"}";
 check(abc_segment_destroy(controller));check(abc_planner_destroy(planner));check(abc_model_release(model));return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
