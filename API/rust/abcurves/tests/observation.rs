use abcurves_native::*;
use std::path::PathBuf;
fn root() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../..")
}
#[test]
fn independent_relative_all_variants_prefixes() {
    for c in [32, 16, 8] {
        for prefix in [0, 160, 164] {
            let m = Model::load(root().join(format!("lib/models/{c}ms"))).unwrap();
            let raw: Vec<_> = (0..164)
                .map(|i| Vec2 {
                    x: i as f64 / 1000.,
                    y: -(i as f64) / 2000.,
                })
                .collect();
            let start = prepare_history(&raw, Vec2 { x: 100., y: 50. }).unwrap();
            let initial = if prefix == 164 {
                start.initial_position
            } else {
                Vec2 { x: 100., y: 50. }
            };
            let history = if prefix == 164 {
                &start.history[..]
            } else if prefix == 160 {
                &raw[..160]
            } else {
                &[]
            };
            let opts = || PlannerOptions {
                seed: 11,
                initial_position: initial,
                history,
                diagnostic_proposals: false,
            };
            let mut a = Planner::new(&m, opts()).unwrap();
            let mut b = Planner::new(&m, opts()).unwrap();
            let mut input = ObservationInput::new(
                &mut a,
                ObservationOptions {
                    max_age_us: 30000,
                    ..Default::default()
                },
            )
            .unwrap();
            let r = input.relative(Vec2 { x: 12., y: -7. }, 0, 0).unwrap();
            assert_eq!(
                r,
                Vec2 {
                    x: initial.x + 12.,
                    y: initial.y - 7.
                }
            );
            b.update_target(r, 0).unwrap();
            for i in 1..201 {
                let t = i * 1000;
                assert_eq!(
                    input.planner_mut().generate_sample(t).unwrap(),
                    b.generate_sample(t).unwrap()
                );
                if i % 17 == 0 {
                    let r = input
                        .relative(Vec2 { x: 10., y: -4. }, t - 12000, t + 500)
                        .unwrap();
                    b.update_target(r, t + 500).unwrap();
                    assert_eq!(
                        input
                            .relative(Vec2 { x: 10., y: -4. }, t - 12000, t + 500)
                            .unwrap_err()
                            .status,
                        16
                    );
                    if t > 30000 {
                        assert_eq!(
                            input
                                .relative(Vec2 { x: 10., y: -4. }, 0, t + 500)
                                .unwrap_err()
                                .status,
                            15
                        );
                    }
                }
            }
            input.planner_mut().reinitialize(opts()).unwrap();
            assert_eq!(input.diagnostics().unwrap().accepted, 0);
            input.absolute(Vec2 { x: 320., y: 120. }, 0, 0).unwrap();
            b.reinitialize(opts()).unwrap();
            b.update_target(Vec2 { x: 320., y: 120. }, 0).unwrap();
            assert_eq!(
                input.planner_mut().advance(128000).unwrap(),
                b.advance(128000).unwrap()
            );
        }
    }
}
#[test]
fn policies_layout_and_errors() {
    let mut a = 0;
    let mut b = 0;
    let mut c = 0;
    unsafe {
        assert_eq!(
            abcurves_sys::abc_observation_layout(&mut a, &mut b, &mut c),
            0
        );
    }
    assert_eq!((a, b, c), (16, 48, 112));
    for policy in [
        ObservationOrdering::Strict,
        ObservationOrdering::Latest,
        ObservationOrdering::Caller,
    ] {
        let m = Model::load(root().join("lib/models/8ms")).unwrap();
        let mut p = Planner::new(&m, Default::default()).unwrap();
        let mut i = ObservationInput::new(
            &mut p,
            ObservationOptions {
                ordering: policy,
                max_age_us: -1,
            },
        )
        .unwrap();
        i.planner_mut().advance(10000).unwrap();
        i.relative(Vec2 { x: 1., y: 2. }, 5000, 10000).unwrap();
        let same = i.relative(Vec2 { x: 3., y: 4. }, 5000, 10000);
        if matches!(policy, ObservationOrdering::Strict) {
            assert_eq!(same.unwrap_err().status, 16);
        } else {
            same.unwrap();
        }
        let older = i.relative(Vec2 { x: 5., y: 6. }, 4000, 10000);
        if matches!(policy, ObservationOrdering::Caller) {
            older.unwrap();
        } else {
            assert_eq!(older.unwrap_err().status, 17);
        }
        i.planner_mut().advance(11000).unwrap();
        i.unavailable(11000, 11000, false).unwrap();
        assert_eq!(i.diagnostics().unwrap().availability, 1);
        i.planner_mut().advance(12000).unwrap();
        i.unavailable(12000, 12000, true).unwrap();
        assert_eq!(i.diagnostics().unwrap().availability, 2);
        assert_eq!(
            i.relative(Vec2 { x: 1., y: 2. }, 0, i64::MAX)
                .unwrap_err()
                .status,
            9
        );
        i.planner_mut().reset(7).unwrap();
        assert_eq!(i.diagnostics().unwrap().accepted, 0);
    }
}

#[test]
fn observation_instances_are_independent_on_threads() {
    fn run(c: u32) -> Vec<Sample> {
        let m = Model::load(root().join(format!("lib/models/{c}ms"))).unwrap();
        let mut p = Planner::new(&m, Default::default()).unwrap();
        let mut input = ObservationInput::new(&mut p, Default::default()).unwrap();
        let mut result = Vec::new();
        for tick in 0..8 {
            let t = tick * 32000;
            input.relative(Vec2 { x: 120., y: -20. }, t, t).unwrap();
            result.extend(input.planner_mut().advance(t + 32000).unwrap());
        }
        result
    }
    let expected: Vec<_> = [32, 16, 8].into_iter().map(run).collect();
    let jobs: Vec<_> = [32, 16, 8]
        .into_iter()
        .map(|c| std::thread::spawn(move || run(c)))
        .collect();
    let actual: Vec<_> = jobs.into_iter().map(|j| j.join().unwrap()).collect();
    assert_eq!(expected, actual);
}
