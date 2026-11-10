from pathlib import Path
import ctypes as C
import pytest
from abcurves_native import *
ROOT=Path(__file__).resolve().parents[3]

@pytest.mark.parametrize('variant',[32,16,8])
def test_deferred_selective_rejection_and_watermarks(variant):
    big=1.3e308
    with Model(ROOT/f'lib/models/{variant}ms') as model,Planner(model,initial_position=(big,big)) as p,Planner(model,initial_position=(big,big)) as ref,SegmentController(p) as s,SegmentController(ref) as r:
        for ctl in (s,r):ctl.absolute((big,big),1000,1000)
        s.relative((big,big),2000,2000)
        for ctl in (s,r):ctl.absolute((big,big),3000,3000)
        for _ in range(8):
            a,b=s.next_segment(),r.next_segment()
            assert bytes(a.samples.array)==bytes(b.samples.array)
            assert (a.metadata.mode,a.metadata.selected_head)==(b.metadata.mode,b.metadata.selected_head)
        d=s.recovery_diagnostics
        assert (d.invalid_pending_observations,d.nonfinite_relative_resolutions,d.pending_observations)==(1,1,0)
        assert (d.last_error_status,d.last_error_capture_us,d.last_error_receipt_us)==(1,2000,2000)
        assert s.diagnostics.segment_generation_errors==0
        for _ in range(3):
            t=p.state.numerical_time_us
            for j in range(1,65):s.relative((big,big),t+j,t+j)
            for _ in range(2):assert bytes(s.next_segment().samples.array)==bytes(r.next_segment().samples.array)
            assert s.recovery_diagnostics.pending_observations==0
        assert s.recovery_diagnostics.recoverable_observation_errors==193
        t=p.state.numerical_time_us
        s.absolute((big,big),t+1000000,t+1000000)
        before=bytes(p.state);s.clear_pending_observations();s.clear_pending_observations()
        assert bytes(p.state)==before and s.diagnostics.resolved_target.x==big
        for ctl in (s,r):ctl.absolute((big,big),t,t)
        assert bytes(s.next_segment().samples.array)==bytes(r.next_segment().samples.array)
        s.reset_diagnostics();assert s.recovery_diagnostics.recoverable_observation_errors==0
        assert s.recovery_diagnostics.last_error_status==1
        s.reset_segment_session();assert s.recovery_diagnostics.last_error_status==0

@pytest.mark.parametrize('variant',[32,16,8])
def test_clear_preserves_active_rng_and_renderer(variant):
    with Model(ROOT/f'lib/models/{variant}ms') as model,Planner(model) as p,Planner(model) as ref,SegmentController(p) as s,SegmentController(ref) as r,Renderer(ROOT/'Native/models/renderer_global_h80.bin') as renderer,Renderer(ROOT/'Native/models/renderer_global_h80.bin') as old:
        for ctl in (s,r):ctl.absolute((420.,170.),0,0)
        for obj in (renderer,old):obj.set_profile([(i%9-4,i%7-3) for i in range(256)]);obj.reset(11)
        for i in range(160):
            a,b=s.next_segment(),r.next_segment()
            assert bytes(a.samples.array)==bytes(b.samples.array)
            assert bytes(renderer.render_segment(a,COMMON_RADIANS_PER_COUNT*8))==bytes(old.render_segment(b,COMMON_RADIANS_PER_COUNT*8))
            t=a.metadata.end_timestamp_us
            for j in range(64):s.absolute((float(j),1.),t+1000000+j,t+1000000+j)
            before=bytes(p.state);s.clear_pending_observations();assert bytes(p.state)==before
            if i in (70,75):
                for ctl in (s,r):
                    if i==70:ctl.unavailable(t,t)
                    else:ctl.absolute((420.,170.),t,t)
        p.close()
        with pytest.raises(NativeError):s.clear_pending_observations()
        with pytest.raises(NativeError):s.recovery_diagnostics

