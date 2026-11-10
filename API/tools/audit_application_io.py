"""Contract inventory/probes; writes only documentation/results.

The C declarations and binding signatures are extracted, not maintained copies.
Semantic annotations were checked against the native and C implementations.
Run with Python 3.10+ and the current binding/native DLL available.
"""
from __future__ import annotations
import ast
import argparse
import ctypes as C
import hashlib
import inspect
import json
import os
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'build/api-reference'
DOC = OUT
HEADER = 'API/c/include/abcurves.h'
SOURCE = 'API/c/src/abcurves.cpp'

def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')

def no_comments(text):
    return re.sub(r'/\*.*?\*/|//[^\n]*', '', text, flags=re.S)

def c_tokens(text):
    # Remove logical preprocessor lines BEFORE looking for decorations. The
    # former regex started at '#define ABC_API' and consumed the whole header
    # up to the first actual ABC_CALL declaration.
    text = re.sub(r'\\\r?\n', '', text)
    text = re.sub(r'^\s*#[^\n]*', '', text, flags=re.M)
    pattern = r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|[A-Za-z_]\w*|0[xX][0-9a-fA-F]+[uUlL]*|\d+[uUlL]*|[^\s]'
    return [m[0] for m in re.finditer(pattern,text,re.S) if not m[0].startswith(('/*','//'))]

def c_type(tokens):
    assert tokens and all(re.fullmatch(r'[A-Za-z_]\w*|\*|\[|\]|\d+', t) for t in tokens), tokens
    result=' '.join(tokens)
    return re.sub(r'\[ (\d+) \]',r'[\1]',result).replace(' [','[')

def parse_header(text):
    tokens=c_tokens(text); operations=[]; structures={}; i=0
    while i<len(tokens):
        if tokens[i]=='ABC_API':
            end=tokens.index(';',i)
            declaration=tokens[i+1:end]
            assert not set(declaration)&{'typedef','enum','{','}','#','ABC_API'}, declaration
            call=declaration.index('ABC_CALL')
            ret=c_type(declaration[:call]); name=declaration[call+1]
            assert re.fullmatch(r'abc_\w+',name) and declaration[call+2]=='(' and declaration[-1]==')'
            assert ret in ('abc_status','const char *'), (name,ret)
            args=[]; group=[]
            for t in declaration[call+3:-1]+[',']:
                if t!=',': group.append(t); continue
                if group and group!=['void']:
                    assert re.fullmatch(r'[A-Za-z_]\w*',group[-1]),group
                    args.append(dict(name=group[-1],type=c_type(group[:-1])))
                group=[]
            operations.append(dict(name=name,return_type=ret,parameters=args)); i=end+1
        elif tokens[i:i+2]==['typedef','struct']:
            name=tokens[i+2]; assert tokens[i+3]=='{'; end=tokens.index('}',i+4)
            assert tokens[end+1:end+3]==[name,';'],name
            rows=[]; group=[]
            for t in tokens[i+4:end]:
                if t!=';':group.append(t);continue
                qualifiers=[]
                while group[0]=='const':qualifiers.append(group.pop(0))
                base=qualifiers+[group.pop(0)]; piece=[]
                for p in group+[',']:
                    if p!=',':piece.append(p);continue
                    at=0
                    while piece[at]=='*':at+=1
                    field=piece[at]; assert re.fullmatch(r'[A-Za-z_]\w*',field)
                    rows.append((field,c_type(base+piece[:at]+piece[at+1:])));piece=[]
                group=[]
            assert not group and name not in structures
            structures[name]=rows; i=end+3
        else:i+=1
    assert len({o['name'] for o in operations})==len(operations), 'duplicate export'
    return operations,structures

def fields():
    return parse_header((ROOT/HEADER).read_text())[1]

def parameter_contract(short,name,typ,structures):
    pointer='*' in typ; base=typ.replace('const','').replace('*','').strip()
    optional=name=='options' and short in ('planner_create','planner_reinitialize','observation_create','segment_create')
    direction='in'
    if pointer and 'const' not in typ:
        direction='out'
        if name=='previous_position' or (base in structures and ('struct_size','uint32_t') in structures[base] and not short.endswith('options_init')):
            direction='inout'
    category='input_scalar'; ownership='value'; validity='valid value of declared type'; lifetime='copied value; see operation effects'; nullable=False; capacity=None
    if base.endswith('_handle'):
        if pointer:
            category='output_handle_address';ownership='caller owns writable storage and returned object token'
            validity='writable address for a newly created handle; no pre-existing live token required'
            lifetime='storage needed only during call; returned handle lives until matching destroy/release'
        else:
            category='input_handle';ownership='caller-managed library token, borrowed for call'
            validity='live same-library token of expected type; no concurrent destruction'
            lifetime='must remain live throughout call; child borrow follows documented parent lifetime'
    elif pointer:
        ownership='caller';lifetime='call only; not retained'
        if base=='char':
            assert 'const' in typ;category='borrowed_string';validity='readable NUL-terminated UTF-8 string; nonempty model path'
        elif optional:
            category='optional_pointer';nullable=True;validity='null selects defaults; otherwise readable initialized size-tagged options'
        elif name in ('raw','profile','intents','samples','prepared','output'):
            category='input_buffer' if direction=='in' else 'output_buffer'
            validity='aligned nonoverlapping storage readable/writable as indicated for the entire claimed element count'
            capacity='raw=164; prepared>=160; profile=256; other buffers use count/capacity arguments; segment output>=model commitment'
            nullable=name in ('intents','samples','output') and short in ('planner_advance','renderer_step_batch','renderer_render_samples','segment_render')
        elif base in structures:
            category='input_output_structure' if direction=='inout' else 'output_structure' if direction=='out' else 'input_structure'
            if name=='previous_position':
                validity='readable/writable finite Vec2 anchor';nullable=True
            elif ('struct_size','uint32_t') in structures[base]:
                validity='writable structure with struct_size>=sizeof(known struct)' if direction=='inout' else 'writable options; initializer receives struct size separately' if direction=='out' else 'readable initialized size-tagged structure with documented reserved fields zero'
            else:validity='writable vector/report structure; no size tag' if direction=='out' else 'readable initialized structure'
        else:
            assert base in ('uint32_t','uint64_t'), (short,name,typ)
            category='output_scalar_address';validity='writable aligned scalar storage; no initial scalar value required'
    nullability_rule='null selects documented defaults' if optional else 'null allowed only when associated element count is zero' if nullable else 'non-null pointer required' if pointer else 'not a pointer'
    return dict(direction=direction,category=category,ownership=ownership,validity=validity,lifetime=lifetime,nullable=nullable,nullability_rule=nullability_rule,capacity_rule=capacity)

