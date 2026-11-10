#ifndef ABCURVES_H
#define ABCURVES_H
#include <stdint.h>
#include <stddef.h>
#if defined(_WIN32)
#define ABC_CALL __cdecl
#if defined(ABC_BUILD_DLL)
#define ABC_API __declspec(dllexport)
#else
#define ABC_API __declspec(dllimport)
#endif
#else
#define ABC_CALL
#define ABC_API __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C" {
#endif
#define ABC_ABI_MAJOR 1u
#define ABC_ABI_MINOR 3u
#define ABC_API_VERSION 0x00010003u
#define ABC_COMMON_RADIANS_PER_COUNT 0.0003509487083280618
/* Opaque generation-checked tokens. Zero is never a live handle. */
typedef uint64_t abc_model_handle;
typedef uint64_t abc_planner_handle;
typedef uint64_t abc_renderer_handle;
typedef uint64_t abc_segment_handle;
typedef int32_t abc_status;
enum {
  ABC_OK = 0,
  ABC_INVALID_ARGUMENT = 1,
  ABC_INVALID_HANDLE = 2,
  ABC_NULL_POINTER = 3,
  ABC_INVALID_STATE = 4,
  ABC_MODEL_NOT_FOUND = 5,
  ABC_CORRUPT_MODEL = 6,
  ABC_UNSUPPORTED_VERSION = 7,
  ABC_ARCHITECTURE_MISMATCH = 8,
  ABC_TIMESTAMP_RANGE = 9,
  ABC_INSUFFICIENT_CAPACITY = 10,
  ABC_ALLOCATION_FAILURE = 11,
  ABC_RENDERER_FAILURE = 12,
  ABC_INTERNAL_ERROR = 13,
  ABC_RESOURCE_LIMIT = 14,
  ABC_STALE_OBSERVATION = 15,
  ABC_DUPLICATE_OBSERVATION = 16,
  ABC_OBSERVATION_ORDER = 17
};
enum { ABC_CAP_PLANNER = 1, ABC_CAP_RENDERER = 2, ABC_CAP_BATCH = 4, ABC_CAP_HISTORY = 8,
       ABC_CAP_OBSERVATION = 16, ABC_CAP_SEGMENTS = 32, ABC_CAP_SEGMENT_RECOVERY = 64 };
enum { ABC_MODE_MOVING = 0, ABC_MODE_BRAKING = 1, ABC_MODE_HOLDING = 2 };
enum { ABC_PLANNER_DIAGNOSTIC_PROPOSALS = 1 };
typedef struct abc_vec2 {
  double x, y;
} abc_vec2;
typedef struct abc_sample {
  int64_t timestamp_us;
  abc_vec2 position, displacement;
} abc_sample;
typedef struct abc_report {
  int16_t dx, dy;
} abc_report;
typedef struct abc_version_info {
  uint32_t struct_size, api_version, abi_major, abi_minor;
  uint32_t engine_major, engine_minor, engine_patch, model_format_version;
  uint32_t sample_size, vec2_size, report_size, options_size, model_info_size, state_size,
      tensor_info_size, reserved;
  uint64_t capabilities;
} abc_version_info;
typedef struct abc_model_info {
  uint32_t struct_size, model_format_version, commitment_ms, history_samples;
  uint32_t prefix_samples, forecast_samples, coarse_tokens, coarse_features;
  uint32_t hidden_units, heads, basis_coefficients, tensor_count;
  uint64_t capabilities;
  char variant[32];
  char sha256[65];
  char reserved[7];
} abc_model_info;
typedef struct abc_tensor_info {
  uint32_t struct_size, dtype, rank, reserved;
  uint64_t dimensions[8];
  uint64_t bytes;
} abc_tensor_info;
typedef struct abc_planner_options {
  uint32_t struct_size, flags;
  uint64_t seed;
  abc_vec2 initial_position;
  const abc_vec2 *history;
  uint64_t history_count;
} abc_planner_options;
typedef struct abc_planner_state {
  uint32_t struct_size, mode;
  int64_t numerical_time_us, requested_time_us, last_decision_us, last_target_receipt_us;
  uint64_t decision_count, motor_evaluations, samples_emitted;
  int32_t selected_head, selected_brake_head;
  uint32_t pending_samples, failed;
  abc_vec2 position;
} abc_planner_state;
/* Size-tagged outputs: set struct_size to sizeof(the struct) before calling. */
ABC_API abc_status ABC_CALL abc_get_version(abc_version_info *out);
ABC_API abc_status ABC_CALL abc_check_abi(uint32_t major, uint32_t minimum_minor);
/* Borrowed strings: status text is static, last-error is thread local until next call. */
ABC_API const char *ABC_CALL abc_status_message(abc_status status);
ABC_API const char *ABC_CALL abc_last_error(void);
ABC_API abc_status ABC_CALL abc_model_load(const char *utf8_directory, abc_model_handle *out);
ABC_API abc_status ABC_CALL abc_model_get_info(abc_model_handle model, abc_model_info *out);
ABC_API abc_status ABC_CALL abc_model_get_tensor_info(abc_model_handle model, const char *name,
                                                      abc_tensor_info *out);
