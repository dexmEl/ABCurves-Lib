use abcurves_native::*;
fn main() -> Result<()> {
    let path=std::env::args().nth(1).expect("model directory");
    let model=Model::load(path)?;
    // Optional PlannerOptions.history borrows 160 prepared samples during construction.
    let mut planner=Planner::new(&model,PlannerOptions::default())?;
    let mut controller=SegmentController::new(&mut planner,SegmentOptions::default())?;
    controller.relative(Vec2{x:120.,y:-7.},0,0)?;
    let first=controller.next_segment()?; // Owns fixed sample storage.
    for sample in first.samples() {
        println!("{} {} {}",sample.timestamp_us,sample.displacement.x,sample.displacement.y);
        assert!(sample.timestamp_us>first.metadata.start_timestamp_us);
    }
    let receipt=first.metadata.end_timestamp_us-1000;
    controller.relative(Vec2{x:12.,y:-6.},receipt,receipt)?;
    let _second=controller.next_segment()?; // first remains independently usable.
    let third=controller.next_segment()?;   // Retain target across observation gap.
    let diagnostics=controller.diagnostics()?;
    assert_eq!(diagnostics.total_segments,3);
    println!("{} {}",third.metadata.sample_count,diagnostics.total_segments);
    Ok(()) // Controller Drop releases exclusive planner borrow before planner Drop.
}
