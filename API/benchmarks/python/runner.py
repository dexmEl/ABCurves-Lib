"""Same fixture and timing boundaries as cpp/runner.cpp; no reference planner."""
import ctypes as C
import json,sys,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'API/python/src'))
from abcurves_native import *
from abcurves_native import State

def main():
 interface,model_path,fixture,stem,renderer_path,renderer_fixture,batch=sys.argv[1:];batch=int(batch);rawmode=interface=='python_raw'
 lines=Path(fixture).read_text().splitlines();duration,seed,x,y,nprefix,nevents=lines[0].split();duration=int(duration);seed=int(seed);nprefix=int(nprefix);prefix=[tuple(map(float,row.split())) for row in lines[1:1+nprefix]];events=[tuple(map(float,row.split())) for row in lines[1+nprefix:]]
 rlines=Path(renderer_fixture).read_text().splitlines();_,scale,_=rlines[0].split();scale=float(scale);profile=[tuple(map(int,row.split())) for row in rlines[1:257]]
 lib=Library();start=time.perf_counter_ns();m=Model(model_path,library=lib);load=(time.perf_counter_ns()-start)/1000;start=time.perf_counter_ns();p=Planner(m,seed=seed,initial_position=(float(x),float(y)),history=prefix or None);initialize=(time.perf_counter_ns()-start)/1000
 renderer=Renderer(renderer_path,library=lib);renderer.set_profile(profile)
 def reset():
  p.reset(seed);renderer.reset(seed)
  for t,x,y in events:p.update_target((x,y),int(t))
 reset();p.advance(duration*1000);reset()
 samples=SampleBuffer(duration);buffer=SampleBuffer(batch);rows=[];timings=[];previous=Vec2(float(x),float(y));reports=(Report*batch)();state=State();state.struct_size=C.sizeof(state);written=C.c_uint64()
 def getstate():
  if rawmode:lib.check(lib.raw.abc_planner_get_state(p._handle,C.byref(state)));return state
  return p.state
 for i in range(0,duration,batch):
  n=min(batch,duration-i);before=getstate();decisions,motors=before.decision_count,before.motor_evaluations
  start=time.perf_counter_ns()
  if rawmode:
   status=lib.raw.abc_planner_advance(p._handle,(i+n)*1000,buffer.array,buffer.capacity,C.byref(written));lib.check(status,written.value);buffer.written=written.value
  else:p.advance_into((i+n)*1000,buffer)
  elapsed=(time.perf_counter_ns()-start)/1000;after=getstate()
  start=time.perf_counter_ns()
  if rawmode:lib.check(lib.raw.abc_renderer_render_samples(renderer._handle,buffer.array,n,C.byref(previous),scale*COMMON_RADIANS_PER_COUNT,reports,len(reports),C.byref(written)),written.value)
  else:renderer.render_samples(buffer,previous,scale*COMMON_RADIANS_PER_COUNT,reports)
  render=(time.perf_counter_ns()-start)/1000
  for j in range(n):
   s=buffer[j];samples.array[i+j]=s;rows.append((s.timestamp_us,s.position.x,s.position.y,s.displacement.x,s.displacement.y,after.mode,after.selected_head,after.decision_count,after.motor_evaluations,after.last_decision_us,after.last_target_receipt_us,after.pending_samples,reports[j].dx,reports[j].dy))
  timings.append(((i+n)*1000,n,elapsed,render,after.decision_count-decisions,after.motor_evaluations-motors))
 reset();start=time.perf_counter_ns();p.advance_into(duration*1000,samples);trace=(time.perf_counter_ns()-start)/1000
 def csv(suffix,header,values):
  with Path(stem+suffix).open('w') as f:
   f.write(header+'\n')
   for row in values:f.write(','.join(map(str,row))+'\n')
 csv('.samples.csv','t,x,y,dx,dy,mode,head,decisions,motors,decision_us,receipt_us,pending,rdx,rdy',rows)
 csv('.timings.csv','t,n,us,renderer_us,decisions,motors',timings)
 overhead=[];total=0
 for _ in range(50):
  start=time.perf_counter_ns()
  for _ in range(1000):total+=getstate().numerical_time_us
  overhead.append((1000,(time.perf_counter_ns()-start)/1000))
 csv('.overhead.csv','calls,us',overhead)
 try:
  import psutil
  memory=getattr(psutil.Process().memory_info(),'private',0)
 except ImportError:memory=None
 Path(stem+'.json').write_text(json.dumps(dict(load_us=load,initialize_us=initialize,batch_trace_us=trace,private_bytes=memory,model_sha256=m.info.sha256.decode(),state_checksum=total)))
 renderer.close();p.close();m.close()
if __name__=='__main__':main()