ABC_API abc_status ABC_CALL abc_model_release(abc_model_handle model);
ABC_API abc_status ABC_CALL abc_planner_options_init(abc_planner_options *out,
                                                     uint32_t struct_size);
ABC_API abc_status ABC_CALL abc_planner_create(abc_model_handle model,
                                               const abc_planner_options *options,
                                               abc_planner_handle *out);
ABC_API abc_status ABC_CALL abc_planner_destroy(abc_planner_handle planner);
ABC_API abc_status ABC_CALL abc_planner_reset(abc_planner_handle planner, uint64_t seed);
ABC_API abc_status ABC_CALL abc_planner_set_position(abc_planner_handle planner, abc_vec2 position);
ABC_API abc_status ABC_CALL abc_planner_update_target(abc_planner_handle planner, abc_vec2 target,
                                                      int64_t receipt_us);
ABC_API abc_status ABC_CALL abc_planner_clear_target(abc_planner_handle planner,
                                                     int64_t receipt_us);
ABC_API abc_status ABC_CALL abc_planner_get_model_info(abc_planner_handle planner,
                                                       abc_model_info *out);
ABC_API abc_status ABC_CALL abc_planner_get_state(abc_planner_handle planner,
                                                  abc_planner_state *out);
ABC_API abc_status ABC_CALL abc_planner_required_capacity(abc_planner_handle planner,
                                                          int64_t timestamp_us, uint64_t *out);
/* Transactional validation. On runtime failure, written counts valid partial output;
   state may advance and latches failed until reset. Caller retains all buffers. */
ABC_API abc_status ABC_CALL abc_planner_advance(abc_planner_handle planner, int64_t timestamp_us,
                                                abc_sample *output, uint64_t capacity,
                                                uint64_t *written);
ABC_API abc_status ABC_CALL abc_planner_generate_sample(abc_planner_handle planner,
                                                        int64_t timestamp_us, abc_sample *output);
ABC_API abc_status ABC_CALL abc_renderer_create(const char *utf8_model_file,
                                                abc_renderer_handle *out);
ABC_API abc_status ABC_CALL abc_renderer_destroy(abc_renderer_handle renderer);
/* Profiles and reports use native hardware counts, X right/Y up. */
ABC_API abc_status ABC_CALL abc_renderer_set_profile(abc_renderer_handle renderer,
                                                     const abc_report *profile, uint64_t count);
/* Reset begins a fresh event using the stored profile, not after every decision. */
ABC_API abc_status ABC_CALL abc_renderer_reset(abc_renderer_handle renderer, uint64_t seed);
ABC_API abc_status ABC_CALL abc_renderer_step(abc_renderer_handle renderer,
                                              abc_vec2 native_count_intent, abc_report *out);
ABC_API abc_status ABC_CALL abc_renderer_step_batch(abc_renderer_handle renderer,
                                                    const abc_vec2 *intents, uint64_t count,
                                                    abc_report *output, uint64_t capacity,
                                                    uint64_t *written);
