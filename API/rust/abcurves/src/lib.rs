//! Safe ownership over the shared ABCurves C ABI; no planning logic lives here.
use abcurves_sys as sys;
pub use abcurves_sys::{
    ModelInfo, Report, Sample, State, TensorInfo, Vec2, Version, COMMON_RADIANS_PER_COUNT,
};
use std::{
    cell::Cell,
    ffi::{CStr, CString},
    fmt,
    marker::PhantomData,
    mem::{size_of, MaybeUninit},
    path::Path,
};
#[derive(Debug, Clone)]
pub struct Error {
    pub status: i32,
    pub message: String,
    pub written: usize,
}
impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "ABCurves {}: {}", self.status, self.message)
    }
}
impl std::error::Error for Error {}
pub type Result<T> = std::result::Result<T, Error>;
fn local(message: &str) -> Error {
    Error {
        status: 1,
        message: message.into(),
        written: 0,
    }
}
fn check(status: i32, written: u64) -> Result<()> {
    if status == 0 {
        Ok(())
    } else {
        Err(Error {
            status,
            message: unsafe { CStr::from_ptr(sys::abc_last_error()) }
                .to_string_lossy()
                .into_owned(),
            written: written as usize,
        })
    }
}
fn string(s: &str) -> Result<CString> {
    CString::new(s).map_err(|_| local("embedded NUL in string"))
}
fn path(p: &Path) -> Result<CString> {
    string(p.to_str().ok_or_else(|| local("path must be UTF-8"))?)
}
pub fn version() -> Result<Version> {
    let mut v = Version {
        struct_size: size_of::<Version>() as u32,
        ..Default::default()
    };
    unsafe {
        check(sys::abc_check_abi(1, 0), 0)?;
        check(sys::abc_get_version(&mut v), 0)?;
    }
    let layouts = [
        (v.sample_size, size_of::<Sample>()),
        (v.vec2_size, size_of::<Vec2>()),
        (v.report_size, size_of::<Report>()),
        (v.options_size, size_of::<sys::Options>()),
        (v.model_info_size, size_of::<ModelInfo>()),
        (v.state_size, size_of::<State>()),
        (v.tensor_info_size, size_of::<TensorInfo>()),
    ];
    if layouts.iter().any(|(c, r)| *c as usize != *r) {
        return Err(local("ABI layout mismatch"));
    }
    Ok(v)
}
/// Immutable model handle. Planners retain independent shared ownership internally.
pub struct Model {
    handle: u64,
}
// SAFETY: Model has only immutable query operations; Rust ownership excludes
// destruction while borrowed. Native registry supports independent concurrent use.
unsafe impl Send for Model {}
unsafe impl Sync for Model {}
impl Model {
    /// Load one of the models embedded in an ABI 1.5 Windows DLL.
    pub fn embedded(commitment_ms: u32) -> Result<Self> {
        if version()?.abi_minor < 5 { return Err(local("embedded models require ABI 1.5")); }
        let mut h = 0;
        unsafe { check(sys::abc_model_load_embedded(commitment_ms, &mut h), 0)?; }
        Ok(Self { handle: h })
    }
    pub fn load(p: impl AsRef<Path>) -> Result<Self> {
        version()?;
        let p = path(p.as_ref())?;
        let mut h = 0;
        unsafe {
            check(sys::abc_model_load(p.as_ptr(), &mut h), 0)?;
        }
        Ok(Self { handle: h })
    }
    pub fn info(&self) -> Result<ModelInfo> {
        let mut value = unsafe { MaybeUninit::<ModelInfo>::zeroed().assume_init() };
        value.struct_size = size_of::<ModelInfo>() as u32;
        unsafe {
            check(sys::abc_model_get_info(self.handle, &mut value), 0)?;
        }
        Ok(value)
    }
    pub fn tensor_info(&self, name: &str) -> Result<TensorInfo> {
        let name = string(name)?;
        let mut value = TensorInfo {
            struct_size: size_of::<TensorInfo>() as u32,
            ..Default::default()
        };
        unsafe {
            check(
                sys::abc_model_get_tensor_info(self.handle, name.as_ptr(), &mut value),
                0,
            )?;
        }
        Ok(value)
    }
}
impl Drop for Model {
    fn drop(&mut self) {
        unsafe {
            sys::abc_model_release(self.handle);
        }
    }
}
pub struct PlannerOptions<'a> {
    pub seed: u64,
    pub initial_position: Vec2,
    pub history: &'a [Vec2],
    pub diagnostic_proposals: bool,
}
impl Default for PlannerOptions<'_> {
    fn default() -> Self {
        Self {
            seed: 7,
            initial_position: Vec2::default(),
            history: &[],
            diagnostic_proposals: false,
        }
    }
}
/// Mutable instance: transferable to another thread, never concurrently shared.
pub struct Planner {
    handle: u64,
    _not_sync: PhantomData<Cell<()>>,
}
impl Planner {
    pub fn new(model: &Model, options: PlannerOptions<'_>) -> Result<Self> {
        let o = sys::Options {
            struct_size: size_of::<sys::Options>() as u32,
            flags: options.diagnostic_proposals as u32,
            seed: options.seed,
            initial_position: options.initial_position,
            history: options.history.as_ptr(),
            history_count: options.history.len() as u64,
        };
        let mut h = 0;
        unsafe {
            check(sys::abc_planner_create(model.handle, &o, &mut h), 0)?;
        }
        Ok(Self {
            handle: h,
            _not_sync: PhantomData,
        })
    }
    pub fn model_info(&self) -> Result<ModelInfo> {
        let mut value = unsafe { MaybeUninit::<ModelInfo>::zeroed().assume_init() };
        value.struct_size = size_of::<ModelInfo>() as u32;
        unsafe {
            check(sys::abc_planner_get_model_info(self.handle, &mut value), 0)?;
        }
        Ok(value)
    }
    pub fn reset(&mut self, seed: u64) -> Result<()> {
        unsafe { check(sys::abc_planner_reset(self.handle, seed), 0) }
    }
    /// Reset using the configured seed and initialization prefix (ABI 1.4).
    pub fn reset_session(&mut self) -> Result<()> {
        unsafe { check(sys::abc_check_abi(1,4),0)?; check(sys::abc_planner_reset_session(self.handle),0) }
    }
    pub fn set_position(&mut self, p: Vec2) -> Result<()> {
        unsafe { check(sys::abc_planner_set_position(self.handle, p), 0) }
    }
    pub fn update_target(&mut self, p: Vec2, time: i64) -> Result<()> {
        unsafe { check(sys::abc_planner_update_target(self.handle, p, time), 0) }
    }
    pub fn clear_target(&mut self, time: i64) -> Result<()> {
        unsafe { check(sys::abc_planner_clear_target(self.handle, time), 0) }
    }
    pub fn state(&self) -> Result<State> {
        let mut s = State {
            struct_size: size_of::<State>() as u32,
            ..Default::default()
        };
        unsafe {
            check(sys::abc_planner_get_state(self.handle, &mut s), 0)?;
        }
        Ok(s)
    }
    pub fn required_capacity(&self, time: i64) -> Result<usize> {
        let mut n = 0;
        unsafe {
            check(
                sys::abc_planner_required_capacity(self.handle, time, &mut n),
                0,
            )?;
        }
        usize::try_from(n).map_err(|_| local("batch exceeds address space"))
    }
    pub fn advance_into(&mut self, time: i64, output: &mut [Sample]) -> Result<usize> {
        let mut n = 0;
        unsafe {
            let status = sys::abc_planner_advance(
                self.handle,
                time,
                output.as_mut_ptr(),
                output.len() as u64,
                &mut n,
            );
            check(status, n)?;
        }
        Ok(n as usize)
    }
    pub fn advance(&mut self, time: i64) -> Result<Vec<Sample>> {
        let n = self.required_capacity(time)?;
        let mut out = Vec::new();
        out.try_reserve_exact(n).map_err(|_| Error {
            status: 11,
            message: "allocation failed".into(),
            written: 0,
        })?;
        out.resize(n, Sample::default());
        let written = self.advance_into(time, &mut out)?;
        out.truncate(written);
        Ok(out)
    }
    pub fn generate_sample(&mut self, time: i64) -> Result<Sample> {
        let mut s = Sample::default();
        unsafe {
            check(
                sys::abc_planner_generate_sample(self.handle, time, &mut s),
                0,
            )?;
        }
        Ok(s)
    }
}
impl Drop for Planner {
    fn drop(&mut self) {
        unsafe {
            sys::abc_planner_destroy(self.handle);
        }
    }
}
/// Persistent C99 Renderer; input is native-count intent with Y up.
pub struct Renderer {
    handle: u64,
    _not_sync: PhantomData<Cell<()>>,
}
impl Renderer {
    /// Create a Renderer using the model embedded in an ABI 1.5 Windows DLL.
    pub fn embedded() -> Result<Self> {
        if version()?.abi_minor < 5 { return Err(local("embedded Renderer requires ABI 1.5")); }
        let mut h = 0;
        unsafe { check(sys::abc_renderer_create_embedded(&mut h), 0)?; }
        Ok(Self { handle: h, _not_sync: PhantomData })
    }
    pub fn load(p: impl AsRef<Path>) -> Result<Self> {
        version()?;
        let p = path(p.as_ref())?;
        let mut h = 0;
        unsafe {
            check(sys::abc_renderer_create(p.as_ptr(), &mut h), 0)?;
        }
        Ok(Self {
            handle: h,
            _not_sync: PhantomData,
        })
    }
    pub fn set_profile(&mut self, profile: &[Report]) -> Result<()> {
        unsafe {
            check(
                sys::abc_renderer_set_profile(self.handle, profile.as_ptr(), profile.len() as u64),
                0,
            )
        }
    }
    pub fn reset(&mut self, seed: u64) -> Result<()> {
        unsafe { check(sys::abc_renderer_reset(self.handle, seed), 0) }
    }
    pub fn step(&mut self, intent: Vec2) -> Result<Report> {
        let mut out = Report::default();
        unsafe {
            check(sys::abc_renderer_step(self.handle, intent, &mut out), 0)?;
        }
        Ok(out)
    }
    pub fn step_batch_into(&mut self, intents: &[Vec2], output: &mut [Report]) -> Result<usize> {
        let mut n = 0;
        unsafe {
            let status = sys::abc_renderer_step_batch(
                self.handle,
                intents.as_ptr(),
                intents.len() as u64,
                output.as_mut_ptr(),
                output.len() as u64,
                &mut n,
            );
            check(status, n)?;
        }
        Ok(n as usize)
    }
    pub fn render_samples_into(
        &mut self,
        samples: &[Sample],
        previous: &mut Vec2,
        gain: f64,
        output: &mut [Report],
    ) -> Result<usize> {
        let mut n = 0;
        unsafe {
            let status = sys::abc_renderer_render_samples(
                self.handle,
                samples.as_ptr(),
                samples.len() as u64,
                previous,
                gain,
                output.as_mut_ptr(),
                output.len() as u64,
                &mut n,
            );
            check(status, n)?;
        }
        Ok(n as usize)
    }
}
impl Drop for Renderer {
    fn drop(&mut self) {
        unsafe {
            sys::abc_renderer_destroy(self.handle);
        }
    }
}

