//! Raw ABI v1 declarations. Ownership and pointer validity are the caller's responsibility.
use std::ffi::c_char;
pub type Status = i32;
pub type Handle = u64;
pub const ABI_MAJOR: u32 = 1;
pub const COMMON_RADIANS_PER_COUNT: f64 = 0.0003509487083280618;
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Vec2 {
    pub x: f64,
    pub y: f64,
}
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Sample {
    pub timestamp_us: i64,
    pub position: Vec2,
    pub displacement: Vec2,
}
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Report {
    pub dx: i16,
    pub dy: i16,
}
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct Version {
    pub struct_size: u32,
    pub api_version: u32,
    pub abi_major: u32,
    pub abi_minor: u32,
    pub engine_major: u32,
    pub engine_minor: u32,
    pub engine_patch: u32,
    pub model_format_version: u32,
    pub sample_size: u32,
    pub vec2_size: u32,
    pub report_size: u32,
    pub options_size: u32,
    pub model_info_size: u32,
    pub state_size: u32,
    pub tensor_info_size: u32,
    pub reserved: u32,
    pub capabilities: u64,
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct ModelInfo {
    pub struct_size: u32,
    pub model_format_version: u32,
    pub commitment_ms: u32,
    pub history_samples: u32,
    pub prefix_samples: u32,
    pub forecast_samples: u32,
    pub coarse_tokens: u32,
    pub coarse_features: u32,
    pub hidden_units: u32,
    pub heads: u32,
    pub basis_coefficients: u32,
    pub tensor_count: u32,
    pub capabilities: u64,
    pub variant: [c_char; 32],
    pub sha256: [c_char; 65],
    pub reserved: [c_char; 7],
}
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct TensorInfo {
    pub struct_size: u32,
    pub dtype: u32,
    pub rank: u32,
    pub reserved: u32,
    pub dimensions: [u64; 8],
    pub bytes: u64,
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Options {
    pub struct_size: u32,
    pub flags: u32,
    pub seed: u64,
    pub initial_position: Vec2,
    pub history: *const Vec2,
    pub history_count: u64,
}
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct State {
    pub struct_size: u32,
    pub mode: u32,
    pub numerical_time_us: i64,
    pub requested_time_us: i64,
    pub last_decision_us: i64,
    pub last_target_receipt_us: i64,
    pub decision_count: u64,
    pub motor_evaluations: u64,
    pub samples_emitted: u64,
    pub selected_head: i32,
    pub selected_brake_head: i32,
    pub pending_samples: u32,
    pub failed: u32,
    pub position: Vec2,
}
#[cfg(not(all(windows, feature = "dynamic", not(feature = "linked"))))]
extern "C" {
    pub fn abc_model_load_embedded(commitment_ms: u32, out: *mut Handle) -> Status;
    pub fn abc_renderer_create_embedded(out: *mut Handle) -> Status;
    pub fn abc_get_version(out: *mut Version) -> Status;
    pub fn abc_check_abi(major: u32, minor: u32) -> Status;
    pub fn abc_status_message(status: Status) -> *const c_char;
    pub fn abc_last_error() -> *const c_char;
    pub fn abc_model_load(path: *const c_char, out: *mut Handle) -> Status;
    pub fn abc_model_get_info(handle: Handle, out: *mut ModelInfo) -> Status;
    pub fn abc_model_get_tensor_info(
        handle: Handle,
        name: *const c_char,
        out: *mut TensorInfo,
    ) -> Status;
    pub fn abc_model_release(handle: Handle) -> Status;
    pub fn abc_planner_options_init(out: *mut Options, size: u32) -> Status;
    pub fn abc_planner_create(model: Handle, options: *const Options, out: *mut Handle) -> Status;
    pub fn abc_planner_destroy(handle: Handle) -> Status;
    pub fn abc_planner_reset(handle: Handle, seed: u64) -> Status;
    pub fn abc_planner_set_position(handle: Handle, position: Vec2) -> Status;
    pub fn abc_planner_update_target(handle: Handle, target: Vec2, time: i64) -> Status;
    pub fn abc_planner_clear_target(handle: Handle, time: i64) -> Status;
    pub fn abc_planner_get_model_info(handle: Handle, out: *mut ModelInfo) -> Status;
    pub fn abc_planner_get_state(handle: Handle, out: *mut State) -> Status;
    pub fn abc_planner_required_capacity(handle: Handle, time: i64, out: *mut u64) -> Status;
    pub fn abc_planner_advance(
        handle: Handle,
        time: i64,
        out: *mut Sample,
        capacity: u64,
        written: *mut u64,
    ) -> Status;
    pub fn abc_planner_generate_sample(handle: Handle, time: i64, out: *mut Sample) -> Status;
    pub fn abc_renderer_create(path: *const c_char, out: *mut Handle) -> Status;
    pub fn abc_renderer_destroy(handle: Handle) -> Status;
    pub fn abc_renderer_set_profile(handle: Handle, profile: *const Report, count: u64) -> Status;
    pub fn abc_renderer_reset(handle: Handle, seed: u64) -> Status;
    pub fn abc_renderer_step(handle: Handle, intent: Vec2, out: *mut Report) -> Status;
    pub fn abc_renderer_step_batch(
        handle: Handle,
        intents: *const Vec2,
        count: u64,
        out: *mut Report,
        capacity: u64,
        written: *mut u64,
    ) -> Status;
    pub fn abc_renderer_render_samples(
        handle: Handle,
        samples: *const Sample,
        count: u64,
        previous: *mut Vec2,
        gain: f64,
        out: *mut Report,
        capacity: u64,
        written: *mut u64,
    ) -> Status;
}

// ABI 1.1 additive observation input declarations.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct ObservationOptions {
    pub struct_size: u32,
    pub ordering: u32,
    pub max_age_us: i64,
}
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct TargetObservation {
    pub struct_size: u32,
    pub kind: u32,
    pub availability: u32,
    pub reserved: u32,
    pub capture_time_us: i64,
    pub receipt_time_us: i64,
    pub value: Vec2,
}
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct ObservationDiagnostics {
    pub struct_size: u32,
    pub availability: u32,
    pub last_rejection: u32,
    pub reserved: u32,
    pub last_capture_us: i64,
    pub last_receipt_us: i64,
    pub last_age_us: i64,
    pub accepted: u64,
    pub rejected: u64,
    pub duplicates: u64,
    pub out_of_order: u64,
    pub stale: u64,
    pub resolved_target: Vec2,
    pub reference_position: Vec2,
}
#[cfg(not(all(windows, feature = "dynamic", not(feature = "linked"))))]
extern "C" {
    pub fn abc_observation_options_init(out: *mut ObservationOptions, size: u32) -> Status;
    pub fn abc_observation_create(
        planner: Handle,
        options: *const ObservationOptions,
        out: *mut Handle,
    ) -> Status;
    pub fn abc_observation_destroy(input: Handle) -> Status;
    pub fn abc_observation_submit(
        input: Handle,
        observation: *const TargetObservation,
        target: *mut Vec2,
    ) -> Status;
    pub fn abc_observation_get_diagnostics(
        input: Handle,
        out: *mut ObservationDiagnostics,
    ) -> Status;
    pub fn abc_observation_layout(
        options: *mut u32,
        observation: *mut u32,
        diagnostics: *mut u32,
    ) -> Status;
    pub fn abc_planner_reinitialize(planner: Handle, options: *const Options) -> Status;
    pub fn abc_prepare_history(
        raw: *const Vec2,
        count: u64,
        current: Vec2,
        prepared: *mut Vec2,
        capacity: u64,
        anchor: *mut Vec2,
    ) -> Status;
}

// ABI 1.2 additive complete commitment controller.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct SegmentOptions {
    pub struct_size:u32, pub ordering:u32, pub flags:u32, pub reserved:u32,
    pub max_age_us:i64, pub inactivity_timeout_us:i64,
    pub observation_capacity:u32, pub reserved2:u32,
}
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct SegmentMetadata {
    pub struct_size:u32, pub mode:u32, pub sample_count:u32, pub fresh_planning_decision:u32,
    pub variant_ms:u32, pub motor_inference:u32, pub selected_head:i32, pub selected_brake_head:i32,
    pub segment_id:u64,
    pub start_timestamp_us:i64, pub end_timestamp_us:i64, pub first_sample_timestamp_us:i64,
    pub last_sample_timestamp_us:i64, pub duration_us:i64,
    pub initial_position:Vec2, pub final_position:Vec2,
}
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct SegmentDiagnostics {
    pub struct_size:u32, pub availability:u32, pub last_rejection:u32, pub pending_observations:u32,
    pub total_segments:u64, pub fresh_planning_decisions:u64,
    pub decisions_with_new_observation:u64, pub decisions_without_new_observation:u64,
    pub stale_observations:u64, pub duplicate_observations:u64, pub out_of_order_observations:u64,
    pub target_unavailable_events:u64, pub target_timeout_events:u64,
    pub segment_generation_errors:u64, pub missed_generation_deadlines:u64,
    pub accepted_observations:u64, pub rejected_observations:u64, pub queue_full_observations:u64,
    pub last_observation_age_us:i64, pub last_observation_receipt_us:i64,
    pub last_capture_us:i64, pub last_receipt_us:i64, pub last_apply_us:i64,
    pub last_planning_latency_us:f64, pub resolved_target:Vec2, pub reference_position:Vec2,
}
#[cfg(not(all(windows, feature = "dynamic", not(feature = "linked"))))]
extern "C" {
    pub fn abc_segment_options_init(out:*mut SegmentOptions,size:u32)->Status;
    pub fn abc_segment_layout(options:*mut u32,metadata:*mut u32,diagnostics:*mut u32)->Status;
    pub fn abc_segment_create(planner:Handle,options:*const SegmentOptions,out:*mut Handle)->Status;
    pub fn abc_segment_destroy(controller:Handle)->Status;
    pub fn abc_segment_submit_observation(controller:Handle,observation:*const TargetObservation)->Status;
    pub fn abc_segment_required_capacity(controller:Handle,out:*mut u64)->Status;
    pub fn abc_segment_next(controller:Handle,out:*mut Sample,capacity:u64,written:*mut u64,metadata:*mut SegmentMetadata)->Status;
    pub fn abc_segment_get_metadata(controller:Handle,out:*mut SegmentMetadata)->Status;
    pub fn abc_segment_get_diagnostics(controller:Handle,out:*mut SegmentDiagnostics)->Status;
    pub fn abc_segment_reset_diagnostics(controller:Handle)->Status;
    pub fn abc_segment_reset_session(controller:Handle)->Status;
    pub fn abc_segment_render(renderer:Handle,samples:*const Sample,count:u64,metadata:*const SegmentMetadata,gain:f64,out:*mut Report,capacity:u64,written:*mut u64)->Status;
}

