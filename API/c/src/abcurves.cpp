#include "abcurves.h"
#include "abcurves/native.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
namespace {
thread_local char error_text[1024]{};
struct Fault {
  abc_status code;
  const char *message;
};
void require(bool condition, abc_status code, const char *message) {
  if (!condition)
    throw Fault{code, message};
}
template <class F> abc_status protect(F &&f, abc_status fallback = ABC_INTERNAL_ERROR) noexcept {
  error_text[0] = 0;
  try {
    f();
    return ABC_OK;
  } catch (const Fault &e) {
    std::snprintf(error_text, sizeof(error_text), "%s", e.message);
    return e.code;
  } catch (const std::bad_alloc &) {
    std::snprintf(error_text, sizeof(error_text), "allocation failed");
    return ABC_ALLOCATION_FAILURE;
  } catch (const abc::SegmentCapacityError &e) {
    std::snprintf(error_text, sizeof(error_text), "%s", e.what());
    return ABC_RESOURCE_LIMIT;
  } catch (const abc::ObservationError &e) {
    std::snprintf(error_text, sizeof(error_text), "%s", e.what());
    switch (e.reason) {
    case abc::ObservationRejection::stale: return ABC_STALE_OBSERVATION;
    case abc::ObservationRejection::duplicate: return ABC_DUPLICATE_OBSERVATION;
    case abc::ObservationRejection::out_of_order: return ABC_OBSERVATION_ORDER;
    case abc::ObservationRejection::timestamp: return ABC_TIMESTAMP_RANGE;
    default: return ABC_INVALID_ARGUMENT;
    }
  } catch (const std::invalid_argument &e) {
    std::snprintf(error_text, sizeof(error_text), "%s", e.what());
    return ABC_INVALID_ARGUMENT;
  } catch (const std::out_of_range &e) {
    std::snprintf(error_text, sizeof(error_text), "%s", e.what());
    return ABC_INVALID_ARGUMENT;
  } catch (const std::logic_error &e) {
    std::snprintf(error_text, sizeof(error_text), "%s", e.what());
    return ABC_INVALID_STATE;
  } catch (const std::exception &e) {
    std::snprintf(error_text, sizeof(error_text), "%s", e.what());
    return fallback;
  } catch (...) {
    std::snprintf(error_text, sizeof(error_text), "unknown native exception");
    return ABC_INTERNAL_ERROR;
  }
}
constexpr unsigned slot_count = 4096;
enum class Kind { model, planner, renderer, observation, segment };
struct Object {
  Kind kind;
  explicit Object(Kind k) : kind(k) {}
  virtual ~Object() = default;
};
struct ModelObject : Object {
  std::shared_ptr<const abc::Model> value;
  explicit ModelObject(std::shared_ptr<const abc::Model> m)
      : Object(Kind::model), value(std::move(m)) {}
};
struct PlannerObject : Object {
  abc::ContinuousPlanner value;
  int64_t requested = 0, last_queued = -1;
  bool failed = false;
  PlannerObject(std::shared_ptr<const abc::Model> m, abc::PlannerOptions o)
      : Object(Kind::planner), value(std::move(m), std::move(o)) {}
};
struct RendererObject : Object {
  abc::Renderer value;
  std::vector<abc::Report> profile;
  bool begun = false, failed = false;
  int64_t last_sample_time = -1;
  explicit RendererObject(const std::filesystem::path &p) : Object(Kind::renderer), value(p) {}
  explicit RendererObject(std::span<const std::uint8_t> bytes) : Object(Kind::renderer), value(bytes) {}
};

std::span<const std::byte> embedded_resource(unsigned id) {
#ifdef _WIN32
  HMODULE module = nullptr;
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          reinterpret_cast<LPCWSTR>(&embedded_resource), &module))
    throw std::runtime_error("cannot locate ABCurves DLL resources");
  auto resource = FindResourceW(module, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10));
  if (!resource) throw std::runtime_error("embedded model resource not found");
  auto size = SizeofResource(module, resource);
  auto loaded = LoadResource(module, resource);
  auto data = loaded ? LockResource(loaded) : nullptr;
  if (!data || !size) throw std::runtime_error("embedded model resource is empty");
  return {static_cast<const std::byte *>(data), size};
#else
  (void)id;
  throw Fault{ABC_UNSUPPORTED_VERSION, "embedded models are available in Windows DLL builds"};
#endif
}

