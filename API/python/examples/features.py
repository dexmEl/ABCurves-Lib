"""Focused, executable API demonstrations; no physical output is performed.

Run: python API/python/examples/features.py lib/models/16ms --scenario async
The Renderer example additionally needs --renderer and --gain; its synthetic
profile is only a smoke fixture, not hardware calibration.
"""
import argparse
from abcurves_native import (
    COMMON_RADIANS_PER_COUNT, Model, NativeError, Planner, Renderer,
    SampleBuffer, SegmentController, prepare_history,
)


def main():
    args = argparse.ArgumentParser(description=__doc__)
    args.add_argument('model')
    args.add_argument('--scenario', choices=('prepared-prefix', 'raw-prefix', 'async', 'gap', 'recovery', 'failure', 'renderer', 'buffers', 'reset'), required=True)
    args.add_argument('--renderer')
    args.add_argument('--gain', type=float)
    a = args.parse_args()
    options = dict(seed=11)
    if a.scenario == 'prepared-prefix':
        options['history'] = [(0.01, -0.005)] * 160
    if a.scenario == 'raw-prefix':
        history = prepare_history([(0.01, -0.005)] * 164, current_position=(0., 0.))
        options.update(history=history.history, initial_position=history.initial_position)
    with Model(a.model) as model, Planner(model, **options) as planner, SegmentController(planner) as controller:
        controller.relative((120., -7.), 0, 0)
        storage = SampleBuffer(controller.required_capacity())
        first = controller.next_segment_into(storage)
        end = first.end_timestamp_us
        if a.scenario == 'async':
            controller.relative((14., -6.), end - 2000, end - 2000)
            controller.relative((12., -5.), end - 1000, end - 1000)
        elif a.scenario == 'gap':
            controller.next_segment()  # Retain target across a missing update.
            end = controller.metadata.end_timestamp_us
            controller.unavailable(end, end)  # Explicit target loss.
        elif a.scenario == 'recovery':
            controller.absolute((30., 10.), end + 1_000_000, end + 1_000_000)
            controller.clear_pending_observations()  # No learned/session reset.
            assert controller.recovery_diagnostics.explicit_pending_clears == 1
        elif a.scenario == 'failure':
            controller.absolute((1.3e308, 1.3e308), end, end)
            try:
                controller.next_segment()
                raise AssertionError('expected genuine numerical failure')
            except NativeError as error:
                assert error.status == 13
                state = controller.state
                print(state.failure_status, state.failure_category, state.error_message)
                assert state.reset_required and planner.state.failed
                controller.reset_segment_session()  # Deliberate recovery.
        elif a.scenario == 'reset':
            controller.reset_segment_session()  # Retains seed11 and prefix.
            assert controller.state.numerical_time_us == 0
        elif a.scenario == 'renderer':
            if not a.renderer or a.gain is None:
                args.error('Renderer requires --renderer and application-calibrated --gain')
            with Renderer(a.renderer) as renderer:
                # Synthetic profile: replace with 256 representative calibrated reports.
                renderer.set_profile([(1, 0), (0, 1), (-1, 0), (0, -1)] * 64)
                renderer.reset(11)
                # Render before storage reuse; preserve this Renderer across segments.
                from abcurves_native import Segment
                reports = renderer.render_segment(Segment(first, storage), a.gain)
                assert len(reports) == first.sample_count
                print('reports', len(reports))
        # Reuse only after the application no longer needs the previous storage.
        second = controller.next_segment_into(storage)
        for sample in storage:
            assert sample.timestamp_us > second.start_timestamp_us
        print(a.scenario, second.sample_count, controller.diagnostics.total_segments)


if __name__ == '__main__':
    main()
