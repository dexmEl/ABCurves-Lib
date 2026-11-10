from pathlib import Path
import ctypes as C
import concurrent.futures
import pytest
from abcurves_native import *
ROOT=Path(__file__).resolve().parents[3]
@pytest.mark.parametrize('c',[32,16,8])
def test_lifecycle(c):
 with Model(ROOT/f'lib/models/{c}ms') as m:
  assert m.info.commitment_ms==c
  assert list(m.tensor_info('motor.input.0.weight').dimensions)[:2]==[96,9]
  a=Planner(m,seed=11);b=Planner(m,seed=11)
 with a,b:
  for p in (a,b):p.update_target((320,120),0)
  batch=a.advance(256000);single=[b.generate_sample(i*1000) for i in range(1,257)]
  assert bytes(batch.array)==b''.join(bytes(s) for s in single)
  a.reset(11);a.update_target((320,120),0);assert bytes(batch.array)==bytes(a.advance(256000).array)
  with pytest.raises(NativeError):a.advance_into(500000,SampleBuffer(1))
  assert a.state.numerical_time_us==256000
  with pytest.raises(ValueError):a.advance(2**63)
  with pytest.raises(ValueError):a.reset(-1)
  a.clear_target(256000);b.clear_target(256000)
  lost=a.advance(512000);single=[b.generate_sample(i*1000) for i in range(257,513)]
  assert bytes(lost.array)==b''.join(bytes(s) for s in single)
  assert tuple(lost[-1].displacement)==(0,0)
  a.update_target((-200,40),512000);a.advance(544000)
  assert a.state.last_target_receipt_us==512000
  a.set_position((1,2))
 with pytest.raises(NativeError):a.state

def test_prefix_and_numpy_lifetime():
 with Model(ROOT/'lib/models/32ms') as m,Planner(m,history=[(.1,.02)]*160) as p:
  p.update_target((200,40),0);out=p.advance(64000);view=out.numpy();expected=view.copy();del out
 assert (view==expected).all()

def test_concurrent():
 def run(c):
  with Model(ROOT/f'lib/models/{c}ms') as m,Planner(m) as p:
   p.update_target((100,20),0);return len(p.advance(1000000))
 with concurrent.futures.ThreadPoolExecutor(3) as pool:assert list(pool.map(run,[32,16,8]))==[1000]*3

def test_renderer():
 import numpy as np
 h=np.load(ROOT/'Native/tests/public_runtime/fixtures/human_start.npz');profile=h['profile_hardware'].copy();profile[:,1]*=-1
 with Renderer(ROOT/'Native/models/renderer_global_h80.bin') as r:
  with pytest.raises(NativeError):r.reset()
  r.set_profile(profile);r.reset(7);intents=[(.1,.03)]*256;batch=r.step_batch(intents);r.reset(7)
  single=[r.step(v) for v in intents];assert bytes(batch)==b''.join(bytes(x) for x in single)
  with Model(ROOT/'lib/models/16ms') as m,Planner(m) as p:
   p.update_target((320,120),0);samples=p.advance(256000);r.reset(7);reports=r.render_samples(samples,Vec2(),float(h['radians_per_count']));assert len(reports)==256
  with pytest.raises(ValueError):r.set_profile([(0,1.5)]*256)

def test_raw_error_thread_and_buffers():
 lib=Library();raw=lib.raw;state=State();state.struct_size=C.sizeof(state)
 assert raw.abc_planner_get_state(0,C.byref(state))==2
 assert b'handle' in raw.abc_last_error()
 with Model(ROOT/'lib/models/32ms',library=lib) as m,Planner(m) as p:
  p.update_target((1,2),0);buf=SampleBuffer(2)
  assert p.advance_into(500,buf)==0
  with pytest.raises(NativeError) as e:p.advance_into(400,buf)
  assert e.value.status==9
  state=State();state.struct_size=1
  assert raw.abc_planner_get_state(p._handle,C.byref(state))==1
 assert raw.abc_planner_destroy(p._handle)==2

@pytest.mark.parametrize('kind,expected',[('version',7),('variant',8),('corrupt',6),('manifest',6)])
def test_bad_models(tmp_path,kind,expected):
 import shutil
 shutil.copytree(ROOT/'lib/models/32ms',tmp_path/'model');folder=tmp_path/'model';p=folder/'model.abcmodel';data=bytearray(p.read_bytes())
 if kind=='version':data[8:12]=(99).to_bytes(4,'little')
 elif kind=='variant':data[12:16]=(99).to_bytes(4,'little')
 elif kind=='corrupt':data[-12]^=1
 elif kind=='manifest':(folder/'manifest.json').write_text('{')
 p.write_bytes(data)
 with pytest.raises(NativeError) as error:Model(folder)
 assert error.value.status==expected

def test_size_extensions_and_registry():
 lib=Library()
 class Extended(C.Structure):_fields_=[('base',State),('tail',C.c_uint64)]
 with Model(ROOT/'lib/models/32ms',library=lib) as m,Planner(m) as p:
  out=Extended();out.base.struct_size=C.sizeof(out);out.tail=0x12345678
  assert lib.raw.abc_planner_get_state(p._handle,C.byref(out.base))==0
  assert out.tail==0x12345678
  assert lib.raw.abc_planner_destroy(m._handle)==2
  for _ in range(200):
   with Planner(m):pass
 with pytest.raises(ValueError):Model('x\0bad',library=lib)
