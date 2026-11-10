"""Replay device-independent Step 3 fixtures across all native interfaces."""
from pathlib import Path
import os,sys,json,subprocess,hashlib
import numpy as np
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'build/validation/observations'
ENV={**os.environ,'PATH':str(ROOT/'build/api/Release')+os.pathsep+os.environ['PATH']}
def fixtures():
 for c in (32,16,8):
  for prefix,policy in ((0,0),(160,1),(164,2)):
   for fps in (60,120,144,240):
    for seed in (7,11):
     name=f'{c}ms_{fps}fps_p{prefix}_s{seed}'
     rows=[f'{seed} 2000 {prefix} {policy} 50000 100 50']
     rows += [f'{(j%13-6)*.03125} {(j%7-3)*.015625}' for j in range(prefix)]
     events=[(0,0,0,0,0,320.,120.)]
     for j in range(1,int(2*fps)):
      at=round(j*1000/fps)*1000
      if 400000<at<650000 or 700000<at<900000:continue
      capture=max(0,at-(j%4)*3000);receipt=at+(500 if j%3==0 else 0)
      event=(at,capture,receipt,j%2,0,100+(j%11)*4.,-30+(j%17)*2.)
      events.append(event)
      if j%11==0:events.append(event) # retransmission
      if j%13==0:events.append((at,max(0,at-100000),receipt,1,0,2.,-3.)) # stale
      if j%17==0:events.append((at,max(0,capture-1000),receipt,1,0,-1.,3.)) # capture reorder
      if j%19==0:events.append((at,capture,receipt,1,0,4.,-3.)) # same-capture revision
     events.extend([(700000,700000,700000,0,1,0.,0.),(850000,850000,850000,0,2,0.,0.),(900000,900000,900000,1,0,200.,-80.),(1999000,2000000,1999000,1,0,0.,0.),(2000000,0,2**63-1,1,0,1.,1.)])
     events.sort(key=lambda e:e[0])
     rows += [' '.join(map(str,event)) for event in events]
     yield c,name,'\n'.join(rows)+'\n'
def main():
 OUT.mkdir(parents=True,exist_ok=True);report={}
 for c,name,text in fixtures():
  folder=OUT/name;folder.mkdir(exist_ok=True);fixture=folder/'input.fixture';fixture.write_text(text)
  model=ROOT/f'lib/models/{c}ms';reference=None;case={}
  for interface in ('direct','cabi','python','rust'):
   stem=folder/interface
   command=([str(ROOT/'build/api/Release/abc_observation_replay.exe'),interface,str(model),str(fixture),str(stem),'1'] if interface in ('direct','cabi') else [sys.executable,str(ROOT/'API/tests/step3/replay.py'),str(model),str(fixture),str(stem)] if interface=='python' else [str(ROOT/'build/rust/release/examples/observation_replay.exe'),str(model),str(fixture),str(stem)])
   result=subprocess.run(command,env=ENV,cwd=ROOT,capture_output=True,text=True)
   if result.returncode:raise RuntimeError((command,result.stdout,result.stderr))
   values=np.loadtxt(str(stem)+'.csv',delimiter=',')
   if reference is None:reference=values
   diff=np.argwhere(values!=reference)
   assert not len(diff),(name,interface,diff[:3],values[diff[0,0]],reference[diff[0,0]])
   case[interface]={'exact':True,'rows':len(values),'sha256':hashlib.sha256(Path(str(stem)+'.csv').read_bytes()).hexdigest()}
  observations=reference[reference[:,0]==0];samples=reference[reference[:,0]==1]
  case['samples']=len(samples);case['observations']=len(observations);case['accepted']=int(np.sum(observations[:,2]==0));case['rejected']=int(np.sum(observations[:,2]!=0))
  case['fixture_sha256']=hashlib.sha256(fixture.read_bytes()).hexdigest();report[name]=case
  (OUT/'summary.json').write_text(json.dumps(report,indent=2));print(name,'exact',flush=True)
 print('Validated',len(report),'fixtures',sum(c['samples'] for c in report.values()),'samples per interface',flush=True)
if __name__=='__main__':main()