OP = {
 'get_version':'Copy runtime versions, capabilities and base ABI layout sizes.',
 'check_abi':'Require ABI major 1 and runtime minor >= requested minimum.',
 'status_message':'Static generic status text; unknown statuses map to internal error text.',
 'last_error':'Borrow thread-local diagnostic text, valid until the next status-returning call on this thread.',
 'model_load':'Load and validate model.abcmodel plus manifest.json; variant derived from bundle.',
 'model_load_embedded':'Load and validate the DLL-embedded 8, 16 or 32 ms bundle.',
 'model_get_info':'Copy model identity and fixed validated architecture metadata.',
 'model_get_tensor_info':'Copy named tensor dtype, rank, dimensions and bytes; no data pointer.',
 'model_release':'Release public model token; existing planners retain shared immutable ownership.',
 'planner_options_init':'Initialize size tag, seed 7, zero position/flags and empty history.',
 'planner_create':'Create independent planner; null options selects native defaults; history copied.',
 'planner_destroy':'Destroy planner token; borrowing child operations subsequently fail.',
 'planner_reset':'Explicit full zero-time reset using supplied seed and stored position/prefix/options.',
 'planner_set_position':'Overwrite virtual position only; does not realign history or commitment points. Advanced legacy operation, rejected while controller attached.',
 'planner_update_target':'Queue absolute target in legacy chronological deque; no bounded async input policy; rejected while controller attached.',
 'planner_clear_target':'Queue legacy target loss; current commitment remains authoritative; rejected while controller attached.',
 'planner_get_model_info':'Copy retained model metadata even after model token release.',
 'planner_get_state':'Copy planner state, including the native generation failure latch; safe after failure.',
 'planner_required_capacity':'Calculate floor((timestamp_us - numerical_time_us)/1000); does not advance.',
 'planner_advance':'Generate all closed 1 ms endpoints through timestamp_us; fractional request also advances requested-time watermark.',
 'planner_generate_sample':'Require exactly one newly closed endpoint and emit it.',
 'renderer_create':'Load separate combined C99 Renderer model file; no profile or active event yet.',
 'renderer_create_embedded':'Load the Renderer model embedded in the Windows DLL.',
 'renderer_destroy':'Release independent persistent Renderer.',
 'renderer_set_profile':'Copy exactly 256 chronological native-count X-right/Y-up reports and prepare profile.',
 'renderer_reset':'Reprepare stored profile and begin event with supplied seed; resets accumulators.',
 'renderer_step':'Render one native-count intent into one int16 report.',
 'renderer_step_batch':'Validate all intents first; render one report per intent; runtime failures can return partial written count.',
 'renderer_render_samples':'Convert differences of absolute sample positions with scalar radians-per-native-count gain; update previous position.',
 'observation_options_init':'Initialize direct Step 3 strict order and max_age_us 250000.',
 'observation_create':'Borrow checked parent planner token for direct observation resolution.',
 'observation_destroy':'Destroy input handle only.',
 'observation_submit':'Resolve immediately in current closed-sample receipt interval, queue legacy event, return resolved target.',
 'observation_get_diagnostics':'Copy direct-handler diagnostics; lazily synchronize parent reset revision.',
 'observation_layout':'Copy exact sizes of three Step 3 structures.',
 'planner_reinitialize':'Strong invalid-options guarantee; full replacement from defaults or new initialization options.',
 'prepare_history':'Filter 164 raw 1 ms common-count displacements with W5; return 160 samples and filtered anchor.',
 'segment_options_init':'Initialize latest order, max age 250000 us, timeout disabled (-1), capacity 64 and zero reserved flags.',
 'segment_layout':'Copy sizes of segment options, metadata and diagnostics.',
 'segment_create':'Exclusively attach to parent at closed numerical boundary without queued legacy targets; second controller rejected.',
 'segment_destroy':'Release exclusive planner mutation domain and destroy controller; does not reset planner or Renderer.',
 'segment_submit_observation':'Accept into bounded pending history; no movement/time/RNG advance; resolution may be deferred.',
 'segment_required_capacity':'Return model commitment 32/16/8; no timestamp or requested segment length.',
 'segment_next':'Apply eligible pending observations when no remainder exists; emit full next commitment, remainder, or one idle sample.',
 'segment_get_metadata':'Copy last successful segment metadata; error before first segment.',
 'segment_get_diagnostics':'Copy counters and latest accepted/applied state; may synchronize after parent reset.',
 'segment_reset_diagnostics':'Clear counters, retain gauges, input ordering, pending records, IDs and planner state.',
 'segment_reset_session':'Explicit planner reset with stored seed/prefix; clear controller session; Renderer untouched.',
 'segment_render':'Render samples with metadata initial anchor using persistent C Renderer; no controller handle needed.',
 'segment_recovery_layout':'Copy size of additive ABI 1.3 recovery snapshot.',
 'segment_get_recovery_diagnostics':'Copy recoverable pending-rejection counters/status/timestamps and pending count.',
 'segment_clear_pending_observations':'Drop pending input/fingerprints, recompute receipt/capture watermarks; preserve applied target and planner state.',
 'planner_reset_session':'Explicit zero-time reset retaining configured seed/prefix; attached controller synchronizes on next call.',
 'segment_state_layout':'Copy size of additive ABI 1.4 failure/reference-window snapshot.',
 'segment_get_state':'Copy latched failure, attempt/last-success timings and exact retained relative-origin window, including after failure.',
}

