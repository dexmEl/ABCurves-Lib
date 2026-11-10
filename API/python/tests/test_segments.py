import ctypes as C
from pathlib import Path
import pytest
from abcurves_native import *
ROOT=Path(__file__).resolve().parents[3]
@pytest.mark.parametrize('c',[32,16,8])
@pytest.mark.parametrize('prefix',[0,160,164])
def test_segments_continuity_and_renderer(c,prefix):
    raw=[(i/1000.,-i/2000.) for i in range(164)]
    start=prepare_history(raw,current_position=(100.,50.))
    opts={'initial_position':start.initial_position if prefix==164 else (100.,50.),'history':start.history if prefix==164 else raw[:prefix] if prefix else None}
    with Model(ROOT/f'lib/models/{c}ms') as model, Planner(model,**opts) as p, Planner(model,**opts) as legacy, SegmentController(p) as ctl, Renderer(ROOT/'Native/models/renderer_global_h80.bin') as renderer, Renderer(ROOT/'Native/models/renderer_global_h80.bin') as old:
        profile=[(i%9-4,i%7-3) for i in range(256)]
        for r in (renderer,old):r.set_profile(profile);r.reset(7)
        ctl.absolute((420.,170.),0,0);legacy.update_target((420.,170.),0)
        prior=Vec2(*opts['initial_position'])
        for i in range(220):
            seg=ctl.next_segment();m=seg.metadata;ref=legacy.advance(m.end_timestamp_us)
            assert len(seg.samples)==c
            assert bytes(seg.samples.array)==bytes(ref.array)
            reports=renderer.render_segment(seg,COMMON_RADIANS_PER_COUNT)
            expected=old.render_samples(ref,prior,COMMON_RADIANS_PER_COUNT)
            assert bytes(reports)==bytes(expected)
            if i%3==0:
                t=m.end_timestamp_us;target=(430.+i%10,170.)
                ctl.absolute(target,t,t);legacy.update_target(target,t)
        assert ctl.diagnostics.missed_generation_deadlines==0

def test_async_order_gaps_timeouts_and_parent():
    with Model(ROOT/'lib/models/16ms') as m, Planner(m) as p, SegmentController(p,inactivity_timeout_us=20000) as s:
        s.relative((120.,-7.),0,0);a=s.next_segment();saved=bytes(a.samples.array)
        s.relative((12.,-7.),5000,7000);s.absolute((320.,120.),12000,14000)
        assert s.diagnostics.pending_observations==2
        b=s.next_segment();assert bytes(a.samples.array)==saved
        d=s.diagnostics;assert (d.last_apply_us,d.last_observation_receipt_us)==(16000,14000)
        assert (b.metadata.start_timestamp_us,b.metadata.initial_position.x)==(16000,a.metadata.final_position.x)
        s.next_segment();s.next_segment();assert s.diagnostics.target_timeout_events==1
        ptime=p.state.numerical_time_us;s.reset_diagnostics();assert p.state.numerical_time_us==ptime
        s.reset_segment_session();assert s.next_segment().metadata.sample_count==1
        s.absolute((120.,-7.),0,0)
        with pytest.raises(NativeError) as e:s.absolute((120.,-7.),0,0)
        assert e.value.status==16
        p.close()
        with pytest.raises(NativeError):s.next_segment()

@pytest.mark.parametrize('bad,status',[
    (TargetObservation((1.,2.),10,9),9),
    (TargetObservation((float('nan'),2.),0,0),1),
    (TargetObservation((1.,2.),0,300000),15),
])
def test_rejections_transactional(bad,status):
    with Model(ROOT/'lib/models/16ms') as m,Planner(m) as p,SegmentController(p) as s:
        before=bytes(p.state)
        with pytest.raises(NativeError) as e:s.submit(bad)
        assert e.value.status==status and before==bytes(p.state)

