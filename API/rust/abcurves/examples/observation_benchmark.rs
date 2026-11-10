use abcurves_native::*;
use std::{
    env,
    fs::File,
    io::{BufWriter, Write},
    time::Instant,
};
fn main() -> std::result::Result<(), Box<dyn std::error::Error>> {
    let args: Vec<_> = env::args().collect();
    let m = Model::load(&args[1])?;
    let mut p = Planner::new(&m, Default::default())?;
    let mut input = ObservationInput::new(
        &mut p,
        ObservationOptions {
            max_age_us: 50000,
            ..Default::default()
        },
    )?;
    let mut rows = Vec::with_capacity(24000);
    for pass in 0..2 {
        input.planner_mut().reset(7)?;
        for i in 1..=8000 {
            let t = i * 1000;
            input.planner_mut().generate_sample(t)?;
            let start = Instant::now();
            input.relative(Vec2 { x: 12., y: -7. }, t, t)?;
            let elapsed = start.elapsed().as_secs_f64() * 1e6;
            if pass == 1 {
                rows.push(("relative_submit", elapsed));
            }
            let start = Instant::now();
            assert_eq!(
                input
                    .relative(Vec2 { x: 12., y: -7. }, t, t)
                    .unwrap_err()
                    .status,
                16
            );
            let elapsed = start.elapsed().as_secs_f64() * 1e6;
            if pass == 1 {
                rows.push(("duplicate_reject", elapsed));
            }
            if t > 50000 {
                let start = Instant::now();
                assert_eq!(
                    input
                        .relative(Vec2 { x: 12., y: -7. }, 0, t)
                        .unwrap_err()
                        .status,
                    15
                );
                let elapsed = start.elapsed().as_secs_f64() * 1e6;
                if pass == 1 {
                    rows.push(("stale_reject", elapsed));
                }
            }
        }
    }
    let mut out = BufWriter::new(File::create(format!("{}.csv", args[2]))?);
    writeln!(out, "stage,us")?;
    for (stage, elapsed) in rows {
        writeln!(out, "{stage},{elapsed}")?;
    }
    Ok(())
}
