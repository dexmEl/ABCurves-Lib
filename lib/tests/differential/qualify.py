"""First-causal-stage differential checks against unchanged optimized Python."""
import os
for k in ('OMP_NUM_THREADS','OPENBLAS_NUM_THREADS','MKL_NUM_THREADS','NUMBA_NUM_THREADS'):os.environ[k]='1'
from pathlib import Path
import argparse,inspect,json,subprocess,sys
ROOT=Path(__file__).resolve().parents[3];sys.path.insert(0,str(ROOT/'Native'))
import numpy as np
from numba import njit
from abcurves._continuous.backend import Assets,NativeBackend
from abcurves._continuous import kernels,neural_native as nn
from abcurves._continuous.rng import RandomStream,softmax

def traced_motor():
    source=inspect.getsource(nn._run_pade9.py_func).split('def _run_pade9',1)[1]
    source='def trace'+source
    source=source.replace('weights, workspace):','weights, workspace, states):')
    source=source.replace('    combined[:96] = hidden','        states[row] = hidden\n    combined[:96] = hidden')
    namespace=dict(np=np,_sigmoid_pade9=nn._sigmoid_pade9,_tanh_pade9=nn._tanh_pade9)
    exec(source,namespace);return njit(fastmath=True,error_model='numpy')(namespace['trace'])

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--output',default='build/validation/components');ap.add_argument('--build',default='build/native/Release');args=ap.parse_args()
    out=ROOT/args.output;out.mkdir(parents=True,exist_ok=True);exe=ROOT/args.build/'abcurves_numerical_probe.exe';report={'rng':{},'components':{}}
    for seed in (0,7,11,2**32+7,2**64-1):
        dest=out/f'rng_{seed}.bin';subprocess.run([str(exe),'--rng',str(seed),str(dest)],check=True)
        raw=dest.read_bytes();r=RandomStream(seed);word=r._words(2048).copy();r=RandomStream(seed);u=r.uniform(2048);r=RandomStream(seed);e=r.exponential(2048);r=RandomStream(seed);p=np.arange(1,17,dtype=np.float32)/np.float32(136);choices=[]
        for i in range(512):
            if i%3==0:r.uniform(1)
            if i%7==0:r.discard_categorical()
            choices.append(r.categorical(p))
        actual=[np.frombuffer(raw,dtype,2048 if i<3 else 512,offset=i*2048*4) for i,dtype in enumerate(('<u4','<f4','<f4','<i4'))]
        exp=[word,u,e,np.array(choices,dtype=np.int32)];equal=[bool(np.array_equal(a,b)) for a,b in zip(actual,exp)]
        report['rng'][str(seed)]={'exact':equal,'exponential_max':float(np.max(np.abs(actual[2]-e)))}
        assert all(equal),report['rng'][str(seed)]
    trace=traced_motor()
    for interval in (32,16,8):
        assets=Assets(ROOT/'Native/models'/('continuous' if interval==32 else f'continuous-{interval}'),allow_custom=interval!=32);engine=NativeBackend(assets);mean_basis,mean_carry=kernels.decoder_geometry(assets.arrays['motor.velocity_basis'],assets.arrays['motor.carry_velocity'])
        for case in range(12):
            t=np.arange(640);h=np.column_stack((.8*np.sin(t*.017+case),.4*np.cos(t*.027+case))) if case else np.zeros((640,2));g=np.column_stack((80+.06*t+20*np.sin(t*.007),-40+.03*t));pos=np.array([23.,-7.]);now=9000000
            available=now+(t-639)*1000-(case%4)*1700;valid=np.ones(640,bool);motion=np.ones(640,bool)
            if case<4:motion[:480]=False;valid[:200]=False
            if case%3==2:motion[::11]=False;valid[::7]=False;available[::5]+=1001
            if case==3:valid[:]=False
            previous=np.column_stack((np.sin(np.arange(16)),np.cos(np.arange(16)))).astype(np.float32);prev_valid=case%2!=0;actual=np.array([3.7,-1.2],np.float64);mode=case%3;age=case*17.;initial_error=80.;innovation_age=case*5.;hold_target=np.array([40.,-25.]);hold_pos=np.array([12.,-3.])
            fixture=out/f'component_{interval}_{case}.fixture'
            with fixture.open('w') as f:
                f.write(' '.join(map(str,(*pos,now,interval,int(prev_valid),*actual)))+'\n')
                f.write(' '.join(map(str,(age,initial_error,innovation_age,*hold_target,*hold_pos,mode)))+'\n')
                for i in range(640):f.write(' '.join(map(str,(*h[i],*g[i],available[i],int(valid[i]),int(motion[i]))))+'\n')
                f.write(' '.join(map(str,previous.ravel()))+'\n')
            dest=out/f'component_{interval}_{case}.bin';subprocess.run([str(exe),str(ROOT/f'lib/models/{interval}ms'),str(fixture),str(dest)],check=True)
            c,f,d=kernels.motor_features(h,pos,g,available,valid,motion,now,backend='numba');raw,context,basis=kernels.event_features(h,pos,g,available,valid,motion,now,hold_age=age,hold_target=hold_target,hold_position=hold_pos,initial_error=initial_error,innovation_age=innovation_age,mode=mode,backend='numba')
            enc,q=engine.motor(c,f,d);enc=enc.copy();q=q.copy();tokens_before=engine._motor.workspace[0].copy();projected_before=engine._motor.workspace[1].copy();states=np.empty((70,96),np.float32);trace(c[0],f[0],d[0],engine._motor.weights,engine._motor.workspace,states)
            assert np.array_equal(enc,engine._motor.encoded) and np.array_equal(q,engine._motor.coefficients), 'tracing changed reference outputs'
            assert np.array_equal(tokens_before,engine._motor.workspace[0]) and np.array_equal(projected_before,engine._motor.workspace[1]), 'tracing changed projections'
            heads=kernels.decode_geometry(q,h[-1],mean_basis,mean_carry);unary,pairs=kernels.selector_inputs(enc,heads,previous if prev_valid else None,actual,backend='numba',elapsed_ms=interval);logits=engine.choice(enc,np.ascontiguousarray(unary[:,:,96:]),pairs,prev_valid).copy();prob=softmax(logits);hazard=engine.hazard(context).copy();duration,coeff,freq=engine.brake(context)
            action=np.zeros((32,2));action[:interval]=kernels.decode_selected(q[3],h[-1],assets.arrays['motor.velocity_basis'][:interval],assets.arrays['motor.carry_velocity'][:interval]);curve=kernels.c2_path(h[-1],np.array([.04,-.02]),float(duration[2]),coeff[2]@basis.T,np.arange(33,dtype=np.float64)+7,backend='numba');activation=np.array([nn._tanh_pade9(np.float32(i-200)*np.float32(.05)) for i in range(401)],np.float32)
            expected=dict(coarse=c,fine=f,dynamics=d,event_raw=raw,event_context=context,event_axis=basis[:,0].copy(),tokens=engine._motor.workspace[0],projected=engine._motor.workspace[1],recurrent_states=states,encoded=enc,coefficients=q,heads=heads,unary_geometry=unary[:,:,96:].copy(),pairs=pairs,selector_logits=logits,selector_probabilities=prob,hazard=hazard,duration=duration,brake_coefficients=coeff,frequency=freq,action=action,curve=curve,activation=activation)
            data=dest.read_bytes();offset=0;result={};first=None;first_nonexact=None
            for name,ref in expected.items():
                actual_value=np.frombuffer(data,ref.dtype,ref.size,offset).reshape(ref.shape);offset+=ref.nbytes;err=float(np.max(np.abs(actual_value-ref)));scale=float(np.max(np.abs(ref)))
                # Strict input boundaries; neural errors bounded by accumulated
                # float32 rounding through matrix reductions and nonlinearities.
                strict=name in ('coarse','fine','dynamics','event_raw','event_context','event_axis','activation')
                atol=0 if strict else 2e-4;rtol=0 if strict else 3e-5
                exact=bool(np.array_equal(actual_value,ref));different=np.flatnonzero(actual_value.ravel()!=ref.ravel())
                if not exact and first_nonexact is None:first_nonexact=dict(stage=name,flat_index=int(different[0]),python=float(ref.ravel()[different[0]]),cpp=float(actual_value.ravel()[different[0]]))
                ok=bool(exact if strict else np.allclose(actual_value,ref,atol=atol,rtol=rtol));result[name]={'max_abs':err,'reference_max_abs':scale,'atol':atol,'rtol':rtol,'pass':ok,'exact':exact}
                if not ok and first is None:first=name
            assert offset==len(data)
            report['components'][f'{interval}_{case}']={'first_divergence':first,'first_nonexact':first_nonexact,'traced_kernel_outputs_exact':True,'stages':result}
            np.savez_compressed(out/f'component_{interval}_{case}_python.npz',**expected)
    (out/'components.json').write_text(json.dumps(report,indent=2));failed=[k for k,v in report['components'].items() if v['first_divergence']]
    print('RNG exact; component first divergences:',failed)
    if failed:
        for k in failed[:4]:print(k,report['components'][k]['first_divergence'],{s:v for s,v in report['components'][k]['stages'].items() if not v['pass']})
    return bool(failed)
if __name__=='__main__':sys.exit(main())
