import csv,json,math,sys,time
from abcurves_native import *
model,outfile=sys.argv[1:];rows=[];buffer=SampleBuffer(32)
with Model(model) as model,Planner(model) as p,SegmentController(p) as ctl:
 c=model.info.commitment_ms
 def timed(stage,fn):
  start=time.perf_counter_ns();value=fn();rows.append((stage,(time.perf_counter_ns()-start)/1000.));return value
 cpu=time.process_time_ns()
 for i in range(2528):
  t=i*c*1000;target=(420.+200.*math.sin(i*.09),170.+90.*math.cos(i*.071))
  timed('observation',lambda:ctl.absolute(target,t,t))
  before=p.state;m=timed('segment',lambda:ctl.next_segment_into(buffer))
  rows[-1]=(*rows[-1],int(bool(m.fresh_planning_decision)),int(bool(m.motor_inference)))
  timed('metadata',lambda:ctl.metadata)
  if i==127:rows.clear();cpu=time.process_time_ns()
 with open(outfile+'.csv','w',newline='') as out:
  writer=csv.writer(out);writer.writerow(['stage','us','fresh','active'])
  for row in rows:writer.writerow(row if len(row)==4 else (*row,0,0))
 with open(outfile+'.json','w') as out:json.dump({'cpu_us':(time.process_time_ns()-cpu)/1000.,'allocations':'not measured; Python and DLL allocators are not instrumented'},out)
