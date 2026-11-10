use abcurves_native::*;
use abcurves_sys as sys;
use std::{
    ffi::{CStr, CString},
    fs,
    io::{BufWriter, Write},
    path::Path,
    time::Instant,
};
fn check(s: i32) {
    if s != 0 {
        panic!(
            "{}",
            unsafe { CStr::from_ptr(sys::abc_last_error()) }.to_string_lossy()
        );
    }
}
struct Raw(u64);
impl Drop for Raw {
    fn drop(&mut self) {
        unsafe {
            sys::abc_planner_destroy(self.0);
        }
    }
}
enum P {
    Safe(Planner),
    Raw(Raw),
}
impl P {
    fn state(&self) -> State {
        match self {
            Self::Safe(p) => p.state().unwrap(),
            Self::Raw(p) => {
                let mut s = State {
                    struct_size: std::mem::size_of::<State>() as u32,
                    ..Default::default()
                };
                unsafe {
                    check(sys::abc_planner_get_state(p.0, &mut s));
                }
                s
            }
        }
    }
    fn reset(&mut self, seed: u64) {
        match self {
            Self::Safe(p) => p.reset(seed).unwrap(),
            Self::Raw(p) => unsafe {
                check(sys::abc_planner_reset(p.0, seed));
            },
        }
    }
    fn target(&mut self, v: Vec2, t: i64) {
        match self {
            Self::Safe(p) => p.update_target(v, t).unwrap(),
            Self::Raw(p) => unsafe {
                check(sys::abc_planner_update_target(p.0, v, t));
            },
        }
    }
    fn advance(&mut self, t: i64, out: &mut [Sample]) {
        match self {
            Self::Safe(p) => {
                assert_eq!(p.advance_into(t, out).unwrap(), out.len());
            }
            Self::Raw(p) => {
                let mut n = 0;
                unsafe {
                    check(sys::abc_planner_advance(
                        p.0,
                        t,
                        out.as_mut_ptr(),
                        out.len() as u64,
                        &mut n,
                    ));
                }
                assert_eq!(n, out.len() as u64);
            }
        }
    }
}
#[cfg(windows)]
fn memory() -> Option<usize> {
    #[repr(C)]
    struct Counters {
        cb: u32,
        page_fault_count: u32,
        values: [usize; 9],
    }
    #[link(name = "kernel32")]
    extern "system" {
        fn GetCurrentProcess() -> *mut std::ffi::c_void;
    }
    #[link(name = "psapi")]
    extern "system" {
        fn GetProcessMemoryInfo(
            process: *mut std::ffi::c_void,
            counters: *mut Counters,
            cb: u32,
        ) -> i32;
    }
    let mut c = Counters {
        cb: std::mem::size_of::<Counters>() as u32,
        page_fault_count: 0,
        values: [0; 9],
    };
    unsafe {
        if GetProcessMemoryInfo(
            GetCurrentProcess(),
            &mut c,
            std::mem::size_of::<Counters>() as u32,
        ) != 0
        {
            Some(c.values[8])
        } else {
            None
        }
    }
}
#[cfg(not(windows))]
fn memory() -> Option<usize> {
    None
}
fn main() -> std::result::Result<(), Box<dyn std::error::Error>> {
    let args: Vec<_> = std::env::args().collect();
    if args.len() != 8 {
        return Err(
            "replay rust|rust_raw model fixture output renderer-model renderer-fixture batch"
                .into(),
        );
    }
    let raw = args[1] == "rust_raw";
    let batch: usize = args[7].parse()?;
    let input = fs::read_to_string(&args[3])?;
    let mut tokens = input.split_whitespace();
    let duration: usize = tokens.next().unwrap().parse()?;
    let seed: u64 = tokens.next().unwrap().parse()?;
    let initial = Vec2 {
        x: tokens.next().unwrap().parse()?,
        y: tokens.next().unwrap().parse()?,
    };
    let np: usize = tokens.next().unwrap().parse()?;
    let ne: usize = tokens.next().unwrap().parse()?;
    let mut history = Vec::new();
    for _ in 0..np {
        history.push(Vec2 {
            x: tokens.next().unwrap().parse()?,
            y: tokens.next().unwrap().parse()?,
        });
    }
    let mut events = Vec::new();
    for _ in 0..ne {
        events.push((
            tokens.next().unwrap().parse::<i64>()?,
            Vec2 {
                x: tokens.next().unwrap().parse()?,
                y: tokens.next().unwrap().parse()?,
            },
        ));
    }
    let rin = fs::read_to_string(&args[6])?;
    let mut rt = rin.split_whitespace();
    rt.next();
    let scale: f64 = rt.next().unwrap().parse()?;
    rt.next();
    let mut profile = Vec::new();
    for _ in 0..256 {
        profile.push(Report {
            dx: rt.next().unwrap().parse()?,
            dy: rt.next().unwrap().parse()?,
        });
    }
    let start = Instant::now();
    let model = Model::load(Path::new(&args[2]))?;
    let load = start.elapsed().as_secs_f64() * 1e6;
    let start = Instant::now();
    let options = PlannerOptions {
        seed,
        initial_position: initial,
        history: &history,
        diagnostic_proposals: false,
    };
    let mut p = if raw {
        let mut m = 0;
        let path = CString::new(args[2].as_str())?;
        unsafe {
            check(sys::abc_model_load(path.as_ptr(), &mut m));
        }
        let o = sys::Options {
            struct_size: std::mem::size_of::<sys::Options>() as u32,
            flags: 0,
            seed,
            initial_position: initial,
            history: history.as_ptr(),
            history_count: history.len() as u64,
        };
        let mut handle = 0;
        unsafe {
            check(sys::abc_planner_create(m, &o, &mut handle));
            check(sys::abc_model_release(m));
        }
        P::Raw(Raw(handle))
    } else {
        P::Safe(Planner::new(&model, options)?)
    };
    let initialize = start.elapsed().as_secs_f64() * 1e6;
    let mut renderer = Renderer::load(&args[5])?;
    renderer.set_profile(&profile)?;
    let reset = |p: &mut P, r: &mut Renderer| {
        p.reset(seed);
        r.reset(seed).unwrap();
        for (t, v) in &events {
            p.target(*v, *t);
        }
    };
    let mut samples = vec![Sample::default(); duration];
    reset(&mut p, &mut renderer);
    p.advance(duration as i64 * 1000, &mut samples);
    reset(&mut p, &mut renderer);
    let mut states = vec![State::default(); duration];
    let mut reports = vec![Report::default(); duration];
    let mut timings = Vec::new();
    let mut previous = initial;
    for i in (0..duration).step_by(batch) {
        let n = batch.min(duration - i);
        let before = p.state();
        let start = Instant::now();
        p.advance((i + n) as i64 * 1000, &mut samples[i..i + n]);
        let elapsed = start.elapsed().as_secs_f64() * 1e6;
        let after = p.state();
        let start = Instant::now();
        renderer.render_samples_into(
            &samples[i..i + n],
            &mut previous,
            scale * COMMON_RADIANS_PER_COUNT,
            &mut reports[i..i + n],
        )?;
        let render = start.elapsed().as_secs_f64() * 1e6;
        for s in &mut states[i..i + n] {
            *s = after;
        }
        timings.push((
            (i + n) * 1000,
            n,
            elapsed,
            render,
            after.decision_count - before.decision_count,
            after.motor_evaluations - before.motor_evaluations,
        ));
    }
    reset(&mut p, &mut renderer);
    let start = Instant::now();
    let mut temp = vec![Sample::default(); duration];
    let start_compute = Instant::now();
    p.advance(duration as i64 * 1000, &mut temp);
    let trace = start_compute.elapsed().as_secs_f64() * 1e6;
    let _ = start;
    let stem = &args[4];
    let mut out = BufWriter::new(fs::File::create(format!("{stem}.samples.csv"))?);
    writeln!(
        out,
        "t,x,y,dx,dy,mode,head,decisions,motors,decision_us,receipt_us,pending,rdx,rdy"
    )?;
    for i in 0..duration {
        let s = samples[i];
        let d = states[i];
        writeln!(
            out,
            "{},{:.17e},{:.17e},{:.17e},{:.17e},{},{},{},{},{},{},{},{},{}",
            s.timestamp_us,
            s.position.x,
            s.position.y,
            s.displacement.x,
            s.displacement.y,
            d.mode,
            d.selected_head,
            d.decision_count,
            d.motor_evaluations,
            d.last_decision_us,
            d.last_target_receipt_us,
            d.pending_samples,
            reports[i].dx,
            reports[i].dy
        )?;
    }
    let mut out = BufWriter::new(fs::File::create(format!("{stem}.timings.csv"))?);
    writeln!(out, "t,n,us,renderer_us,decisions,motors")?;
    for (t, n, u, r, d, m) in timings {
        writeln!(out, "{t},{n},{u},{r},{d},{m}")?;
    }
    let mut out = BufWriter::new(fs::File::create(format!("{stem}.overhead.csv"))?);
    writeln!(out, "calls,us")?;
    let mut sum = 0i64;
    for _ in 0..50 {
        let start = Instant::now();
        for _ in 0..1000 {
            sum += std::hint::black_box(p.state()).numerical_time_us;
        }
        writeln!(out, "1000,{}", start.elapsed().as_secs_f64() * 1e6)?;
    }
    let info = model.info()?;
    let hash = unsafe { CStr::from_ptr(info.sha256.as_ptr()) }.to_str()?;
    let private = memory()
        .map(|v| v.to_string())
        .unwrap_or_else(|| "null".into());
    fs::write(format!("{stem}.json"),format!("{{\"load_us\":{load},\"initialize_us\":{initialize},\"batch_trace_us\":{trace},\"private_bytes\":{private},\"model_sha256\":\"{hash}\",\"state_checksum\":{sum}}}"))?;
    Ok(())
}
