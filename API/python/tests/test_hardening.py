from pathlib import Path
import pytest
from abcurves_native import Model, Planner, SegmentController, ObservationInput, NativeError
ROOT=Path(__file__).resolve().parents[3]

@pytest.mark.parametrize('variant',[32,16,8])
def test_failure_diagnostics_reset_and_capacity(variant):
    with Model(ROOT/f'lib/models/{variant}ms') as model,Planner(model) as p,SegmentController(p) as s:
        assert s.required_capacity()==variant
        s.next_segment();saved=bytes(s.metadata)
        s.absolute((1.3e308,1.3e308),1000,1000)
        with pytest.raises(NativeError) as error:s.next_segment()
        assert error.value.status==13 and error.value.written==0
        assert p.state.failed==1
        f=s.state
        assert (f.failure_status,f.failure_category,f.failure_reason,f.reset_required)==(13,1,1,1)
        assert f.requested_time_us==1000+variant*1000 and f.numerical_time_us==1000
        assert f.last_segment_id==1 and f.last_segment_end_us==1000
        assert b'nonfinite event probability' in f.error_message
        assert s.diagnostics.segment_generation_errors==1
        assert s.recovery_diagnostics.recoverable_observation_errors==0
        assert bytes(s.metadata)==saved
        with pytest.raises(NativeError) as error:s.next_segment()
        assert error.value.status==4 and bytes(s.state)==bytes(f)
        s.reset_diagnostics()
        assert s.diagnostics.segment_generation_errors==0 and bytes(s.state)==bytes(f)
        s.reset_segment_session()
        assert not s.state.reset_required and not p.state.failed
        assert len(s.next_segment().samples)==1

def test_exclusive_domain_released_and_reset_synchronization():
    with Model(ROOT/'lib/models/16ms') as model,Planner(model) as p,ObservationInput(p) as direct:
        with SegmentController(p) as s:
            before=bytes(p.state)
            operations=[lambda:SegmentController(p),lambda:ObservationInput(p),lambda:p.set_position((99,22)),lambda:p.update_target((320,120),0),lambda:p.clear_target(0),lambda:p.advance(1000),lambda:direct.absolute((320,120),0,0)]
            for op in operations:
                with pytest.raises(NativeError) as error:op()
                assert error.value.status==4 and bytes(p.state)==before
            assert direct.diagnostics.accepted==0
            s.absolute((320,120),0,0);s.next_segment()
            p.reset_session();assert s.state.generation_attempts==0
            p.reinitialize(seed=11);assert s.diagnostics.pending_observations==0
        p.update_target((320,120),0);assert len(p.advance(16000))==16
        with SegmentController(p):pass

@pytest.mark.parametrize('variant',[32,16,8])
@pytest.mark.parametrize('prefix',[False,True])
@pytest.mark.parametrize('seed',[7,11,11+2**32])
def test_explicit_reset_semantics(variant,prefix,seed):
    history=[(.03,-.01)]*160 if prefix else None
    with Model(ROOT/f'lib/models/{variant}ms') as model,Planner(model,seed=seed,history=history) as p,Planner(model,seed=seed,history=history) as ref:
        def compare():
            with SegmentController(p) as s,SegmentController(ref) as t:
                s.relative((320,120),0,0);t.relative((320,120),0,0)
                for _ in range(8):
                    a=s.next_segment();b=t.next_segment()
                    assert bytes(a.samples.array)==bytes(b.samples.array)
                    assert bytes(a.metadata)==bytes(b.metadata)
        compare();p.reset_session();ref.reset_session();compare()
        p.reset();ref.reset(7);compare()  # Existing Python default remains 7.
        p.reset(11+2**32);ref.reset(11);compare()

@pytest.mark.parametrize('variant',[32,16,8])
def test_relative_window_is_queryable_and_nonmutating(variant):
    with Model(ROOT/f'lib/models/{variant}ms') as model,Planner(model) as p,SegmentController(p) as s:
        s.absolute((320,120),0,0);s.next_segment();m=s.next_segment().metadata
        state=s.state;before=bytes(p.state)
        assert (state.relative_origin_earliest_us,state.relative_origin_latest_us)==(m.start_timestamp_us,m.end_timestamp_us)
        with pytest.raises(NativeError) as error:s.relative((5,6),1,1)
        assert error.value.status==9 and bytes(p.state)==before
        assert s.state.last_origin_error_receipt_us==1 and s.diagnostics.pending_observations==0
        for t in [m.start_timestamp_us,m.start_timestamp_us+1,m.end_timestamp_us,m.end_timestamp_us+1000]:
            s.relative((float(t),6),t,t)
        assert s.diagnostics.pending_observations==4
        s.next_segment();assert s.diagnostics.pending_observations==1

