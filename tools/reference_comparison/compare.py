"""Reproduce original Python (Numba/BLAS) versus direct C++ measurements.

No production code, expectations or weights are edited. Raw results go to build/.
"""
from __future__ import annotations
import os
for name in ('OMP_NUM_THREADS','OPENBLAS_NUM_THREADS','MKL_NUM_THREADS','NUMBA_NUM_THREADS'):
    os.environ[name]='1'
import argparse
import csv
import hashlib
import importlib.metadata
import json
from pathlib import Path
import platform
import subprocess
import sys
import time
from datetime import datetime, timezone

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'Native'))
import numpy as np
from abcurves._continuous.planner import Planner
from abcurves._continuous.backend import Assets
from abcurves._continuous.runtime import MovementRuntime
from abcurves._continuous import kernels
from abcurves.continuous import prepare_history
from abcurves.portable_renderer import PortableRendererModel


def stats(values):
    x=np.asarray(values,dtype=np.float64)
    return dict(count=len(x),median=float(np.median(x)),mean=float(x.mean()),
                p95=float(np.percentile(x,95)),p99=float(np.percentile(x,99)),maximum=float(x.max()))


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def machine_context(native):
    cpu=platform.processor()
    if os.name=='nt':
        cpu=subprocess.check_output(['powershell','-NoProfile','-Command',
            '(Get-CimInstance Win32_Processor).Name'],text=True).strip()
    compiler={}
    for path in native.parent.parent.glob('CMakeFiles/*/CMakeCXXCompiler.cmake'):
        import re
        content=path.read_text()
        for key in ('CMAKE_CXX_COMPILER_ID','CMAKE_CXX_COMPILER_VERSION','CMAKE_CXX_COMPILER_ARCHITECTURE_ID'):
            match=re.search(r'set\('+key+r' "?([^"\)]+)',content)
            if match:compiler[key]=match[1]
    return dict(cpu=cpu,logical_processors=os.cpu_count(),compiler=compiler,
        cmake=subprocess.check_output(['cmake','--version'],text=True).splitlines()[0],
        build='Release, /O2 /fp:strict, ABC_PROFILE=OFF; AVX2 dispatched; no FMA contraction',
        isolation='Thread pools set to 1; no affinity, clock/frequency, power-plan or OS scheduling isolation.')


def assets(c):
    return Assets(ROOT/'Native/models'/('continuous' if c==32 else f'continuous-{c}'),allow_custom=c!=32)


