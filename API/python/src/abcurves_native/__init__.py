"""Native ABCurves via ABI v1. No dependency on the original abcurves package."""
from __future__ import annotations
import ctypes as C
import ctypes.util
import os
import sys
import threading
from pathlib import Path
from typing import Iterable

COMMON_RADIANS_PER_COUNT = 0.0003509487083280618
U32,U64,I64=C.c_uint32,C.c_uint64,C.c_int64
class Vec2(C.Structure):
    _fields_=[('x',C.c_double),('y',C.c_double)]
    def __iter__(self):return iter((self.x,self.y))
class Sample(C.Structure):
    _fields_=[('timestamp_us',I64),('position',Vec2),('displacement',Vec2)]
class Report(C.Structure):
    _fields_=[('dx',C.c_int16),('dy',C.c_int16)]
class Version(C.Structure):
    _fields_=[(n,U32) for n in ('struct_size','api_version','abi_major','abi_minor','engine_major','engine_minor','engine_patch','model_format_version','sample_size','vec2_size','report_size','options_size','model_info_size','state_size','tensor_info_size','reserved')]+[('capabilities',U64)]
class ModelInfo(C.Structure):
    _fields_=[(n,U32) for n in ('struct_size','model_format_version','commitment_ms','history_samples','prefix_samples','forecast_samples','coarse_tokens','coarse_features','hidden_units','heads','basis_coefficients','tensor_count')]+[('capabilities',U64),('variant',C.c_char*32),('sha256',C.c_char*65),('reserved',C.c_char*7)]
class TensorInfo(C.Structure):
    _fields_=[(n,U32) for n in ('struct_size','dtype','rank','reserved')]+[('dimensions',U64*8),('bytes',U64)]
class Options(C.Structure):
    _fields_=[('struct_size',U32),('flags',U32),('seed',U64),('initial_position',Vec2),('history',C.POINTER(Vec2)),('history_count',U64)]
class State(C.Structure):
    _fields_=[('struct_size',U32),('mode',U32)]+[(n,I64) for n in ('numerical_time_us','requested_time_us','last_decision_us','last_target_receipt_us')]+[(n,U64) for n in ('decision_count','motor_evaluations','samples_emitted')]+[('selected_head',C.c_int32),('selected_brake_head',C.c_int32),('pending_samples',U32),('failed',U32),('position',Vec2)]

class NativeError(RuntimeError):
    def __init__(self,status:int,message:str,written:int=0):
        self.status=status;self.written=written;super().__init__(f'ABCurves status {status}: {message}')

def _unsigned(n:int)->int:
    if not isinstance(n,int) or not 0<=n<2**64:raise ValueError('expected unsigned 64-bit integer')
    return n

def _time(n:int)->int:
    if not isinstance(n,int) or not 0<=n<2**63:raise ValueError('expected nonnegative signed 64-bit timestamp')
    return n

def _vec(v:Vec2|tuple[float,float])->Vec2:return v if isinstance(v,Vec2) else Vec2(*v)
def _path(value):
    text=os.fspath(value)
    if not isinstance(text,str) or '\0' in text:raise ValueError('path must be a UTF-8 string without NUL')
    return text.encode('utf8')

def _struct(cls):
    result=cls();result.struct_size=C.sizeof(cls);return result

def _find_library(explicit=None):
    if explicit is not None:return os.fspath(explicit)
    override=os.environ.get('ABCURVES_LIBRARY')
    if override:return override
    name='abcurves.dll' if sys.platform=='win32' else ('libabcurves.dylib' if sys.platform=='darwin' else 'libabcurves.so')
    bundled=Path(__file__).parent/'_native'/name
    if bundled.is_file():return str(bundled)
    # Relative development discovery, never a developer-specific absolute path.
    for parent in Path(__file__).resolve().parents:
        for relative in ('build/api/Release','build/api','bin','lib'):
            p=parent/relative/name
            if p.is_file():return str(p)
    found=C.util.find_library('abcurves')
    if found:return found
    raise FileNotFoundError('Set ABCURVES_LIBRARY or provide library= with the built C ABI library')

