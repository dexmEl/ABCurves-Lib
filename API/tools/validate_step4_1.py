"""Four-interface recovery replay, preserving Step 4 historical artifacts."""
import csv,json,os,subprocess,sys,hashlib,re
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'build/validation/recovery'
ENV={**os.environ,'PYTHONPATH':str(ROOT/'API/python/src'),'ABCURVES_LIBRARY':str(ROOT/'build/api/Release/abcurves.dll'),'ABCURVES_LIB_DIR':str(ROOT/'build/api/Release'),'PATH':str(ROOT/'build/api/Release')+os.pathsep+os.environ['PATH']}

def parse(p):
    def number(s):return int(s) if re.fullmatch(r'-?\d+',s) and len(s)<22 else float(s)
    return [[r[0],*(number(x) for x in r[1:])] for r in csv.reader(p.open())]

def main():
    OUT.mkdir(parents=True,exist_ok=True);summary={}
    for c in (32,16,8):
        for seed in (7,11):
            for huge in (False,True):
                name=f'{c}ms_s{seed}_'+('overflow' if huge else 'active_clear')
                value='1.3e308 1.3e308' if huge else '420 170'
                lines=[f'{seed} 0 '+('1.3e308 1.3e308' if huge else '100 50')]
                if huge:
                    lines+=['O 1000 1000 0 0 '+value,'O 2000 2000 1 0 '+value,'O 3000 3000 0 0 '+value,'N','N','N','Q']
                    # Generate before then after a full queue of 64 future bad
                    # inputs, keeping exact diagnostics and original ordering.
                    t=1000+c*2000
                    for _ in range(4):
                        lines += [f'O {t+i} {t+i} 1 0 {value}' for i in range(1,65)]
                        lines+=['Q','N','N','Q'];t+=2*c*1000
                else:
                    lines+=['O 0 0 0 0 '+value,'N'];t=c*1000
                for j in range(80):
                    lines += [f'O {t+1000000+i} {t+1000000+i} 0 0 {value}' for i in range(64)] if j%10==0 else [f'O {t+1000000} {t+1000000} 0 0 {value}']
                    lines+=['Q','K','K','Q']
                    if j in (35,40):lines += [f'O {t} {t} 0 {1 if j==35 else 0} {value}']
                    lines+=['N'];t+=1000 if 35<=j<40 else c*1000
                lines+=['Z','Q','X','Q','K','Q','N','Q']
                fixture=OUT/(name+'.fixture');fixture.write_text('\n'.join(lines)+'\n')
                reference=None;result={}
                for lang in ('direct','cabi','python','rust'):
                    output=OUT/(name+'_'+lang+'.csv')
                    cmd=([str(ROOT/'build/api/Release/abc_segment_replay.exe'),lang] if lang in ('direct','cabi') else [sys.executable,str(ROOT/'API/tests/step4/replay.py')] if lang=='python' else [str(ROOT/'build/rust/release/examples/segment_replay.exe')])
                    subprocess.run([*cmd,str(ROOT/f'lib/models/{c}ms'),str(fixture),str(ROOT/'Native/models/renderer_global_h80.bin'),str(output)],check=True,env=ENV,cwd=ROOT)
                    rows=parse(output)
                    if reference is None:reference=rows
                    assert rows==reference,(name,lang,next(((i,a,b) for i,(a,b) in enumerate(zip(reference,rows)) if a!=b),None))
                    result[lang]={'exact':True,'sha256':hashlib.sha256(output.read_bytes()).hexdigest()}
                result['samples']=sum(r[0]=='S' for r in reference);result['segments']=sum(r[0]=='M' for r in reference)
                result['recovery_snapshots']=sum(r[0]=='Q' for r in reference)
                result['rejected_deferred']=max(r[3] for r in reference if r[0]=='Q')
                summary[name]=result;print(name,'exact',flush=True)
    (OUT/'summary.json').write_text(json.dumps(summary,indent=2))
    print('Recovery fixtures',len(summary),'samples',sum(r['samples'] for r in summary.values()),'segments',sum(r['segments'] for r in summary.values()),flush=True)

if __name__=='__main__':main()