def test_immediate_errors_and_old_layouts():
    assert C.sizeof(SegmentDiagnostics)==208 and C.sizeof(SegmentRecoveryDiagnostics)==72
    with Model(ROOT/'lib/models/16ms') as m,Planner(m) as p,SegmentController(p) as s:
        for obs in [TargetObservation((float('inf'),0),0,0,True),TargetObservation((float('nan'),0),0,0),TargetObservation((1,2),2,1)]:
            before=bytes(p.state)
            with pytest.raises(NativeError):s.submit(obs)
            assert before==bytes(p.state)
        assert s.recovery_diagnostics.recoverable_observation_errors==0

@pytest.mark.parametrize('ordering',[ObservationOrdering.STRICT,ObservationOrdering.LATEST,ObservationOrdering.CALLER])
def test_cleared_fingerprints_and_receipt_watermarks(ordering):
    with Model(ROOT/'lib/models/16ms') as m,Planner(m) as p,SegmentController(p,ordering=ordering) as s:
        s.absolute((420.,170.),0,0);s.next_segment()
        s.absolute((430.,171.),1000000,1000000)
        s.clear_pending_observations()
        # Cancellation forgets only pending fingerprints: exact resend works.
        s.absolute((430.,171.),1000000,1000000);s.clear_pending_observations()
        # The future canceled watermark does not reject chronological new input.
        s.absolute((431.,172.),16000,16000)
        with pytest.raises(NativeError) as e:s.absolute((420.,170.),0,0)
        assert e.value.status==16  # retained applied fingerprint

@pytest.mark.parametrize('variant',[32,16,8])
def test_internal_inference_failure_is_not_recovered(variant):
    with Model(ROOT/f'lib/models/{variant}ms') as m,Planner(m) as p,SegmentController(p) as s:
        # Finite absolute coordinates with a huge offset can overflow model
        # feature math. This is a core failure, not relative-resolution recovery.
        s.absolute((1.3e308,1.3e308),0,0)
        with pytest.raises(NativeError) as e:s.next_segment()
        assert e.value.status==13 and p.state.numerical_time_us==0
        with pytest.raises(NativeError):s.next_segment()
        s.reset_segment_session()  # explicit recovery remains required here
        assert s.recovery_diagnostics.recoverable_observation_errors==0
        assert p.state.failed==0

def test_rejected_pending_does_not_poison_strict_watermark():
    big=1.3e308
    model=Model(ROOT/'lib/models/16ms')
    with Planner(model,initial_position=(big,big)) as p,SegmentController(p,ordering=ObservationOrdering.STRICT) as s:
        s.relative((big,big),1000,1000)
        s.next_segment();s.next_segment()
        assert s.diagnostics.last_capture_us==-1 and s.diagnostics.last_receipt_us==-1
        # Corrected input can use the rejected record's original capture.
        s.absolute((big,big),1000,1000);s.next_segment()
        assert s.diagnostics.resolved_target.x==big
    model.close()

def test_future_valid_survives_deferred_rejection_and_rejected_fingerprint_removed():
    big=1.3e308
    with Model(ROOT/'lib/models/16ms') as model,Planner(model,initial_position=(big,big)) as p,SegmentController(p) as s:
        s.relative((big,big),1000,1000)
        s.absolute((big,big),10000,10000)
        s.next_segment();s.next_segment()
        assert s.recovery_diagnostics.pending_observations==1
        assert s.diagnostics.last_receipt_us==10000
        # Surviving future input still enforces the existing receipt rule.
        with pytest.raises(NativeError) as e:s.absolute((big,big),2000,2000)
        assert e.value.status==17
        s.clear_pending_observations()
        # Same invalid fingerprint is accepted again after removal, rather than
        # rejected as a duplicate; now its sum fails immediately at submission.
        with pytest.raises(NativeError) as e:s.relative((big,big),1000,1000)
        assert e.value.status==1
        for _ in range(4):assert len(s.next_segment().samples)==1