// Independent observation extension. All validation and resolution are native.
pub use sys::ObservationDiagnostics;
#[repr(u32)]
#[derive(Clone, Copy, Debug, Default)]
pub enum ObservationOrdering {
    #[default]
    Strict = 0,
    Latest = 1,
    Caller = 2,
}
#[repr(u32)]
#[derive(Clone, Copy, Debug, Default)]
pub enum TargetAvailability {
    #[default]
    Available = 0,
    Unavailable = 1,
    Invalidated = 2,
}
#[derive(Clone, Copy, Debug)]
pub struct ObservationOptions {
    pub ordering: ObservationOrdering,
    pub max_age_us: i64,
}
impl Default for ObservationOptions {
    fn default() -> Self {
        Self {
            ordering: ObservationOrdering::Strict,
            max_age_us: 250000,
        }
    }
}
#[derive(Clone, Copy, Debug)]
pub struct TargetObservation {
    pub value: Vec2,
    pub capture_time_us: i64,
    pub receipt_time_us: i64,
    pub relative: bool,
    pub availability: TargetAvailability,
}
fn observation_abi() -> Result<()> {
    unsafe {
        check(sys::abc_check_abi(1, 1), 0)?;
    }
    let (mut a, mut b, mut c) = (0, 0, 0);
    unsafe {
        check(sys::abc_observation_layout(&mut a, &mut b, &mut c), 0)?;
    }
    if a as usize != size_of::<sys::ObservationOptions>()
        || b as usize != size_of::<sys::TargetObservation>()
        || c as usize != size_of::<sys::ObservationDiagnostics>()
    {
        return Err(local("observation ABI layout mismatch"));
    }
    Ok(())
}
/// Exclusive input handler borrowing the planner. No execution state exists.
/// Advance via planner_mut(), collecting through receipt before submit().
/// Capture time is metadata; relative errors use latest closed virtual position.
pub struct ObservationInput<'a> {
    handle: u64,
    planner: &'a mut Planner,
}
impl<'a> ObservationInput<'a> {
    pub fn new(planner: &'a mut Planner, options: ObservationOptions) -> Result<Self> {
        observation_abi()?;
        let o = sys::ObservationOptions {
            struct_size: size_of::<sys::ObservationOptions>() as u32,
            ordering: options.ordering as u32,
            max_age_us: options.max_age_us,
        };
        let mut handle = 0;
        unsafe {
            check(
                sys::abc_observation_create(planner.handle, &o, &mut handle),
                0,
            )?;
        }
        Ok(Self { handle, planner })
    }
    pub fn planner_mut(&mut self) -> &mut Planner {
        self.planner
    }
    pub fn submit(&mut self, observation: TargetObservation) -> Result<Vec2> {
        let o = sys::TargetObservation {
            struct_size: size_of::<sys::TargetObservation>() as u32,
            kind: observation.relative as u32,
            availability: observation.availability as u32,
            reserved: 0,
            capture_time_us: observation.capture_time_us,
            receipt_time_us: observation.receipt_time_us,
            value: observation.value,
        };
        let mut out = Vec2::default();
        unsafe {
            check(sys::abc_observation_submit(self.handle, &o, &mut out), 0)?;
        }
        Ok(out)
    }
    pub fn relative(
        &mut self,
        error: Vec2,
        capture_time_us: i64,
        receipt_time_us: i64,
    ) -> Result<Vec2> {
        self.submit(TargetObservation {
            value: error,
            capture_time_us,
            receipt_time_us,
            relative: true,
            availability: TargetAvailability::Available,
        })
    }
    pub fn absolute(
        &mut self,
        target: Vec2,
        capture_time_us: i64,
        receipt_time_us: i64,
    ) -> Result<Vec2> {
        self.submit(TargetObservation {
            value: target,
            capture_time_us,
            receipt_time_us,
            relative: false,
            availability: TargetAvailability::Available,
        })
    }
    pub fn unavailable(
        &mut self,
        capture_time_us: i64,
        receipt_time_us: i64,
        invalidate: bool,
    ) -> Result<()> {
        self.submit(TargetObservation {
            value: Vec2::default(),
            capture_time_us,
            receipt_time_us,
            relative: false,
            availability: if invalidate {
                TargetAvailability::Invalidated
            } else {
                TargetAvailability::Unavailable
            },
        })
        .map(|_| ())
    }
    pub fn diagnostics(&mut self) -> Result<ObservationDiagnostics> {
        let mut out = ObservationDiagnostics {
            struct_size: size_of::<ObservationDiagnostics>() as u32,
            ..Default::default()
        };
        unsafe {
            check(
                sys::abc_observation_get_diagnostics(self.handle, &mut out),
                0,
            )?;
        }
        Ok(out)
    }
}
impl Drop for ObservationInput<'_> {
    fn drop(&mut self) {
        unsafe {
            sys::abc_observation_destroy(self.handle);
        }
    }
}
impl Planner {
    /// Explicit initialization boundary; invalid options leave the instance intact.
    pub fn reinitialize(&mut self, options: PlannerOptions<'_>) -> Result<()> {
        let o = sys::Options {
            struct_size: size_of::<sys::Options>() as u32,
            flags: options.diagnostic_proposals as u32,
            seed: options.seed,
            initial_position: options.initial_position,
            history: options.history.as_ptr(),
            history_count: options.history.len() as u64,
        };
        unsafe { check(sys::abc_planner_reinitialize(self.handle, &o), 0) }
    }
}
#[derive(Clone, Debug)]
pub struct PreparedHistory {
    pub initial_position: Vec2,
    pub observed_position: Vec2,
    pub history: [Vec2; 160],
}
pub fn prepare_history(raw: &[Vec2], current_position: Vec2) -> Result<PreparedHistory> {
    observation_abi()?;
    let mut result = PreparedHistory {
        initial_position: Vec2::default(),
        observed_position: current_position,
        history: [Vec2::default(); 160],
    };
    unsafe {
        check(
            sys::abc_prepare_history(
                raw.as_ptr(),
                raw.len() as u64,
                current_position,
                result.history.as_mut_ptr(),
                160,
                &mut result.initial_position,
            ),
            0,
        )?;
    }
    Ok(result)
}