class Library:
    """Owns the loaded DLL and validates ABI version and every exposed layout."""
    def __init__(self,path=None):
        self.path=_find_library(path);self.raw=C.CDLL(self.path)
        lib=self.raw
        signatures={
        'get_version':[C.POINTER(Version)],'check_abi':[U32,U32],
        'model_load':[C.c_char_p,C.POINTER(U64)],'model_release':[U64],
        'model_get_info':[U64,C.POINTER(ModelInfo)],'model_get_tensor_info':[U64,C.c_char_p,C.POINTER(TensorInfo)],
        'planner_options_init':[C.POINTER(Options),U32],
        'planner_create':[U64,C.POINTER(Options),C.POINTER(U64)],'planner_destroy':[U64],
        'planner_reset':[U64,U64],'planner_set_position':[U64,Vec2],
        'planner_update_target':[U64,Vec2,I64],'planner_clear_target':[U64,I64],
        'planner_get_model_info':[U64,C.POINTER(ModelInfo)],'planner_get_state':[U64,C.POINTER(State)],'planner_required_capacity':[U64,I64,C.POINTER(U64)],
        'planner_advance':[U64,I64,C.POINTER(Sample),U64,C.POINTER(U64)],
        'planner_generate_sample':[U64,I64,C.POINTER(Sample)],
        'renderer_create':[C.c_char_p,C.POINTER(U64)],'renderer_destroy':[U64],
        'renderer_set_profile':[U64,C.POINTER(Report),U64],'renderer_reset':[U64,U64],
        'renderer_step':[U64,Vec2,C.POINTER(Report)],
        'renderer_step_batch':[U64,C.POINTER(Vec2),U64,C.POINTER(Report),U64,C.POINTER(U64)],
        'renderer_render_samples':[U64,C.POINTER(Sample),U64,C.POINTER(Vec2),C.c_double,C.POINTER(Report),U64,C.POINTER(U64)]}
        for name,args in signatures.items():fn=getattr(lib,'abc_'+name);fn.argtypes=args;fn.restype=C.c_int32
        lib.abc_last_error.argtypes=[];lib.abc_last_error.restype=C.c_char_p
        lib.abc_status_message.argtypes=[C.c_int32];lib.abc_status_message.restype=C.c_char_p
        self.check(lib.abc_check_abi(1,0));v=_struct(Version);self.check(lib.abc_get_version(C.byref(v)))
        for name,cls in (('sample',Sample),('vec2',Vec2),('report',Report),('options',Options),('model_info',ModelInfo),('state',State),('tensor_info',TensorInfo)):
            if getattr(v,name+'_size')!=C.sizeof(cls):raise RuntimeError(f'{name} ABI layout mismatch')
        self.version=v
        if v.abi_minor >= 1:_configure_observations(self)
        if v.abi_minor >= 2:_configure_segments(self)
        if v.abi_minor >= 4:
            lib.abc_planner_reset_session.argtypes=[U64];lib.abc_planner_reset_session.restype=C.c_int32
        if v.abi_minor >= 5:
            lib.abc_model_load_embedded.argtypes=[U32,C.POINTER(U64)];lib.abc_model_load_embedded.restype=C.c_int32
            lib.abc_renderer_create_embedded.argtypes=[C.POINTER(U64)];lib.abc_renderer_create_embedded.restype=C.c_int32
    def check(self,status:int,written:int=0)->None:
        if status:raise NativeError(status,self.raw.abc_last_error().decode('utf8',errors='replace'),written)

class _Owned:
    _destroy:str
    def _setup(self,library):self.library=library if isinstance(library,Library) else Library(library);self._handle=U64();self._lock=threading.RLock()
    def _open(self):
        if not self._handle.value:raise NativeError(2,'object is closed')
    def close(self)->None:
        with self._lock:
            if self._handle.value:
                self.library.check(getattr(self.library.raw,self._destroy)(self._handle));self._handle=U64()
    def __enter__(self):self._open();return self
    def __exit__(self,*_):self.close()
    def __del__(self):
        try:self.close()
        except Exception:pass

