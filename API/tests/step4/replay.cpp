#include "abcurves/native.hpp"
#include "abcurves.h"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>
template<class... T>void row(std::ostream&o,const char*type,T... values){o<<type;((o<<','<<values),...);o<<'\n';}
void check(int status){if(status)throw std::runtime_error(abc_last_error());}
int reason(abc::ObservationRejection r){switch(r){case abc::ObservationRejection::timestamp:return 9;case abc::ObservationRejection::stale:return 15;case abc::ObservationRejection::duplicate:return 16;case abc::ObservationRejection::out_of_order:return 17;default:return 1;}}
#define DIAG(d) row(out,"D",d.total_segments,d.fresh_planning_decisions,d.decisions_with_new_observation,d.decisions_without_new_observation,d.stale_observations,d.duplicate_observations,d.out_of_order_observations,d.target_unavailable_events,d.target_timeout_events,d.segment_generation_errors,d.missed_generation_deadlines,d.accepted_observations,d.rejected_observations,d.queue_full_observations,d.last_observation_age_us,d.last_observation_receipt_us,d.last_capture_us,d.last_receipt_us,d.last_apply_us,d.pending_observations,(uint32_t)d.availability,(uint32_t)d.last_rejection,d.resolved_target.x,d.resolved_target.y,d.reference_position.x,d.reference_position.y)
#define META(m) row(out,"M",m.segment_id,m.variant_ms,m.sample_count,m.start_timestamp_us,m.end_timestamp_us,m.first_sample_timestamp_us,m.last_sample_timestamp_us,m.duration_us,m.initial_position.x,m.initial_position.y,m.final_position.x,m.final_position.y,(uint32_t)m.mode,(uint32_t)m.fresh_planning_decision,(uint32_t)m.motor_inference,m.selected_head,m.selected_brake_head)
int main(int argc,char**argv)try{
 if(argc!=6)throw std::runtime_error("direct|cabi model fixture Renderer output");
 bool capi=std::string(argv[1])=="cabi";std::ifstream input(argv[3]);std::ofstream out(argv[5]);out<<std::setprecision(17);
 uint64_t seed;int prefix;std::string line;std::getline(input,line);std::istringstream header(line);header>>seed>>prefix;
 double initial_x=100,initial_y=50;header>>initial_x>>initial_y;
 abc::PlannerOptions po;po.seed=seed;po.initial_position={initial_x,initial_y};std::array<abc::Vec2,164> raw;
 for(size_t i=0;i<164;++i)raw[i]={double(i)/1000.,-double(i)/2000.};
 if(prefix==160)po.initial_history.assign(raw.begin(),raw.begin()+160);
 if(prefix==164){auto h=abc::prepare_history(raw,po.initial_position);po.initial_position=h.initial_position;po.initial_history.assign(h.history.begin(),h.history.end());}
 auto model=abc::Model::load(argv[2]);abc::ContinuousPlanner planner(model,po);abc::SegmentOptions options;options.inactivity_timeout_us=90000;
 abc::SegmentController controller(planner,options);abc::Renderer renderer(argv[4]);std::vector<abc::Report> profile(256);
 for(size_t i=0;i<256;++i)profile[i]={static_cast<int16_t>(int(i%9)-4),static_cast<int16_t>(int(i%7)-3)};
 renderer.observe_profile(profile);renderer.begin(11);
 abc_model_handle mh=0;abc_planner_handle ph=0;abc_segment_handle sh=0;abc_renderer_handle rh=0;
 if(capi){check(abc_model_load(argv[2],&mh));abc_planner_options o{};check(abc_planner_options_init(&o,sizeof o));o.seed=seed;o.initial_position={po.initial_position.x,po.initial_position.y};
  std::array<abc_vec2,160> h;for(size_t i=0;i<po.initial_history.size();++i)h[i]={po.initial_history[i].x,po.initial_history[i].y};o.history=h.data();o.history_count=po.initial_history.size();
  check(abc_planner_create(mh,&o,&ph));abc_segment_options so;check(abc_segment_options_init(&so,sizeof so));so.inactivity_timeout_us=90000;check(abc_segment_create(ph,&so,&sh));
  check(abc_renderer_create(argv[4],&rh));std::array<abc_report,256> rp;for(size_t i=0;i<256;++i)rp[i]={profile[i].dx,profile[i].dy};check(abc_renderer_set_profile(rh,rp.data(),256));check(abc_renderer_reset(rh,11));}
 while(std::getline(input,line)){
  std::istringstream command(line);char kind;command>>kind;if(!command)continue;
  if(kind=='O'){
   abc::TargetObservation o;unsigned k,a;command>>o.capture_time_us>>o.receipt_time_us>>k>>a>>o.value.x>>o.value.y;o.kind=(abc::ObservationKind)k;o.availability=(abc::TargetAvailability)a;
   int status=0;if(capi){abc_target_observation co{sizeof co,k,a,0,o.capture_time_us,o.receipt_time_us,{o.value.x,o.value.y}};status=abc_segment_submit_observation(sh,&co);}
   else try{controller.submit_observation(o);}catch(const abc::ObservationError&e){status=reason(e.reason);}catch(const abc::SegmentCapacityError&){status=14;}
   row(out,"O",status);
  }else if(kind=='N'){
   if(capi){std::array<abc_sample,32>s;std::array<abc_report,32>r;abc_segment_metadata m{};m.struct_size=sizeof m;uint64_t n,w;check(abc_segment_next(sh,s.data(),32,&n,&m));check(abc_segment_render(rh,s.data(),n,&m,abc::COMMON_RADIANS_PER_COUNT*8.,r.data(),32,&w));META(m);
    for(size_t i=0;i<n;++i)row(out,"S",s[i].timestamp_us,s[i].position.x,s[i].position.y,s[i].displacement.x,s[i].displacement.y,r[i].dx,r[i].dy);
   }else{std::array<abc::Sample,32>s;std::array<abc::Report,32>r;abc::SegmentMetadata m;auto n=controller.next_segment(s,m);abc::SegmentController::render_segment(renderer,std::span(s.data(),n),m,abc::COMMON_RADIANS_PER_COUNT*8.,r);META(m);
    for(size_t i=0;i<n;++i)row(out,"S",s[i].timestamp_us,s[i].position.x,s[i].position.y,s[i].displacement.x,s[i].displacement.y,r[i].dx,r[i].dy);}
  }else if(kind=='E'){
   int status=0;uint64_t written=0;
   if(capi){std::array<abc_sample,32> s;abc_segment_metadata m{};m.struct_size=sizeof m;status=abc_segment_next(sh,s.data(),32,&written,&m);}
   else try{std::array<abc::Sample,32> s;abc::SegmentMetadata m;written=controller.next_segment(s,m);}
   catch(const std::bad_alloc&){status=11;}catch(const std::logic_error&){status=4;}catch(const std::exception&){status=13;}
   row(out,"E",status,written);
  }else if(kind=='F'){
   abc_segment_state s{};s.struct_size=sizeof s;
   if(capi)check(abc_segment_get_state(sh,&s));else{
    auto v=controller.state();s.failure_category=uint32_t(v.failure.category);s.failure_reason=uint32_t(v.failure.reason);s.reset_required=v.failure.reset_required;
    s.failure_status=v.failure.reset_required?(v.failure.category==abc::FailureCategory::resource?11:13):0;
    s.generation_attempts=v.generation_attempts;s.last_segment_id=v.last_segment_id;s.numerical_time_us=v.numerical_time_us;s.requested_time_us=v.requested_time_us;
    s.last_attempt_start_us=v.last_attempt_start_us;s.last_attempt_end_us=v.last_attempt_end_us;s.last_segment_start_us=v.last_segment_start_us;s.last_segment_end_us=v.last_segment_end_us;
    s.relative_origin_earliest_us=v.relative_origin_earliest_us;s.relative_origin_latest_us=v.relative_origin_latest_us;s.last_origin_error_receipt_us=v.last_origin_error_receipt_us;
    std::copy_n(v.failure.message,256,s.error_message);
   }
   row(out,"F",s.failure_status,s.failure_category,s.failure_reason,s.reset_required,s.generation_attempts,s.last_segment_id,s.numerical_time_us,s.requested_time_us,s.last_attempt_start_us,s.last_attempt_end_us,s.last_segment_start_us,s.last_segment_end_us,s.relative_origin_earliest_us,s.relative_origin_latest_us,s.last_origin_error_receipt_us,s.error_message);
  }else if(kind=='V') {uint64_t seed;command>>seed;if(capi)check(abc_planner_reset(ph,seed));else planner.reset(seed);
  }else if(kind=='K'){if(capi)check(abc_segment_clear_pending_observations(sh));else controller.clear_pending_observations();
  }else if(kind=='Q'){
   abc_segment_recovery_diagnostics d{};d.struct_size=sizeof d;
   if(capi)check(abc_segment_get_recovery_diagnostics(sh,&d));else{
    auto v=controller.recovery_diagnostics();d.pending_observations=v.pending_observations;
    d.last_error_status=v.last_recoverable_error==abc::ObservationRejection::none?0:v.last_recoverable_error==abc::ObservationRejection::stale?15:v.last_recoverable_error==abc::ObservationRejection::timestamp?9:1;
    d.recoverable_observation_errors=v.recoverable_observation_errors;d.invalid_pending_observations=v.invalid_pending_observations;
    d.nonfinite_relative_resolutions=v.nonfinite_relative_resolutions;d.stale_pending_observations=v.stale_pending_observations;d.explicit_pending_clears=v.explicit_pending_clears;
    d.last_error_capture_us=v.last_error_capture_us;d.last_error_receipt_us=v.last_error_receipt_us;
   }
   row(out,"Q",d.pending_observations,d.last_error_status,d.recoverable_observation_errors,d.invalid_pending_observations,d.nonfinite_relative_resolutions,d.stale_pending_observations,d.explicit_pending_clears,d.last_error_capture_us,d.last_error_receipt_us);
  }else if(kind=='Z'){if(capi)check(abc_segment_reset_diagnostics(sh));else controller.reset_diagnostics();}
  else if(kind=='X'){if(capi){check(abc_segment_reset_session(sh));check(abc_renderer_reset(rh,11));}else{controller.reset_segment_session();renderer.observe_profile(profile);renderer.begin(11);}}
  if(capi){abc_segment_diagnostics d{};d.struct_size=sizeof d;check(abc_segment_get_diagnostics(sh,&d));DIAG(d);}else{auto d=controller.diagnostics();DIAG(d);}
 }
 if(capi){check(abc_segment_destroy(sh));check(abc_planner_destroy(ph));check(abc_model_release(mh));check(abc_renderer_destroy(rh));}
 return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
