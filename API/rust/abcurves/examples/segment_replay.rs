use abcurves_native::*;
use std::{env,fs,io::{BufWriter,Write}};
macro_rules! row {($out:expr,$kind:expr,$($v:expr),+)=>{{write!($out,"{}",$kind).unwrap();$(write!($out,",{}",$v).unwrap();)+writeln!($out).unwrap();}}}
fn main(){
 let a:Vec<_>=env::args().collect();let text=fs::read_to_string(&a[2]).unwrap();let mut lines=text.lines();let header:Vec<_>=lines.next().unwrap().split_whitespace().collect();let seed=header[0].parse().unwrap();let prefix:usize=header[1].parse().unwrap();
 let initial=if header.len()>2{Vec2{x:header[2].parse().unwrap(),y:header[3].parse().unwrap()}}else{Vec2{x:100.,y:50.}};
 let raw:Vec<_>=(0..164).map(|i|Vec2{x:i as f64/1000.,y:-(i as f64)/2000.}).collect();let prepared=prepare_history(&raw,Vec2{x:100.,y:50.}).unwrap();
 let model=Model::load(&a[1]).unwrap();let mut planner=Planner::new(&model,PlannerOptions{seed,initial_position:if prefix==164{prepared.initial_position}else{initial},history:if prefix==164{&prepared.history[..]}else{&raw[..prefix]},diagnostic_proposals:false}).unwrap();
 let mut ctl=SegmentController::new(&mut planner,SegmentOptions{inactivity_timeout_us:90000,..Default::default()}).unwrap();
 let mut renderer=Renderer::load(&a[3]).unwrap();let profile:Vec<_>=(0..256).map(|i|Report{dx:(i%9-4) as i16,dy:(i%7-3) as i16}).collect();renderer.set_profile(&profile).unwrap();renderer.reset(11).unwrap();let mut out=BufWriter::new(fs::File::create(&a[4]).unwrap());
 for line in lines{let p:Vec<_>=line.split_whitespace().collect();if p.is_empty(){continue;}
  match p[0]{
   "O"=>{let o=TargetObservation{capture_time_us:p[1].parse().unwrap(),receipt_time_us:p[2].parse().unwrap(),relative:p[3]=="1",availability:match p[4]{"0"=>TargetAvailability::Available,"1"=>TargetAvailability::Unavailable,_=>TargetAvailability::Invalidated},value:Vec2{x:p[5].parse().unwrap(),y:p[6].parse().unwrap()}};let status=ctl.submit(o).err().map_or(0,|e|e.status);row!(out,"O",status);},
   "N"=>{let seg=ctl.next_segment().unwrap();let m=seg.metadata;let mut reports=[Report::default();32];renderer.render_segment_into(&seg,COMMON_RADIANS_PER_COUNT*8.,&mut reports).unwrap();
    row!(out,"M",m.segment_id,m.variant_ms,m.sample_count,m.start_timestamp_us,m.end_timestamp_us,m.first_sample_timestamp_us,m.last_sample_timestamp_us,m.duration_us,m.initial_position.x,m.initial_position.y,m.final_position.x,m.final_position.y,m.mode,m.fresh_planning_decision,m.motor_inference,m.selected_head,m.selected_brake_head);
    for (s,r) in seg.samples().iter().zip(reports.iter()){row!(out,"S",s.timestamp_us,s.position.x,s.position.y,s.displacement.x,s.displacement.y,r.dx,r.dy);}
   },
   "E"=>{let result=ctl.next_segment();let(status,written)=match result{Ok(seg)=>(0,seg.samples().len()),Err(e)=>(e.status,e.written)};row!(out,"E",status,written);},
   "F"=>{let s=ctl.state().unwrap();let message=unsafe{std::ffi::CStr::from_ptr(s.error_message.as_ptr())}.to_str().unwrap();row!(out,"F",s.failure_status,s.failure_category,s.failure_reason,s.reset_required,s.generation_attempts,s.last_segment_id,s.numerical_time_us,s.requested_time_us,s.last_attempt_start_us,s.last_attempt_end_us,s.last_segment_start_us,s.last_segment_end_us,s.relative_origin_earliest_us,s.relative_origin_latest_us,s.last_origin_error_receipt_us,message);},
   "V"=>{drop(ctl);planner.reset(p[1].parse().unwrap()).unwrap();ctl=SegmentController::new(&mut planner,SegmentOptions{inactivity_timeout_us:90000,..Default::default()}).unwrap();},
   "K"=>ctl.clear_pending_observations().unwrap(),
   "Q"=>{let d=ctl.recovery_diagnostics().unwrap();row!(out,"Q",d.pending_observations,d.last_error_status,d.recoverable_observation_errors,d.invalid_pending_observations,d.nonfinite_relative_resolutions,d.stale_pending_observations,d.explicit_pending_clears,d.last_error_capture_us,d.last_error_receipt_us);},
   "Z"=>ctl.reset_diagnostics().unwrap(),
   "X"=>{ctl.reset_segment_session().unwrap();renderer.reset(11).unwrap();},_=>panic!("unknown command")
  }
  let d=ctl.diagnostics().unwrap();row!(out,"D",d.total_segments,d.fresh_planning_decisions,d.decisions_with_new_observation,d.decisions_without_new_observation,d.stale_observations,d.duplicate_observations,d.out_of_order_observations,d.target_unavailable_events,d.target_timeout_events,d.segment_generation_errors,d.missed_generation_deadlines,d.accepted_observations,d.rejected_observations,d.queue_full_observations,d.last_observation_age_us,d.last_observation_receipt_us,d.last_capture_us,d.last_receipt_us,d.last_apply_us,d.pending_observations,d.availability,d.last_rejection,d.resolved_target.x,d.resolved_target.y,d.reference_position.x,d.reference_position.y);
 }
}