std::shared_ptr<const abc::Model> embedded_model(uint32_t commitment) {
  const unsigned index = commitment == 8 ? 0 : commitment == 16 ? 1 : 2;
  static std::array<std::once_flag, 3> flags;
  static std::array<std::shared_ptr<const abc::Model>, 3> models;
  std::call_once(flags[index], [=] {
    auto binary = embedded_resource(101 + index * 2);
    auto metadata = embedded_resource(102 + index * 2);
    auto model = abc::Model::load_memory(binary, {reinterpret_cast<const char *>(metadata.data()), metadata.size()});
    if (model->commitment_ms() != static_cast<int>(commitment))
      throw std::runtime_error("embedded model variant mismatch");
    models[index] = std::move(model);
  });
  return models[index];
}
struct ObservationObject : Object {
  uint64_t parent;
  abc::ObservationInput value;
  ObservationObject(uint64_t h, abc::ContinuousPlanner& p, abc::ObservationOptions o)
      : Object(Kind::observation), parent(h), value(p, o) {}
};
struct SegmentObject : Object {
  uint64_t parent;
  abc::SegmentController value;
  SegmentObject(uint64_t h, abc::ContinuousPlanner& p, abc::SegmentOptions o)
      : Object(Kind::segment), parent(h), value(p, o) {}
};
struct Slot {
  std::atomic<uint64_t> token{0};
  std::atomic<Object *> object{nullptr};
};
std::array<Slot, slot_count> slots;
std::mutex registry_mutex;
uint64_t generation = 0;
uint64_t put(std::unique_ptr<Object> object) {
  std::lock_guard lock(registry_mutex);
  require(generation < (UINT64_MAX >> 16), ABC_RESOURCE_LIMIT, "handle generations exhausted");
  for (unsigned i = 0; i < slot_count; i++)
    if (slots[i].token.load() == 0) {
      auto token = (++generation << 16) | (i + 1);
      slots[i].object.store(object.release(), std::memory_order_relaxed);
      slots[i].token.store(token, std::memory_order_release);
      return token;
    }
  throw Fault{ABC_RESOURCE_LIMIT, "4096 live handles limit reached"};
}
Object *lookup(uint64_t handle, Kind kind) {
  auto index = unsigned(handle & 65535u);
  require(index > 0 && index <= slot_count, ABC_INVALID_HANDLE, "invalid handle");
  auto &slot = slots[index - 1];
  require(slot.token.load(std::memory_order_acquire) == handle, ABC_INVALID_HANDLE,
          "expired or invalid handle");
  auto *object = slot.object.load(std::memory_order_relaxed);
  require(object && object->kind == kind, ABC_INVALID_HANDLE, "wrong handle type");
  return object;
}
void destroy(uint64_t handle, Kind kind) {
  std::lock_guard lock(registry_mutex);
  auto *p = lookup(handle, kind);
  auto &slot = slots[(handle & 65535u) - 1];
  slot.token.store(0, std::memory_order_release);
  slot.object.store(nullptr, std::memory_order_relaxed);
  delete p;
}
PlannerObject &planner(uint64_t h) {
  return *static_cast<PlannerObject *>(lookup(h, Kind::planner));
}
RendererObject &renderer(uint64_t h) {
  return *static_cast<RendererObject *>(lookup(h, Kind::renderer));
}
template <class T> void output_struct(T *p) {
  require(p, ABC_NULL_POINTER, "null output structure");
  auto size = p->struct_size;
  require(size >= sizeof(T), ABC_INVALID_ARGUMENT, "structure too small for ABI v1");
  *p = {};
  p->struct_size = size;
}
std::filesystem::path path(const char *p) {
  require(p, ABC_NULL_POINTER, "null UTF-8 path");
  require(*p, ABC_INVALID_ARGUMENT, "empty path");
  return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(p)));
}
abc::Vec2 vec(abc_vec2 p) { return {p.x, p.y}; }
abc_vec2 vec(abc::Vec2 p) { return {p.x, p.y}; }
bool finite(abc_vec2 p) { return std::isfinite(p.x) && std::isfinite(p.y); }
void sample_copy(abc_sample &out, const abc::Sample &s) {
  out = {s.timestamp_us, vec(s.position), vec(s.displacement)};
}
uint64_t required(PlannerObject &p, int64_t t) {
  require(!p.failed && !p.value.failed(), ABC_INVALID_STATE, "planner failed; reset required");
  require(!p.value.has_segment_controller(), ABC_INVALID_STATE, "SegmentController owns planner input/output");
  require(t >= 0 && t >= p.requested, ABC_TIMESTAMP_RANGE,
          "timestamp must be nonnegative and monotonic");
  return uint64_t((t - p.value.diagnostics().numerical_time_us) / 1000);
}
void advance(PlannerObject &p, int64_t t, abc_sample *out, uint64_t capacity, uint64_t *written) {
  require(written, ABC_NULL_POINTER, "null samples-written output");
  *written = 0;
  auto count = required(p, t);
  require(capacity <= SIZE_MAX / sizeof(abc_sample), ABC_INVALID_ARGUMENT,
          "capacity exceeds addressable memory");
  require(count <= capacity, ABC_INSUFFICIENT_CAPACITY, "output capacity too small");
  require(!count || out, ABC_NULL_POINTER, "null sample buffer");
  p.requested = t;
  if (!count) {
    p.value.advance(t, nullptr, 0);
    return;
  }
  std::array<abc::Sample, 128> scratch;
  while (*written < count) {
    auto n = std::min<uint64_t>(128, count - *written);
    for (size_t i = 0; i < n; i++)
      scratch[i].timestamp_us = -1;
    auto endpoint =
        count - *written <= 128 ? t : p.value.diagnostics().numerical_time_us + int64_t(n) * 1000;
    try {
      p.value.advance(endpoint, scratch.data(), size_t(n));
    } catch (...) {
      for (size_t i = 0; i < n && scratch[i].timestamp_us >= 0; i++)
        sample_copy(out[(*written)++], scratch[i]);
      p.failed = true;
      throw;
    }
    for (size_t i = 0; i < n; i++)
      sample_copy(out[(*written)++], scratch[i]);
  }
}
void render_ready(RendererObject &r) {
  require(r.begun && !r.failed, ABC_INVALID_STATE,
          "Renderer requires reset with a prepared profile");
}
void counts(uint64_t n, uint64_t capacity, const void *in, void *out, uint64_t *written) {
  require(written, ABC_NULL_POINTER, "null reports-written output");
  *written = 0;
  require(n <= SIZE_MAX / sizeof(abc_sample) && capacity <= SIZE_MAX / sizeof(abc_report),
          ABC_INVALID_ARGUMENT, "count exceeds addressable memory");
  require(n <= capacity, ABC_INSUFFICIENT_CAPACITY, "report capacity too small");
  require(!n || (in && out), ABC_NULL_POINTER, "null Renderer buffer");
}
void valid_intent(abc_vec2 v) {
  require(finite(v), ABC_INVALID_ARGUMENT, "nonfinite Renderer intent");
  for (double x : {v.x, v.y}) {
    double q = double(float(x)) * 65536.;
    require(std::isfinite(q) && q >= double(INT32_MIN) - .5 && q < double(INT32_MAX) + .5,
            ABC_INVALID_ARGUMENT, "Renderer intent exceeds Q16 range");
  }
}
void model_info(const abc::Model &m, abc_model_info *out) {
  output_struct(out);
  out->model_format_version = 1;
  out->commitment_ms = m.commitment_ms();
  out->history_samples = 640;
  out->prefix_samples = 160;
  out->forecast_samples = 128;
  out->coarse_tokens = 70;
  out->coarse_features = 9;
  out->hidden_units = 96;
  out->heads = 16;
  out->basis_coefficients = 21;
  out->tensor_count = 43;
  out->capabilities = 13;
  std::snprintf(out->variant, sizeof(out->variant), "%s", m.variant().c_str());
  std::snprintf(out->sha256, sizeof(out->sha256), "%s", m.sha256().c_str());
}

} // namespace
extern "C" {
abc_status ABC_CALL abc_get_version(abc_version_info *out) {
  return protect([&] {
    output_struct(out);
    out->api_version = ABC_API_VERSION;
    out->abi_major = 1;
    out->abi_minor = ABC_ABI_MINOR;
    out->engine_major = ABC_ENGINE_VERSION_MAJOR;
    out->engine_minor = ABC_ENGINE_VERSION_MINOR;
    out->engine_patch = ABC_ENGINE_VERSION_PATCH;
    out->model_format_version = 1;
    out->sample_size = sizeof(abc_sample);
    out->vec2_size = sizeof(abc_vec2);
    out->report_size = sizeof(abc_report);
    out->options_size = sizeof(abc_planner_options);
    out->model_info_size = sizeof(abc_model_info);
    out->state_size = sizeof(abc_planner_state);
    out->tensor_info_size = sizeof(abc_tensor_info);
    out->capabilities = 255;
  });
}
abc_status ABC_CALL abc_check_abi(uint32_t major, uint32_t minor) {
  return protect([&] {
    require(major == 1 && minor <= ABC_ABI_MINOR, ABC_UNSUPPORTED_VERSION,
            "ABI major/minor incompatible");
  });
}
const char *ABC_CALL abc_last_error(void) { return error_text; }
const char *ABC_CALL abc_status_message(abc_status s) {
  switch (s) {
  case ABC_OK:
    return "success";
  case ABC_INVALID_ARGUMENT:
    return "invalid argument";
  case ABC_INVALID_HANDLE:
    return "invalid handle";
  case ABC_NULL_POINTER:
    return "null pointer";
  case ABC_INVALID_STATE:
    return "invalid state";
  case ABC_MODEL_NOT_FOUND:
    return "model not found";
  case ABC_CORRUPT_MODEL:
    return "corrupt model";
  case ABC_UNSUPPORTED_VERSION:
    return "unsupported version";
  case ABC_ARCHITECTURE_MISMATCH:
    return "model architecture mismatch";
  case ABC_TIMESTAMP_RANGE:
    return "timestamp out of range";
  case ABC_INSUFFICIENT_CAPACITY:
    return "insufficient output capacity";
  case ABC_ALLOCATION_FAILURE:
    return "allocation failure";
  case ABC_RENDERER_FAILURE:
    return "Renderer failure";
  case ABC_RESOURCE_LIMIT:
    return "resource limit";
  case ABC_STALE_OBSERVATION: return "stale observation";
  case ABC_DUPLICATE_OBSERVATION: return "duplicate observation";
  case ABC_OBSERVATION_ORDER: return "observation out of order";
  default:
    return "internal error";
  }
}
abc_status ABC_CALL abc_model_load(const char *directory, abc_model_handle *out) {
  return protect(
      [&] {
        require(out, ABC_NULL_POINTER, "null model handle output");
        *out = 0;
        auto p = path(directory);
        require(std::filesystem::is_regular_file(p / "model.abcmodel") &&
                    std::filesystem::is_regular_file(p / "manifest.json"),
                ABC_MODEL_NOT_FOUND, "model binary or manifest not found");
        std::ifstream stream(p / "model.abcmodel", std::ios::binary);
        std::array<unsigned char, 24> header{};
        stream.read(reinterpret_cast<char *>(header.data()), header.size());
        if (stream && std::memcmp(header.data(), "ABCMODL1", 8) == 0) {
          auto u = [&](int i) {
            return uint32_t(header[i]) | (uint32_t(header[i + 1]) << 8) |
                   (uint32_t(header[i + 2]) << 16) | (uint32_t(header[i + 3]) << 24);
          };
          require(u(8) == 1, ABC_UNSUPPORTED_VERSION, "unsupported model binary version");
          require((u(12) == 8 || u(12) == 16 || u(12) == 32) && u(12) == u(16),
                  ABC_ARCHITECTURE_MISMATCH, "unsupported model variant/commitment");
        }
        try {
          *out = put(std::make_unique<ModelObject>(abc::Model::load(p)));
        } catch (const std::runtime_error &e) {
          std::string_view m = e.what();
          if (m.find("incompatible") != m.npos || m.find("unsupported") != m.npos)
            throw Fault{ABC_ARCHITECTURE_MISMATCH,
                        "model architecture/configuration incompatible; validate the bundle"};
          throw;
        }
      },
      ABC_CORRUPT_MODEL);
}
abc_status ABC_CALL abc_model_load_embedded(uint32_t commitment_ms, abc_model_handle *out) {
  return protect([&] {
    require(out, ABC_NULL_POINTER, "null model handle output");
    *out = 0;
    require(commitment_ms == 8 || commitment_ms == 16 || commitment_ms == 32,
            ABC_INVALID_ARGUMENT, "embedded commitment must be 8, 16, or 32 ms");
    *out = put(std::make_unique<ModelObject>(embedded_model(commitment_ms)));
  }, ABC_CORRUPT_MODEL);
}
abc_status ABC_CALL abc_model_get_info(abc_model_handle h, abc_model_info *out) {
  return protect(
      [&] { model_info(*static_cast<ModelObject *>(lookup(h, Kind::model))->value, out); });
}
abc_status ABC_CALL abc_planner_get_model_info(abc_planner_handle h, abc_model_info *out) {
  return protect([&] { model_info(planner(h).value.model(), out); });
}
abc_status ABC_CALL abc_model_get_tensor_info(abc_model_handle h, const char *name,
                                              abc_tensor_info *out) {
  return protect([&] {
    auto &m = *static_cast<ModelObject *>(lookup(h, Kind::model))->value;
    require(name, ABC_NULL_POINTER, "null tensor name");
    auto &t = m.tensor(name);
    output_struct(out);
    out->dtype = t.dtype;
    out->rank = uint32_t(t.shape.size());
    out->bytes = t.bytes;
    std::copy(t.shape.begin(), t.shape.end(), out->dimensions);
  });
}
abc_status ABC_CALL abc_model_release(abc_model_handle h) {
  return protect([&] { destroy(h, Kind::model); });
}
abc_status ABC_CALL abc_planner_options_init(abc_planner_options *out, uint32_t size) {
  return protect([&] {
    require(out, ABC_NULL_POINTER, "null options");
    require(size >= sizeof(*out), ABC_INVALID_ARGUMENT, "options structure too small");
    *out = {};
    out->struct_size = size;
    out->seed = 7;
  });
}
abc_status ABC_CALL abc_planner_create(abc_model_handle h, const abc_planner_options *options,
                                       abc_planner_handle *out) {
  return protect([&] {
    require(out, ABC_NULL_POINTER, "null planner handle output");
    *out = 0;
    auto model = static_cast<ModelObject *>(lookup(h, Kind::model))->value;
    abc::PlannerOptions o;
    if (options) {
      require(options->struct_size >= sizeof(*options) && !(options->flags & ~1u),
              ABC_INVALID_ARGUMENT, "incompatible planner options");
      require(options->history_count == 0 || options->history_count == 160, ABC_INVALID_ARGUMENT,
              "history must contain 160 prepared samples");
      require(!options->history_count || options->history, ABC_NULL_POINTER, "null history");
      o.seed = options->seed;
      o.initial_position = vec(options->initial_position);
      o.diagnostics = (options->flags & 1) != 0;
      for (uint64_t i = 0; i < options->history_count; i++)
        o.initial_history.push_back(vec(options->history[i]));
    }
    *out = put(std::make_unique<PlannerObject>(std::move(model), std::move(o)));
  });
}
abc_status ABC_CALL abc_planner_destroy(abc_planner_handle h) {
  return protect([&] { destroy(h, Kind::planner); });
}
abc_status ABC_CALL abc_planner_reset(abc_planner_handle h, uint64_t seed) {
  return protect([&] {
    auto &p = planner(h);
    p.value.reset(seed);
    p.requested = 0;
    p.last_queued = -1;
    p.failed = false;
  });
}
abc_status ABC_CALL abc_planner_set_position(abc_planner_handle h, abc_vec2 p) {
  return protect([&] { planner(h).value.set_position(vec(p)); });
}
abc_status ABC_CALL abc_planner_update_target(abc_planner_handle h, abc_vec2 target, int64_t t) {
  return protect([&] {
    auto &p = planner(h);
    require(!p.failed, ABC_INVALID_STATE, "planner failed; reset required");
    require(t >= p.value.diagnostics().numerical_time_us && t >= 0 && t >= p.last_queued,
            ABC_TIMESTAMP_RANGE, "receipt precedes numerical time or queued receipt");
    p.value.update_target(vec(target), t);
    p.last_queued = t;
  });
}
abc_status ABC_CALL abc_planner_clear_target(abc_planner_handle h, int64_t t) {
  return protect([&] {
    auto &p = planner(h);
    require(!p.failed, ABC_INVALID_STATE, "planner failed; reset required");
    require(t >= p.value.diagnostics().numerical_time_us && t >= 0 && t >= p.last_queued,
            ABC_TIMESTAMP_RANGE, "receipt precedes numerical time or queued receipt");
    p.value.clear_target(t);
    p.last_queued = t;
  });
}
abc_status ABC_CALL abc_planner_get_state(abc_planner_handle h, abc_planner_state *out) {
  return protect([&] {
    auto &p = planner(h);
    auto d = p.value.diagnostics();
    output_struct(out);
    out->mode = uint32_t(p.value.mode());
    out->numerical_time_us = d.numerical_time_us;
    out->requested_time_us = p.requested;
    out->last_decision_us = d.last_decision_us;
    out->last_target_receipt_us = d.last_target_receipt_us;
    out->decision_count = d.decision_count;
    out->motor_evaluations = d.motor_evaluations;
    out->samples_emitted = d.buffered_samples;
    out->selected_head = d.selected_head;
    out->selected_brake_head = d.selected_brake_head;
    out->pending_samples = d.pending_samples;
    out->failed = p.failed || p.value.failed();
    out->position = vec(p.value.position());
  });
}
abc_status ABC_CALL abc_planner_required_capacity(abc_planner_handle h, int64_t t, uint64_t *out) {
  return protect([&] {
    require(out, ABC_NULL_POINTER, "null required capacity");
    *out = required(planner(h), t);
  });
}
abc_status ABC_CALL abc_planner_advance(abc_planner_handle h, int64_t t, abc_sample *out,
                                        uint64_t capacity, uint64_t *written) {
  return protect([&] {
    if (written)
      *written = 0;
    advance(planner(h), t, out, capacity, written);
  });
}
abc_status ABC_CALL abc_planner_generate_sample(abc_planner_handle h, int64_t t, abc_sample *out) {
  return protect([&] {
    auto &p = planner(h);
    require(out, ABC_NULL_POINTER, "null sample output");
    require(required(p, t) == 1, ABC_INVALID_ARGUMENT, "exactly one closed endpoint required");
    uint64_t n;
    advance(p, t, out, 1, &n);
  });
}
abc_status ABC_CALL abc_renderer_create(const char *file, abc_renderer_handle *out) {
  return protect(
      [&] {
        require(out, ABC_NULL_POINTER, "null Renderer handle output");
        *out = 0;
        auto p = path(file);
        require(std::filesystem::is_regular_file(p), ABC_MODEL_NOT_FOUND,
                "Renderer model not found");
        *out = put(std::make_unique<RendererObject>(p));
      },
      ABC_RENDERER_FAILURE);
}
abc_status ABC_CALL abc_renderer_create_embedded(abc_renderer_handle *out) {
  return protect([&] {
    require(out, ABC_NULL_POINTER, "null Renderer handle output");
    *out = 0;
    auto bytes = embedded_resource(107);
    *out = put(std::make_unique<RendererObject>(std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t *>(bytes.data()), bytes.size())));
  }, ABC_RENDERER_FAILURE);
}
abc_status ABC_CALL abc_renderer_destroy(abc_renderer_handle h) {
  return protect([&] { destroy(h, Kind::renderer); });
}
abc_status ABC_CALL abc_renderer_set_profile(abc_renderer_handle h, const abc_report *reports,
                                             uint64_t n) {
  return protect(
      [&] {
        auto &r = renderer(h);
        require(n == 256, ABC_INVALID_ARGUMENT, "profile requires 256 reports");
        require(reports, ABC_NULL_POINTER, "null profile");
        std::vector<abc::Report> profile;
        profile.reserve(256);
        for (size_t i = 0; i < 256; i++)
          profile.push_back({reports[i].dx, reports[i].dy});
        r.begun = false;
        r.failed = true;
        r.value.observe_profile(profile);
        r.profile = std::move(profile);
        r.begun = false;
        r.failed = false;
        r.last_sample_time = -1;
      },
      ABC_RENDERER_FAILURE);
}
abc_status ABC_CALL abc_renderer_reset(abc_renderer_handle h, uint64_t seed) {
  return protect(
      [&] {
        auto &r = renderer(h);
        require(r.profile.size() == 256, ABC_INVALID_STATE, "Renderer profile not prepared");
        r.begun = false;
        r.failed = true;
        r.value.observe_profile(r.profile);
        r.value.begin(seed);
        r.begun = true;
        r.failed = false;
        r.last_sample_time = -1;
      },
      ABC_RENDERER_FAILURE);
}
abc_status ABC_CALL abc_renderer_step(abc_renderer_handle h, abc_vec2 v, abc_report *out) {
  return protect(
      [&] {
        auto &r = renderer(h);
        render_ready(r);
        require(out, ABC_NULL_POINTER, "null report output");
        valid_intent(v);
        try {
          auto report = r.value.step(vec(v));
          *out = {report.dx, report.dy};
        } catch (...) {
          r.failed = true;
          throw;
        }
      },
      ABC_RENDERER_FAILURE);
}
abc_status ABC_CALL abc_renderer_step_batch(abc_renderer_handle h, const abc_vec2 *in, uint64_t n,
                                            abc_report *out, uint64_t capacity, uint64_t *written) {
  return protect(
      [&] {
        if (written)
          *written = 0;
        auto &r = renderer(h);
        render_ready(r);
        counts(n, capacity, in, out, written);
        for (size_t i = 0; i < n; i++)
          valid_intent(in[i]);
        try {
          for (size_t i = 0; i < n; i++) {
            auto v = r.value.step(vec(in[i]));
            out[i] = {v.dx, v.dy};
            ++*written;
          }
        } catch (...) {
          r.failed = true;
          throw;
        }
      },
      ABC_RENDERER_FAILURE);
}
abc_status ABC_CALL abc_renderer_render_samples(abc_renderer_handle h, const abc_sample *in,
                                                uint64_t n, abc_vec2 *previous, double gain,
                                                abc_report *out, uint64_t capacity,
                                                uint64_t *written) {
  return protect(
      [&] {
        if (written)
          *written = 0;
        auto &r = renderer(h);
        render_ready(r);
        counts(n, capacity, in, out, written);
        require(!n || previous, ABC_NULL_POINTER, "null previous position");
        require(std::isfinite(gain) && gain > 0, ABC_INVALID_ARGUMENT,
                "calibrated gain must be positive and finite");
        double scale = gain / abc::COMMON_RADIANS_PER_COUNT;
        require(std::isfinite(scale) && scale > 0, ABC_INVALID_ARGUMENT,
                "invalid calibrated scale");
        if (!n)
          return;
        require(finite(*previous), ABC_INVALID_ARGUMENT, "invalid previous position");
        abc_vec2 last = *previous;
        int64_t time = r.last_sample_time;
        for (size_t i = 0; i < n; i++) {
          require(in[i].timestamp_us > time, ABC_TIMESTAMP_RANGE,
                  "Renderer sample timestamps must increase");
          valid_intent({(in[i].position.x - last.x) / scale, (in[i].position.y - last.y) / scale});
          last = in[i].position;
          time = in[i].timestamp_us;
        }
        try {
          for (size_t i = 0; i < n; i++) {
            auto v = r.value.step({(in[i].position.x - previous->x) / scale,
                                   (in[i].position.y - previous->y) / scale});
            out[i] = {v.dx, v.dy};
            *previous = in[i].position;
            r.last_sample_time = in[i].timestamp_us;
            ++*written;
          }
        } catch (...) {
          r.failed = true;
          throw;
        }
      },
      ABC_RENDERER_FAILURE);
}
}