def python_benchmark(c,path):
    samples=[]
    def measure(name,n,f):
        batch=16 if name in ('target_update','decode_selected','prepare_history') else 1
        for i in range(32):f(i)
        for i in range(n):
            b=time.perf_counter_ns()
            for j in range(batch):f(i)
            elapsed=(time.perf_counter_ns()-b)/1000/batch
            samples.append((name,elapsed))
    bundle=assets(c);p=Planner(bundle,planning_interval_ms=c);p.prewarm();r=MovementRuntime(p,planning_interval_ms=c)
    measure('model_load',20,lambda _:assets(c))
    measure('initialize_loaded',200,lambda _:MovementRuntime(Planner(bundle,planning_interval_ms=c),planning_interval_ms=c))
    measure('reset_session',1000,lambda _:r.reset(seed=7))
    receipt=iter(range(16032))
    measure('target_update',1000,lambda i:r.update_target((320.+i*.01,120.),next(receipt)))
    for label,until in [('fresh_decision',1000),('complete_commitment',c*1000)]:
        r=MovementRuntime(p,history=np.tile([.1,.05],(160,1)),planning_interval_ms=c)
        for i in range(-32,400):
            r.reset(seed=7);r.update_target((320.,120.),0)
            b=time.perf_counter_ns();r.advance(until);elapsed=(time.perf_counter_ns()-b)/1000
            assert p.motor_evaluations==1
            if i>=0:samples.append((label,elapsed))
    r=MovementRuntime(p,planning_interval_ms=c);r.update_target((320.,120.),0)
    for i in range(-32,1200):
        while c-r._pending_index<4:r.advance(r.now_us+1000)
        b=time.perf_counter_ns()
        for j in range(4):r.advance(r.now_us+1000)
        elapsed=(time.perf_counter_ns()-b)/1000/4
        if i>=0:samples.append(('buffered_sample',elapsed))
    t=np.arange(640);h=np.column_stack((.8*np.sin(t*.017),.4*np.cos(t*.027)));g=np.column_stack((80.+.06*t,-40.+.03*t))
    a=9000000+(t-639)*1000;valid=np.ones(640,bool);pos=np.array([23.,-7.]);now=9000000
    args=(h,pos,g,a,valid,valid,now)
    f=kernels.motor_features(*args,backend='numba')
    efargs=dict(hold_age=160.,hold_target=np.array([40.,-25.]),hold_position=np.array([12.,-3.]),initial_error=80.,innovation_age=0.,mode=0,backend='numba')
    raw,context,basis=kernels.event_features(*args,**efargs)
    measure('motor_features',1000,lambda _:kernels.motor_features(*args,backend='numba'))
    measure('event_features',1000,lambda _:kernels.event_features(*args,**efargs))
    measure('motor_inference',500,lambda _:p.engine.motor(*f))
    encoded,coeff=p.engine.motor(*f)
    def selector(_):
        heads=kernels.decode_geometry(coeff,h[-1],p.mean_basis,p.mean_carry)
        unary,pairs=kernels.selector_inputs(encoded,heads,None,np.array([3.7,-1.2]),backend='numba',elapsed_ms=c)
        p.engine.choice(encoded,np.ascontiguousarray(unary[:,:,96:]),pairs,False)
    measure('selector_geometry_network',1000,selector)
    measure('hazard_inference',1000,lambda _:p.engine.hazard(context))
    measure('brake_inference',1000,lambda _:p.engine.brake(context))
    measure('decode_selected',1000,lambda _:kernels.decode_selected(coeff[3],h[-1],p.velocity_basis[:c],p.carry_velocity[:c]))
    brake_coeff=np.column_stack((.02*np.arange(5),-.01*np.arange(5)))
    measure('brake_path',1000,lambda _:kernels.c2_path(h[-1],np.array([.04,-.02]),64.,brake_coeff,np.arange(c+1,dtype=float)+7.,backend='numba'))
    t=np.arange(164);raw=np.column_stack((.3*np.sin(t*.03),.2*np.cos(t*.04)))
    measure('prepare_history',1000,lambda _:prepare_history(raw,current_xy=(23.,-7.)))
    image=ROOT/'Native/models/renderer_global_h80.bin';library=ROOT/'Native/abcurves/_native/abcurves_renderer.dll'
    measure('renderer_load',20,lambda _:PortableRendererModel(image,library=library))
    model=PortableRendererModel(image,library=library);i=np.arange(256);profile=np.column_stack((i%9-4,i%7-3)).astype(np.int16)
    measure('renderer_profile',200,lambda _:model.prepare_context(profile))
    context=model.prepare_context(profile)
    measure('renderer_begin',200,lambda _:context.begin_stream(event_seed=7))
    stream=context.begin_stream(event_seed=7)
    t=np.arange(1200);intents=np.column_stack((.7*np.sin(t*.013),.4*np.cos(t*.017)))
    measure('renderer_step',1200,lambda i:stream.step(intents[i]))
    with path.open('w',newline='') as out:
        w=csv.writer(out);w.writerow(('operation','us'));w.writerows(samples)


def benchmark(args):
    out=args.out/'performance';out.mkdir(parents=True,exist_ok=True)
    smooth=np.column_stack((.7*np.sin(np.arange(8000)*.013),.4*np.cos(np.arange(8000)*.017)))
    input_path=out/'renderer-input.txt'
    with input_path.open('w') as f:
        f.write(str(len(smooth))+'\n');np.savetxt(f,smooth,fmt='%.17g')
    pooled={str(c):{'python':{},'cpp':{}} for c in (32,16,8)}
    for rep in range(args.runs):
        for c in (32,16,8):
            for lang in (('python','cpp') if rep%2==0 else ('cpp','python')):
                path=out/f'{c}_{lang}_r{rep}.csv'
                if lang=='python':python_benchmark(c,path)
                else:subprocess.run([str(args.native),str(ROOT),str(c),str(path),str(input_path)],check=True)
                with path.open(newline='') as f:
                    for row in csv.DictReader(f):pooled[str(c)][lang].setdefault(row['operation'],[]).append(float(row['us']))
                print('benchmark',rep+1,c,lang,flush=True)
    result={}
    for c,languages in pooled.items():
        result[c]={}
        assert languages['cpp'].keys()==languages['python'].keys()
        for name in languages['python']:
            original=stats(languages['python'][name]);native=stats(languages['cpp'][name])
            result[c][name]=dict(python=original,cpp=native,speedup=original['median']/native['median'])
    report=dict(schema=1,comparison='Local original Python Numba/BLAS vs direct native C++20; same variant weights.',
                runs=args.runs,warmup_per_operation=32,units='microseconds',alternating_order=True,variants=result)
    (out/'summary.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')