pub use sys::{SegmentDiagnostics, SegmentMetadata, SegmentRecoveryDiagnostics, SegmentState};
#[derive(Clone, Copy, Debug)]
pub struct SegmentOptions {
    pub ordering: ObservationOrdering,
    pub max_age_us:i64,
    pub inactivity_timeout_us:i64,
    pub observation_capacity:u32,
}
impl Default for SegmentOptions {
    fn default()->Self {Self{ordering:ObservationOrdering::Latest,max_age_us:250000,inactivity_timeout_us:-1,observation_capacity:64}}
}
/// Owns a fixed stack-sized batch. No object or allocation per 1 ms sample.
#[derive(Clone, Debug)]
pub struct Segment {
    pub metadata:SegmentMetadata,
    storage:[Sample;32],
    len:usize,
}
impl Segment {pub fn samples(&self)->&[Sample] {&self.storage[..self.len]}}
/// Exclusive mutable planner borrow, released by RAII. No Send/Sync override.
pub struct SegmentController<'a> {handle:u64,planner:&'a mut Planner}
impl<'a> SegmentController<'a> {
    pub fn required_capacity(&self)->Result<usize> {
        let mut n=0; unsafe {check(sys::abc_segment_required_capacity(self.handle,&mut n),0)?;}
        usize::try_from(n).map_err(|_|local("segment capacity exceeds addressable memory"))
    }
    pub fn state(&mut self)->Result<sys::SegmentState> {
        let mut size=0;
        let mut s=sys::SegmentState{struct_size:size_of::<sys::SegmentState>() as u32,..Default::default()};
        unsafe {
            check(sys::abc_check_abi(1,4),0)?;check(sys::abc_segment_state_layout(&mut size),0)?;
            if size!=s.struct_size {return Err(local("segment state ABI layout mismatch"));}
            check(sys::abc_segment_get_state(self.handle,&mut s),0)?;
        } Ok(s)
    }
    pub fn new(planner:&'a mut Planner,options:SegmentOptions)->Result<Self> {
        let mut sizes=(0,0,0);
        unsafe {
            check(sys::abc_check_abi(1,2),0)?;
            check(sys::abc_segment_layout(&mut sizes.0,&mut sizes.1,&mut sizes.2),0)?;
        }
        if sizes!=(size_of::<sys::SegmentOptions>() as u32,size_of::<SegmentMetadata>() as u32,size_of::<SegmentDiagnostics>() as u32) {return Err(local("segment ABI layout mismatch"));}
        let o=sys::SegmentOptions{struct_size:size_of::<sys::SegmentOptions>() as u32,ordering:options.ordering as u32,max_age_us:options.max_age_us,inactivity_timeout_us:options.inactivity_timeout_us,observation_capacity:options.observation_capacity,..Default::default()};
        let mut handle=0;
        unsafe{check(sys::abc_segment_create(planner.handle,&o,&mut handle),0)?;}
        Ok(Self{handle,planner})
    }
    pub fn submit(&mut self,o:TargetObservation)->Result<()> {
        let raw=sys::TargetObservation{struct_size:size_of::<sys::TargetObservation>() as u32,kind:o.relative as u32,availability:o.availability as u32,reserved:0,capture_time_us:o.capture_time_us,receipt_time_us:o.receipt_time_us,value:o.value};
        unsafe{check(sys::abc_segment_submit_observation(self.handle,&raw),0)}
    }
    pub fn relative(&mut self,error:Vec2,capture_time_us:i64,receipt_time_us:i64)->Result<()> {
        self.submit(TargetObservation{value:error,capture_time_us,receipt_time_us,relative:true,availability:TargetAvailability::Available})
    }
    pub fn absolute(&mut self,target:Vec2,capture_time_us:i64,receipt_time_us:i64)->Result<()> {
        self.submit(TargetObservation{value:target,capture_time_us,receipt_time_us,relative:false,availability:TargetAvailability::Available})
    }
    pub fn unavailable(&mut self,capture_time_us:i64,receipt_time_us:i64,invalidate:bool)->Result<()> {
        self.submit(TargetObservation{value:Vec2::default(),capture_time_us,receipt_time_us,relative:false,availability:if invalidate{TargetAvailability::Invalidated}else{TargetAvailability::Unavailable}})
    }
    pub fn next_segment_into(&mut self,out:&mut [Sample])->Result<(usize,SegmentMetadata)> {
        let mut n=0;let mut m=SegmentMetadata{struct_size:size_of::<SegmentMetadata>() as u32,..Default::default()};
        unsafe {let status=sys::abc_segment_next(self.handle,out.as_mut_ptr(),out.len() as u64,&mut n,&mut m);check(status,n)?;}
        Ok((n as usize,m))
    }
    pub fn next_segment(&mut self)->Result<Segment> {
        let mut result=Segment{metadata:SegmentMetadata::default(),storage:[Sample::default();32],len:0};
        let (n,m)=self.next_segment_into(&mut result.storage)?;result.len=n;result.metadata=m;Ok(result)
    }
    pub fn metadata(&mut self)->Result<SegmentMetadata> {
        let mut m=SegmentMetadata{struct_size:size_of::<SegmentMetadata>() as u32,..Default::default()};
        unsafe{check(sys::abc_segment_get_metadata(self.handle,&mut m),0)?;}Ok(m)
    }
    pub fn diagnostics(&mut self)->Result<SegmentDiagnostics> {
        let mut d=SegmentDiagnostics{struct_size:size_of::<SegmentDiagnostics>() as u32,..Default::default()};
        unsafe{check(sys::abc_segment_get_diagnostics(self.handle,&mut d),0)?;}Ok(d)
    }
    pub fn reset_diagnostics(&mut self)->Result<()> {unsafe{check(sys::abc_segment_reset_diagnostics(self.handle),0)}}
    pub fn clear_pending_observations(&mut self)->Result<()> {
        unsafe{check(sys::abc_check_abi(1,3),0)?;check(sys::abc_segment_clear_pending_observations(self.handle),0)}
    }
    pub fn recovery_diagnostics(&mut self)->Result<SegmentRecoveryDiagnostics> {
        let mut size=0;
        let mut d=SegmentRecoveryDiagnostics{struct_size:size_of::<SegmentRecoveryDiagnostics>() as u32,..Default::default()};
        unsafe{
            check(sys::abc_check_abi(1,3),0)?;check(sys::abc_segment_recovery_layout(&mut size),0)?;
            if size!=d.struct_size{return Err(local("recovery ABI layout mismatch"));}
            check(sys::abc_segment_get_recovery_diagnostics(self.handle,&mut d),0)?;
        }Ok(d)
    }
    pub fn reset_segment_session(&mut self)->Result<()> {unsafe{check(sys::abc_segment_reset_session(self.handle),0)}}
    pub fn planner_state(&self)->Result<State>{self.planner.state()}
}
impl Drop for SegmentController<'_> {fn drop(&mut self){unsafe{sys::abc_segment_destroy(self.handle);}}}
impl Renderer {
    pub fn render_segment_into(&mut self,segment:&Segment,gain:f64,out:&mut [Report])->Result<usize> {
        let mut n=0;
        unsafe{let status=sys::abc_segment_render(self.handle,segment.samples().as_ptr(),segment.len as u64,&segment.metadata,gain,out.as_mut_ptr(),out.len() as u64,&mut n);check(status,n)?;}Ok(n as usize)
    }
}
