from pathlib import Path
import ctypes as C
import sys
import pytest
from abcurves_native import *
ROOT=Path(__file__).resolve().parents[3]

@pytest.mark.parametrize('variant',[32,16,8])
@pytest.mark.parametrize('prefix',[0,160,164])
def test_independent_relative(variant,prefix):
 raw=[(i/1000.,-i/2000.) for i in range(164)];initial=(100.,50.);history=None
 if prefix==160:history=raw[:160]
 if prefix==164:
  start=prepare_history(raw,current_position=initial);initial=start.initial_position;history=start.history
 with Model(ROOT/f'lib/models/{variant}ms') as model,Planner(model,initial_position=initial,history=history,seed=11) as a,Planner(model,initial_position=initial,history=history,seed=11) as b,ObservationInput(a,max_age_us=30000) as inp:
  target=inp.relative((12,-7),0,0);assert tuple(target)==(initial.x+12,initial.y-7) if isinstance(initial,Vec2) else tuple(target)==(112,43)
  b.update_target(target,0)
  for t in range(1000,201000,1000):
   assert bytes(a.generate_sample(t))==bytes(b.generate_sample(t))
   if t%17000==0:
    # External movement is only local application state; no telemetry API exists.
    physical_mouse=(99999+t,-9999)
    before=a.state;target=inp.relative((10,-4),t-12000,t+500);b.update_target(target,t+500)
    assert tuple(target)==(a.state.position.x+10,a.state.position.y-4)
    assert a.state.motor_evaluations==before.motor_evaluations
    with pytest.raises(NativeError) as e:inp.relative((10,-4),t-12000,t+500)
    assert e.value.status==16
    if t>30000:
     with pytest.raises(NativeError) as e:inp.relative((10,-4),0,t+500)
     assert e.value.status==15
  a.reinitialize(seed=11,initial_position=initial,history=history);assert inp.diagnostics.accepted==0
  inp.absolute((320,120),0,0);b.reinitialize(seed=11,initial_position=initial,history=history);b.update_target((320,120),0)
  assert bytes(a.advance(128000).array)==bytes(b.advance(128000).array)

@pytest.mark.parametrize('ordering',[ObservationOrdering.STRICT,ObservationOrdering.LATEST,ObservationOrdering.CALLER])
def test_ordering_and_availability(ordering):
 with Model(ROOT/'lib/models/16ms') as model,Planner(model) as p,ObservationInput(p,ordering=ordering,max_age_us=-1) as inp:
  p.advance(10000);inp.relative((1,2),5000,10000)
  if ordering==ObservationOrdering.STRICT:
   with pytest.raises(NativeError) as e:inp.relative((3,4),5000,10000)
   assert e.value.status==16
  else:inp.relative((3,4),5000,10000)
  if ordering==ObservationOrdering.CALLER:inp.relative((5,6),4000,10000)
  else:
   with pytest.raises(NativeError) as e:inp.relative((5,6),4000,10000)
   assert e.value.status==17
  p.advance(11000);inp.unavailable(11000,11000);assert inp.diagnostics.availability==1
  p.advance(12000);inp.unavailable(12000,12000,invalidate=True);assert inp.diagnostics.availability==2
  p.advance(13000);inp.relative((100,20),13000,13000);assert inp.diagnostics.availability==0
  p.reset();assert inp.diagnostics.accepted==0


def test_timestamp_boundaries_and_rejection():
 with Model(ROOT/'lib/models/32ms') as model,Planner(model) as p,ObservationInput(p) as inp:
  with pytest.raises(NativeError) as e:inp.relative((1,2),0,1000)
  assert e.value.status==9 and p.state.numerical_time_us==0
  with pytest.raises(NativeError):inp.relative((1,2),1,0)
  with pytest.raises(ValueError):inp.relative((1,2),-1,0)
  with pytest.raises(ValueError):inp.relative((1,2),0,2**63)
  with pytest.raises(NativeError):inp.relative((float('nan'),2),0,0)
  p.set_position((1e308,0))
  with pytest.raises(NativeError):inp.relative((1e308,2),0,0)
  assert inp.diagnostics.accepted==0
  p.set_position((0,0));p.advance(500);inp.relative((1,2),0,500)
  with pytest.raises(NativeError) as e:inp.relative((2,3),0,499)
  assert e.value.status==9
  p.reset();p.advance(300000)
  with pytest.raises(NativeError) as e:inp.absolute((1,2),0,300000)
  assert e.value.status==15
  # No age limit: large delays accepted, based on receipt virtual position only.
 with Model(ROOT/'lib/models/8ms') as model,Planner(model) as p,ObservationInput(p,max_age_us=-1) as inp:
  p.advance(1000000);assert tuple(inp.relative((12,-7),0,1000000))==(12,-7)
  with pytest.raises(NativeError):inp.relative((1,2),0,2**63-1)