class Model(_Owned):
    _destroy='abc_model_release'
    def __init__(self,directory:os.PathLike|str,*,library=None):
        self._setup(library);self.library.check(self.library.raw.abc_model_load(_path(directory),C.byref(self._handle)))
    @classmethod
    def embedded(cls,commitment_ms:int,*,library=None):
        obj=cls.__new__(cls);obj._setup(library)
        if obj.library.version.abi_minor<5:raise NativeError(7,'embedded models require ABI 1.5')
        if commitment_ms not in (8,16,32):raise ValueError('commitment must be 8, 16, or 32 ms')
        obj.library.check(obj.library.raw.abc_model_load_embedded(commitment_ms,C.byref(obj._handle)))
        return obj
    @property
    def info(self)->ModelInfo:
        with self._lock:
            self._open();out=_struct(ModelInfo);self.library.check(self.library.raw.abc_model_get_info(self._handle,C.byref(out)));return out
    def tensor_info(self,name:str)->TensorInfo:
        with self._lock:
            if '\0' in name:raise ValueError('tensor name contains NUL')
            self._open();out=_struct(TensorInfo);self.library.check(self.library.raw.abc_model_get_tensor_info(self._handle,name.encode(),C.byref(out)));return out

class SampleBuffer:
    """Caller-owned contiguous ABI samples; views keep their backing array alive."""
    def __init__(self,capacity:int):self.array=(Sample*_unsigned(capacity))();self.written=0
    def __len__(self):return self.written
    def __getitem__(self,index):
        if isinstance(index,slice):return self.array[:self.written][index]
        if index<0:index+=self.written
        if not 0<=index<self.written:raise IndexError(index)
        return self.array[index]
    @property
    def capacity(self):return len(self.array)
    def numpy(self):
        """Optional zero-copy structured ndarray; owns a reference to this array."""
        import numpy as np
        dtype=np.dtype([('timestamp_us','<i8'),('position','<f8',(2,)),('displacement','<f8',(2,))])
        return np.frombuffer(self.array,dtype=dtype,count=self.written)

class Planner(_Owned):
    _destroy='abc_planner_destroy'
    def __init__(self,model:Model,*,seed:int=7,initial_position=(0.,0.),history:Iterable|None=None,diagnostic_proposals:bool=False):
        self._setup(model.library);options=Options();self.library.check(self.library.raw.abc_planner_options_init(C.byref(options),C.sizeof(options)))
        options.seed=_unsigned(seed);options.initial_position=_vec(initial_position);options.flags=int(diagnostic_proposals)
        if history is not None:
            rows=[_vec(v) for v in history];storage=(Vec2*len(rows))(*rows);options.history=storage;options.history_count=len(rows)
        with model._lock:
            model._open();self.library.check(self.library.raw.abc_planner_create(model._handle,C.byref(options),C.byref(self._handle)))
    @property
    def model_info(self)->ModelInfo:
        with self._lock:
            self._open();out=_struct(ModelInfo);self.library.check(self.library.raw.abc_planner_get_model_info(self._handle,C.byref(out)));return out
    def reset(self,seed:int=7)->None:
        with self._lock:self._open();self.library.check(self.library.raw.abc_planner_reset(self._handle,_unsigned(seed)))
    def reset_session(self)->None:
        """Reset time/state with the configured seed and prefix (ABI 1.4)."""
        with self._lock:
            self._open()
            if self.library.version.abi_minor<4:raise NativeError(7,'stored-seed planner reset requires ABI 1.4')
            self.library.check(self.library.raw.abc_planner_reset_session(self._handle))
    def reinitialize(self,*,seed:int=7,initial_position=(0.,0.),history:Iterable|None=None,diagnostic_proposals:bool=False)->None:
        _require_observations(self.library)
        options=_struct(Options);options.seed=_unsigned(seed);options.initial_position=_vec(initial_position);options.flags=int(diagnostic_proposals)
        if history is not None:
            rows=[_vec(v) for v in history];storage=(Vec2*len(rows))(*rows);options.history=storage;options.history_count=len(rows)
        with self._lock:self._open();self.library.check(self.library.raw.abc_planner_reinitialize(self._handle,C.byref(options)))
    def set_position(self,position)->None:
        with self._lock:self._open();self.library.check(self.library.raw.abc_planner_set_position(self._handle,_vec(position)))
    def update_target(self,target,receipt_us:int)->None:
        with self._lock:self._open();self.library.check(self.library.raw.abc_planner_update_target(self._handle,_vec(target),_time(receipt_us)))
    def clear_target(self,receipt_us:int)->None:
        with self._lock:self._open();self.library.check(self.library.raw.abc_planner_clear_target(self._handle,_time(receipt_us)))
    @property
    def state(self)->State:
        with self._lock:
            self._open();out=_struct(State);self.library.check(self.library.raw.abc_planner_get_state(self._handle,C.byref(out)));return out
    def advance_into(self,timestamp_us:int,output:SampleBuffer)->int:
        with self._lock:
            self._open();n=U64();status=self.library.raw.abc_planner_advance(self._handle,_time(timestamp_us),output.array,output.capacity,C.byref(n));output.written=n.value;self.library.check(status,n.value);return n.value
    def advance(self,timestamp_us:int)->SampleBuffer:
        with self._lock:
            self._open();n=U64();self.library.check(self.library.raw.abc_planner_required_capacity(self._handle,_time(timestamp_us),C.byref(n)));result=SampleBuffer(n.value);self.advance_into(timestamp_us,result);return result
    def generate_sample(self,timestamp_us:int,output:Sample|None=None)->Sample:
        with self._lock:
            self._open();out=output if output is not None else Sample();self.library.check(self.library.raw.abc_planner_generate_sample(self._handle,_time(timestamp_us),C.byref(out)));return out