MEANING = {
 'x':'X coordinate, right positive.', 'y':'Y coordinate, up positive.',
 'dx':'Signed integer native X report delta, right positive.', 'dy':'Signed integer native Y report delta, up positive.',
 'timestamp_us':'Closed 1 ms numerical endpoint; segment samples occupy (start,end].',
 'position':'Absolute independent virtual angular position, not physical device position.',
 'displacement':'One-ms learned displacement relative to preceding sample (first relative to segment initial position); floating rounding may differ from subtracting absolute positions.',
 'struct_size':'Caller initializes to sizeof(struct); runtime preserves provided tag and writes known prefix only.',
 'reserved':'Reserved, zero in initialized/copied structures.', 'reserved2':'Reserved, zero required on segment input.',
 'api_version':'Encoded API 0x00010005.', 'abi_major':'C ABI major 1.', 'abi_minor':'C ABI minor 5.',
 'engine_major':'Native engine major 0.', 'engine_minor':'Native engine minor 2.', 'engine_patch':'Native engine patch 0.',
 'model_format_version':'Supported native planner model format 1.',
 'sample_size':'sizeof(abc_sample).', 'vec2_size':'sizeof(abc_vec2).', 'report_size':'sizeof(abc_report).',
 'options_size':'sizeof(abc_planner_options).', 'model_info_size':'sizeof(abc_model_info).',
 'state_size':'sizeof(abc_planner_state).', 'tensor_info_size':'sizeof(abc_tensor_info).',
 'capabilities':'Runtime mask 255 in ABI 1.4 version (includes hardening128); model metadata mask13 (planner/batch/history), not runtime extension mask.',
 'commitment_ms':'Model-defined fixed learned commitment: 32, 16 or 8 ms.',
 'history_samples':'640 retained 1 ms samples.', 'prefix_samples':'160 prepared initialization displacements.',
 'forecast_samples':'128 decoder forecast samples; not returned commitment length.',
 'coarse_tokens':'70 motor coarse history tokens.', 'coarse_features':'9 features per coarse token.',
 'hidden_units':'96 neural hidden units.', 'heads':'16 learned selection heads.',
 'basis_coefficients':'21 basis coefficients.', 'tensor_count':'43 validated tensors.',
 'variant':'NUL-terminated native identity continuous-32, continuous-16 or continuous-8; manifest string additionally ends ms.',
 'sha256':'64 hexadecimal characters plus terminator, validated model binary SHA-256.',
 'dtype':'1 float32, 2 float64 tensor storage.', 'rank':'Tensor rank 1..8.',
 'dimensions':'Eight slots; first rank dimensions meaningful, unused slots zero.', 'bytes':'Tensor byte size.',
 'flags':'Planner bit 1 diagnostic proposals; segment flags must be zero.',
 'seed':'Planner accepts uint64, consumes low 32 bits to seed three deterministic streams. Renderer retains uint64 event seed.',
 'initial_position':'Finite virtual origin (0,0) by default; with raw history use filtered anchor, not observed anchor.',
 'history':'Optional chronological 160 prepared 1 ms displacement pairs ending at initial_position; copied at initialization.',
 'history_count':'0 or 160; pointer required only for 160.',
 'mode':'0 moving, 1 braking, 2 holding. Snapshot is final learned mode, not per-sample labeling.',
 'numerical_time_us':'Latest generated closed endpoint; starts at zero and advances with returned movement.',
 'requested_time_us':'Last successful/requested legacy time watermark or synchronized successful segment request; may lead numerical time.',
 'last_decision_us':'Boundary of latest successful learned decision; -1 before any.',
 'last_target_receipt_us':'Receipt of current available target; -1 if unavailable.',
 'decision_count':'Successful learned decisions since planner reset.',
 'motor_evaluations':'Motor inference count, distinct from policy decisions.',
 'samples_emitted':'All generated closed samples, including zero idle samples.',
 'selected_head':'Latest selected motor head 0..15 or -1; buffered/idle segments can retain earlier head.',
 'selected_brake_head':'Latest brake head 0..15 or -1; not necessarily new on buffered/idle segments.',
 'pending_samples':'Remaining generated commitment samples in core, 0..variant_ms.',
 'failed':'Latched native or C-wrapper generation failure; 1 requires explicit reset, including segment-origin failure.',
 'ordering':'0 strict, 1 latest, 2 caller; all require nondecreasing receipts. Latest does not discard intermediate input.',
 'max_age_us':'Receipt minus capture limit (default 250000); -1 disables this check only.',
 'kind':'0 absolute angular target; 1 relative angular error.',
 'availability':'0 available, 1 unavailable, 2 invalidated; latter two share target-loss behavior.',
 'capture_time_us':'Original nonnegative capture timestamp in session microseconds; no physical-motion reconstruction.',
 'receipt_time_us':'Original nonnegative actual receipt, capture<=receipt; determines causal eligibility/reference origin.',
 'value':'Finite angular target/error vector even for unavailable input; use (0,0) for loss.',
 'last_capture_us':'Most recently accepted capture watermark; segment removal can rewind it to surviving input/applied observation; -1 initially.',
 'last_receipt_us':'Most recently accepted receipt watermark, including future pending input; -1 initially.',
 'last_age_us':'Direct Step 3 receipt minus capture at accepted submission; -1 initially.',
 'accepted':'Lifetime direct-handler accepted submissions since reset.', 'rejected':'Direct-handler observation rejections since reset; pre-handler ABI errors excluded.',
 'duplicates':'Direct-handler duplicate rejection count.', 'out_of_order':'Direct-handler order rejection count.', 'stale':'Direct-handler age rejection count.',
 'last_rejection':'ObservationRejection 0 none,1 invalid,2 timestamp,3 stale,4 duplicate,5 order; not a C status.',
 'resolved_target':'Latest applied absolute angular target; (0,0) for loss in controller/direct handler.',
 'reference_position':'Virtual position used for latest resolution; receipt origin for relative input; not a physical or capture-time position.',
 'inactivity_timeout_us':'Optional boundary-time minus last applied receipt timeout; strict > comparison; -1 disabled.',
 'observation_capacity':'Pending queue limit 1..64; default 64, fixed maximum storage.',
 'sample_count':'Actual written prefix length 1..variant_ms; can be remainder or one idle sample.',
 'fresh_planning_decision':'1 if decision count increased during this segment, otherwise 0.',
 'variant_ms':'Model nominal commitment length 32/16/8, also maximum sample capacity.',
 'motor_inference':'1 if motor evaluation count increased during this segment, otherwise 0.',
 'segment_id':'Successful segment ID starting 1; increments per segment, reset by session/parent reset; diagnostics reset does not reset ID.',
 'start_timestamp_us':'Open start boundary, equal to numerical time before generation.',
 'end_timestamp_us':'Closed final boundary, equal to last returned sample timestamp.',
 'first_sample_timestamp_us':'start_timestamp_us + 1000.', 'last_sample_timestamp_us':'end_timestamp_us.',
 'duration_us':'end-start = sample_count*1000.', 'final_position':'Virtual position after final sample; next segment initial position under exclusive use.',
 'total_segments':'Successful next_segment calls since diagnostics/session reset.',
 'fresh_planning_decisions':'Segments containing a new learned decision since counter reset.',
 'decisions_with_new_observation':'Fresh decisions with >=1 applied observation since prior decision; loss also counts as observation.',
 'decisions_without_new_observation':'Fresh decisions with no newly applied input; normal gap diagnostic.',
 'stale_observations':'Submission stale rejections plus deferred retained-history expiry.',
 'duplicate_observations':'Submission duplicate rejections.', 'out_of_order_observations':'Submission capture/receipt order rejections.',
 'target_unavailable_events':'Applied unavailable/invalidated records plus automatic timeout events.',
 'target_timeout_events':'Automatic inactivity expirations at eligible boundaries, once until target reacquisition.',
 'segment_generation_errors':'Exceptions inside pending application/planner advance; does not count preflight capacity/overflow/mixing failures or recovered input errors.',
 'missed_generation_deadlines':'Always zero: no application deadline/physical execution clock input exists.',
 'accepted_observations':'Intake acceptance counter; includes later-deferred rejections.',
 'rejected_observations':'Submission rejections and recovered pending rejections; queue full included. ABI argument failures excluded.',
 'queue_full_observations':'New input refused at configured capacity; existing pending records retained.',
 'last_observation_age_us':'At last eligible application boundary: boundary minus latest applied capture (even explicit loss); not a continuously updated clock.',
 'last_observation_receipt_us':'Latest applied original receipt; differs from pending last_receipt_us.',
 'last_apply_us':'Logical boundary at which latest input was applied; supplied internally; -1 initially.',
 'last_planning_latency_us':'Wall duration of latest successful next_segment pending application plus full advance/drain; not isolated neural time, not output schedule latency.',
 'pending_observations':'Current queued observation count, including future records.',
 'last_error_status':'Existing C status 0 initially,1 invalid deferred sum,9 missing relative origin,15 expired history; persists across diagnostics reset.',
 'recoverable_observation_errors':'Recovered application-time observation errors; no planner failure implied.',
 'invalid_pending_observations':'All removed invalid pending records (including timestamp/stale); currently same total as recoverable_observation_errors.',
 'nonfinite_relative_resolutions':'Finite relative input plus finite deferred origin overflow count.',
 'stale_pending_observations':'Accepted records aging outside 640 ms history before application.',
 'explicit_pending_clears':'Calls to pending-only clear, including empty clears.',
 'last_error_capture_us':'Original capture of latest automatically rejected pending record; -1 initially.',
 'last_error_receipt_us':'Original receipt of latest automatically rejected pending record; -1 initially.',
 'failure_category':'0 none,1 numerical,2 inference,3 resource,4 internal; first latched generation cause.',
 'failure_reason':'0 none,1 nonfinite policy,2 invalid primitive,3 invalid probabilities,4 allocation,5 native exception,6 unknown.',
 'reset_required':'1 after genuine generation failure; explicit session/seed reset or reinitialize required; never automatic.',
 'failure_status':'0 healthy;13 genuine numerical/inference/internal failure;11 generation allocation failure. Snapshot query itself returns0.',
 'generation_attempts':'Generation calls entering pending application/advance since session reset; preflight rejection excluded.',
 'last_segment_id':'ID of last successfully returned segment,0 before success; failure does not replace it.',
 'last_attempt_start_us':'Numerical boundary at latest generation attempt;-1 before any.',
 'last_attempt_end_us':'Requested end of latest attempt;-1 if no attempt or failure before advance.',
 'last_segment_start_us':'Open start of last successful segment;-1 before success.',
 'last_segment_end_us':'Closed end of last successful segment;-1 before success.',
 'relative_origin_earliest_us':'Earliest inclusive historical relative receipt supported by latest segment/current boundary.',
 'relative_origin_latest_us':'Latest inclusive historical receipt with an existing origin, equal to numerical time; future receipts may queue.',
 'last_origin_error_receipt_us':'Most recent receipt rejected specifically for missing retained relative origin;-1 before any.',
 'error_message':'First latched generation reason, NUL-terminated fixed256 bytes (at most255 message bytes);empty while healthy.',
}

