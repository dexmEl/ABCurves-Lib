use abcurves_native::*;

fn main() -> Result<()> {
    let model = Model::embedded(16)?;
    let mut planner = Planner::new(&model, PlannerOptions::default())?;
    let mut controller = SegmentController::new(&mut planner, SegmentOptions::default())?;
    controller.relative(Vec2 { x: 120.0, y: -7.0 }, 0, 0)?;
    let segment = controller.next_segment()?;
    for sample in segment.samples() {
        println!("{} {} {}", sample.timestamp_us, sample.displacement.x, sample.displacement.y);
    }
    Ok(())
}
