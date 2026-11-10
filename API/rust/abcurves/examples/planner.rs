use abcurves_native::{Model, Planner, PlannerOptions, Vec2};
fn main() -> Result<(), Box<dyn std::error::Error>> {
    let model = Model::load(std::env::args().nth(1).ok_or("model directory required")?)?;
    let mut planner = Planner::new(&model, PlannerOptions::default())?;
    drop(model);
    planner.update_target(Vec2 { x: 320., y: 120. }, 0)?;
    let samples = planner.advance(128000)?;
    println!("{} samples; {:?}", samples.len(), samples.last());
    Ok(())
}
