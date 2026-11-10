"""Raw-prefix and persistent Renderer comparisons using measured replay paths."""
from compare import *


def write_fixture(path,data):
    duration,seed,initial,history,events=data
    with path.open('w') as f:
        f.write(' '.join(map(str,(duration,seed,*initial,len(history),len(events))))+'\n')
        np.savetxt(f,history,fmt='%.17g')
        for row in events:f.write(' '.join(map(str,row))+'\n')


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,default=ROOT/'build/reference-comparison-final')
    a=p.parse_args();a.out=a.out.resolve();out=a.out/'accuracy';out.mkdir(parents=True,exist_ok=True)
    # The C ABI is used only to call the same native prefix function for this
    # correctness check, never for the direct native performance measurements.
    sys.path.insert(0,str(ROOT/'API/python/src'));os.environ['ABCURVES_LIBRARY']=str(ROOT/'build/api/Release/abcurves.dll')
    from abcurves_native import prepare_history as native_prepare
    t=np.arange(164);raw=np.column_stack((.25*(t%9-4),.125*(t%7-3)))
    py_prefix=prepare_history(raw,current_xy=(23.,-7.));cpp_prefix=native_prepare(raw,current_position=(23.,-7.))
    native_h=np.array([[x.x,x.y] for x in cpp_prefix.history]);native_anchor=np.array([cpp_prefix.initial_position.x,cpp_prefix.initial_position.y])
    prefix=dict(history=metrics(py_prefix.history,native_h),anchor=metrics(py_prefix.initial_xy.reshape(1,2),native_anchor.reshape(1,2)))
    # Also check non-binary-exact input rather than only a dyadic test pattern.
    smooth=np.column_stack((.3*np.sin(t*.03),.2*np.cos(t*.04)))
    smooth_py=prepare_history(smooth,current_xy=(23.,-7.))
    smooth_cpp=native_prepare(smooth,current_position=(23.,-7.))
    smooth_check=dict(history=metrics(smooth_py.history,np.array([[x.x,x.y] for x in smooth_cpp.history])),
        anchor=metrics(smooth_py.initial_xy.reshape(1,2),np.array([[smooth_cpp.initial_position.x,smooth_cpp.initial_position.y]])))
    policy={c:Planner(assets(c),planning_interval_ms=c) for c in (32,16,8)}
    for obj in policy.values():obj.prewarm()
    raw_cases={}
    for c in (32,16,8):
        for seed in (7,11,13):
            base=ROOT/f'tests/baselines/legacy/{c}ms_120fps_p0_s{seed}_mixed_irregular.fixture'
            duration,_,_,_,events=read_fixture(base)
            path=out/f'{c}ms_raw164_s{seed}.fixture';stem=path.with_suffix('')
            write_fixture(path,(duration,seed,native_anchor,native_h,events))
            subprocess.run([str(ROOT/'build/api/engine/Release/abcurves_replay.exe'),str(ROOT/f'lib/models/{c}ms'),str(path),str(stem)],check=True)
            native=np.loadtxt(str(stem)+'.csv',delimiter=',',skiprows=1)
            original=replay_python(c,(duration,seed,py_prefix.initial_xy,py_prefix.history,events),policy[c])
            np.savez_compressed(str(stem)+'_python.npz',rows=original)
            raw_cases[path.stem]=dict(displacement=metrics(original[:,3:5],native[:,3:5]),position=metrics(original[:,1:3],native[:,1:3]),
                timestamps_equal=bool(np.array_equal(original[:,0],native[:,0])),
                diagnostics_equal=bool(np.array_equal(original[:,5:],native[:,6:])),
                max_cumulative_displacement_deviation=float(np.abs(np.cumsum(native[:,3:5],axis=0)-np.cumsum(original[:,3:5],axis=0)).max()))
            print('raw prefix',path.stem,flush=True)
    (out/'raw-prefix.json').write_text(json.dumps(dict(preparation=prefix,sinusoidal_preparation=smooth_check,cases=raw_cases),indent=2)+'\n',encoding='utf-8')

    renderer_dir=a.out/'renderer';renderer_dir.mkdir(exist_ok=True)
    model=PortableRendererModel(ROOT/'Native/models/renderer_global_h80.bin',library=ROOT/'Native/abcurves/_native/abcurves_renderer.dll')
    report={}
    for fixture in sorted((ROOT/'tests/baselines/renderer').glob('*_same_input.fixture')):
        name=fixture.name.removesuffix('_same_input.fixture');data=fixture.read_text().splitlines();n,scale,seed=data[0].split();scale=float(scale);seed=int(seed)
        profile=np.array([list(map(int,line.split())) for line in data[1:257]],np.int16);context=model.prepare_context(profile)
        py=np.load(str(out/name)+'_python.npz')['rows'][:,3:5]
        cpp=np.loadtxt(str(out/name)+'.csv',delimiter=',',skiprows=1)[:,3:5]
        stream=context.begin_stream(event_seed=seed);reference=np.array([stream.step(value/scale) for value in py],np.int16)
        np.savez_compressed(renderer_dir/(name+'_python.npz'),reports=reference)
        entry={}
        for kind,values in [('same_input',py),('independent_planners',cpp)]:
            inp=renderer_dir/(name+'_'+kind+'.fixture');dest=renderer_dir/(name+'_'+kind+'.csv')
            with inp.open('w') as f:
                f.write(f'{len(values)} {scale:.17g} {seed}\n');np.savetxt(f,profile,fmt='%d');np.savetxt(f,values,fmt='%.17g')
            subprocess.run([str(ROOT/'build/api/engine/Release/abcurves_renderer_probe.exe'),str(ROOT/'Native/models/renderer_global_h80.bin'),str(inp),str(dest)],check=True)
            native=np.loadtxt(dest,delimiter=',',skiprows=1,dtype=np.int64);delta=native-reference.astype(np.int64);idx=np.flatnonzero(np.any(delta!=0,axis=1));cum=np.cumsum(delta,axis=0)
            different=np.any(cum!=0,axis=1);longest=run=0
            for value in different:
                run=run+1 if value else 0;longest=max(longest,run)
            entry[kind]=dict(reports=len(reference),exact_reports=len(reference)-len(idx),mismatches=len(idx),
                first_mismatch_sample=int(idx[0]+1) if len(idx) else None,maximum_per_axis_difference=int(np.abs(delta).max()),
                maximum_cumulative_difference=int(np.abs(cum).max()),final_cumulative_difference=cum[-1].tolist(),
                longest_cumulative_divergence_samples=longest,
                mismatches_detail=[dict(sample=int(i+1),python=reference[i].tolist(),native=native[i].tolist(),cumulative=cum[i].tolist()) for i in idx])
        report[name]=entry;print('renderer',name,entry['independent_planners']['mismatches'],flush=True)
    (renderer_dir/'summary.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')


if __name__=='__main__':main()