STRUCT_APIS = {
 'abc_vec2':['abc_planner_set_position','abc_planner_update_target','abc_renderer_step','abc_observation_submit','abc_prepare_history'],
 'abc_sample':['abc_planner_advance','abc_planner_generate_sample','abc_segment_next'],
 'abc_report':['abc_renderer_set_profile','abc_renderer_step','abc_renderer_step_batch','abc_renderer_render_samples','abc_segment_render'],
 'abc_version_info':['abc_get_version'], 'abc_model_info':['abc_model_get_info','abc_planner_get_model_info'],
 'abc_tensor_info':['abc_model_get_tensor_info'], 'abc_planner_options':['abc_planner_options_init','abc_planner_create','abc_planner_reinitialize'],
 'abc_planner_state':['abc_planner_get_state'],
 'abc_observation_options':['abc_observation_options_init','abc_observation_create'],
 'abc_target_observation':['abc_observation_submit','abc_segment_submit_observation'],
 'abc_observation_diagnostics':['abc_observation_get_diagnostics'],
 'abc_segment_options':['abc_segment_options_init','abc_segment_create'],
 'abc_segment_metadata':['abc_segment_next','abc_segment_get_metadata'],
 'abc_segment_diagnostics':['abc_segment_get_diagnostics'],
 'abc_segment_recovery_diagnostics':['abc_segment_get_recovery_diagnostics'],
 'abc_segment_state':['abc_segment_get_state'],
}
DEFAULTS = {
 'abc_planner_options':dict(flags=0,seed=7,initial_position=[0,0],history=None,history_count=0),
 'abc_observation_options':dict(ordering=0,max_age_us=250000),
 'abc_segment_options':dict(ordering=1,flags=0,reserved=0,max_age_us=250000,inactivity_timeout_us=-1,observation_capacity=64,reserved2=0),
 'abc_target_observation':{},
}

def units(name, typ):
    if name.endswith('_us') or name in ('receipt_us',): return 'microseconds'
    if name in ('radians_per_native_count','gain'): return 'radians/native count'
    if name in ('variant_ms','commitment_ms'): return 'milliseconds'
    if name in ('x','y','position','displacement','initial_position','final_position','resolved_target','reference_position','target','value','current_position','filtered_anchor','history','raw','prepared'): return 'common angular counts'
    if name in ('dx','dy','profile','native_count_intent','intents'): return 'native counts'
    if 'size' in name or name=='bytes': return 'bytes'
    if 'count' in name or name in ('capacity','written','pending_samples','pending_observations'): return 'elements/events (see description)'
    return 'dimensionless'

