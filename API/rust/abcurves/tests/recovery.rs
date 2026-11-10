use abcurves_native::*;
use std::path::PathBuf;
fn root()->PathBuf{PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../..")}
#[test]
fn selective_overflow_recovery_and_pending_clear(){
 for c in [32,16,8]{
  let m=Model::load(root().join(format!("lib/models/{c}ms"))).unwrap();let big=Vec2{x:1.3e308,y:1.3e308};
  let options=||PlannerOptions{initial_position:big,..Default::default()};
  let mut p=Planner::new(&m,options()).unwrap();let mut reference=Planner::new(&m,options()).unwrap();
  let mut s=SegmentController::new(&mut p,Default::default()).unwrap();let mut r=SegmentController::new(&mut reference,Default::default()).unwrap();
  s.absolute(big,1000,1000).unwrap();r.absolute(big,1000,1000).unwrap();s.relative(big,2000,2000).unwrap();
  s.absolute(big,3000,3000).unwrap();r.absolute(big,3000,3000).unwrap();
  for _ in 0..8{assert_eq!(s.next_segment().unwrap().samples(),r.next_segment().unwrap().samples());}
  let d=s.recovery_diagnostics().unwrap();assert_eq!((d.invalid_pending_observations,d.nonfinite_relative_resolutions,d.pending_observations),(1,1,0));assert_eq!((d.last_error_status,d.last_error_receipt_us),(1,2000));
  for _ in 0..3{
   let t=s.planner_state().unwrap().numerical_time_us;
   for i in 1..=64{s.relative(big,t+i,t+i).unwrap();}
   for _ in 0..2{assert_eq!(s.next_segment().unwrap().samples(),r.next_segment().unwrap().samples());}
  }
  assert_eq!(s.recovery_diagnostics().unwrap().recoverable_observation_errors,193);
  let state=s.planner_state().unwrap();s.absolute(big,state.numerical_time_us+1000000,state.numerical_time_us+1000000).unwrap();
  s.clear_pending_observations().unwrap();s.clear_pending_observations().unwrap();let after=s.planner_state().unwrap();assert_eq!(after.position,state.position);assert_eq!(after.numerical_time_us,state.numerical_time_us);
  s.absolute(big,state.numerical_time_us,state.numerical_time_us).unwrap();r.absolute(big,state.numerical_time_us,state.numerical_time_us).unwrap();assert_eq!(s.next_segment().unwrap().samples(),r.next_segment().unwrap().samples());
  s.reset_diagnostics().unwrap();assert_eq!(s.recovery_diagnostics().unwrap().recoverable_observation_errors,0);assert_eq!(s.recovery_diagnostics().unwrap().last_error_status,1);
  s.reset_segment_session().unwrap();assert_eq!(s.recovery_diagnostics().unwrap().last_error_status,0);
 }
}
#[test]
fn clear_active_state_renderer_and_instances(){
 fn run(c:u32){
  let m=Model::load(root().join(format!("lib/models/{c}ms"))).unwrap();let mut p=Planner::new(&m,Default::default()).unwrap();let mut q=Planner::new(&m,Default::default()).unwrap();let mut s=SegmentController::new(&mut p,Default::default()).unwrap();let mut r=SegmentController::new(&mut q,Default::default()).unwrap();
  let target=Vec2{x:420.,y:170.};s.absolute(target,0,0).unwrap();r.absolute(target,0,0).unwrap();
  let mut renderer=Renderer::load(root().join("Native/models/renderer_global_h80.bin")).unwrap();let mut old=Renderer::load(root().join("Native/models/renderer_global_h80.bin")).unwrap();let profile:Vec<_>=(0..256).map(|i|Report{dx:(i%9-4) as i16,dy:(i%7-3) as i16}).collect();for rr in [&mut renderer,&mut old]{rr.set_profile(&profile).unwrap();rr.reset(11).unwrap();}
  for i in 0..160{
   let a=s.next_segment().unwrap();let b=r.next_segment().unwrap();assert_eq!(a.samples(),b.samples());let mut aa=[Report::default();32];let mut bb=aa;renderer.render_segment_into(&a,COMMON_RADIANS_PER_COUNT*8.,&mut aa).unwrap();old.render_segment_into(&b,COMMON_RADIANS_PER_COUNT*8.,&mut bb).unwrap();assert_eq!(aa,bb);
   let t=a.metadata.end_timestamp_us;for j in 0..64{s.absolute(target,t+1000000+j,t+1000000+j).unwrap();}s.clear_pending_observations().unwrap();assert_eq!(s.planner_state().unwrap().numerical_time_us,t);
   if i==70{s.unavailable(t,t,false).unwrap();r.unavailable(t,t,false).unwrap();}if i==75{s.absolute(target,t,t).unwrap();r.absolute(target,t,t).unwrap();}
  }
 }
 let jobs:Vec<_>=[32,16,8].into_iter().map(|c|std::thread::spawn(move||run(c))).collect();for j in jobs{j.join().unwrap();}
}
