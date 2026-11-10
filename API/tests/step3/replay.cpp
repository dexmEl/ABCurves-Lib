#include "abcurves/native.hpp"
#include "abcurves.h"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>
struct Event {int64_t at,capture,receipt;uint32_t kind,availability;abc::Vec2 value;};
int status(const abc::ObservationError&e){switch(e.reason){case abc::ObservationRejection::timestamp:return 9;case abc::ObservationRejection::stale:return 15;case abc::ObservationRejection::duplicate:return 16;case abc::ObservationRejection::out_of_order:return 17;default:return 1;}}
int main(int argc,char**argv){try{
 if(argc!=6)throw std::runtime_error("interface model fixture output batch");
 bool direct=std::string(argv[1])=="direct";std::ifstream f(argv[3]);uint64_t seed;int duration,prefix,ordering;int64_t age;abc::Vec2 initial;
 f>>seed>>duration>>prefix>>ordering>>age>>initial.x>>initial.y;abc::PlannerOptions options;options.seed=seed;options.initial_position=initial;
 std::vector<abc::Vec2> history(prefix);for(auto&v:history)f>>v.x>>v.y;
 if(prefix==164){auto p=abc::prepare_history(history,initial);options.initial_position=p.initial_position;options.initial_history.assign(p.history.begin(),p.history.end());}else options.initial_history=history;
 std::vector<Event> events;Event e;while(f>>e.at>>e.capture>>e.receipt>>e.kind>>e.availability>>e.value.x>>e.value.y)events.push_back(e);
 std::ofstream out(std::string(argv[4])+".csv");out<<std::setprecision(17);std::size_t cursor=0;
 auto emit=[&](int64_t time,auto submit,auto generate,auto diag,auto state){
  while(cursor<events.size() && events[cursor].at==time){auto event=events[cursor++];auto code=submit(event);auto d=diag();
   out<<0<<','<<time<<','<<code<<','<<d.last_capture_us<<','<<d.last_receipt_us<<','<<d.last_age_us<<','<<d.accepted<<','<<d.rejected<<','<<d.duplicates<<','<<d.out_of_order<<','<<d.stale<<','<<uint32_t(d.availability)<<','<<uint32_t(d.last_rejection)<<','<<d.resolved_target.x<<','<<d.resolved_target.y<<','<<d.reference_position.x<<','<<d.reference_position.y<<'\n';
  }
  if(time<duration*1000LL){auto s=generate(time+1000);auto d=state();out<<1<<','<<s.timestamp_us<<','<<uint32_t(d.mode)<<','<<d.selected_head<<','<<d.selected_brake_head<<','<<d.decision_count<<','<<d.motor_evaluations<<','<<d.pending_samples<<','<<d.numerical_time_us<<','<<d.last_decision_us<<','<<d.last_target_receipt_us<<",0,0,"<<s.position.x<<','<<s.position.y<<','<<s.displacement.x<<','<<s.displacement.y<<'\n';}
 };
 (void)argv[5];
 if(direct){abc::ContinuousPlanner p(abc::Model::load(argv[2]),options);abc::ObservationInput input(p,{static_cast<abc::ObservationOrdering>(ordering),age});
  for(int t=0;t<=duration;++t)emit(t*1000LL,[&](const Event&e){try{input.submit({e.value,e.capture,e.receipt,static_cast<abc::ObservationKind>(e.kind),static_cast<abc::TargetAvailability>(e.availability)});return 0;}catch(const abc::ObservationError&error){return status(error);}},[&](int64_t t){return p.generate_sample(t);},[&]{return input.diagnostics();},[&]{return p.diagnostics();});
 }else{
  auto check=[](int code){if(code)throw std::runtime_error(abc_last_error());};abc_model_handle m;abc_planner_handle p;abc_observation_handle input;abc_planner_options o{};check(abc_model_load(argv[2],&m));check(abc_planner_options_init(&o,sizeof o));o.seed=seed;o.initial_position={options.initial_position.x,options.initial_position.y};std::vector<abc_vec2> h;for(auto v:options.initial_history)h.push_back({v.x,v.y});o.history=h.data();o.history_count=h.size();check(abc_planner_create(m,&o,&p));check(abc_model_release(m));abc_observation_options io{sizeof io,uint32_t(ordering),age};check(abc_observation_create(p,&io,&input));
  for(int t=0;t<=duration;++t)emit(t*1000LL,[&](const Event&e){abc_target_observation observation{sizeof observation,e.kind,e.availability,0,e.capture,e.receipt,{e.value.x,e.value.y}};abc_vec2 r;return abc_observation_submit(input,&observation,&r);},[&](int64_t t){abc_sample s;check(abc_planner_generate_sample(p,t,&s));return s;},[&]{abc_observation_diagnostics d{};d.struct_size=sizeof d;check(abc_observation_get_diagnostics(input,&d));return d;},[&]{abc_planner_state d{};d.struct_size=sizeof d;check(abc_planner_get_state(p,&d));return d;});
  check(abc_observation_destroy(input));check(abc_planner_destroy(p));
 }
 if(cursor!=events.size())throw std::runtime_error("unconsumed fixture events");return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