class Renderer(_Owned):
    _destroy='abc_renderer_destroy'
    def __init__(self,model_file:os.PathLike|str,*,library=None):
        self._setup(library);self.library.check(self.library.raw.abc_renderer_create(_path(model_file),C.byref(self._handle)))
    @classmethod
    def embedded(cls,*,library=None):
        obj=cls.__new__(cls);obj._setup(library)
        if obj.library.version.abi_minor<5:raise NativeError(7,'embedded Renderer requires ABI 1.5')
        obj.library.check(obj.library.raw.abc_renderer_create_embedded(C.byref(obj._handle)))
        return obj
    def set_profile(self,profile:Iterable)->None:
        rows=[]
        for row in profile:
            x,y=(row.dx,row.dy) if isinstance(row,Report) else row
            if int(x)!=x or int(y)!=y or not(-32768<=x<=32767 and -32768<=y<=32767):raise ValueError('profile requires signed int16 reports')
            rows.append(Report(int(x),int(y)))
        array=(Report*len(rows))(*rows)
        with self._lock:self._open();self.library.check(self.library.raw.abc_renderer_set_profile(self._handle,array,len(rows)))
    def reset(self,seed:int=7)->None:
        with self._lock:self._open();self.library.check(self.library.raw.abc_renderer_reset(self._handle,_unsigned(seed)))
    def step(self,intent,output:Report|None=None)->Report:
        with self._lock:
            self._open();out=output if output is not None else Report();self.library.check(self.library.raw.abc_renderer_step(self._handle,_vec(intent),C.byref(out)));return out
    def step_batch(self,intents,output=None):
        rows=intents if isinstance(intents,C.Array) and intents._type_ is Vec2 else (lambda rows:(Vec2*len(rows))(*rows))([_vec(v) for v in intents])
        out=output if output is not None else (Report*len(rows))();written=U64()
        if not isinstance(out,C.Array) or out._type_ is not Report:raise TypeError('expected Report array')
        with self._lock:
            self._open();status=self.library.raw.abc_renderer_step_batch(self._handle,rows,len(rows),out,len(out),C.byref(written));self.library.check(status,written.value)
        return out
    def render_samples(self,samples:SampleBuffer,previous_position:Vec2,radians_per_native_count:float,output=None):
        out=output if output is not None else (Report*len(samples))();written=U64()
        if not isinstance(out,C.Array) or out._type_ is not Report:raise TypeError('expected Report array')
        with self._lock:
            self._open();status=self.library.raw.abc_renderer_render_samples(self._handle,samples.array,len(samples),C.byref(previous_position),radians_per_native_count,out,len(out),C.byref(written));self.library.check(status,written.value)
        return out

__all__=['Library','Model','Planner','Renderer','SampleBuffer','Sample','Report','Vec2','State','ModelInfo','NativeError','COMMON_RADIANS_PER_COUNT']

# ABI 1.1 independent target observation input.
from dataclasses import dataclass
from enum import IntEnum

class ObservationOrdering(IntEnum):
    STRICT=0
    LATEST=1
    CALLER=2
class TargetAvailability(IntEnum):
    AVAILABLE=0
    UNAVAILABLE=1
    INVALIDATED=2