def read_fixture(path):
    lines=path.read_text().splitlines();hdr=lines[0].split();duration,seed=int(hdr[0]),int(hdr[1]);initial=np.array(hdr[2:4],float);n,ne=map(int,hdr[4:6])
    history=np.array([list(map(float,x.split())) for x in lines[1:1+n]]).reshape(-1,2)
    events=[(int(row[0]),float(row[1]),float(row[2])) for row in (line.split() for line in lines[1+n:1+n+ne])]
    return duration,seed,initial,history,events


def replay_python(c,data,policy):
    duration,seed,initial,history,events=data
    r=MovementRuntime(policy,seed=seed,initial_xy=initial,history=history if len(history) else None,planning_interval_ms=c)
    for t,x,y in events:r.update_target((x,y),t)
    rows=np.empty((duration,12));previous=initial.copy();last_head=-1;decision_us=-1
    for i in range(duration):
        prior=policy.motor_evaluations;decisions=r.decisions
        incoming=r._history[(r._cursor+639)%640].copy()
        value=r.advance((i+1)*1000);pos=value['xy'][0];delta=pos-previous;previous=pos.copy()
        if r.decisions>decisions:last_head=-1
        if policy.motor_evaluations>prior:
            heads=kernels.decode_geometry(policy.engine._motor.coefficients,incoming,policy.mean_basis,policy.mean_carry)
            matches=np.flatnonzero(np.all(heads==policy.previous,axis=(1,2)));assert len(matches)==1;last_head=int(matches[0])
        if r.decisions>decisions:decision_us=i*1000
        rows[i]=[value['time_us'][0],*pos,*delta,r.decisions,policy.motor_evaluations,policy.mode if policy.mode is not None else 2,last_head,
                 decision_us,r.receipt_us if r._target is not None else -1,c-r._pending_index]
    return rows


def metrics(original,native):
    delta=native-original;exact=np.all(original==native,axis=1);bitwise=np.all(original.view(np.uint64)==native.view(np.uint64),axis=1)
    index=np.flatnonzero(~exact)
    return dict(samples=len(exact),exact_samples=int(exact.sum()),bitwise_samples=int(bitwise.sum()),
                exact_percent=float(100*exact.mean()),maximum_absolute=float(np.abs(delta).max()),
                mean_absolute=float(np.abs(delta).mean()),squared_error_sum=float(np.square(delta).sum()),
                rmse=float(np.sqrt(np.mean(delta*delta))),first_divergent_sample=int(index[0]+1) if len(index) else None)