def inventory():
    header = no_comments((ROOT/HEADER).read_text())
    inputs, outputs, operations = [], [], []
    declarations, structures = parse_header((ROOT/HEADER).read_text())
    for declaration in declarations:
        ret, function = declaration['return_type'],declaration['name']
        short = function.removeprefix('abc_')
        assert short in OP, function
        args=[]
        for arg in declaration['parameters']:
            typ,name=arg['type'],arg['name']
            semantics=parameter_contract(short,name,typ,structures)
            direction=semantics['direction']
            args.append(dict(name=name,type=typ,**semantics))
            optional = name=='options' and short in ('planner_create','planner_reinitialize','observation_create','segment_create')
            description = semantics['validity']+'. '+param_description(short,name,typ)
            row=dict(name=name,api=function,type=typ,units=units(name,typ),description=description,source=HEADER,evidence='source-confirmed',**{k:v for k,v in semantics.items() if k!='direction'})
            if direction in ('in','inout'):
                inputs.append(dict(row,required=not semantics['nullable'],default=None,valid_range=param_range(short,name,typ),when_supplied='at call',runtime_change=True,planner_state_effect=effect(short),rng_effect=rng(short),learned_behavior_effect=learned(short),variant_specific=False,omitted_behavior=semantics['nullability_rule'] if semantics['nullable'] else 'no implicit C default; follow pointer/count rules',error_behavior='C status; see contract status table',direction=direction))
            if direction in ('out','inout'):
                output_row=dict(row);output_row.update(availability='on successful call; written may describe partial runtime output for legacy planner/Renderer',direction=direction)
                output_row['lifetime']='returned token until destroy/release; writable address needed only during call' if semantics['category']=='output_handle_address' else 'snapshot/data independent of handle after call; until caller overwrites/frees storage'
                outputs.append(output_row)
                if direction=='out':
                    inputs.append(dict(row,description='Caller supplies this output-storage address. '+description,required=not semantics['nullable'],default=None,valid_range=param_range(short,name,typ),when_supplied='at call',runtime_change=True,planner_state_effect='storage only; operation effects described separately',rng_effect='storage only',learned_behavior_effect=False,variant_specific=False,omitted_behavior=semantics['nullability_rule'],error_behavior='ABC_NULL_POINTER or validation status',direction='output-storage input'))
        operations.append(dict(name=function,return_type=ret,signature=f'{ret} {function}('+', '.join(a['type']+' '+a['name'] for a in args)+')',parameters=args,description=OP[short],classification=('recommended' if short.startswith('segment_') or short in ('model_load','planner_create','model_release','planner_destroy','get_version','check_abi','status_message','last_error') else 'advanced/legacy or optional Renderer'),source=HEADER))
        outputs.append(dict(name='return',api=function,type=ret,category='returned_status' if ret=='abc_status' else 'borrowed_pointer',units='status code' if ret=='abc_status' else 'text',ownership='value' if ret=='abc_status' else 'library-borrowed',availability='every call',lifetime='value' if ret=='abc_status' else OP[short],description='Stable numeric status 0..17; no exceptions cross ABI.' if ret=='abc_status' else OP[short],source=SOURCE,evidence='source-confirmed'))
    assert len(operations)==len(OP)==55,len(operations)
    input_structs=set(DEFAULTS)|{'abc_vec2','abc_report'}
    output_structs=set(structures)-{'abc_target_observation'}
    for structure,rows in structures.items():
        for name,typ in rows:
            assert name in MEANING,(structure,name)
            row=dict(name=structure+'.'+name,api=STRUCT_APIS[structure],type=typ,units=units(name,typ),description=MEANING[name],source=HEADER,evidence='source-confirmed')
            if structure in input_structs or name=='struct_size':
                default=DEFAULTS.get(structure,{}).get(name)
                input_row=dict(row)
                if structure=='abc_vec2':input_row['units']='common angular counts, except raw Renderer intent uses native counts'
                if structure=='abc_report':input_row['api']=['abc_renderer_set_profile']
                inputs.append(dict(input_row,required=name not in DEFAULTS.get(structure,{}),default=default,default_origin='options_init/native defaults' if name in DEFAULTS.get(structure,{}) else 'none: zero-initialized observation is an explicit value, not an API default',valid_range=field_range(structure,name),when_supplied='initialization/reinitialization' if 'options' in structure else 'runtime call',runtime_change='options require creating replacement handler/controller; planner options change by reinitialize' if 'options' in structure else True,planner_state_effect='none for size tag; otherwise see operation',rng_effect='seed initializes streams; accepted observations do not consume RNG; future planning may differ',learned_behavior_effect=name!='struct_size',variant_specific=False,omitted_behavior='use initializer/null-options defaults' if default is not None or name=='history' else 'required when using this structure'))
            if structure in output_structs:
                output_row=dict(row)
                if structure in ('abc_planner_options','abc_observation_options','abc_segment_options'):output_row['api']=[structure.removesuffix('s')+'s_init']
                if structure=='abc_vec2':output_row['api']=['abc_observation_submit','abc_prepare_history']
                if structure=='abc_report':output_row['api']=[x for x in row['api'] if x!='abc_renderer_set_profile']
                outputs.append(dict(output_row,ownership='caller snapshot' if structure not in ('abc_planner_options','abc_observation_options','abc_segment_options') else 'caller initialized options',availability='see producing api; fields of sample/report meaningful only within written prefix',lifetime='until caller overwrites/frees backing storage',constraints=field_range(structure,name),variant_specific=name in ('commitment_ms','variant','sha256','variant_ms','sample_count','duration_us')))
    for structure,names,apis in [
      ('abc_sample',('timestamp_us','position'),['abc_renderer_render_samples','abc_segment_render']),
      ('abc_segment_metadata',('struct_size','sample_count','initial_position'),['abc_segment_render']),
    ]:
        for name,typ in structures[structure]:
            if name in names:
                inputs.append(dict(name=structure+'.'+name,api=apis,type=typ,units=units(name,typ),required=True,default=None,valid_range='count must match metadata; positions/derived intents finite; C rendered timestamps strictly increasing',when_supplied='Renderer composition',description=MEANING[name],source=SOURCE,evidence='source-confirmed',runtime_change=True,planner_state_effect='none; Renderer only',rng_effect='Renderer generation consumes event RNG',learned_behavior_effect='Renderer only',variant_specific=False,omitted_behavior='no implicit default'))
    # C++-only inputs absent as C fields, plus binding-specific configuration.
    extras=[
      ('model','abc::ContinuousPlanner::ContinuousPlanner','std::shared_ptr<const abc::Model>',True,None,'Loaded immutable model required; planner retains shared ownership.'),
      ('model_directory','abc::ContinuousPlanner::load','const std::filesystem::path&',True,None,'Loads a validated bundle then returns shared_ptr<ContinuousPlanner>.'),
      ('options','abc::ContinuousPlanner::load / ContinuousPlanner','abc::PlannerOptions',False,'{}','Native defaults: position zero, empty history, seed 7, diagnostics false.'),
      ('seed','abc::ContinuousPlanner::reset','std::uint64_t',False,'stored seed (no-argument overload)','reset() preserves configured seed; reset(seed) replaces it.'),
      ('diagnostics','abc::PlannerOptions','bool',False,False,'Compute otherwise skipped proposals; increases inference work and changes selected-head diagnostics; maintains intended RNG schedule.'),
      ('initial_history','abc::PlannerOptions','std::vector<abc::Vec2>',False,[],'Empty or exactly 160 finite chronological common-count displacements. Initial velocity derived from this history.'),
      ('raw','abc::prepare_history','std::span<const abc::Vec2>',True,None,'Exactly 164 finite chronological 1 ms common-count displacements; no physical position requirement.'),
      ('current_position','abc::prepare_history','abc::Vec2',True,None,'Finite virtual/observed initialization anchor; result.initial_position is filtered anchor.'),
      ('planner','abc::SegmentController / abc::ObservationInput','abc::ContinuousPlanner&',True,None,'Borrow, not ownership. Both weakly detect parent destruction; SegmentController exclusively owns the target/movement mutation domain until destruction.'),
      ('options','abc::SegmentController','abc::SegmentOptions',False,'{}','latest / 250000 us / -1 timeout / capacity 64.'),
      ('options','abc::ObservationInput','abc::ObservationOptions',False,'{}','strict / 250000 us.'),
      ('output','abc::SegmentController::next_segment','std::span<abc::Sample>',True,None,'Caller span size >= variant_ms; metadata reference is also required.'),
      ('library','Python Model / Renderer / prepare_history / Library','Library | path | None',False,None,'Explicit library, ABCURVES_LIBRARY, bundled library, development search, then system find_library. Planner/controller inherit parent Library.'),
      ('capacity','Python SampleBuffer','int',True,None,'0 <= capacity < 2**64, subject to Python/addressable allocation limits; capacity in samples.'),
      ('output','Python Planner.generate_sample / Renderer.step','Sample | Report | None',False,None,'Allocate one output if omitted; otherwise mutate provided ctypes object.'),
      ('output','Python Renderer.step_batch / render_samples / render_segment','ctypes Report array | None',False,None,'Allocate report array if omitted; supplied capacity must cover input count.'),
      ('invalidate','Python SegmentController.unavailable / ObservationInput.unavailable','bool',False,False,'False unavailable, true invalidated; same planner loss policy.'),
      ('seed','Python Planner.reset / Renderer.reset','int',False,7,'Python reset() passes 7, unlike no-argument native planner reset which preserves stored seed.'),
      ('options','Rust Planner::new / Planner::reinitialize','PlannerOptions',True,None,'Explicit options value required; Default::default gives seed7/zero origin/empty history/false proposals.'),
      ('options','Rust SegmentController::new / ObservationInput::new','SegmentOptions / ObservationOptions',True,None,'Explicit options required; Default::default matches native defaults for the respective handler.'),
      ('seed','Rust Planner::reset / Renderer::reset','u64',True,None,'Existing reset requires seed; additive Planner::reset_session retains configured seed/prefix.'),
      ('reset_session','Python Planner.reset_session / Rust Planner::reset_session / C abc_planner_reset_session','operation with no seed argument',True,None,'ABI 1.4 stored-seed and stored-prefix session reset; Python reset() retains its old seed7 default.'),
      ('invalidate','Rust SegmentController::unavailable / ObservationInput::unavailable','bool',True,None,'Explicit boolean; no default argument.'),
      ('ABCURVES_LIB_DIR','Rust abcurves-sys build.rs','environment path',False,'build/api/Release on Windows; build/api elsewhere','Link-time native library directory; runtime DLL/shared-library discovery remains OS/application responsibility.'),
      ('ABC_FORCE_PORTABLE','native matvec_kernel process environment','environment string',False,'unset: runtime CPU dispatch','Exact string 1 forces portable/SSE2 kernel. Other values use automatic AVX2/no-FMA when compiled and supported, otherwise portable. Cached process-wide on first dispatch; not a per-planner runtime setter or model-architecture option.'),
      ('ABC_ENABLE_AVX2','lib/CMakeLists.txt','CMake bool',False,True,'Build-time runtime-dispatch availability, not an observation/capture frequency option.'),
      ('ABC_PROFILE','lib/CMakeLists.txt','CMake bool',False,False,'Build-only internal profiling hooks; ordinary public APIs do not expose component timing arrays. Does not replace per-segment latency snapshot.'),
    ]
    for name,api,typ,required,default,desc in extras:
        source='API/python/src/abcurves_native/__init__.py' if 'Python' in api else 'API/rust/abcurves/src/lib.rs' if 'Rust' in api else 'lib/include/abcurves/native.hpp'
        if name=='ABC_FORCE_PORTABLE':source='lib/src/core/matvec.cpp'
        if name in ('ABC_ENABLE_AVX2','ABC_PROFILE'):source='lib/CMakeLists.txt'
        if name=='ABCURVES_LIB_DIR':source='API/rust/abcurves-sys/build.rs'
        inputs.append(dict(name=name,api=api,type=typ,units=units(name,typ),required=required,default=default,valid_range=None,when_supplied='see description',description=desc,source=source,evidence='source-confirmed',planner_state_effect='see corresponding native operation',rng_effect='see corresponding native operation',learned_behavior_effect='see corresponding native operation',runtime_change=False if name.startswith('ABC_') else True,variant_specific=False,omitted_behavior='default when optional; required argument otherwise'))
    for name,typ,desc in [
      ('abc::TensorView.data','const void*','Borrowed immutable tensor memory owned by Model; C/Python/Rust expose metadata only.'),
      ('abc::TensorView.shape','std::vector<uint32_t>','Tensor dimensions; tensor() returns const TensorView& tied to Model lifetime.'),
      ('abc::Model.source_directory()','const filesystem::path&','Original model directory; borrowed from Model.'),
      ('abc::Model::load / ContinuousPlanner::load','shared_ptr<const Model> / shared_ptr<ContinuousPlanner>','Owned shared native objects returned on load; destruct through RAII. No C token or stable C++ binary ABI.'),
      ('abc::PreparedHistory.history','std::array<Vec2,160>','Owned 160 filtered chronological displacement samples, not pointers into raw input.'),
      ('abc::PreparedHistory.initial_position','abc::Vec2','Filtered virtual initialization anchor, generally different from observed_position.'),
      ('abc::PreparedHistory.observed_position','abc::Vec2','Echo of caller current_position; C returns only anchor/history; Python/Rust synthesize echo without planner algorithm.'),
      ('abc::Diagnostics.buffered_samples','uint64_t','All emitted samples; C calls this samples_emitted.'),
      ('abc::SegmentRecoveryDiagnostics.last_recoverable_error','abc::ObservationRejection','Native reason enum 0/1/2/3; C/Python/Rust map to status 0/1/9/15.'),
      ('abc::ContinuousPlanner.reset_revision()','uint64_t','Initialization/reset revision used by handlers to clear session metadata lazily.'),
      ('abc::ContinuousPlanner.failed() / failure()','bool / abc::PlannerFailure','Native latched generation failure and fixed message/category/reason; explicit reset required; read-only and usable after failure.'),
      ('abc::ContinuousPlanner.has_segment_controller()','bool','True while one controller exclusively owns the target/movement mutation domain; no thread synchronization implied.'),
      ('abc::SegmentController.state()','abc::SegmentState','Failure snapshot, generation attempt and last-success times, relative-origin window and last missing-origin receipt; no learned mutation.'),
      ('abc::Renderer.ready()','bool','Profile prepared, not proof begin() has been called.'),
      ('Python Segment.samples','SampleBuffer','Owned contiguous mutable buffer; reusable backing storage aliases views.'),
      ('Python SampleBuffer.numpy()','numpy.ndarray','Optional zero-copy structured view of written prefix, retains storage; copies required before reuse if old data still needed.'),
      ('Rust Segment::samples()','&[Sample]','Borrow of fixed [Sample;32] owned by Segment; Segment may outlive controller; clone makes independent copy.'),
      ('Python NativeError','NativeError(status,message,written)','Exception with native status/detail and partial written count; local validation raises ValueError/TypeError.'),
      ('Rust Error','Error { status:i32,message:String,written:usize }','Owned Result error; local wrapper errors generally status1; allocation reserve failure uses status11.'),
      ('Python SampleBuffer.written / capacity / array','int / int / ctypes Sample array','Actual valid prefix length, allocated element count and owned mutable contiguous storage; len(buffer) is written, not capacity.'),
      ('Python prepared history','PreparedHistory(initial_position,history,observed_position)','Frozen dataclass container; nested ctypes history/Vec2 remain mutable caller-owned values.'),
      ('Rust prepared history','PreparedHistory { initial_position,observed_position,history:[Vec2;160] }','Owned value returned by W5 helper; no borrow of raw input.'),
    ]:
        outputs.append(dict(name=name,api=name,type=typ,units='see description',ownership='see description',availability='after corresponding call',description=desc,source='lib/include/abcurves/native.hpp / Python / Rust wrappers',evidence='source-confirmed'))
    py=ast.parse((ROOT/'API/python/src/abcurves_native/__init__.py').read_text())
    py_surface=[]
    for node in py.body:
        if isinstance(node,ast.ClassDef) and not node.name.startswith('_'):
            for f in node.body:
                if isinstance(f,ast.FunctionDef) and (not f.name.startswith('_') or f.name=='__init__'):
                    py_surface.append(dict(api=node.name+'.'+f.name,arguments=ast.unparse(f.args),property=any(isinstance(d,ast.Name) and d.id=='property' for d in f.decorator_list)))
        if isinstance(node,ast.FunctionDef) and not node.name.startswith('_'):
            py_surface.append(dict(api=node.name,arguments=ast.unparse(node.args)))
    rust_surface=[]
    for match in re.finditer(r'pub fn (\w+)([^\{;]*)(?:\{|;)',(ROOT/'API/rust/abcurves/src/lib.rs').read_text()):
        rust_surface.append('pub fn '+match[1]+' '.join(match[2].split()))
    native_headers={p.name:p.read_text() for p in (ROOT/'lib/include/abcurves').glob('*.hpp')}
    common=dict(schema='abcurves.application-io-audit.v2',abi={'major':1,'minor':4,'api_version':'0x00010004'},evidence='Source-confirmed unless a narrower evidence label says otherwise; fresh probes are separate.',scope='Native lib/ plus C ABI and abcurves_native Python / abcurves-native Rust; original Python and standalone low-level C99 Renderer are separate surfaces.',null_default_meaning='No implicit default, except where omitted_behavior explicitly says null options selects defaults.')
    enums={name:int(value) for name,value in re.findall(r'\b(ABC_[A-Z_]+)\s*=\s*(\d+)',header)}
    write_json(DOC/'application_inputs.json',dict(common,inputs=inputs,c_constants=enums,c_operations=operations,python_signatures=py_surface,rust_safe_signatures=rust_surface,native_public_headers=native_headers))
    write_json(DOC/'application_outputs.json',dict(common,outputs=outputs,c_constants=enums,c_structures={k:[dict(name=n,type=t,description=MEANING[n]) for n,t in v] for k,v in structures.items()}))
    lines=['\n## Appendix A. Complete C operation inventory\n',f'All {len(operations)} exported operations (including all 50 original exports), including advanced/legacy paths. Signatures are extracted from the header; descriptions are source-checked. Pointer sizes/counts are elements unless named bytes/size.\n']
    for op in operations:
        lines += ['### '+op['name']+'\n','```c\n'+op['signature']+';\n```\n',op['description']+'\n']
    lines+=['\n## Appendix B. Exact public structure fields\n','The JSON inventories additionally list each operation argument and its direction. Types below are the C layout; native equivalents use the names in the public headers. See sections 7-15 for validation, sentinels, ownership and default rules.\n']
    for structure, rows in structures.items():
        lines+=['### '+structure+'\n','| Field | Type | Meaning |','|---|---|---|']
        lines += [f'| `{n}` | `{t}` | {MEANING[n]} |' for n,t in rows]
        lines+=['']
    lines+=['\n## Appendix C. Exact Python wrapper signatures\n','Properties are queries; `close`/context management are inherited by owned wrappers. `Renderer.render_segment(segment, radians_per_native_count, output=None)` is attached by assignment.\n','```text']
    lines += [r['api']+'('+r['arguments']+')'+(' [property]' if r.get('property') else '') for r in py_surface]
    lines += ['```\n','## Appendix D. Rust safe signatures\n',f'Ordered as in `API/rust/abcurves/src/lib.rs`; repeated method names belong to the enclosing types described in the main reference. Raw `abcurves-sys` declares all {len(operations)} C exports.\n','```rust']+rust_surface+['```\n']
    (OUT/'inventory_appendix.md').write_text('\n'.join(lines),encoding='utf-8')
    return dict(c_exports=len(operations),inputs=len(inputs),outputs=len(outputs),c_structures=len(structures),c_fields=sum(map(len,structures.values())))