def test_future_queue_and_overflow_metadata():
    with Model(ROOT/'lib/models/8ms') as m,Planner(m) as p,SegmentController(p,observation_capacity=2) as s:
        s.absolute((1.,2.),2**63-1,2**63-1)
        assert s.next_segment().metadata.sample_count==1
        s.absolute((3.,2.),2**63-1,2**63-1)
        with pytest.raises(NativeError) as e:s.absolute((4.,2.),2**63-1,2**63-1)
        assert e.value.status==14
        s.reset_segment_session();s.relative((12.,-7.),0,0);s.next_segment()
        with pytest.raises(NativeError) as e:s.next_segment_into(SampleBuffer(1))
        assert e.value.status==10
        assert s.metadata.segment_id==1

@pytest.mark.parametrize('receipt,apply',[(15999,16000),(16000,16000),(16001,32000)])
def test_exact_boundaries_and_future_eligibility(receipt,apply):
    with Model(ROOT/'lib/models/16ms') as m,Planner(m) as p,SegmentController(p) as s:
        s.absolute((320.,120.),0,0);s.next_segment()
        s.absolute((420.,170.),receipt,receipt)
        s.next_segment()
        if receipt>16000:
            assert s.diagnostics.pending_observations==1
            assert s.diagnostics.last_observation_age_us==16000
            s.next_segment()
        assert s.diagnostics.last_apply_us==apply
        assert s.diagnostics.last_observation_receipt_us==receipt
        s.absolute((421.,171.),receipt,receipt)
        assert s.diagnostics.pending_observations==1

@pytest.mark.parametrize('ordering',[ObservationOrdering.STRICT,ObservationOrdering.LATEST,ObservationOrdering.CALLER])
def test_controller_capture_policies(ordering):
    with Model(ROOT/'lib/models/8ms') as m,Planner(m) as p,SegmentController(p,ordering=ordering) as s:
        s.absolute((320.,120.),5,10)
        if ordering==ObservationOrdering.STRICT:
            with pytest.raises(NativeError) as e:s.absolute((321.,121.),5,10)
            assert e.value.status==16
        else:s.absolute((321.,121.),5,10)
        if ordering==ObservationOrdering.CALLER:s.absolute((322.,122.),4,10)
        else:
            with pytest.raises(NativeError) as e:s.absolute((322.,122.),4,10)
            assert e.value.status==17

def test_independent_controllers_concurrently_and_read_only_queries():
    from concurrent.futures import ThreadPoolExecutor
    def run(c):
        with Model(ROOT/f'lib/models/{c}ms') as m,Planner(m) as p,SegmentController(p) as ctl:
            ctl.absolute((420.,170.),0,0);result=[]
            for i in range(48):
                before=bytes(p.state)
                for _ in range(4):ctl.diagnostics
                assert bytes(p.state)==before
                s=ctl.next_segment();result.append(bytes(s.samples.array))
            return result
    expected=[run(c) for c in (32,16,8)]
    with ThreadPoolExecutor(max_workers=3) as pool:assert list(pool.map(run,(32,16,8)))==expected

def test_1000hz_observations_preserve_intermediate_history():
    with Model(ROOT/'lib/models/16ms') as model,Planner(model) as p,Planner(model) as legacy,SegmentController(p) as s,ObservationInput(legacy) as direct:
        s.absolute((420.,170.),0,0);direct.absolute((420.,170.),0,0)
        first=s.next_segment();saved=bytes(first.samples.array)
        for i in range(1,17):
            point=legacy.generate_sample(i*1000)
            assert bytes(point)==bytes(first.samples.array[i-1])
            target=(420.+i,170.-i)
            direct.absolute(target,i*1000,i*1000);s.absolute(target,i*1000,i*1000)
        assert s.diagnostics.pending_observations==16
        second=s.next_segment();expected=legacy.advance(32000)
        assert bytes(second.samples.array)==bytes(expected.array)
        assert bytes(first.samples.array)==saved
        assert s.diagnostics.last_observation_receipt_us==16000