class _ObservationOptions(C.Structure):
    _fields_=[('struct_size',U32),('ordering',U32),('max_age_us',I64)]
class _Observation(C.Structure):
    _fields_=[(n,U32) for n in ('struct_size','kind','availability','reserved')]+[('capture_time_us',I64),('receipt_time_us',I64),('value',Vec2)]
class ObservationDiagnostics(C.Structure):
    _fields_=[(n,U32) for n in ('struct_size','availability','last_rejection','reserved')]+[(n,I64) for n in ('last_capture_us','last_receipt_us','last_age_us')]+[(n,U64) for n in ('accepted','rejected','duplicates','out_of_order','stale')]+[('resolved_target',Vec2),('reference_position',Vec2)]
@dataclass(frozen=True)
class TargetObservation:
    value: tuple[float,float]
    capture_time_us: int
    receipt_time_us: int
    relative: bool=False
    availability: TargetAvailability=TargetAvailability.AVAILABLE
@dataclass(frozen=True)
class PreparedHistory:
    initial_position: Vec2
    history: C.Array
    observed_position: Vec2

def _require_observations(library):
    if library.version.abi_minor < 1:raise NativeError(7,'observation input requires ABI 1.1')

def _configure_observations(library):
    signatures={
      'observation_options_init':[C.POINTER(_ObservationOptions),U32],
      'observation_create':[U64,C.POINTER(_ObservationOptions),C.POINTER(U64)],
      'observation_destroy':[U64],
      'observation_submit':[U64,C.POINTER(_Observation),C.POINTER(Vec2)],
      'observation_get_diagnostics':[U64,C.POINTER(ObservationDiagnostics)],
      'observation_layout':[C.POINTER(U32),C.POINTER(U32),C.POINTER(U32)],
      'planner_reinitialize':[U64,C.POINTER(Options)],
      'prepare_history':[C.POINTER(Vec2),U64,Vec2,C.POINTER(Vec2),U64,C.POINTER(Vec2)]}
    for name,args in signatures.items():
        fn=getattr(library.raw,'abc_'+name);fn.argtypes=args;fn.restype=C.c_int32
    sizes=[U32(),U32(),U32()]
    library.check(library.raw.abc_observation_layout(*(C.byref(x) for x in sizes)))
    if [x.value for x in sizes]!=[C.sizeof(t) for t in (_ObservationOptions,_Observation,ObservationDiagnostics)]:raise RuntimeError('observation ABI layout mismatch')

class ObservationInput(_Owned):
    """Borrows a planner and shares its lock. Advance/collect through receipt first.

    Relative errors use its latest closed virtual position. Capture time never
    reconstructs movement or compensates for observation delay. Use one input
    handler per planner and avoid legacy future target queues while attached.
    """
    _destroy='abc_observation_destroy'
    def __init__(self,planner:Planner,*,ordering:ObservationOrdering=ObservationOrdering.STRICT,max_age_us:int=250000):
        self._setup(planner.library);_require_observations(self.library)
        self.planner=planner;self._lock=planner._lock
        if not isinstance(max_age_us,int) or not -1<=max_age_us<2**63:raise ValueError('max_age_us must be -1 or a nonnegative int64')
        options=_struct(_ObservationOptions);options.ordering=ObservationOrdering(ordering);options.max_age_us=max_age_us
        with self._lock:
            planner._open();self.library.check(self.library.raw.abc_observation_create(planner._handle,C.byref(options),C.byref(self._handle)))
    def submit(self,observation:TargetObservation)->Vec2:
        o=_struct(_Observation);o.value=_vec(observation.value);o.capture_time_us=_time(observation.capture_time_us);o.receipt_time_us=_time(observation.receipt_time_us);o.kind=int(observation.relative);o.availability=TargetAvailability(observation.availability)
        with self._lock:
            self._open();self.planner._open();out=Vec2();self.library.check(self.library.raw.abc_observation_submit(self._handle,C.byref(o),C.byref(out)));return out
    def relative(self,error,capture_time_us:int,receipt_time_us:int)->Vec2:
        return self.submit(TargetObservation(error,capture_time_us,receipt_time_us,relative=True))
    def absolute(self,target,capture_time_us:int,receipt_time_us:int)->Vec2:
        return self.submit(TargetObservation(target,capture_time_us,receipt_time_us))
    def unavailable(self,capture_time_us:int,receipt_time_us:int,*,invalidate:bool=False)->None:
        self.submit(TargetObservation((0.,0.),capture_time_us,receipt_time_us,availability=TargetAvailability.INVALIDATED if invalidate else TargetAvailability.UNAVAILABLE))
    @property
    def diagnostics(self)->ObservationDiagnostics:
        with self._lock:
            self._open();self.planner._open();out=_struct(ObservationDiagnostics);self.library.check(self.library.raw.abc_observation_get_diagnostics(self._handle,C.byref(out)));return out

