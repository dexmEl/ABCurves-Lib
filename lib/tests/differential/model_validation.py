"""Malformed model bundles must fail before planner state is constructed."""
import hashlib,json,shutil,struct,subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
EXE=ROOT/'build/native/Release/abcurves_numerical_probe.exe'
SOURCE=ROOT/'lib/models/32ms'
RESULT=ROOT/'build/validation/components'
def main():
    RESULT.mkdir(parents=True,exist_ok=True);cases={}
    with tempfile.TemporaryDirectory(prefix='abcurves_models_') as temp:
        for name in ('valid','missing','truncated','hash','version','variant','dimensions','dtype','nonfinite','same_count_shape','manifest_config','manifest_shape','manifest_tensor_hash','manifest_duplicate','manifest_large','manifest_unicode'):
            path=Path(temp)/name;shutil.copytree(SOURCE,path)
            data=bytearray((path/'model.abcmodel').read_bytes());manifest=json.loads((path/'manifest.json').read_text());rehash=False
            if name=='missing':(path/'model.abcmodel').unlink()
            elif name=='truncated':data=data[:100]
            elif name=='hash':data[-1]^=1
            elif name=='version':struct.pack_into('<I',data,8,42);rehash=True
            elif name=='variant':struct.pack_into('<I',data,16,8);rehash=True
            elif name in ('dimensions','dtype','nonfinite'):
                length,ndim,dtype,_=struct.unpack_from('<HBBI',data,24);shape_offset=32+length
                if name=='dimensions':struct.pack_into('<I',data,shape_offset,175)
                if name=='dtype':data[27]=3
                if name=='nonfinite':struct.pack_into('<I',data,shape_offset+4*ndim+8,0x7fc00000)
                rehash=True
            elif name=='manifest_config':manifest['planner_config']['position_scale']=82
            elif name=='manifest_shape':manifest['tensors']['events.brake.bias']['shape']=[175]
            elif name=='manifest_tensor_hash':manifest['tensors']['events.brake.bias']['sha256']='0'*64
            elif name=='same_count_shape':
                at=24
                while at<len(data):
                    length,ndim,dtype,flags=struct.unpack_from('<HBBI',data,at);tensor_name=data[at+8:at+8+length].decode();shape_at=at+8+length;size_at=shape_at+4*ndim;size=struct.unpack_from('<Q',data,size_at)[0]
                    if tensor_name=='events.encoder.0.weight':struct.pack_into('<II',data,shape_at,32,96);manifest['tensors'][tensor_name]['shape']=[32,96];break
                    at=size_at+8+size
                rehash=True
            elif name=='manifest_unicode':manifest['source_directory']='continuous_\U0001f3af'
            if name!='missing':(path/'model.abcmodel').write_bytes(data)
            if rehash:manifest['model_sha256']=hashlib.sha256(data).hexdigest()
            text=json.dumps(manifest)
            if name=='manifest_duplicate':text=text[:-1]+',"schema":"abcurves.native.model.v1"}'
            if name=='manifest_large':text+=' '*(1024*1024)
            (path/'manifest.json').write_text(text)
            r=subprocess.run([str(EXE),'--load',str(path)],capture_output=True,text=True);passed=(r.returncode==0)==(name in ('valid','manifest_unicode'));cases[name]={'pass':passed,'exit_code':r.returncode,'error':r.stderr.strip()}
    (RESULT/'model_validation.json').write_text(json.dumps(cases,indent=2));assert all(v['pass'] for v in cases.values()),cases
    print('model validation:',len(cases),'passed')
if __name__=='__main__':main()
