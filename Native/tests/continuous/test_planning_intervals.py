"""Cadence changes preserve causal output and physical-time policy state."""
import math

import numpy as np
import pytest

from abcurves import ContinuousPlanner
from abcurves._continuous import kernels
from abcurves._continuous.planner import Planner
from abcurves._continuous.runtime import MovementRuntime, RuntimeFailure


class TargetPlanner:
    def __init__(self, interval):
        self.planning_interval_ms = interval

    def reset(self, *, seed):
        self.calls = []

    def plan(self, history, position, target, available, valid, motion_known, now_us):
        self.calls.append((now_us, target[-1].copy()))
        return np.tile(target[-1], (self.planning_interval_ms, 1))


@pytest.mark.parametrize('interval', [8, 16, 32])
def test_receipt_first_affects_next_uncommitted_sample(interval):
    planner = TargetPlanner(interval)
    runtime = MovementRuntime(planner, planning_interval_ms=interval)
    runtime.update_target((1., 0.), 0)
    runtime.advance(1000)
    runtime.update_target((-1., 0.), 1000)
    first = runtime.advance(interval * 1000)
    np.testing.assert_array_equal(first['xy'][:, 0], np.arange(2, interval + 1))
    assert len(planner.calls) == 1 and runtime.needs_plan
    last = runtime.advance((interval + 1) * 1000)
    assert [c[0] for c in planner.calls] == [0, interval * 1000]
    np.testing.assert_array_equal(last['xy'], [[interval - 1, 0]])
    assert runtime.planning_interval_ms == interval
    runtime.reset()
    assert runtime.planning_interval_ms == interval and runtime.fresh_decisions == 0


@pytest.mark.parametrize('interval', [8, 16, 32])
def test_partial_failure_boundary_uses_selected_interval(interval):
    class Broken(TargetPlanner):
        def plan(self, *args):
            result = super().plan(*args)
            if len(self.calls) == 2:
                result[0] = np.nan
            return result
    runtime = MovementRuntime(Broken(interval), planning_interval_ms=interval)
    runtime.update_target((1., 0.), 0)
    with pytest.raises(RuntimeFailure) as caught:
        runtime.advance(100000)
    assert caught.value.at_us == interval * 1000
    assert len(caught.value.partial['xy']) == interval
    assert runtime.failed


@pytest.mark.parametrize('invalid', [True, np.bool_(False), 8., '16', None, 0, -8, 4, 24, 64])
def test_invalid_interval_is_rejected_before_loading_assets(invalid):
    with pytest.raises(ValueError, match='planning_interval_ms'):
        ContinuousPlanner('nonexistent-assets', planning_interval_ms=invalid)


def test_runtime_rejects_policy_interval_mismatch():
    with pytest.raises(ValueError, match='must match'):
        MovementRuntime(TargetPlanner(8), planning_interval_ms=16)


@pytest.mark.parametrize('interval', [8, 16, 32])
@pytest.mark.parametrize('backend', ['numpy', 'numba'])
def test_selector_compares_forecasts_at_the_same_physical_times(interval, backend):
    # The first comparison pair must use the predecessor samples at the elapsed
    # offset. This detects accidentally retaining the old 32 ms predecessor shift.
    times = np.arange(2.5, 65, 4, dtype=np.float32)
    previous = np.column_stack((times, -times))
    current = np.tile(previous[interval // 4:interval // 4 + 8], (16, 1, 1))
    shift = interval // 4
    actual = previous[:shift].sum(0) * np.float32(4)
    _, pairs = kernels.selector_inputs(np.zeros((1, 96), np.float32), current, previous, actual,
                                      elapsed_ms=interval, backend=backend)
    np.testing.assert_allclose(pairs[0, :, :16], 0, atol=1e-7)
    np.testing.assert_allclose(pairs[0, :, 18:20], 0, atol=1e-7)


def policy_fixture(interval):
    class ConstantHazard:
        def hazard(self, context):
            return np.full(2, math.log(.2 / .8), np.float32)
    policy = object.__new__(Planner)
    policy._planning_interval_ms = interval
    policy.engine = ConstantHazard()
    policy.diagnostics = False
    policy.skip_unused = True
    policy.kernel_backend = 'numpy'
    policy._times = np.arange(interval + 1, dtype=np.float64)
    policy._recent_offsets = np.arange(1 - interval, 1, dtype=np.int64) * 1000
    policy.reset(seed=7)
    policy._motor = lambda *args: (np.zeros((interval, 2)), 0, np.full(16, 1 / 16))
    history = np.zeros((640, 2))
    position = np.zeros(2)
    target = np.full((640, 2), 100.)
    available = np.full(640, -640000, np.int64)
    known = np.ones(640, bool)
    policy._initialize(history, position, target[-1], 0)
    return policy, (history, position, target, available, known, known)


@pytest.mark.parametrize('mode', [0, 2])
def test_constant_hazard_exposure_is_independent_of_decision_frequency(mode):
    integrals = []
    for interval in (8, 16, 32):
        policy, args = policy_fixture(interval)
        policy.mode = mode
        policy.stop_budget = policy.resume_budget = 100.
        for t in range(0, 32000, interval * 1000):
            policy.plan(*args, t)
        integrals.append(policy.stop_integral if mode == 0 else policy.resume_integral)
        assert policy.age == 192.
    np.testing.assert_allclose(integrals, [-math.log(.8)] * 3, rtol=1e-6)


@pytest.mark.parametrize('interval', [8, 16, 32])
def test_braking_follows_one_curve_until_its_physical_end(interval):
    policy, args = policy_fixture(interval)
    policy.mode = 1
    policy.age = 0.
    policy.duration = 20.
    policy.brake_v[:] = [1., -.5]
    policy.brake_a[:] = [.01, -.02]
    policy.coeff[0] = [12., -7.]
    blocks = []
    while policy.mode == 1:
        t = len(blocks) * interval * 1000
        blocks.append(policy.plan(*args, t))
    actual = np.concatenate(blocks)
    curve = kernels.c2_path(policy.brake_v, policy.brake_a, policy.duration, policy.coeff,
                            np.arange(len(actual) + 1, dtype=float), backend='numpy')
    np.testing.assert_allclose(actual, np.diff(curve, axis=0), atol=1e-12)
    assert policy.age == len(actual) - policy.duration
    assert policy.mode == 2


@pytest.mark.parametrize('interval', [8, 16, 32])
def test_real_model_chunking_reset_and_fresh_counts(interval):
    planner = ContinuousPlanner(seed=71, planning_interval_ms=interval, prewarm=False)
    def run(chunks):
        planner.reset()
        planner.update_target((80., -35.), 0)
        planner.update_target((-30., 65.), 15500)
        planner.update_target((110., 20.), 66001)
        output = [planner.advance(t) for t in chunks]
        return {k:np.concatenate([part[k] for part in output]) for k in ('xy', 'time_us')}
    whole = run([160000])
    assert planner.fresh_decisions == 160 // interval
    chunked = run([500, 1500, 8000, 16000, 32100, 97000, 160000])
    for key in whole:
        np.testing.assert_array_equal(whole[key], chunked[key])
    assert np.isfinite(whole['xy']).all()