// Additive ABI 1.1 entry points. Existing planner output path has no input hook.
namespace {
abc::PlannerOptions initialization_options(const abc_planner_options* options) {
  abc::PlannerOptions o;
  if (!options) return o;
  require(options->struct_size >= sizeof(*options) && !(options->flags & ~1u),
          ABC_INVALID_ARGUMENT, "incompatible planner options");
  require(options->history_count == 0 || options->history_count == 160,
          ABC_INVALID_ARGUMENT, "history must contain 160 prepared samples");
  require(!options->history_count || options->history, ABC_NULL_POINTER, "null history");
  o.seed = options->seed;
  o.initial_position = vec(options->initial_position);
  o.diagnostics = (options->flags & 1) != 0;
  for (uint64_t i=0; i<options->history_count; ++i) o.initial_history.push_back(vec(options->history[i]));
  return o;
}
ObservationObject& observation(uint64_t h) {
  auto& o = *static_cast<ObservationObject*>(lookup(h, Kind::observation));
  auto& p = planner(o.parent); // validate before dereferencing borrowed reference
  require(!p.failed, ABC_INVALID_STATE, "planner failed; reset required");
  return o;
}
SegmentObject& segment(uint64_t h) {
  auto& s = *static_cast<SegmentObject*>(lookup(h, Kind::segment));
  planner(s.parent); // parent token check; failed parents still permit snapshots/reset
  return s;
}
}
extern "C" {
abc_status ABC_CALL abc_observation_options_init(abc_observation_options* out, uint32_t size) {
  return protect([&] {
    require(out, ABC_NULL_POINTER, "null observation options");
    require(size >= sizeof(*out), ABC_INVALID_ARGUMENT, "observation options too small");
    *out = {}; out->struct_size = size; out->max_age_us = 250000;
  });
}
abc_status ABC_CALL abc_observation_layout(uint32_t* a, uint32_t* b, uint32_t* c) {
  return protect([&] {
    require(a && b && c, ABC_NULL_POINTER, "null layout output");
    *a=sizeof(abc_observation_options); *b=sizeof(abc_target_observation); *c=sizeof(abc_observation_diagnostics);
  });
}
abc_status ABC_CALL abc_observation_create(abc_planner_handle h, const abc_observation_options* options,
                                           abc_observation_handle* out) {
  return protect([&] {
    require(out, ABC_NULL_POINTER, "null observation handle output"); *out=0;
    auto& p=planner(h);
    require(!p.failed, ABC_INVALID_STATE, "planner failed; reset required");
    abc::ObservationOptions o;
    if (options) {
      require(options->struct_size >= sizeof(*options), ABC_INVALID_ARGUMENT, "observation options too small");
      o.ordering=static_cast<abc::ObservationOrdering>(options->ordering); o.max_age_us=options->max_age_us;
    }
    *out=put(std::make_unique<ObservationObject>(h,p.value,o));
  });
}
abc_status ABC_CALL abc_observation_destroy(abc_observation_handle h) {
  return protect([&] { destroy(h,Kind::observation); });
}
abc_status ABC_CALL abc_observation_submit(abc_observation_handle h, const abc_target_observation* o,
                                           abc_vec2* target) {
  return protect([&] {
    require(o && target, ABC_NULL_POINTER, "null observation or resolved-target output");
    require(o->struct_size >= sizeof(*o) && o->reserved == 0, ABC_INVALID_ARGUMENT, "incompatible observation structure");
    auto& input=observation(h);
    auto& p=planner(input.parent);
    auto resolved=input.value.submit({vec(o->value),o->capture_time_us,o->receipt_time_us,
      static_cast<abc::ObservationKind>(o->kind),static_cast<abc::TargetAvailability>(o->availability)});
    p.last_queued=o->receipt_time_us;
    *target=vec(resolved);
  });
}
abc_status ABC_CALL abc_observation_get_diagnostics(abc_observation_handle h, abc_observation_diagnostics* out) {
  return protect([&] {
    auto d=observation(h).value.diagnostics(); output_struct(out);
    out->availability=uint32_t(d.availability); out->last_rejection=uint32_t(d.last_rejection);
    out->last_capture_us=d.last_capture_us; out->last_receipt_us=d.last_receipt_us; out->last_age_us=d.last_age_us;
    out->accepted=d.accepted; out->rejected=d.rejected; out->duplicates=d.duplicates;
    out->out_of_order=d.out_of_order; out->stale=d.stale;
    out->resolved_target=vec(d.resolved_target); out->reference_position=vec(d.reference_position);
  });
}
abc_status ABC_CALL abc_planner_reinitialize(abc_planner_handle h, const abc_planner_options* options) {
  return protect([&] {
    auto& p=planner(h); p.value.reinitialize(initialization_options(options));
    p.requested=0; p.last_queued=-1; p.failed=false;
  });
}
abc_status ABC_CALL abc_prepare_history(const abc_vec2* raw, uint64_t count, abc_vec2 current,
    abc_vec2* prepared, uint64_t capacity, abc_vec2* anchor) {
  return protect([&] {
    require(count==164, ABC_INVALID_ARGUMENT, "exactly 164 raw displacements required");
    require(capacity>=160 && capacity<=SIZE_MAX/sizeof(abc_vec2), ABC_INSUFFICIENT_CAPACITY, "prepared output capacity must be >=160");
    require(raw && prepared && anchor, ABC_NULL_POINTER, "null history input/output");
    std::array<abc::Vec2,164> input;
    for (size_t i=0;i<164;++i) input[i]=vec(raw[i]);
    auto start=abc::prepare_history(input,vec(current));
    for (size_t i=0;i<160;++i) prepared[i]=vec(start.history[i]);
    *anchor=vec(start.initial_position);
  });
}
abc_status ABC_CALL abc_segment_options_init(abc_segment_options* out, uint32_t size) {
  return protect([&] {
    require(out, ABC_NULL_POINTER, "null segment options");
    require(size >= sizeof(*out), ABC_INVALID_ARGUMENT, "segment options too small");
    *out = {};
    out->struct_size = size;
    out->ordering = ABC_ORDER_LATEST;
    out->max_age_us = 250000;
    out->inactivity_timeout_us = -1;
    out->observation_capacity = 64;
  });
}
abc_status ABC_CALL abc_segment_layout(uint32_t* a, uint32_t* b, uint32_t* c) {
  return protect([&] {
    require(a && b && c, ABC_NULL_POINTER, "null segment layout output");
    *a = sizeof(abc_segment_options);
    *b = sizeof(abc_segment_metadata);
    *c = sizeof(abc_segment_diagnostics);
  });
}
abc_status ABC_CALL abc_segment_create(abc_planner_handle h, const abc_segment_options* options,
                                       abc_segment_handle* out) {
  return protect([&] {
    require(out, ABC_NULL_POINTER, "null segment handle output");
    *out = 0;
    auto& p = planner(h);
    require(!p.failed, ABC_INVALID_STATE, "planner failed; reset required");
    abc::SegmentOptions o;
    if (options) {
      require(options->struct_size >= sizeof(*options) && options->flags == 0 &&
                  options->reserved == 0 && options->reserved2 == 0,
              ABC_INVALID_ARGUMENT, "incompatible segment options");
      require(options->ordering <= ABC_ORDER_CALLER, ABC_INVALID_ARGUMENT,
              "invalid observation ordering");
      require(options->max_age_us >= -1 && options->inactivity_timeout_us >= -1 &&
                  options->observation_capacity > 0 && options->observation_capacity <= 64,
              ABC_INVALID_ARGUMENT, "invalid segment observation options");
      o.ordering = static_cast<abc::ObservationOrdering>(options->ordering);
      o.max_age_us = options->max_age_us;
      o.inactivity_timeout_us = options->inactivity_timeout_us;
      o.observation_capacity = options->observation_capacity;
    }
    *out = put(std::make_unique<SegmentObject>(h, p.value, o));
  });
}
abc_status ABC_CALL abc_segment_destroy(abc_segment_handle h) {
  return protect([&] { destroy(h, Kind::segment); });
}
abc_status ABC_CALL abc_segment_submit_observation(abc_segment_handle h,
                                                   const abc_target_observation* o) {
  return protect([&] {
    require(o, ABC_NULL_POINTER, "null segment observation");
    require(o->struct_size >= sizeof(*o) && o->reserved == 0, ABC_INVALID_ARGUMENT,
            "incompatible observation structure");
    auto& s = segment(h);
    s.value.submit_observation({vec(o->value), o->capture_time_us,
                                                o->receipt_time_us,
                                                static_cast<abc::ObservationKind>(o->kind),
                                                static_cast<abc::TargetAvailability>(o->availability)});
  });
}
abc_status ABC_CALL abc_segment_required_capacity(abc_segment_handle h, uint64_t* out) {
  return protect([&] {
    require(out, ABC_NULL_POINTER, "null segment capacity output");
    *out = segment(h).value.required_capacity();
  });
}
void copy_segment_metadata(const abc::SegmentMetadata& in, abc_segment_metadata* out) {
  output_struct(out);
  out->mode = static_cast<uint32_t>(in.mode);
  out->sample_count = in.sample_count;
  out->fresh_planning_decision = in.fresh_planning_decision ? 1u : 0u;
  out->segment_id = in.segment_id;
  out->start_timestamp_us = in.start_timestamp_us;
  out->end_timestamp_us = in.end_timestamp_us;
  out->first_sample_timestamp_us = in.first_sample_timestamp_us;
  out->last_sample_timestamp_us = in.last_sample_timestamp_us;
  out->duration_us = in.duration_us;
  out->initial_position = vec(in.initial_position);
  out->final_position = vec(in.final_position);
  out->variant_ms = in.variant_ms;
  out->motor_inference = in.motor_inference;
  out->selected_head = in.selected_head;
  out->selected_brake_head = in.selected_brake_head;
}
abc_status ABC_CALL abc_segment_next(abc_segment_handle h, abc_sample* output, uint64_t capacity,
                                     uint64_t* written, abc_segment_metadata* metadata) {
  return protect([&] {
    require(written, ABC_NULL_POINTER, "null segment written output");
    *written = 0;
    require(metadata, ABC_NULL_POINTER, "null segment metadata output");
    require(metadata->struct_size >= sizeof(*metadata), ABC_INVALID_ARGUMENT, "segment metadata too small");
    auto& s = segment(h);
    const auto required = s.value.required_capacity();
    require(capacity >= required, ABC_INSUFFICIENT_CAPACITY, "segment output capacity too small");
    require(output, ABC_NULL_POINTER, "null segment output");
    abc::SegmentMetadata m;
    std::array<abc::Sample, 32> scratch;
    std::size_t n;
    try { n = s.value.next_segment(scratch, m); }
    catch (...) {
      auto& p = planner(s.parent);
      p.failed = p.failed || p.value.failed();
      p.requested = p.value.requested_time_us();
      throw;
    }
    for (std::size_t i = 0; i < n; ++i)
      sample_copy(output[i], scratch[i]);
    *written = n;
    auto& p = planner(s.parent);
    p.requested = p.value.requested_time_us();
    p.last_queued = p.value.diagnostics().last_target_receipt_us;
    copy_segment_metadata(m, metadata);
  });
}
abc_status ABC_CALL abc_segment_get_metadata(abc_segment_handle h, abc_segment_metadata* out) {
  return protect([&] {
    auto& s = segment(h);
    const auto m = s.value.metadata();
    copy_segment_metadata(m, out);
  });
}
abc_status ABC_CALL abc_segment_get_diagnostics(abc_segment_handle h,
                                                abc_segment_diagnostics* out) {
  return protect([&] {
    auto d = segment(h).value.diagnostics();
    output_struct(out);
    out->availability = static_cast<uint32_t>(d.availability);
    out->last_rejection = static_cast<uint32_t>(d.last_rejection);
    out->pending_observations = d.pending_observations;
    out->total_segments = d.total_segments;
    out->fresh_planning_decisions = d.fresh_planning_decisions;
    out->decisions_with_new_observation = d.decisions_with_new_observation;
    out->decisions_without_new_observation = d.decisions_without_new_observation;
    out->stale_observations = d.stale_observations;
    out->duplicate_observations = d.duplicate_observations;
    out->out_of_order_observations = d.out_of_order_observations;
    out->target_unavailable_events = d.target_unavailable_events;
    out->target_timeout_events = d.target_timeout_events;
    out->segment_generation_errors = d.segment_generation_errors;
    out->missed_generation_deadlines = d.missed_generation_deadlines;
    out->last_observation_age_us = d.last_observation_age_us;
    out->last_observation_receipt_us = d.last_observation_receipt_us;
    out->last_capture_us = d.last_capture_us;
    out->last_receipt_us = d.last_receipt_us;
    out->last_planning_latency_us = d.last_planning_latency_us;
    out->last_apply_us = d.last_apply_us;
    out->resolved_target = vec(d.resolved_target);
    out->reference_position = vec(d.reference_position);
    out->accepted_observations = d.accepted_observations;
    out->rejected_observations = d.rejected_observations;
    out->queue_full_observations = d.queue_full_observations;
  });
}
abc_status ABC_CALL abc_segment_reset_diagnostics(abc_segment_handle h) {
  return protect([&] { segment(h).value.reset_diagnostics(); });
}
abc_status ABC_CALL abc_segment_recovery_layout(uint32_t* size) {
  return protect([&] { require(size,ABC_NULL_POINTER,"null recovery layout");
    *size=sizeof(abc_segment_recovery_diagnostics); });
}
abc_status ABC_CALL abc_segment_get_recovery_diagnostics(abc_segment_handle h,
    abc_segment_recovery_diagnostics* out) {
  return protect([&] {
    auto d=segment(h).value.recovery_diagnostics(); output_struct(out);
    out->pending_observations=d.pending_observations; out->reserved=0;
    switch(d.last_recoverable_error) {
      case abc::ObservationRejection::none: out->last_error_status=ABC_OK; break;
      case abc::ObservationRejection::invalid: out->last_error_status=ABC_INVALID_ARGUMENT; break;
      case abc::ObservationRejection::stale: out->last_error_status=ABC_STALE_OBSERVATION; break;
      case abc::ObservationRejection::timestamp: out->last_error_status=ABC_TIMESTAMP_RANGE; break;
      default: throw std::logic_error("invalid recovery diagnostic reason");
    }
    out->recoverable_observation_errors=d.recoverable_observation_errors;
    out->invalid_pending_observations=d.invalid_pending_observations;
    out->nonfinite_relative_resolutions=d.nonfinite_relative_resolutions;
    out->stale_pending_observations=d.stale_pending_observations;
    out->explicit_pending_clears=d.explicit_pending_clears;
    out->last_error_capture_us=d.last_error_capture_us;
    out->last_error_receipt_us=d.last_error_receipt_us;
  });
}
abc_status ABC_CALL abc_segment_clear_pending_observations(abc_segment_handle h) {
  return protect([&] { segment(h).value.clear_pending_observations(); });
}
abc_status ABC_CALL abc_segment_reset_session(abc_segment_handle h) {
  return protect([&] {
    auto& s = segment(h); s.value.reset_segment_session();
    auto& p = planner(s.parent); p.requested=0; p.last_queued=-1; p.failed=false;
  });
}
abc_status ABC_CALL abc_planner_reset_session(abc_planner_handle h) {
  return protect([&] {
    auto& p=planner(h); p.value.reset();
    p.requested=0; p.last_queued=-1; p.failed=false;
  });
}
abc_status ABC_CALL abc_segment_state_layout(uint32_t* out) {
  return protect([&] {require(out,ABC_NULL_POINTER,"null state layout"); *out=sizeof(abc_segment_state);});
}
abc_status ABC_CALL abc_segment_get_state(abc_segment_handle h, abc_segment_state* out) {
  return protect([&] {
    auto s=segment(h).value.state(); output_struct(out);
    out->failure_category=static_cast<uint32_t>(s.failure.category);
    out->failure_reason=static_cast<uint32_t>(s.failure.reason);
    out->reset_required=s.failure.reset_required;
    out->failure_status=!s.failure.reset_required?ABC_OK:
        s.failure.category==abc::FailureCategory::resource?ABC_ALLOCATION_FAILURE:ABC_INTERNAL_ERROR;
    out->generation_attempts=s.generation_attempts; out->last_segment_id=s.last_segment_id;
    out->numerical_time_us=s.numerical_time_us; out->requested_time_us=s.requested_time_us;
    out->last_attempt_start_us=s.last_attempt_start_us; out->last_attempt_end_us=s.last_attempt_end_us;
    out->last_segment_start_us=s.last_segment_start_us; out->last_segment_end_us=s.last_segment_end_us;
    out->relative_origin_earliest_us=s.relative_origin_earliest_us;
    out->relative_origin_latest_us=s.relative_origin_latest_us;
    out->last_origin_error_receipt_us=s.last_origin_error_receipt_us;
    std::memcpy(out->error_message,s.failure.message,sizeof out->error_message);
  });
}
}

extern "C" abc_status ABC_CALL abc_segment_render(abc_renderer_handle h,
    const abc_sample* samples, uint64_t count, const abc_segment_metadata* metadata,
    double gain, abc_report* output, uint64_t capacity, uint64_t* written) {
  // Use the established C99 Renderer path, supplying the segment's own anchor.
  // Only Renderer state changes; no previous position is fed to the planner.
  const auto validation = protect([&] {
    require(written, ABC_NULL_POINTER, "null segment report count");
    *written=0;
    require(metadata, ABC_NULL_POINTER, "null segment metadata");
    require(metadata->struct_size>=sizeof(*metadata), ABC_INVALID_ARGUMENT, "segment metadata too small");
    require(count==metadata->sample_count, ABC_INVALID_ARGUMENT, "segment sample count mismatch");
  },ABC_RENDERER_FAILURE);
  if(validation) return validation;
  auto previous=metadata->initial_position;
  return abc_renderer_render_samples(h,samples,count,&previous,gain,output,capacity,written);
}