def effect(short):
    if short.startswith(('model_','check_','get_','status_','last_')) or any(x in short for x in ('get_','required_capacity','layout','options_init')):return 'no learned/time mutation; handler queries may lazily synchronize after parent reset'
    if 'submit' in short or 'pending' in short or 'diagnostics' in short:return 'input bookkeeping/target event queue; no movement or RNG advance'
    return OP[short]
def rng(short):
    if short in ('planner_create','planner_reset','planner_reset_session','planner_reinitialize','segment_reset_session','renderer_reset'):return 'initializes/resets RNG'
    if short in ('planner_advance','planner_generate_sample','segment_next','renderer_step','renderer_step_batch','renderer_render_samples','segment_render'):return 'generation may consume RNG; buffered/idle planner retrieval does not execute fresh inference'
    return 'no RNG consumption'
def learned(short):
    return 'affects subsequent target history/decisions when applied' if 'submit' in short or short in ('planner_update_target','planner_clear_target') else effect(short)
def param_range(short,name,typ):
    if name=='options':return 'null or initialized size-tagged options of correct type'
    if 'handle' in typ and '*' in typ:return 'non-null writable new-handle storage; initial contents need not be a live token'
    if 'handle' in typ:return 'live same-library token of correct type; no concurrent destruction'
    if name.endswith('_us'):return 'signed int64 >=0; additional monotonic/receipt/reference rules in contract'
    if name=='seed':return '0..2**64-1; planner uses low32 bits'
    if name=='radians_per_native_count':return 'finite >0; gain/common_radians_per_count finite >0'
    if name=='capacity':return 'see operation count; valid writable nonoverlapping storage for claimed capacity'
    if '*' in typ:return 'valid aligned nonoverlapping pointer for call; nullable only under documented count/options rules'
    return 'see operation and structure rules'
