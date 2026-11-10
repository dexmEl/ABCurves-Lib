"""Replay retained Step 1.5 fixtures across direct C++, C ABI, Python and Rust."""
import os,sys,json,subprocess,argparse,time
from pathlib import Path
import numpy as np
ROOT=Path(__file__).resolve().parents[2]
BASE=ROOT/'tests/baselines';RESULT=ROOT/'build/validation'
ENV={**os.environ,'PATH':str(ROOT/'build/api/Release')+os.pathsep+os.environ['PATH'],'CARGO_TARGET_DIR':str(ROOT/'build/rust')}
RENDERER=BASE/'renderer/32ms_120fps_p0_s7_mixed_irregular_same_input.fixture'

def run(interface,model,fixture,stem,batch):
 stem.parent.mkdir(parents=True,exist_ok=True)
 command=([str(ROOT/'build/api/Release/abc_api_runner.exe')] if interface in ('direct','cabi') else [sys.executable,str(ROOT/'API/benchmarks/python/runner.py')] if interface.startswith('python') else [str(ROOT/'build/rust/release/examples/replay.exe')])
 subprocess.run(command+[interface,str(model),str(fixture),str(stem),str(ROOT/'Native/models/renderer_global_h80.bin'),str(RENDERER),str(batch)],check=True,env=ENV,cwd=ROOT)

def stats(v):
 a=np.asarray(v);return dict(count=len(a),mean=float(a.mean()),median=float(np.median(a)),p95=float(np.percentile(a,95)),p99=float(np.percentile(a,99)),maximum=float(a.max()),total=float(a.sum())) if len(a) else None

def validate(full=False):
 fixtures=sorted((BASE/'legacy').glob('*.fixture'))
 if not full:fixtures=[p for p in fixtures if '_120fps_' in p.name and ('_s7_mixed_irregular' in p.name)]
 report={}
 for index,fixture in enumerate(fixtures):
  name=fixture.stem;variant=name.split('_')[0];model=ROOT/'lib/models'/variant;folder=RESULT/'differential'/name
  reference=None;case={}
  for interface in ('direct','cabi','python','rust'):
   stem=folder/interface;run(interface,model,fixture,stem,1);data=np.loadtxt(str(stem)+'.samples.csv',delimiter=',',skiprows=1);meta=json.loads(Path(str(stem)+'.json').read_text())
   if reference is None:reference=data;digest=meta['model_sha256']
   different=np.argwhere(data!=reference);case[interface]=dict(samples=len(data),exact=not len(different),model_identity_exact=meta['model_sha256']==digest,first_difference=different[0].tolist() if len(different) else None)
   if len(different):raise AssertionError((name,interface,case[interface]))
  original=np.loadtxt(BASE/'legacy'/(name+'_cpp.csv.gz'),delimiter=',',skiprows=1)
  case['step1_5_exact']=bool(np.array_equal(reference[:,:5],original[:,:5]));assert case['step1_5_exact'],name
  report[name]=case
  print('differential',index+1,len(fixtures),name,'exact',flush=True)
  (RESULT/'differential/summary.json').write_text(json.dumps(report,indent=2))
 return report

def benchmark(repetitions=5):
 results={};out=RESULT/'performance'
 interfaces=('direct','cabi','python','rust','python_raw','rust_raw')
 batches=(1,8,16,32,64,128,256)
 for rep in range(repetitions):
  for c in (32,16,8):
   fixture=BASE/f'legacy/{c}ms_120fps_p0_s7_mixed.fixture'
   for batch in batches:
    for interface in (interfaces if rep%2==0 else tuple(reversed(interfaces))):
     stem=out/f'{c}ms/{interface}/b{batch}_r{rep}';run(interface,ROOT/f'lib/models/{c}ms',fixture,stem,batch)
    print('benchmark',rep+1,repetitions,c,'batch',batch,flush=True)
 summarize()

def summarize():
 result={}
 for c in (32,16,8):
  variants={}
  for interface in ('direct','cabi','python','rust','python_raw','rust_raw'):
   by_batch={}
   for batch in (1,8,16,32,64,128,256):
    values={'calls_us':[],'fresh_us':[],'active_us':[],'buffered_us':[],'renderer_us':[],'state_query_us':[],'batch_trace_us':[],'load_us':[],'initialize_us':[],'private_bytes':[]}
    for timing in sorted((RESULT/f'performance/{c}ms/{interface}').glob(f'b{batch}_r*.timings.csv')):
     stem=str(timing)[:-12];a=np.loadtxt(timing,delimiter=',',skiprows=1);meta=json.loads(Path(stem+'.json').read_text());overhead=np.loadtxt(stem+'.overhead.csv',delimiter=',',skiprows=1)
     values['calls_us'].extend(a[:,2]);values['fresh_us'].extend(a[a[:,4]>0,2]);values['active_us'].extend(a[a[:,5]>0,2]);values['buffered_us'].extend(a[a[:,4]==0,2]);values['renderer_us'].extend(a[:,3]);values['state_query_us'].extend(overhead[:,1]/overhead[:,0])
     for key in ('batch_trace_us','load_us','initialize_us','private_bytes'):
      if meta.get(key) is not None and not (interface=='rust_raw' and key=='initialize_us'):values[key].append(meta[key])
    by_batch[str(batch)]={key:stats(v) for key,v in values.items()}
   variants[interface]=by_batch
  result[str(c)]=variants
 (RESULT/'performance/summary.json').write_text(json.dumps(result,indent=2));return result

def validate_batches():
 """Check benchmark paths without adding work to timed regions."""
 report={}
 for c in (32,16,8):
  root=RESULT/f'performance/{c}ms'
  reference=np.loadtxt(root/'direct/b1_r0.samples.csv',delimiter=',',skiprows=1)
  cases=0
  for path in sorted(root.glob('*/*.samples.csv')):
   batch=int(path.name.split('_')[0][1:]);data=np.loadtxt(path,delimiter=',',skiprows=1)
   # Per-batch diagnostics describe its final endpoint; compare those endpoints.
   endpoints=np.minimum(np.arange(batch,len(data)+batch,batch),len(data))-1
   assert np.array_equal(data[:,:5],reference[:,:5]),path
   assert np.array_equal(data[:,12:14],reference[:,12:14]),path
   assert np.array_equal(data[endpoints,5:12],reference[endpoints,5:12]),path
   meta=json.loads(Path(str(path).replace('.samples.csv','.json')).read_text())
   assert meta['model_sha256']==json.loads((root/'direct/b1_r0.json').read_text())['model_sha256'],path
   cases+=1
  report[str(c)]={'streams':cases,'samples_per_stream':len(reference),'all_batch_sizes_exact':True}
  print('batch validation',c,cases,'streams exact',flush=True)
 (RESULT/'performance/batch_validation.json').write_text(json.dumps(report,indent=2))

def main():
 global RESULT
 p=argparse.ArgumentParser();p.add_argument('--full',action='store_true');p.add_argument('--benchmark',action='store_true');p.add_argument('--repetitions',type=int,default=5);p.add_argument('--summarize',action='store_true');p.add_argument('--validate-batches',action='store_true');p.add_argument('--results',type=Path,help='Write into a separate results tree, preserving historical receipts');a=p.parse_args()
 if a.results:RESULT=a.results.resolve()
 if a.validate_batches:validate_batches()
 elif a.summarize:summarize()
 elif a.benchmark:benchmark(a.repetitions)
 else:validate(a.full)
if __name__=='__main__':main()
