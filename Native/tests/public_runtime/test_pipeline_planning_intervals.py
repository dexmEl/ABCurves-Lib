"""Keep one renderer state across faster planner commitments."""
import numpy as np
import pytest


@pytest.mark.parametrize('interval', [8, 16, 32])
def test_pipeline_samples_and_renderer_state_do_not_depend_on_read_batches(make_pipeline, interval):
    pipeline = make_pipeline(planning_interval_ms=interval)
    def run(chunks):
        pipeline.reset()
        pipeline.update_target((80., -35.), 0)
        pipeline.update_target((-30., 65.), 15500)
        parts = [pipeline.advance(t) for t in chunks]
        return {k:np.concatenate([part[k] for part in parts]) for k in parts[0]}
    whole = run([96000])
    renderer = pipeline.renderer
    chunked = run(range(1000, 96001, 1000))
    assert pipeline.renderer is not renderer
    assert pipeline.planning_interval_ms == interval
    assert pipeline.movement.fresh_decisions == 96 // interval
    for key in whole:
        np.testing.assert_array_equal(whole[key], chunked[key])
    assert whole['reports'].shape == (96, 2)
    assert whole['reports'].dtype == np.int16