// ABI 1.3: ABI 1.2 structures remain frozen.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct SegmentRecoveryDiagnostics {
    pub struct_size:u32, pub pending_observations:u32, pub last_error_status:i32, pub reserved:u32,
    pub recoverable_observation_errors:u64, pub invalid_pending_observations:u64,
    pub nonfinite_relative_resolutions:u64, pub stale_pending_observations:u64, pub explicit_pending_clears:u64,
    pub last_error_capture_us:i64, pub last_error_receipt_us:i64,
}
#[cfg(not(all(windows, feature = "dynamic", not(feature = "linked"))))]
extern "C" {
    pub fn abc_segment_recovery_layout(size:*mut u32)->Status;
    pub fn abc_segment_get_recovery_diagnostics(controller:Handle,out:*mut SegmentRecoveryDiagnostics)->Status;
    pub fn abc_segment_clear_pending_observations(controller:Handle)->Status;
}

// ABI 1.4: additive failure/reference-window snapshot; earlier layouts frozen.
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct SegmentState {
    pub struct_size:u32, pub failure_category:u32, pub failure_reason:u32, pub reset_required:u32,
    pub failure_status:i32, pub reserved:u32,
    pub generation_attempts:u64, pub last_segment_id:u64,
    pub numerical_time_us:i64, pub requested_time_us:i64,
    pub last_attempt_start_us:i64, pub last_attempt_end_us:i64,
    pub last_segment_start_us:i64, pub last_segment_end_us:i64,
    pub relative_origin_earliest_us:i64, pub relative_origin_latest_us:i64,
    pub last_origin_error_receipt_us:i64, pub error_message:[c_char;256],
}
impl Default for SegmentState {
    fn default()->Self { unsafe { std::mem::zeroed() } }
}
#[cfg(not(all(windows, feature = "dynamic", not(feature = "linked"))))]
extern "C" {
    pub fn abc_planner_reset_session(planner:Handle)->Status;
    pub fn abc_segment_state_layout(state_size:*mut u32)->Status;
    pub fn abc_segment_get_state(controller:Handle,out:*mut SegmentState)->Status;
}

#[cfg(all(windows, feature = "dynamic", not(feature = "linked")))]
include!(concat!(env!("OUT_DIR"), "/dynamic.rs"));