def parity(args):
    out=args.out/'accuracy';out.mkdir(parents=True,exist_ok=True);report={};policies={}
    paths=sorted((ROOT/'tests/baselines/legacy').glob('*.fixture'))
    if args.quick:paths=[p for p in paths if '_120fps_p0_s7_mixed_irregular' in p.name]
    for c in (32,16,8):
        policies[c]=Planner(assets(c),planning_interval_ms=c);policies[c].prewarm()
    for path in paths:
        c=int(path.name.split('ms')[0]);data=read_fixture(path);stem=out/path.stem
        subprocess.run([str(args.replay),str(ROOT/f'lib/models/{c}ms'),str(path),str(stem)],check=True)
        native=np.loadtxt(str(stem)+'.csv',delimiter=',',skiprows=1)
        original=replay_python(c,data,policies[c])
        np.savez_compressed(str(stem)+'_python.npz',rows=original)
        entry=dict(displacement=metrics(original[:,3:5],native[:,3:5]),position=metrics(original[:,1:3],native[:,1:3]),
                   timestamps_equal=bool(np.array_equal(original[:,0],native[:,0])),
                   decisions_equal=bool(np.array_equal(original[:,5],native[:,6])),
                   motor_counts_equal=bool(np.array_equal(original[:,6],native[:,7])),
                   modes_equal=bool(np.array_equal(original[:,7],native[:,8])),
                   selected_heads_equal=bool(np.array_equal(original[:,8],native[:,9])),
                   decision_boundaries_equal=bool(np.array_equal(original[:,9],native[:,10])),
                   receipt_times_equal=bool(np.array_equal(original[:,10],native[:,11])),
                   commitment_remainders_equal=bool(np.array_equal(original[:,11],native[:,12])),
                   final_position_difference=(native[-1,1:3]-original[-1,1:3]).tolist(),
                   max_cumulative_displacement_deviation=float(np.abs(np.cumsum(native[:,3:5],axis=0)-np.cumsum(original[:,3:5],axis=0)).max()),
                   fixture_sha256=sha(path))
        report[path.stem]=entry
        print('parity',path.stem,entry['displacement']['maximum_absolute'],flush=True)
    (out/'cases.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    aggregate={}
    for c in (32,16,8):
        cases={k:v for k,v in report.items() if k.startswith(str(c)+'ms')};n=sum(e['position']['samples'] for e in cases.values())
        totals={}
        for kind in ('displacement','position'):
            x=[e[kind] for e in cases.values()]
            totals[kind]=dict(samples=n,exact_samples=sum(e['exact_samples'] for e in x),bitwise_samples=sum(e['bitwise_samples'] for e in x),
                              maximum_absolute=max(e['maximum_absolute'] for e in x),
                              mean_absolute=sum(e['mean_absolute']*e['samples'] for e in x)/n,
                              rmse=float(np.sqrt(sum(e['squared_error_sum'] for e in x)/(2*n))))
        aggregate[str(c)]=dict(fixtures=len(cases),**totals,
            max_cumulative_displacement_deviation=max(e['max_cumulative_displacement_deviation'] for e in cases.values()),
            diagnostic_checks={k:all(e[k] for e in cases.values()) for k in ('timestamps_equal','decisions_equal','motor_counts_equal','modes_equal','selected_heads_equal','decision_boundaries_equal','receipt_times_equal','commitment_remainders_equal')},
            first_divergent_case=next((k for k,e in cases.items() if e['position']['first_divergent_sample'] is not None),None))
    (out/'summary.json').write_text(json.dumps(aggregate,indent=2)+'\n',encoding='utf-8')


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,default=ROOT/'build/reference-comparison-final');p.add_argument('--runs',type=int,default=5)
    p.add_argument('--native',type=Path,default=ROOT/'build/reference-tools/Release/reference_benchmark.exe')
    p.add_argument('--replay',type=Path,default=ROOT/'build/api/engine/Release/abcurves_replay.exe')
    p.add_argument('--benchmark',action='store_true');p.add_argument('--parity',action='store_true');p.add_argument('--quick',action='store_true')
    a=p.parse_args();a.out=a.out.resolve();a.out.mkdir(parents=True,exist_ok=True)
    metadata=dict(recorded_utc=datetime.now(timezone.utc).isoformat(),platform=platform.platform(),processor=platform.processor(),python=sys.version,numpy=np.__version__,
        machine=machine_context(a.native),
        packages={name:importlib.metadata.version(name) for name in ('numba','llvmlite','numpy','scipy')},
        threads={k:os.environ[k] for k in ('OMP_NUM_THREADS','OPENBLAS_NUM_THREADS','MKL_NUM_THREADS','NUMBA_NUM_THREADS')},
        runtime_source_sha256={path.relative_to(ROOT).as_posix():sha(path) for directory in ('lib/src','lib/include','Native/abcurves') for path in (ROOT/directory).rglob('*') if path.is_file() and '__pycache__' not in path.parts},
        native_executable_sha256=sha(a.native) if a.native.is_file() else None,replay_sha256=sha(a.replay),
        assets=json.loads((ROOT/'docs/evidence/model-assets.json').read_text())['assets'])
    (a.out/'environment.json').write_text(json.dumps(metadata,indent=2)+'\n',encoding='utf-8')
    if a.benchmark:
        benchmark(a)
        (a.out/'performance/environment.json').write_text(json.dumps(metadata,indent=2)+'\n',encoding='utf-8')
    if a.parity:
        parity(a)
        (a.out/'accuracy/environment.json').write_text(json.dumps(metadata,indent=2)+'\n',encoding='utf-8')

if __name__=='__main__':main()
