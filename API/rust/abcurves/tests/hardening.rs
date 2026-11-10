use abcurves_native::*;
use std::path::PathBuf;
fn root()->PathBuf {PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../..")}
#[test]
fn failure_snapshot_capacity_and_explicit_reset() {
 for c in [32,16,8] {
  let m=Model::load(root().join(format!("lib/models/{c}ms"))).unwrap();
  let mut p=Planner::new(&m,Default::default()).unwrap();
  let mut s=SegmentController::new(&mut p,Default::default()).unwrap();
  assert_eq!(s.required_capacity().unwrap(),c);
  s.next_segment().unwrap();
  s.absolute(Vec2{x:1.3e308,y:1.3e308},1000,1000).unwrap();
  let err=s.next_segment().unwrap_err();assert_eq!(err.status,13);assert_eq!(err.written,0);
  let f=s.state().unwrap();assert_eq!((f.failure_status,f.failure_category,f.failure_reason,f.reset_required),(13,1,1,1));
  assert!(f.requested_time_us>f.numerical_time_us);assert_eq!(f.last_segment_id,1);
  assert_eq!(s.planner_state().unwrap().failed,1);
  assert_eq!(s.diagnostics().unwrap().segment_generation_errors,1);
  assert_eq!(s.recovery_diagnostics().unwrap().recoverable_observation_errors,0);
  assert_eq!(s.metadata().unwrap().segment_id,1);
  assert_eq!(s.next_segment().unwrap_err().status,4);
  assert_eq!(s.state().unwrap().error_message,f.error_message);
  s.reset_segment_session().unwrap();assert_eq!(s.state().unwrap().reset_required,0);assert_eq!(s.next_segment().unwrap().samples().len(),1);
 }
}
#[test]
fn stored_seed_prefix_low32_and_changing_reset() {
 for c in [32,16,8] {for seed in [7,11,11+(1u64<<32)] {for prefix in [false,true] {
  let m=Model::load(root().join(format!("lib/models/{c}ms"))).unwrap();let history=[Vec2{x:0.03,y:-0.01};160];
  let opts=||PlannerOptions{seed,history:if prefix{&history[..]}else{&[]},..Default::default()};
  let mut p=Planner::new(&m,opts()).unwrap();let mut q=Planner::new(&m,opts()).unwrap();
  for i in 0..4 {
   {let mut a=SegmentController::new(&mut p,Default::default()).unwrap();let mut b=SegmentController::new(&mut q,Default::default()).unwrap();
    a.relative(Vec2{x:320.,y:120.},0,0).unwrap();b.relative(Vec2{x:320.,y:120.},0,0).unwrap();
    for _ in 0..8 {let x=a.next_segment().unwrap();let y=b.next_segment().unwrap();assert_eq!(x.samples(),y.samples());assert_eq!(x.metadata.selected_head,y.metadata.selected_head);}
   }
   if i==0 {p.reset_session().unwrap();q.reset_session().unwrap();}
   else if i==1 {p.reset(7).unwrap();q.reset(7).unwrap();}
   else {p.reset(11+(1u64<<32)).unwrap();q.reset(11).unwrap();}
  }
 }}}
}
#[test]
fn relative_window_is_discoverable() {
 for c in [32,16,8] {
  let m=Model::load(root().join(format!("lib/models/{c}ms"))).unwrap();let mut p=Planner::new(&m,Default::default()).unwrap();let mut s=SegmentController::new(&mut p,Default::default()).unwrap();
  s.absolute(Vec2{x:320.,y:120.},0,0).unwrap();s.next_segment().unwrap();let seg=s.next_segment().unwrap();let w=s.state().unwrap();
  assert_eq!(w.relative_origin_earliest_us,seg.metadata.start_timestamp_us);assert_eq!(w.relative_origin_latest_us,seg.metadata.end_timestamp_us);
  let before=s.planner_state().unwrap();assert_eq!(s.relative(Vec2{x:5.,y:6.},1,1).unwrap_err().status,9);
  assert_eq!(s.state().unwrap().last_origin_error_receipt_us,1);assert_eq!(s.planner_state().unwrap().position,before.position);
  for t in [seg.metadata.start_timestamp_us,seg.metadata.start_timestamp_us+1,seg.metadata.end_timestamp_us,seg.metadata.end_timestamp_us+1000] {s.relative(Vec2{x:t as f64,y:6.},t,t).unwrap();}
  assert_eq!(s.diagnostics().unwrap().pending_observations,4);s.next_segment().unwrap();assert_eq!(s.diagnostics().unwrap().pending_observations,1);
 }
}
