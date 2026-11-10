use abcurves_native::*;
use std::path::PathBuf;
fn root()->PathBuf{PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../..")}
#[test]
fn complete_segments_continuity_renderer_and_prefixes(){
 for c in [32,16,8]{for prefix in [0,160,164]{
  let model=Model::load(root().join(format!("lib/models/{c}ms"))).unwrap();let raw:Vec<_>=(0..164).map(|i|Vec2{x:i as f64/1000.,y:-(i as f64)/2000.}).collect();let prepared=prepare_history(&raw,Vec2{x:100.,y:50.}).unwrap();
  let options=||PlannerOptions{initial_position:if prefix==164{prepared.initial_position}else{Vec2{x:100.,y:50.}},history:if prefix==164{&prepared.history[..]}else{&raw[..prefix]},..Default::default()};
  let mut p=Planner::new(&model,options()).unwrap();let mut legacy=Planner::new(&model,options()).unwrap();let mut ctl=SegmentController::new(&mut p,Default::default()).unwrap();
  ctl.relative(Vec2{x:320.,y:120.},0,0).unwrap();let start=legacy.state().unwrap().position;legacy.update_target(Vec2{x:start.x+320.,y:start.y+120.},0).unwrap();
  let mut renderer=Renderer::load(root().join("Native/models/renderer_global_h80.bin")).unwrap();let mut old=Renderer::load(root().join("Native/models/renderer_global_h80.bin")).unwrap();let profile:Vec<_>=(0..256).map(|i|Report{dx:(i%9-4) as i16,dy:(i%7-3) as i16}).collect();
  for r in [&mut renderer,&mut old]{r.set_profile(&profile).unwrap();r.reset(7).unwrap();}
  let mut previous=start;
  for i in 0..300{
   let s=ctl.next_segment().unwrap();assert_eq!(s.samples().len(),c);let expected=legacy.advance(s.metadata.end_timestamp_us).unwrap();assert_eq!(s.samples(),expected);
   let mut reports=[Report::default();32];let mut reference=[Report::default();32];renderer.render_segment_into(&s,COMMON_RADIANS_PER_COUNT,&mut reports).unwrap();old.render_samples_into(&expected,&mut previous,COMMON_RADIANS_PER_COUNT,&mut reference).unwrap();assert_eq!(reports,reference);
   if i%3==0{let t=s.metadata.end_timestamp_us;let target=Vec2{x:420.+(i%11) as f64,y:170.};ctl.absolute(target,t,t).unwrap();legacy.update_target(target,t).unwrap();}
  }
  assert_eq!(ctl.diagnostics().unwrap().total_segments,300);
 }}
}
#[test]
fn asynchronous_gaps_timeout_reset_and_errors(){
 let m=Model::load(root().join("lib/models/16ms")).unwrap();let mut p=Planner::new(&m,Default::default()).unwrap();let mut s=SegmentController::new(&mut p,SegmentOptions{inactivity_timeout_us:20000,..Default::default()}).unwrap();
 s.relative(Vec2{x:120.,y:-7.},0,0).unwrap();let first=s.next_segment().unwrap();s.relative(Vec2{x:12.,y:-7.},5000,7000).unwrap();s.absolute(Vec2{x:320.,y:120.},12000,14000).unwrap();
 assert_eq!(s.diagnostics().unwrap().pending_observations,2);let second=s.next_segment().unwrap();assert_eq!(first.metadata.final_position,second.metadata.initial_position);assert_eq!(s.diagnostics().unwrap().last_apply_us,16000);
 s.next_segment().unwrap();s.next_segment().unwrap();assert_eq!(s.diagnostics().unwrap().target_timeout_events,1);let time=s.planner_state().unwrap().numerical_time_us;s.reset_diagnostics().unwrap();assert_eq!(s.planner_state().unwrap().numerical_time_us,time);
 s.reset_segment_session().unwrap();assert_eq!(s.next_segment().unwrap().samples().len(),1);s.absolute(Vec2{x:120.,y:-7.},0,0).unwrap();assert_eq!(s.absolute(Vec2{x:120.,y:-7.},0,0).unwrap_err().status,16);
 let mut short=[Sample::default();1];assert_eq!(s.next_segment_into(&mut short).unwrap_err().status,10);
}

#[test]
fn independent_segment_controllers_on_threads(){
 fn run(c:u32)->Vec<Sample>{
  let model=Model::load(root().join(format!("lib/models/{c}ms"))).unwrap();let mut planner=Planner::new(&model,Default::default()).unwrap();
  let mut ctl=SegmentController::new(&mut planner,Default::default()).unwrap();ctl.absolute(Vec2{x:420.,y:170.},0,0).unwrap();let mut all=Vec::new();
  for _ in 0..48{let segment=ctl.next_segment().unwrap();all.extend_from_slice(segment.samples());}all
 }
 let expected:Vec<_>=[32,16,8].into_iter().map(run).collect();let jobs:Vec<_>=[32,16,8].into_iter().map(|c|std::thread::spawn(move||run(c))).collect();
 let actual:Vec<_>=jobs.into_iter().map(|j|j.join().unwrap()).collect();assert_eq!(expected,actual);
}