def test_w5_original_python_exact():
 import numpy as np
 sys.path.insert(0,str(ROOT/'Native'))
 from abcurves.continuous import prepare_history as reference
 for origin in ((0,0),(100,50),(1e12,-1e12)):
  raw=np.random.default_rng(17).normal(size=(164,2))
  expected=reference(raw,current_xy=origin);actual=prepare_history(raw,current_position=origin)
  assert np.array_equal(np.array([tuple(v) for v in actual.history]),expected.history)
  assert np.array_equal(tuple(actual.initial_position),expected.initial_xy)
 with pytest.raises(ValueError):prepare_history([(0,0)]*163,current_position=(0,0))
 with pytest.raises(NativeError):prepare_history([(float('inf'),0)]*164,current_position=(0,0))


def test_parent_lifetime_and_raw_abi_errors():
 lib=Library()
 assert lib.version.abi_minor>=1
 p=None
 with Model(ROOT/'lib/models/32ms',library=lib) as m:
  p=Planner(m);inp=ObservationInput(p)
 p.close()
 with pytest.raises(NativeError):inp.relative((1,2),0,0)
 inp.close()
 assert not any('execut' in name or 'reconcil' in name or 'acknowledge' in name for name in dir(lib.raw))

@pytest.mark.parametrize('variant',[32,16,8])
@pytest.mark.parametrize('batch',[1,8,16,32,64,128,256])
def test_relative_batch_sizes(variant,batch):
 with Model(ROOT/f'lib/models/{variant}ms') as m,Planner(m) as a,Planner(m) as b,ObservationInput(a) as ia,ObservationInput(b) as ib:
  for start in range(0,1024000,256000):
   ia.relative((320,-80),start,start);ib.relative((320,-80),start,start)
   actual=[]
   for endpoint in range(start+batch*1000,start+256001,batch*1000):actual.extend(bytes(s) for s in a.advance(endpoint))
   expected=[bytes(b.generate_sample(t)) for t in range(start+1000,start+256001,1000)]
   assert actual==expected
   assert a.state.selected_head==b.state.selected_head and a.state.mode==b.state.mode

def test_observation_instances_on_threads():
 import concurrent.futures
 def run(c):
  with Model(ROOT/f'lib/models/{c}ms') as m,Planner(m) as p,ObservationInput(p) as i:
   result=[]
   for t in range(0,256000,32000):
    i.relative((120,-20),t,t);result.append(bytes(p.advance(t+32000).array))
   return b''.join(result)
 expected=[run(c) for c in (32,16,8)]
 with concurrent.futures.ThreadPoolExecutor(3) as pool:assert list(pool.map(run,(32,16,8)))==expected

def test_duplicate_receipts_and_caller_window():
 with Model(ROOT/'lib/models/32ms') as model,Planner(model) as p,ObservationInput(p,ordering=ObservationOrdering.LATEST,max_age_us=-1) as inp:
  p.advance(500);inp.relative((1,2),0,500)
  with pytest.raises(NativeError) as e:inp.relative((1,2),0,800)
  assert e.value.status==16
  inp.relative((3,4),0,800) # revised observation at same capture
  with pytest.raises(NativeError) as e:inp.relative((5,6),0,700)
  assert e.value.status==17
 with Model(ROOT/'lib/models/8ms') as model,Planner(model) as p,ObservationInput(p,ordering=ObservationOrdering.CALLER,max_age_us=-1) as inp:
  for tick in range(65):
   p.advance(tick*1000);inp.absolute((1,2),tick*1000,tick*1000)
  # Fingerprint zero has expired. Caller policy deliberately accepts this replay.
  inp.absolute((1,2),0,64000)
  assert inp.diagnostics.accepted==66
