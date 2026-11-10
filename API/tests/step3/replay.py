"""Independent observation replay, shared with native and Rust runners."""
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'python/src'))
from abcurves_native import *

def replay(model,fixture,output):
 lines=Path(fixture).read_text().splitlines();seed,duration,prefix,ordering,age,x,y=lines[0].split();seed,duration,prefix,ordering,age=map(int,(seed,duration,prefix,ordering,age));initial=(float(x),float(y));history=[tuple(map(float,line.split())) for line in lines[1:1+prefix]]
 if prefix==164:
  start=prepare_history(history,current_position=initial);initial=start.initial_position;history=start.history
 events=[line.split() for line in lines[1+prefix:]];cursor=0
 with Model(model) as m,Planner(m,seed=seed,initial_position=initial,history=history or None) as p,ObservationInput(p,ordering=ObservationOrdering(ordering),max_age_us=age) as inp,open(str(output)+'.csv','w') as out:
  for tick in range(duration+1):
   t=tick*1000
   while cursor<len(events) and int(events[cursor][0])==t:
    at,capture,receipt,kind,availability,ex,ey=events[cursor];cursor+=1;code=0
    try:inp.submit(TargetObservation((float(ex),float(ey)),int(capture),int(receipt),bool(int(kind)),TargetAvailability(int(availability))))
    except NativeError as e:code=e.status
    d=inp.diagnostics
    row=[0,t,code,d.last_capture_us,d.last_receipt_us,d.last_age_us,d.accepted,d.rejected,d.duplicates,d.out_of_order,d.stale,d.availability,d.last_rejection,d.resolved_target.x,d.resolved_target.y,d.reference_position.x,d.reference_position.y]
    out.write(','.join(map(str,row))+'\n')
   if tick<duration:
    s=p.generate_sample(t+1000);d=p.state;out.write(','.join(map(str,[1,s.timestamp_us,d.mode,d.selected_head,d.selected_brake_head,d.decision_count,d.motor_evaluations,d.pending_samples,d.numerical_time_us,d.last_decision_us,d.last_target_receipt_us,0,0,s.position.x,s.position.y,s.displacement.x,s.displacement.y]))+'\n')
  assert cursor==len(events)
if __name__=='__main__':replay(*sys.argv[1:4])