def prepare_history(raw_displacements:Iterable,*,current_position,library=None)->PreparedHistory:
    """Original W5 preprocessing, including filtered anchor; initialization only."""
    lib=library if isinstance(library,Library) else Library(library);_require_observations(lib)
    rows=[_vec(v) for v in raw_displacements]
    if len(rows)!=164:raise ValueError('exactly 164 raw displacement pairs required')
    raw=(Vec2*164)(*rows);prepared=(Vec2*160)();anchor=Vec2();current=_vec(current_position)
    lib.check(lib.raw.abc_prepare_history(raw,164,current,prepared,160,C.byref(anchor)))
    return PreparedHistory(anchor,prepared,current)

__all__+=['ObservationInput','ObservationOrdering','TargetAvailability','TargetObservation','ObservationDiagnostics','PreparedHistory','prepare_history']

# ABI 1.2 complete commitments and asynchronous observations.
class _SegmentOptions(C.Structure):
    _fields_=[(n,U32) for n in ('struct_size','ordering','flags','reserved')]+[(n,I64) for n in ('max_age_us','inactivity_timeout_us')]+[('observation_capacity',U32),('reserved2',U32)]
class SegmentMetadata(C.Structure):
    _fields_=[(n,U32) for n in ('struct_size','mode','sample_count','fresh_planning_decision','variant_ms','motor_inference')]+[('selected_head',C.c_int32),('selected_brake_head',C.c_int32),('segment_id',U64)]+[(n,I64) for n in ('start_timestamp_us','end_timestamp_us','first_sample_timestamp_us','last_sample_timestamp_us','duration_us')]+[('initial_position',Vec2),('final_position',Vec2)]
class SegmentDiagnostics(C.Structure):
    _fields_=[(n,U32) for n in ('struct_size','availability','last_rejection','pending_observations')]+[(n,U64) for n in ('total_segments','fresh_planning_decisions','decisions_with_new_observation','decisions_without_new_observation','stale_observations','duplicate_observations','out_of_order_observations','target_unavailable_events','target_timeout_events','segment_generation_errors','missed_generation_deadlines','accepted_observations','rejected_observations','queue_full_observations')]+[(n,I64) for n in ('last_observation_age_us','last_observation_receipt_us','last_capture_us','last_receipt_us','last_apply_us')]+[('last_planning_latency_us',C.c_double),('resolved_target',Vec2),('reference_position',Vec2)]
class SegmentRecoveryDiagnostics(C.Structure):
    _fields_=[('struct_size',U32),('pending_observations',U32),('last_error_status',C.c_int32),('reserved',U32)]+[(n,U64) for n in ('recoverable_observation_errors','invalid_pending_observations','nonfinite_relative_resolutions','stale_pending_observations','explicit_pending_clears')]+[(n,I64) for n in ('last_error_capture_us','last_error_receipt_us')]
class SegmentState(C.Structure):
    _fields_=[(n,U32) for n in ('struct_size','failure_category','failure_reason','reset_required')]+[('failure_status',C.c_int32),('reserved',U32)]+[(n,U64) for n in ('generation_attempts','last_segment_id')]+[(n,I64) for n in ('numerical_time_us','requested_time_us','last_attempt_start_us','last_attempt_end_us','last_segment_start_us','last_segment_end_us','relative_origin_earliest_us','relative_origin_latest_us','last_origin_error_receipt_us')]+[('error_message',C.c_char*256)]

@dataclass(frozen=True)
class Segment:
    metadata: SegmentMetadata
    samples: SampleBuffer