def param_description(short,name,typ):
    if 'handle' in typ and '*' in typ:return 'Output address receives a newly created owned token; destroy/release using matching operation.'
    if name=='options':return 'Null selects defaults; non-null options must be fully initialized, including size/flags/reserved.'
    if name=='written':return 'Output element count, initialized to zero when valid; see runtime partial-output limits.'
    if name=='out' or name=='output':return OP[short]+' Caller supplies output storage; size-tagged outputs require struct_size input.'
    if name=='previous_position':return 'In/out finite common-count anchor, updated after each successful report; Renderer-only bookkeeping, never planner feedback.'
    if name=='metadata':return 'Segment snapshot output (size tag required) or const render input containing sample_count and initial_position.'
    if name=='count':return 'Number of inputs/reports; Renderer profile exactly256, segment render exactly metadata.sample_count.'
    if name=='capacity':return 'Caller storage element capacity; segment output >= model commitment regardless of actual shorter result.'
    if name=='observation':return 'Size-tagged finite angular observation, reserved0; capture<=receipt; see observation contract.'
    if name=='utf8_directory':return 'Nonempty UTF-8 path to directory containing both model.abcmodel and manifest.json.'
    if name=='utf8_model_file':return 'Nonempty UTF-8 path to combined Renderer binary (separate from planner model directory).'
    if name=='radians_per_native_count':return 'Application scalar calibration; smooth common-count differences divided by gain/COMMON_RADIANS_PER_COUNT.'
    if 'handle' in typ:return 'Live correctly typed token; borrowed only for call; parent lifetime and serialization rules apply.'
    return MEANING.get(name,OP[short]+' Parameter '+name+' follows the exact declaration and main contract rules.')
def field_range(structure,name):
    if name=='struct_size':return '>=sizeof(current known structure); extension bytes untouched'
    if name=='ordering':return '0..2'
    if name=='kind':return '0..1'
    if name=='availability':return '0..2'
    if name in ('max_age_us','inactivity_timeout_us'):return '-1 or 0..INT64_MAX'
    if name=='observation_capacity':return '1..64'
    if name=='history_count':return '0 or 160'
    if name=='flags':return '0 or 1' if structure=='abc_planner_options' else '0'
    if name.startswith('reserved'):return 'zero'
    if name=='seed':return '0..UINT64_MAX'
    if name in ('value','initial_position'):return 'both coordinates finite; deferred relative sum must also be finite'
    return 'see field meaning and contract'

