"""Python binding input costs; native computation and Python object conversion included."""
import sys,time,json
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'python/src'))
from abcurves_native import *
model,stem=sys.argv[1:3]
with Model(model) as m,Planner(m) as p,ObservationInput(p,max_age_us=50000) as inp:
 def run(collect=False):
  p.reset();rows=[];sample=Sample()
  for i in range(8000):
   t=(i+1)*1000;p.generate_sample(t,sample)
   start=time.perf_counter_ns();inp.relative((12.,-7.),t,t);elapsed=(time.perf_counter_ns()-start)/1000
   if collect:rows.append(('relative_submit',elapsed))
   start=time.perf_counter_ns()
   try:inp.relative((12.,-7.),t,t)
   except NativeError as e:assert e.status==16
   elapsed=(time.perf_counter_ns()-start)/1000
   if collect:rows.append(('duplicate_reject',elapsed))
   if t>50000:
    start=time.perf_counter_ns()
    try:inp.relative((12.,-7.),0,t)
    except NativeError as e:assert e.status==15
    elapsed=(time.perf_counter_ns()-start)/1000
    if collect:rows.append(('stale_reject',elapsed))
  return rows
 run();rows=run(True)
 Path(stem+'.csv').write_text('stage,us\n'+''.join(f'{stage},{elapsed}\n' for stage,elapsed in rows))
