#include "abcurves.h"
#include <array>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <vector>
using Clock=std::chrono::steady_clock;
struct Row {const char* stage; double us;};
static void check(abc_status s) {if(s) throw std::runtime_error(abc_last_error());}
int main(int argc,char**argv) try {
  if(argc!=3) throw std::runtime_error("model output.csv");
  abc_model_handle model; abc_planner_handle p; abc_segment_handle s;
  check(abc_model_load(argv[1],&model)); check(abc_planner_create(model,nullptr,&p));
  std::vector<Row> rows; rows.reserve(16000);
  auto timed=[&](const char* stage,auto fn) {auto a=Clock::now();fn();rows.push_back({stage,std::chrono::duration<double,std::micro>(Clock::now()-a).count()});};
  abc_planner_state state{}; state.struct_size=sizeof state;
  for(int i=0;i<4128;++i) {
    timed("controller_create",[&]{check(abc_segment_create(p,nullptr,&s));});
    timed("planner_state",[&]{check(abc_planner_get_state(p,&state));});
    // Baseline allowed this identity mutation; hardened library rejects it.
    timed("ownership_check",[&]{auto status=abc_planner_set_position(p,state.position);if(status!=ABC_OK && status!=ABC_INVALID_STATE)check(status);});
    timed("controller_destroy",[&]{check(abc_segment_destroy(s));});
    if(i==127) rows.clear();
  }
  check(abc_segment_create(p,nullptr,&s));
  abc_target_observation o{sizeof o,ABC_TARGET_RELATIVE,ABC_TARGET_AVAILABLE,0,0,0,{320,120}};
  std::array<abc_sample,32> out; uint64_t n; abc_segment_metadata m{};m.struct_size=sizeof m;
  for(int i=0;i<1128;++i) {
    o.capture_time_us=o.receipt_time_us=i?m.end_timestamp_us:0;
    timed("relative_validation",[&]{check(abc_segment_submit_observation(s,&o));});
    check(abc_segment_next(s,out.data(),32,&n,&m));
  }
  std::ofstream file(argv[2]);file<<std::setprecision(17)<<"stage,us\n";for(auto r:rows)file<<r.stage<<','<<r.us<<'\n';
  check(abc_segment_destroy(s));check(abc_planner_destroy(p));check(abc_model_release(model));
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