def probes():
    sys.path.insert(0,str(ROOT/'API/python/src'))
    os.environ['ABCURVES_LIBRARY']=str(ROOT/'build/api/Release/abcurves.dll')
    import abcurves_native as a
    lib=a.Library(); raw=lib.raw
    result={'library':lib.path,'dll_sha256':hashlib.sha256(Path(lib.path).read_bytes()).hexdigest(),'version':{n:getattr(lib.version,n) for n in ('api_version','abi_major','abi_minor','engine_major','engine_minor','engine_patch','capabilities')},'checks':[]}
    def check(name,condition,detail=None):
        assert condition,(name,detail)
        result['checks'].append(dict(name=name,passed=True,detail=detail))
    check('abi',lib.version.abi_minor==4 and lib.version.capabilities==255)
    result['layouts']={n:C.sizeof(getattr(a,n)) for n in ('Vec2','Sample','Report','Version','ModelInfo','TensorInfo','Options','State','_ObservationOptions','_Observation','ObservationDiagnostics','_SegmentOptions','SegmentMetadata','SegmentDiagnostics','SegmentRecoveryDiagnostics')}
    for n,cls in [('planner',a.Options),('observation',a._ObservationOptions),('segment',a._SegmentOptions)]:
        obj=cls();lib.check(getattr(raw,'abc_'+n+'_options_init')(C.byref(obj),C.sizeof(obj)))
        result[n+'_defaults']={key:(list(value) if isinstance(value,a.Vec2) else None if isinstance(value,C._Pointer) else value) for key,_ in cls._fields_ for value in [getattr(obj,key)]}
    for variant in (32,16,8):
        with a.Model(ROOT/f'lib/models/{variant}ms',library=lib) as model:
            info=model.info
            check(f'model-{variant}',info.commitment_ms==variant and info.tensor_count==43 and info.capabilities==13,info.variant.decode())
            with a.Planner(model) as p,a.SegmentController(p) as s:
                s.relative((320,120),0,0);first=s.next_segment()
                check(f'boundaries-{variant}',len(first.samples)==variant and first.metadata.first_sample_timestamp_us==1000 and first.metadata.end_timestamp_us==variant*1000)
                t=first.metadata.end_timestamp_us
                s.relative((12,-7),t-1000,t-1000)
                s.next_segment()
                check(f'historical-receipt-{variant}',s.diagnostics.last_apply_us==t and s.diagnostics.last_observation_receipt_us==t-1000)
                before=bytes(p.state);meta=bytes(s.metadata);s.absolute((420,170),t*10,t*10);s.clear_pending_observations()
                check(f'clear-state-{variant}',before==bytes(p.state) and meta==bytes(s.metadata) and s.diagnostics.pending_observations==0)
            with a.Planner(model,initial_position=(1.3e308,1.3e308)) as p,a.SegmentController(p) as s:
                s.relative((1.3e308,1.3e308),1000,1000);s.next_segment();seg=s.next_segment();d=s.recovery_diagnostics
                check(f'recovery-{variant}',seg.metadata.end_timestamp_us==2000 and d.last_error_status==1 and d.pending_observations==0 and d.nonfinite_relative_resolutions==1)
            with a.Planner(model,seed=7) as p,a.Planner(model,seed=7+2**32) as q:
                p.update_target((320,120),0);q.update_target((320,120),0)
                b=p.advance(64000);c=q.advance(64000)
                check(f'seed-low32-{variant}',bytes(b.array)==bytes(c.array))
            with a.Planner(model) as p,a.SegmentController(p) as s:
                s.absolute((1.3e308,1.3e308),0,0)
                try:s.next_segment();raise AssertionError('expected internal failure')
                except a.NativeError as e:status=e.status
                state=p.state
                d=s.diagnostics;failure=s.state
                check(f'internal-failure-gauge-{variant}',status==13 and state.failed==1 and d.segment_generation_errors==1 and failure.failure_status==13 and failure.reset_required==1,dict(status=status,failed=state.failed,requested=state.requested_time_us,numerical=state.numerical_time_us,message=failure.error_message.decode()))
                s.reset_segment_session();check(f'explicit-failure-reset-{variant}',len(s.next_segment().samples)==1)
    with a.Model(ROOT/'lib/models/16ms',library=lib) as model,a.Planner(model) as p,a.SegmentController(p) as s:
        s.absolute((320,120),0,0);buf=a.SampleBuffer(32);s.next_segment_into(buf);sample=buf[0];old_t=sample.timestamp_us
        view=buf.numpy();s.next_segment_into(buf)
        check('reusable-buffer-aliases',sample.timestamp_us!=old_t and view['timestamp_us'][0]==sample.timestamp_us)
        snap=view.copy();s.next_segment_into(buf);check('numpy-copy-independent',snap['timestamp_us'][0]!=view['timestamp_us'][0])
    with a.Model(ROOT/'lib/models/16ms',library=lib) as model,a.Planner(model) as p,a.SegmentController(p) as s:
        s.relative((320,120),0,0);s.next_segment();before=bytes(p.state)
        try:p.set_position((100,50));raise AssertionError('expected exclusive domain rejection')
        except a.NativeError as e:status=e.status
        check('direct-set-position-rejected',status==4 and bytes(p.state)==before,'Exclusive attachment rejects before mutation.')
    write_json(OUT/'probes.json',result)
    return result

def assemble_contract():
    """Generate detailed declarations without changing public prose guides."""
    text=(OUT/'inventory_appendix.md').read_text(encoding='utf-8')
    text=text.replace('See sections 7-15 for validation, sentinels, ownership and default rules.',
                      'See docs/api/overview.md, docs/advanced/data-and-options.md, docs/api/errors.md and docs/advanced/state-and-memory.md in the source tree.')
    (DOC/'symbols.md').write_text('# Generated public symbol reference\n\nGenerated from the current C header and language bindings. Regenerate with `python API/tools/audit_application_io.py`.\n'+text,encoding='utf-8')

def verify_artifacts():
    inputs=json.loads((DOC/'application_inputs.json').read_text())
    outputs=json.loads((DOC/'application_outputs.json').read_text())
    symbols={o['name'] for o in inputs['c_operations']}
    assert len(symbols)==55
    for row in inputs['inputs']+outputs['outputs']:
        api=row['api']
        for value in api if isinstance(api,list) else [api]:
            if value.startswith('abc_'):assert value in symbols,(row['name'],value)
    result=dict(json_syntax='valid',c_exports=len(symbols),input_entries=len(inputs['inputs']),output_entries=len(outputs['outputs']))
    write_json(OUT/'verification.json',result)
    return result

def generate(output=None, *, run_probes=False):
    global DOC, OUT
    DOC = OUT = Path(output or OUT).resolve()
    OUT.mkdir(parents=True,exist_ok=True)
    # Regeneration must never mutate runtime sources/models or historical receipts.
    protected=[p for folder in ('lib/src','lib/include','lib/models','API/c','API/python/src','API/rust') for p in (ROOT/folder).rglob('*') if p.is_file() and '__pycache__' not in p.parts]
    before={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in protected}
    counts=inventory();probe=probes() if run_probes else None
    write_json(OUT/'inventory_summary.json',counts)
    assemble_contract()
    verify_artifacts()
    assert all(hashlib.sha256(Path(p).read_bytes()).hexdigest()==digest for p,digest in before.items()), 'generator mutated production sources'
    return dict(counts,probe_checks=len(probe['checks']) if probe else 0,layouts=probe['layouts'] if probe else {})

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',type=Path,default=OUT)
    parser.add_argument('--probes',action='store_true',help='Also run live ABI, failure, seed and buffer checks against the Release DLL.')
    args=parser.parse_args()
    print(json.dumps(generate(args.out,run_probes=args.probes),indent=2))
