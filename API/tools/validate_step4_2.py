"""Exact four-interface hardening replay, including new failure/window snapshots."""
import csv
import json
from pathlib import Path
import subprocess
import sys
import os
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'build/validation'
ENV={**os.environ,'PYTHONPATH':str(ROOT/'API/python/src'),'ABCURVES_LIBRARY':str(ROOT/'build/api/Release/abcurves.dll'),'ABCURVES_LIB_DIR':str(ROOT/'build/api/Release'),'PATH':str(ROOT/'build/api/Release')+os.pathsep+os.environ['PATH']}
def parse(path):
    def value(s):
        # Rust Display writes huge f64 values without an exponent. Preserve
        # true integer metadata exactly, but parse those coordinates as f64.
        try:return int(s) if len(s)<22 else float(s)
        except ValueError:
            try:return float(s)
            except ValueError:return s
    return [[row[0],*(value(s) for s in row[1:])] for row in csv.reader(path.open())]
def main():
    folder=OUT/'hardening_replay';folder.mkdir(parents=True,exist_ok=True);summary={}
    for c in (32,16,8):
        for prefix in (0,160,164):
            for seed in (7,11,11+2**32):
                name=f'{c}ms_p{prefix}_s{seed}';fixture=folder/(name+'.fixture')
                lines=[f'{seed} {prefix}','F','O 0 0 0 0 320 120','N','F','N','F','O 1 1 1 0 5 6','F',f'O {c*1000} {c*1000} 1 0 12 -7',f'O {c*2000} {c*2000} 1 0 14 -6','N','F','X','F','N','O 1000 1000 0 0 1.3e308 1.3e308','E','F','Q','Z','F','E','F','X','F','O 0 0 0 0 320 120','N','V 11','F','X','O 0 0 0 0 320 120','N','F']
                fixture.write_text('\n'.join(lines)+'\n');reference=None;case={}
                for lang in ('direct','cabi','python','rust'):
                    output=folder/(name+'_'+lang+'.csv')
                    cmd=[ROOT/'build/api/Release/abc_segment_replay.exe',lang] if lang in ('direct','cabi') else [sys.executable,ROOT/'API/tests/step4/replay.py'] if lang=='python' else [ROOT/'build/rust/release/examples/segment_replay.exe']
                    subprocess.run(list(map(str,[*cmd,ROOT/f'lib/models/{c}ms',fixture,ROOT/'Native/models/renderer_global_h80.bin',output])),cwd=ROOT,env=ENV,check=True)
                    rows=parse(output)
                    if reference is None:reference=rows
                    assert rows==reference,(name,lang,next(((i,a,b) for i,(a,b) in enumerate(zip(reference,rows)) if a!=b),None))
                    case[lang]={'exact':True,'rows':len(rows)}
                assert any(row[:5]==['F',13,1,1,1] for row in reference)
                assert [row for row in reference if row[0]=='E']==[['E',13,0],['E',4,0]]
                case.update(samples=sum(r[0]=='S' for r in reference),segments=sum(r[0]=='M' for r in reference),state_snapshots=sum(r[0]=='F' for r in reference))
                summary[name]=case
    (folder/'summary.json').write_text(json.dumps(summary,indent=2));print('Hardening replay:',len(summary),'fixtures,',sum(r['state_snapshots'] for r in summary.values()),'state snapshots per interface, exact')
if __name__=='__main__':main()