/* Converts differences of absolute common-count positions using calibrated gain.
   previous_position is caller state, updated after each successful report. */
ABC_API abc_status ABC_CALL abc_renderer_render_samples(abc_renderer_handle renderer,
                                                        const abc_sample *samples, uint64_t count,
                                                        abc_vec2 *previous_position,
                                                        double radians_per_native_count,
                                                        abc_report *output, uint64_t capacity,
                                                        uint64_t *written);
/* ABI 1.1: optional independent observation input; original layouts unchanged. */
typedef uint64_t abc_observation_handle;
enum { ABC_ORDER_STRICT = 0, ABC_ORDER_LATEST = 1, ABC_ORDER_CALLER = 2 };
enum { ABC_TARGET_AVAILABLE = 0, ABC_TARGET_UNAVAILABLE = 1, ABC_TARGET_INVALIDATED = 2 };
enum { ABC_TARGET_ABSOLUTE = 0, ABC_TARGET_RELATIVE = 1 };
enum { ABC_OBS_ACCEPTED = 0, ABC_OBS_INVALID = 1, ABC_OBS_TIMESTAMP = 2,
       ABC_OBS_STALE = 3, ABC_OBS_DUPLICATE = 4, ABC_OBS_ORDER = 5 };
typedef struct abc_observation_options {
  uint32_t struct_size, ordering;
  int64_t max_age_us; /* -1 disables; default 250000 */
} abc_observation_options;
typedef struct abc_target_observation {
  uint32_t struct_size, kind, availability, reserved;
  int64_t capture_time_us, receipt_time_us;
  abc_vec2 value;
} abc_target_observation;
typedef struct abc_observation_diagnostics {
  uint32_t struct_size, availability, last_rejection, reserved;
  int64_t last_capture_us, last_receipt_us, last_age_us;
  uint64_t accepted, rejected, duplicates, out_of_order, stale;
  abc_vec2 resolved_target, reference_position;
} abc_observation_diagnostics;
ABC_API abc_status ABC_CALL abc_observation_options_init(abc_observation_options *out, uint32_t size);
/* Borrows a checked planner token. Destroy input before planner; stale parent
   handles are detected. Serialize operations with all operations on its planner. */
ABC_API abc_status ABC_CALL abc_observation_create(abc_planner_handle planner,
    const abc_observation_options *options, abc_observation_handle *out);
ABC_API abc_status ABC_CALL abc_observation_destroy(abc_observation_handle input);
/* First advance/collect through receipt. Receipt lies in current closed-sample
   interval; relative values use its latest closed virtual position. No hidden advance. */
ABC_API abc_status ABC_CALL abc_observation_submit(abc_observation_handle input,
    const abc_target_observation *observation, abc_vec2 *resolved_target);
ABC_API abc_status ABC_CALL abc_observation_get_diagnostics(abc_observation_handle input,
    abc_observation_diagnostics *out);
ABC_API abc_status ABC_CALL abc_observation_layout(uint32_t *options_size,
    uint32_t *observation_size, uint32_t *diagnostics_size);
/* Explicit reinitialization; options follow existing prepared-history semantics.
   Associated observation metadata resets lazily on next input operation. */
ABC_API abc_status ABC_CALL abc_planner_reinitialize(abc_planner_handle planner,
    const abc_planner_options *options);
/* Original W5: raw_count must be 164, prepared capacity >=160. Caller buffers;
   no writes on validation failure. Filtered anchor differs from observed current. */
ABC_API abc_status ABC_CALL abc_prepare_history(const abc_vec2 *raw, uint64_t raw_count,
    abc_vec2 current_position, abc_vec2 *prepared, uint64_t capacity, abc_vec2 *filtered_anchor);