def _configure_segments(library):
    signatures={
      'segment_options_init':[C.POINTER(_SegmentOptions),U32],
      'segment_layout':[C.POINTER(U32),C.POINTER(U32),C.POINTER(U32)],
      'segment_create':[U64,C.POINTER(_SegmentOptions),C.POINTER(U64)],
      'segment_destroy':[U64],
      'segment_submit_observation':[U64,C.POINTER(_Observation)],
      'segment_required_capacity':[U64,C.POINTER(U64)],
      'segment_next':[U64,C.POINTER(Sample),U64,C.POINTER(U64),C.POINTER(SegmentMetadata)],
      'segment_get_metadata':[U64,C.POINTER(SegmentMetadata)],
      'segment_get_diagnostics':[U64,C.POINTER(SegmentDiagnostics)],
      'segment_reset_diagnostics':[U64],'segment_reset_session':[U64],
      'segment_render':[U64,C.POINTER(Sample),U64,C.POINTER(SegmentMetadata),C.c_double,C.POINTER(Report),U64,C.POINTER(U64)]}
    if library.version.abi_minor>=3:
        signatures.update({'segment_recovery_layout':[C.POINTER(U32)],'segment_get_recovery_diagnostics':[U64,C.POINTER(SegmentRecoveryDiagnostics)],'segment_clear_pending_observations':[U64]})
        size=U32()
        fn=library.raw.abc_segment_recovery_layout;fn.argtypes=[C.POINTER(U32)];fn.restype=C.c_int32
        library.check(fn(C.byref(size)))
        if size.value!=C.sizeof(SegmentRecoveryDiagnostics):raise RuntimeError('recovery ABI layout mismatch')
    for name,args in signatures.items():
        fn=getattr(library.raw,'abc_'+name);fn.argtypes=args;fn.restype=C.c_int32
    if library.version.abi_minor>=4:
        library.raw.abc_segment_state_layout.argtypes=[C.POINTER(U32)];library.raw.abc_segment_state_layout.restype=C.c_int32
        library.raw.abc_segment_get_state.argtypes=[U64,C.POINTER(SegmentState)];library.raw.abc_segment_get_state.restype=C.c_int32
        size=U32();library.check(library.raw.abc_segment_state_layout(C.byref(size)))
        if size.value!=C.sizeof(SegmentState):raise RuntimeError('segment state ABI layout mismatch')
    sizes=[U32(),U32(),U32()]
    library.check(library.raw.abc_segment_layout(*(C.byref(x) for x in sizes)))
    if [x.value for x in sizes]!=[C.sizeof(t) for t in (_SegmentOptions,SegmentMetadata,SegmentDiagnostics)]:raise RuntimeError('segment ABI layout mismatch')

