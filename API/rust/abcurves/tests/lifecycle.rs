use abcurves_native::*;
use std::path::PathBuf;
fn root() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../..")
}
#[test]
fn layouts() {
    let v = version().unwrap();
    assert_eq!(v.abi_major, 1);
    assert_eq!(std::mem::offset_of!(Sample, position), 8);
    assert_eq!(std::mem::offset_of!(State, position), 80);
}
#[test]
fn variants_and_batch() {
    for c in [32, 16, 8] {
        let m = Model::load(root().join(format!("lib/models/{c}ms"))).unwrap();
        assert_eq!(m.info().unwrap().commitment_ms, c);
        assert_eq!(
            m.tensor_info("motor.input.0.weight").unwrap().dimensions[..2],
            [96, 9]
        );
        let mut a = Planner::new(&m, PlannerOptions::default()).unwrap();
        let mut b = Planner::new(&m, PlannerOptions::default()).unwrap();
        drop(m);
        for p in [&mut a, &mut b] {
            p.update_target(Vec2 { x: 320., y: 120. }, 0).unwrap();
        }
        let batch = a.advance(256000).unwrap();
        let single: Vec<_> = (1..=256)
            .map(|i| b.generate_sample(i * 1000).unwrap())
            .collect();
        assert_eq!(batch, single);
        assert!(a.advance(1000).is_err());
        a.reset(7).unwrap();
        a.update_target(Vec2 { x: 320., y: 120. }, 0).unwrap();
        assert_eq!(batch, a.advance(256000).unwrap());
        a.clear_target(256000).unwrap();
        b.clear_target(256000).unwrap();
        let lost = a.advance(512000).unwrap();
        let single: Vec<_> = (257..=512)
            .map(|i| b.generate_sample(i * 1000).unwrap())
            .collect();
        assert_eq!(lost, single);
        assert_eq!(lost.last().unwrap().displacement, Vec2 { x: 0., y: 0. });
        a.update_target(Vec2 { x: -200., y: 40. }, 512000).unwrap();
        a.advance(544000).unwrap();
        assert_eq!(a.state().unwrap().last_target_receipt_us, 512000);
    }
}
#[test]
fn independent_threads() {
    let jobs: Vec<_> = [32, 16, 8]
        .into_iter()
        .map(|c| {
            std::thread::spawn(move || {
                let m = Model::load(root().join(format!("lib/models/{c}ms"))).unwrap();
                let mut p = Planner::new(&m, PlannerOptions::default()).unwrap();
                p.update_target(Vec2 { x: 120., y: 20. }, 0).unwrap();
                p.advance(1000000).unwrap().len()
            })
        })
        .collect();
    for j in jobs {
        assert_eq!(j.join().unwrap(), 1000);
    }
}
#[test]
fn errors() {
    assert!(Model::load(root().join("does-not-exist")).is_err());
    let m = Model::load(root().join("lib/models/32ms")).unwrap();
    let mut p = Planner::new(&m, PlannerOptions::default()).unwrap();
    assert_eq!(p.advance_into(1000, &mut []).unwrap_err().status, 10);
    assert_eq!(p.advance(-1).unwrap_err().status, 9);
}

#[test]
fn renderer_profile_and_batch() {
    let f=root().join("tests/baselines/renderer/32ms_120fps_p0_s7_mixed_irregular_same_input.fixture");
    let text = std::fs::read_to_string(f).unwrap();
    let mut t = text.split_whitespace();
    for _ in 0..3 {
        t.next();
    }
    let profile: Vec<_> = (0..256)
        .map(|_| Report {
            dx: t.next().unwrap().parse().unwrap(),
            dy: t.next().unwrap().parse().unwrap(),
        })
        .collect();
    let mut r = Renderer::load(root().join("Native/models/renderer_global_h80.bin")).unwrap();
    assert!(r.reset(7).is_err());
    r.set_profile(&profile).unwrap();
    r.reset(7).unwrap();
    let intents = vec![Vec2 { x: 0.1, y: 0.03 }; 256];
    let mut reports = vec![Report::default(); 256];
    r.step_batch_into(&intents, &mut reports).unwrap();
    r.reset(7).unwrap();
    let singles: Vec<_> = intents.iter().map(|p| r.step(*p).unwrap()).collect();
    assert_eq!(reports, singles);
    let m = Model::load(root().join("lib/models/16ms")).unwrap();
    let mut p = Planner::new(&m, PlannerOptions::default()).unwrap();
    p.update_target(Vec2 { x: 320., y: 120. }, 0).unwrap();
    let s = p.advance(256000).unwrap();
    r.reset(7).unwrap();
    let mut previous = Vec2::default();
    assert_eq!(
        r.render_samples_into(&s, &mut previous, 0.0008, &mut reports)
            .unwrap(),
        256
    );
}
#[test]
fn shared_model_threads() {
    let m = std::sync::Arc::new(Model::load(root().join("lib/models/16ms")).unwrap());
    let mut workers = Vec::new();
    for _ in 0..4 {
        let m = m.clone();
        workers.push(std::thread::spawn(move || {
            let mut p = Planner::new(&m, PlannerOptions::default()).unwrap();
            p.update_target(Vec2 { x: 120., y: 40. }, 0).unwrap();
            p.advance(256000).unwrap()
        }));
    }
    let first = workers.remove(0).join().unwrap();
    for w in workers {
        assert_eq!(first, w.join().unwrap());
    }
}
