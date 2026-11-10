"""Deterministic segment fixtures; only input generators use nominal FPS."""
import csv,hashlib,json,os,re,subprocess,sys
from pathlib import Path
from abcurves_native import *
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'build/validation/segments'
ENV={**os.environ,'PYTHONPATH':str(ROOT/'API/python/src'),'CARGO_TARGET_DIR':str(ROOT/'build/rust'),'ABCURVES_LIB_DIR':str(ROOT/'build/api/Release'),'PATH':str(ROOT/'build/api/Release')+os.pathsep+os.environ['PATH']}

def fixtures():
 folder=ROOT/'API/tests/step4/fixtures';folder.mkdir(parents=True,exist_ok=True)
 for c in (32,16,8):
  with Model(ROOT/f'lib/models/{c}ms') as model:
   for rate in (60,120,144,240,360,777):
    for prefix in (0,160,164):
     for seed in (7,11):
      name=f'{c}ms_{rate}hz_p{prefix}_s{seed}';lines=[f'{seed} {prefix}']
      raw=[(i/1000.,-i/2000.) for i in range(164)];h=prepare_history(raw,current_position=(100.,50.))
      opts={'seed':seed,'initial_position':h.initial_position if prefix==164 else (100.,50.),'history':h.history if prefix==164 else raw[:prefix] if prefix else None}
      with Planner(model,**opts) as p,SegmentController(p,inactivity_timeout_us=90000) as s:
       def submit(o):
        lines.append(f'O {o.capture_time_us} {o.receipt_time_us} {int(o.relative)} {int(o.availability)} {o.value[0]:.17g} {o.value[1]:.17g}')
        try:s.submit(o)
        except NativeError:pass
       submit(TargetObservation((420.,170.),0,0));index=1;receipt=7000 if rate==777 else 1000000//rate
       for segment in range(320):
        lines.append('N');m=s.next_segment().metadata
        while receipt<=m.end_timestamp_us:
         o=TargetObservation((12.,-7.) if index%3==0 else (420.+index%29*8.,170.+index%11),max(0,receipt-3500),receipt,relative=index%3==0,availability=TargetAvailability.UNAVAILABLE if index%97>=90 else TargetAvailability.AVAILABLE)
         if index%211<180:
          submit(o)
          if index%29==0:submit(o)
          if index%43==0:submit(TargetObservation((1.,2.),max(0,receipt-3501),receipt-1))
         index+=1;receipt+=([7000,7000,7000,1500,1,30501,500,11000][index%8] if rate==777 else 1000000//rate)
       # Exercise counter-only reset, chronological revision, capture/receipt
       # validation, explicit loss/reacquisition and an actual future receipt.
       lines+=['Z','X','O 10 9 0 0 1 2','O 0 300000 0 0 1 2','O 0 0 0 0 420 170','N','O 7000 7000 0 0 430 170','O 14000 14000 0 0 440 170','N','O 21000 21000 0 1 0 0','O 28000 28000 0 0 450 170','N','X','O 100000 100000 0 0 420 170','N','O 9223372036854775807 9223372036854775807 0 0 1 2','Z','X','N']
      path=folder/(name+'.fixture');path.write_text('\n'.join(lines)+'\n');yield c,name,path

def load(path):
 def number(s):return int(s) if re.fullmatch(r'-?\d+',s) else float(s)
 return [[row[0],*(number(x) for x in row[1:])] for row in csv.reader(path.open())]

def main():
 OUT.mkdir(parents=True,exist_ok=True);report={}
 for c,name,fixture in fixtures():
  model=ROOT/f'lib/models/{c}ms';renderer=ROOT/'Native/models/renderer_global_h80.bin';reference=None;case={}
  for lang in ('direct','cabi','python','rust'):
   output=OUT/name/(lang+'.csv');output.parent.mkdir(parents=True,exist_ok=True)
   cmd=([str(ROOT/'build/api/Release/abc_segment_replay.exe'),lang] if lang in ('direct','cabi') else [sys.executable,str(ROOT/'API/tests/step4/replay.py')] if lang=='python' else [str(ROOT/'build/rust/release/examples/segment_replay.exe')])
   subprocess.run([*cmd,str(model),str(fixture),str(renderer),str(output)],check=True,env=ENV,cwd=ROOT)
   rows=load(output)
   if reference is None:reference=rows
   if rows!=reference:
    first=next((i,a,b) for i,(a,b) in enumerate(zip(reference,rows)) if a!=b)
    raise AssertionError((name,lang,first))
   case[lang]={'exact':True,'rows':len(rows),'sha256':hashlib.sha256(output.read_bytes()).hexdigest()}
  case['samples']=sum(r[0]=='S' for r in reference);case['segments']=sum(r[0]=='M' for r in reference);case['observations']=sum(r[0]=='O' for r in reference);case['fixture_sha256']=hashlib.sha256(fixture.read_bytes()).hexdigest();report[name]=case
  (OUT/'summary.json').write_text(json.dumps(report,indent=2));print(name,'exact',flush=True)
 print('Validated',len(report),'fixtures',sum(c['samples'] for c in report.values()),'samples per interface',flush=True)
if __name__=='__main__':main()
