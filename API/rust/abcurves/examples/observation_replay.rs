use abcurves_native::*;
use std::{
    env, fs,
    fs::File,
    io::{BufWriter, Write},
};
fn main() -> std::result::Result<(), Box<dyn std::error::Error>> {
    let args: Vec<_> = env::args().collect();
    let text = fs::read_to_string(&args[2])?;
    let mut lines = text.lines();
    let h: Vec<_> = lines.next().unwrap().split_whitespace().collect();
    let seed = h[0].parse()?;
    let duration: i64 = h[1].parse()?;
    let prefix: usize = h[2].parse()?;
    let ordering = match h[3] {
        "0" => ObservationOrdering::Strict,
        "1" => ObservationOrdering::Latest,
        _ => ObservationOrdering::Caller,
    };
    let age = h[4].parse()?;
    let mut initial = Vec2 {
        x: h[5].parse()?,
        y: h[6].parse()?,
    };
    let mut history = Vec::new();
    for _ in 0..prefix {
        let r: Vec<_> = lines.next().unwrap().split_whitespace().collect();
        history.push(Vec2 {
            x: r[0].parse()?,
            y: r[1].parse()?,
        });
    }
    if prefix == 164 {
        let start = prepare_history(&history, initial)?;
        initial = start.initial_position;
        history = start.history.to_vec();
    }
    let events: Vec<Vec<&str>> = lines.map(|s| s.split_whitespace().collect()).collect();
    let mut cursor = 0;
    let m = Model::load(&args[1])?;
    let mut p = Planner::new(
        &m,
        PlannerOptions {
            seed,
            initial_position: initial,
            history: &history,
            diagnostic_proposals: false,
        },
    )?;
    let mut input = ObservationInput::new(
        &mut p,
        ObservationOptions {
            ordering,
            max_age_us: age,
        },
    )?;
    let mut out = BufWriter::new(File::create(format!("{}.csv", args[3]))?);
    for tick in 0..=duration {
        let time = tick * 1000;
        while cursor < events.len() && events[cursor][0].parse::<i64>()? == time {
            let e = &events[cursor];
            cursor += 1;
            let availability = match e[4] {
                "0" => TargetAvailability::Available,
                "1" => TargetAvailability::Unavailable,
                _ => TargetAvailability::Invalidated,
            };
            let code = input
                .submit(TargetObservation {
                    value: Vec2 {
                        x: e[5].parse()?,
                        y: e[6].parse()?,
                    },
                    capture_time_us: e[1].parse()?,
                    receipt_time_us: e[2].parse()?,
                    relative: e[3] == "1",
                    availability,
                })
                .err()
                .map_or(0, |e| e.status);
            let d = input.diagnostics()?;
            writeln!(
                out,
                "0,{time},{code},{},{},{},{},{},{},{},{},{},{},{},{},{},{}",
                d.last_capture_us,
                d.last_receipt_us,
                d.last_age_us,
                d.accepted,
                d.rejected,
                d.duplicates,
                d.out_of_order,
                d.stale,
                d.availability,
                d.last_rejection,
                d.resolved_target.x,
                d.resolved_target.y,
                d.reference_position.x,
                d.reference_position.y
            )?;
        }
        if tick < duration {
            let s = input.planner_mut().generate_sample(time + 1000)?;
            let d = input.planner_mut().state()?;
            writeln!(
                out,
                "1,{},{},{},{},{},{},{},{},{},{},0,0,{},{},{},{}",
                s.timestamp_us,
                d.mode,
                d.selected_head,
                d.selected_brake_head,
                d.decision_count,
                d.motor_evaluations,
                d.pending_samples,
                d.numerical_time_us,
                d.last_decision_us,
                d.last_target_receipt_us,
                s.position.x,
                s.position.y,
                s.displacement.x,
                s.displacement.y
            )?;
        }
    }
    assert_eq!(cursor, events.len());
    Ok(())
}
