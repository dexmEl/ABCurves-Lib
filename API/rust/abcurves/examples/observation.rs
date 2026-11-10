use abcurves_native::*;
fn main() -> std::result::Result<(), Box<dyn std::error::Error>> {
    let model = Model::load(std::env::args().nth(1).expect("model directory"))?;
    let mut planner = Planner::new(&model, Default::default())?;
    let mut observations = ObservationInput::new(&mut planner, Default::default())?;
    let mut output = [Sample::default(); 128];
    observations
        .planner_mut()
        .advance_into(12000, &mut output)?;
    let target = observations.relative(Vec2 { x: 12., y: -7. }, 0, 12000)?;
    println!("absolute virtual target: {},{}", target.x, target.y);
    observations
        .planner_mut()
        .advance_into(24000, &mut output)?;
    Ok(())
}