class SegmentController(_Owned):
    """Pull one complete native commitment. Holds a planner reference and lock.

    Observations may arrive while the last segment is consumed externally.
    Relative input refers to the closed virtual position at recorded receipt;
    receipts older than the last returned segment cannot resolve relative input.
    Serialize calls and use this as the planner's exclusive input/output path.
    """
    _destroy='abc_segment_destroy'
    def __init__(self,planner:Planner,*,ordering:ObservationOrdering=ObservationOrdering.LATEST,max_age_us:int=250000,inactivity_timeout_us:int=-1,observation_capacity:int=64):
        self._setup(planner.library)
        if self.library.version.abi_minor<2:raise NativeError(7,'segments require ABI 1.2')
        self.planner=planner;self._lock=planner._lock
        for value in (max_age_us,inactivity_timeout_us):
            if not isinstance(value,int) or not -1<=value<2**63:raise ValueError('timeout/age must be -1 or a nonnegative int64')
        if not isinstance(observation_capacity,int) or not 1<=observation_capacity<=64:raise ValueError('capacity must be 1..64')
        options=_struct(_SegmentOptions);options.ordering=ObservationOrdering(ordering);options.max_age_us=max_age_us;options.inactivity_timeout_us=inactivity_timeout_us;options.observation_capacity=observation_capacity
        with self._lock:
            planner._open();self.library.check(self.library.raw.abc_segment_create(planner._handle,C.byref(options),C.byref(self._handle)))
    def submit(self,observation:TargetObservation)->None:
        o=_struct(_Observation);o.value=_vec(observation.value);o.capture_time_us=_time(observation.capture_time_us);o.receipt_time_us=_time(observation.receipt_time_us);o.kind=int(observation.relative);o.availability=TargetAvailability(observation.availability)
        with self._lock:
            self._open();self.planner._open();self.library.check(self.library.raw.abc_segment_submit_observation(self._handle,C.byref(o)))
    def relative(self,error,capture_time_us:int,receipt_time_us:int)->None:
        self.submit(TargetObservation(error,capture_time_us,receipt_time_us,relative=True))
    def absolute(self,target,capture_time_us:int,receipt_time_us:int)->None:
        self.submit(TargetObservation(target,capture_time_us,receipt_time_us))
    def unavailable(self,capture_time_us:int,receipt_time_us:int,*,invalidate:bool=False)->None:
        self.submit(TargetObservation((0.,0.),capture_time_us,receipt_time_us,availability=TargetAvailability.INVALIDATED if invalidate else TargetAvailability.UNAVAILABLE))
    def next_segment_into(self,output:SampleBuffer)->SegmentMetadata:
        with self._lock:
            self._open();self.planner._open();m=_struct(SegmentMetadata);n=U64()
            status=self.library.raw.abc_segment_next(self._handle,output.array,output.capacity,C.byref(n),C.byref(m));output.written=n.value;self.library.check(status,n.value);return m
    def next_segment(self)->Segment:
        with self._lock:
            samples=SampleBuffer(self.required_capacity());return Segment(self.next_segment_into(samples),samples)
    def required_capacity(self)->int:
        with self._lock:
            self._open();self.planner._open();n=U64();self.library.check(self.library.raw.abc_segment_required_capacity(self._handle,C.byref(n)));return n.value
    @property
    def state(self)->SegmentState:
        """Failure snapshot and exact relative-origin window; no generation."""
        with self._lock:
            self._open();self.planner._open()
            if self.library.version.abi_minor<4:raise NativeError(7,'segment state requires ABI 1.4')
            out=_struct(SegmentState);self.library.check(self.library.raw.abc_segment_get_state(self._handle,C.byref(out)));return out
    @property
    def metadata(self)->SegmentMetadata:
        with self._lock:
            self._open();self.planner._open();m=_struct(SegmentMetadata);self.library.check(self.library.raw.abc_segment_get_metadata(self._handle,C.byref(m)));return m
    @property
    def diagnostics(self)->SegmentDiagnostics:
        with self._lock:
            self._open();self.planner._open();d=_struct(SegmentDiagnostics);self.library.check(self.library.raw.abc_segment_get_diagnostics(self._handle,C.byref(d)));return d
    def reset_diagnostics(self)->None:
        with self._lock:
            self._open();self.planner._open();self.library.check(self.library.raw.abc_segment_reset_diagnostics(self._handle))
    def clear_pending_observations(self)->None:
        with self._lock:
            self._open();self.planner._open()
            if self.library.version.abi_minor<3:raise NativeError(7,'pending recovery requires ABI 1.3')
            self.library.check(self.library.raw.abc_segment_clear_pending_observations(self._handle))
    @property
    def recovery_diagnostics(self)->SegmentRecoveryDiagnostics:
        with self._lock:
            self._open();self.planner._open()
            if self.library.version.abi_minor<3:raise NativeError(7,'pending recovery requires ABI 1.3')
            d=_struct(SegmentRecoveryDiagnostics);self.library.check(self.library.raw.abc_segment_get_recovery_diagnostics(self._handle,C.byref(d)));return d
    def reset_segment_session(self)->None:
        with self._lock:
            self._open();self.planner._open();self.library.check(self.library.raw.abc_segment_reset_session(self._handle))

def _render_segment(self,segment:Segment,radians_per_native_count:float,output=None):
    out=output if output is not None else (Report*len(segment.samples))();written=U64()
    if not isinstance(out,C.Array) or out._type_ is not Report:raise TypeError('expected Report array')
    with self._lock:
        self._open();self.library.check(self.library.raw.abc_segment_render(self._handle,segment.samples.array,len(segment.samples),C.byref(segment.metadata),radians_per_native_count,out,len(out),C.byref(written)),written.value)
    return out
Renderer.render_segment=_render_segment
__all__+=['SegmentController','Segment','SegmentMetadata','SegmentDiagnostics','SegmentRecoveryDiagnostics','SegmentState']