/* ABI 1.2: pull-based complete committed trajectory segments. */
typedef struct abc_segment_options {
  uint32_t struct_size, ordering, flags, reserved;
  int64_t max_age_us, inactivity_timeout_us;
  uint32_t observation_capacity, reserved2;
} abc_segment_options;
typedef struct abc_segment_metadata {
  uint32_t struct_size, mode, sample_count, fresh_planning_decision;
  uint32_t variant_ms, motor_inference;
  int32_t selected_head, selected_brake_head;
  uint64_t segment_id;
  int64_t start_timestamp_us, end_timestamp_us, first_sample_timestamp_us,
      last_sample_timestamp_us, duration_us;
  abc_vec2 initial_position, final_position;
} abc_segment_metadata;
typedef struct abc_segment_diagnostics {
  uint32_t struct_size, availability, last_rejection, pending_observations;
  uint64_t total_segments, fresh_planning_decisions, decisions_with_new_observation,
      decisions_without_new_observation, stale_observations, duplicate_observations,
      out_of_order_observations, target_unavailable_events, target_timeout_events,
      segment_generation_errors, missed_generation_deadlines, accepted_observations,
      rejected_observations, queue_full_observations;
  int64_t last_observation_age_us, last_observation_receipt_us, last_capture_us,
      last_receipt_us, last_apply_us;
  double last_planning_latency_us;
  abc_vec2 resolved_target, reference_position;
} abc_segment_diagnostics;
ABC_API abc_status ABC_CALL abc_segment_options_init(abc_segment_options *out, uint32_t size);
ABC_API abc_status ABC_CALL abc_segment_layout(uint32_t *options_size, uint32_t *metadata_size,
                                               uint32_t *diagnostics_size);
ABC_API abc_status ABC_CALL abc_segment_create(abc_planner_handle planner,
                                               const abc_segment_options *options,
                                               abc_segment_handle *out);
ABC_API abc_status ABC_CALL abc_segment_destroy(abc_segment_handle controller);
ABC_API abc_status ABC_CALL abc_segment_submit_observation(abc_segment_handle controller,
    const abc_target_observation *observation);
ABC_API abc_status ABC_CALL abc_segment_required_capacity(abc_segment_handle controller,
                                                          uint64_t *out);
ABC_API abc_status ABC_CALL abc_segment_next(abc_segment_handle controller, abc_sample *output,
                                             uint64_t capacity, uint64_t *written,
                                             abc_segment_metadata *metadata);
ABC_API abc_status ABC_CALL abc_segment_get_metadata(abc_segment_handle controller,
                                                     abc_segment_metadata *out);
ABC_API abc_status ABC_CALL abc_segment_get_diagnostics(abc_segment_handle controller,
                                                        abc_segment_diagnostics *out);
ABC_API abc_status ABC_CALL abc_segment_reset_diagnostics(abc_segment_handle controller);
ABC_API abc_status ABC_CALL abc_segment_reset_session(abc_segment_handle controller);
/* ABI 1.3. Separate snapshot: existing ABI 1.2 layouts remain unchanged. */
typedef struct abc_segment_recovery_diagnostics {
  uint32_t struct_size, pending_observations;
  int32_t last_error_status;
  uint32_t reserved;
  uint64_t recoverable_observation_errors, invalid_pending_observations;
  uint64_t nonfinite_relative_resolutions, stale_pending_observations, explicit_pending_clears;
  int64_t last_error_capture_us, last_error_receipt_us;
} abc_segment_recovery_diagnostics;
ABC_API abc_status ABC_CALL abc_segment_recovery_layout(uint32_t *diagnostics_size);
ABC_API abc_status ABC_CALL abc_segment_get_recovery_diagnostics(abc_segment_handle controller,
    abc_segment_recovery_diagnostics *out);
ABC_API abc_status ABC_CALL abc_segment_clear_pending_observations(abc_segment_handle controller);
/* Render a returned segment using persistent Renderer and segment start anchor.
   Calibration converts common angular positions to Renderer native-count intent.
   Reports must not be angular-converted again. No planner movement feedback. */
ABC_API abc_status ABC_CALL abc_segment_render(abc_renderer_handle renderer,
    const abc_sample *samples, uint64_t count, const abc_segment_metadata *metadata,
    double radians_per_native_count, abc_report *output, uint64_t capacity, uint64_t *written);
#ifdef __cplusplus
}
#endif
#endif
