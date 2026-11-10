#include "abcurves/model.hpp"
#include "json.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace abc {
namespace {

struct Sha256 {
  std::uint32_t h[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
  std::uint64_t bits = 0;
  std::array<std::uint8_t, 64> block{};
  std::size_t used = 0;
  static constexpr std::uint32_t rotr(std::uint32_t x, int n) {
    return (x >> n) | (x << (32 - n));
  }
  void compress(const std::uint8_t *p) {
    static constexpr std::uint32_t k[64] = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu,
        0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u,
        0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u,
        0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
        0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u,
        0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
        0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
        0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
        0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u,
        0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u, 0x1e376c08u,
        0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu,
        0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
        0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};
    std::uint32_t w[64]{};
    for (int i = 0; i < 16; ++i)
      w[i] = (std::uint32_t(p[i * 4]) << 24) |
             ((std::uint32_t)p[i * 4 + 1] << 16) |
             ((std::uint32_t)p[i * 4 + 2] << 8) | p[i * 4 + 3];
    for (int i = 16; i < 64; ++i) {
      const auto s0 =
          rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
      const auto s1 =
          rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5],
                  g = h[6], q = h[7];
    for (int i = 0; i < 64; ++i) {
      const auto S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
      const auto ch = (e & f) ^ ((~e) & g);
      const auto t1 = q + S1 + ch + k[i] + w[i];
      const auto S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
      const auto maj = (a & b) ^ (a & c) ^ (b & c);
      const auto t2 = S0 + maj;
      q = g;
      g = f;
      f = e;
      e = d + t1;
      d = c;
      c = b;
      b = a;
      a = t1 + t2;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
    h[5] += f;
    h[6] += g;
    h[7] += q;
  }
  void update(const std::uint8_t *p, std::size_t n) {
    bits += static_cast<std::uint64_t>(n) * 8;
    while (n) {
      const auto take = std::min(n, block.size() - used);
      std::memcpy(block.data() + used, p, take);
      used += take;
      p += take;
      n -= take;
      if (used == block.size()) {
        compress(block.data());
        used = 0;
      }
    }
  }
  std::string finish() {
    block[used++] = 0x80;
    if (used > 56) {
      while (used < 64)
        block[used++] = 0;
      compress(block.data());
      used = 0;
    }
    while (used < 56)
      block[used++] = 0;
    for (int i = 7; i >= 0; --i)
      block[used++] = static_cast<std::uint8_t>(bits >> (i * 8));
    compress(block.data());
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (auto x : h)
      out << std::setw(8) << x;
    return out.str();
  }
};

std::string sha256_bytes(std::span<const std::byte> bytes) {
  Sha256 hash;
  hash.update(reinterpret_cast<const std::uint8_t *>(bytes.data()), bytes.size());
  return hash.finish();
}

struct Reader {
  const std::byte *p;
  const std::byte *end;
  template <class T> T read() {
    if (static_cast<std::size_t>(end - p) < sizeof(T))
      throw std::runtime_error("truncated ABC model");
    T value{};
    std::memcpy(&value, p, sizeof(T));
    p += sizeof(T);
    return value;
  }
  void read_bytes(void *out, std::size_t n) {
    if (static_cast<std::size_t>(end - p) < n)
      throw std::runtime_error("truncated ABC model tensor");
    std::memcpy(out, p, n);
    p += n;
  }
};

std::uint16_t read_u16(Reader &r) { return r.read<std::uint16_t>(); }
std::uint32_t read_u32(Reader &r) { return r.read<std::uint32_t>(); }
std::uint64_t read_u64(Reader &r) { return r.read<std::uint64_t>(); }

std::string variant_for_commit(std::uint32_t commit) {
  if (commit == 8)
    return "continuous-8";
  if (commit == 16)
    return "continuous-16";
  if (commit == 32)
    return "continuous-32";
  throw std::runtime_error("unsupported Continuous Planner commitment");
}

void validate_tensor(const std::string &name, const TensorView &t) {
  if (t.dtype != 1 && t.dtype != 2)
    throw std::runtime_error("unsupported tensor dtype: " + name);
  std::size_t elements = 1;
  for (auto d : t.shape) {
    if (d == 0 || elements > std::numeric_limits<std::size_t>::max() / d)
      throw std::runtime_error("invalid tensor dimensions: " + name);
    elements *= d;
  }
  const std::size_t item = t.dtype == 1 ? sizeof(float) : sizeof(double);
  if (t.bytes % item != 0 || elements != t.bytes / item)
    throw std::runtime_error("tensor byte count mismatch: " + name);
  if (t.dtype == 1) {
    const auto *values = static_cast<const float *>(t.data);
    for (std::size_t i = 0; i < elements; ++i)
      if (!std::isfinite(values[i]))
        throw std::runtime_error("non-finite tensor: " + name);
  } else {
    const auto *values = static_cast<const double *>(t.data);
    for (std::size_t i = 0; i < elements; ++i)
      if (!std::isfinite(values[i]))
        throw std::runtime_error("non-finite tensor: " + name);
  }
}

} // namespace

std::shared_ptr<const Model>
Model::load(const std::filesystem::path &directory) {
  if (std::endian::native != std::endian::little)
    throw std::runtime_error("model v1 requires a little-endian host");
  const auto path = directory / "model.abcmodel";
  std::ifstream stream(path, std::ios::binary | std::ios::ate);
  if (!stream)
    throw std::runtime_error("cannot open native model: " + path.string());
  const auto size = stream.tellg();
  if (size < 24 || size > 64 * 1024 * 1024)
    throw std::runtime_error("invalid native model size");
  std::vector<std::byte> binary(static_cast<std::size_t>(size));
  stream.seekg(0);
  stream.read(reinterpret_cast<char *>(binary.data()), size);
  if (!stream)
    throw std::runtime_error("cannot read native model: " + path.string());
  std::ifstream manifest_stream(directory / "manifest.json", std::ios::binary | std::ios::ate);
  if (!manifest_stream) throw std::runtime_error("native model manifest is missing");
  const auto manifest_bytes = manifest_stream.tellg();
  if (manifest_bytes < 0 || manifest_bytes > 1024 * 1024)
    throw std::runtime_error("model manifest too large");
  std::string manifest(static_cast<std::size_t>(manifest_bytes), '\0');
  manifest_stream.seekg(0);
  manifest_stream.read(manifest.data(), manifest_bytes);
  if (!manifest_stream) throw std::runtime_error("cannot read model manifest");
  return parse(binary, manifest, directory);
}

std::shared_ptr<const Model> Model::load_memory(std::span<const std::byte> binary,
                                                std::string_view manifest) {
  return parse(binary, manifest, {});
}

std::shared_ptr<const Model> Model::parse(std::span<const std::byte> binary,
                                         std::string_view manifest,
                                         const std::filesystem::path &directory) {
  if (std::endian::native != std::endian::little)
    throw std::runtime_error("model v1 requires a little-endian host");
  if (binary.size() < 24 || binary.size() > 64 * 1024 * 1024 || manifest.size() > 1024 * 1024)
    throw std::runtime_error("invalid native model or manifest size");
  auto model = std::shared_ptr<Model>(new Model());
  model->directory_ = directory;
  model->storage_.assign(binary.begin(), binary.end());
  Reader r{model->storage_.data(),
           model->storage_.data() + model->storage_.size()};
  char magic[8]{};
  r.read_bytes(magic, sizeof magic);
  if (std::memcmp(magic, "ABCMODL1", 8) != 0)
    throw std::runtime_error("invalid ABC model magic");
  const auto version = read_u32(r);
  const auto variant_code = read_u32(r);
  const auto commitment = read_u32(r);
  const auto count = read_u32(r);
  if (version != 1 || variant_code != commitment ||
      (commitment != 8 && commitment != 16 && commitment != 32))
    throw std::runtime_error("unsupported ABC model version or variant");
  model->commitment_ms_ = static_cast<int>(commitment);
  model->variant_ = variant_for_commit(commitment);
  if (count > 1000)
    throw std::runtime_error("unreasonable ABC tensor count");
  model->aligned_tensors_.reserve(count);

  for (std::uint32_t i = 0; i < count; ++i) {
    const auto name_len = read_u16(r);
    const auto ndim = r.read<std::uint8_t>();
    const auto dtype = r.read<std::uint8_t>();
    if (read_u32(r) != 0)
      throw std::runtime_error("unsupported tensor record flags");
    if (name_len == 0 || name_len > 512 || ndim == 0 || ndim > 8)
      throw std::runtime_error("invalid ABC tensor record");
    std::string name(name_len, '\0');
    r.read_bytes(name.data(), name_len);
    TensorView view;
    view.dtype = dtype;
    view.shape.reserve(ndim);
    for (std::uint8_t d = 0; d < ndim; ++d)
      view.shape.push_back(read_u32(r));
    const auto bytes = read_u64(r);
    if (bytes > static_cast<std::uint64_t>(r.end - r.p))
      throw std::runtime_error("tensor outside ABC model");
    view.bytes = static_cast<std::size_t>(bytes);
    model->aligned_tensors_.emplace_back((view.bytes + 7) / 8);
    auto &owned = model->aligned_tensors_.back();
    std::memcpy(owned.data(), r.p, view.bytes);
    view.data = owned.data();
    r.p += bytes;
    validate_tensor(name, view);
    if (!model->tensors_.emplace(std::move(name), view).second)
      throw std::runtime_error("duplicate ABC tensor");
  }
  if (r.p != r.end)
    throw std::runtime_error("unexpected trailing bytes in ABC model");

  // A model is accepted only when all tensors used by the native planner are
  // present. This catches a wrong variant before any mutable planner state is
  // created and keeps the three bundles interchangeable at the API boundary.
  static constexpr const char *required[] = {"motor.position_basis",
                                             "motor.velocity_basis",
                                             "motor.carry_velocity",
                                             "motor.input.0.weight",
                                             "motor.input.0.bias",
                                             "motor.encoder.weight_ih_l0",
                                             "motor.encoder.weight_hh_l0",
                                             "motor.encoder.bias_ih_l0",
                                             "motor.encoder.bias_hh_l0",
                                             "motor.trunk.0.weight",
                                             "motor.trunk.0.bias",
                                             "motor.output.weight",
                                             "motor.output.bias",
                                             "motor.selection.unary.0.weight",
                                             "motor.selection.unary.0.bias",
                                             "motor.selection.unary.2.weight",
                                             "motor.selection.unary.2.bias",
                                             "motor.selection.context.0.weight",
                                             "motor.selection.context.0.bias",
                                             "motor.selection.pair.0.weight",
                                             "motor.selection.pair.0.bias",
                                             "motor.selection.pair.2.weight",
                                             "motor.selection.pair.2.bias"};
  for (const char *name : required)
    if (!model->tensors_.contains(name))
      throw std::runtime_error(std::string("native model is missing tensor: ") +
                               name);
  const auto doc = detail::JsonReader(manifest).parse();
  model->sha256_ = sha256_bytes(binary);
  if (!doc.at("model_sha256").str(model->sha256_))
    throw std::runtime_error("native model SHA-256 mismatch");
  const auto &format = doc.at("format");
  const auto &config = doc.at("planner_config");
  const auto &constants = doc.at("constants");
  if (!doc.at("schema").str("abcurves.native.model.v1") ||
      !format.at("magic").str("ABCMODL1") || !format.at("version").num(1) ||
      !format.at("endianness").str("little") ||
      !format.at("neural_dtype").str("float32") ||
      !format.at("geometry_dtype").str("float64") ||
      !doc.at("commitment_ms").num(commitment) ||
      !doc.at("variant").str("continuous-" + std::to_string(commitment) +
                             "ms") ||
      !config.at("commit").num(commitment) || !config.at("enc").str("gru") ||
      !config.at("decoder").str("prodmp") || !config.at("history").num(640) ||
      !config.at("forecast").num(128) || !config.at("head_hold").num(1) ||
      config.at("memory").kind != detail::Json::boolean ||
      config.at("memory").flag ||
      config.at("dynamics").kind != detail::Json::boolean ||
      !config.at("dynamics").flag ||
      !config.at("velocity_scale").num(1.1982087601807259) ||
      !config.at("position_scale").num(81.73350722609987) ||
      !config.at("target_secant_ms").num(32) ||
      !constants.at("common_radians_per_count").num(COMMON_RADIANS_PER_COUNT) ||
      !constants.at("history_samples").num(640) ||
      !constants.at("warm_history_samples").num(160) ||
      !constants.at("sample_us").num(1000))
    throw std::runtime_error("incompatible native model configuration");
  struct Expected {
    const char *name;
    std::uint8_t dtype;
    std::vector<std::uint32_t> shape;
  };
  const Expected expected[] = {
      {"motor.position_basis", 2, {128, 21}},
      {"motor.velocity_basis", 2, {128, 21}},
      {"motor.carry_position", 2, {128}},
      {"motor.carry_velocity", 2, {128}},
      {"motor.physical_transform", 2, {21, 21}},
      {"motor.qr_r", 2, {21, 21}},
      {"motor.singular_values", 2, {21}},
      {"motor.original_position_basis", 2, {128, 21}},
      {"motor.original_velocity_basis", 2, {128, 21}},
      {"motor.input.0.weight", 1, {96, 9}},
      {"motor.input.0.bias", 1, {96}},
      {"motor.encoder.weight_ih_l0", 1, {288, 96}},
      {"motor.encoder.weight_hh_l0", 1, {288, 96}},
      {"motor.encoder.bias_ih_l0", 1, {288}},
      {"motor.encoder.bias_hh_l0", 1, {288}},
      {"motor.trunk.0.weight", 1, {96, 158}},
      {"motor.trunk.0.bias", 1, {96}},
      {"motor.output.weight", 1, {672, 96}},
      {"motor.output.bias", 1, {672}},
      {"motor.selection.unary.0.weight", 1, {64, 112}},
      {"motor.selection.unary.0.bias", 1, {64}},
      {"motor.selection.unary.2.weight", 1, {1, 64}},
      {"motor.selection.unary.2.bias", 1, {1}},
      {"motor.selection.context.0.weight", 1, {16, 96}},
      {"motor.selection.context.0.bias", 1, {16}},
      {"motor.selection.pair.0.weight", 1, {32, 36}},
      {"motor.selection.pair.0.bias", 1, {32}},
      {"motor.selection.pair.2.weight", 1, {1, 32}},
      {"motor.selection.pair.2.bias", 1, {1}},
      {"events.encoder.0.weight", 1, {96, 32}},
      {"events.encoder.0.bias", 1, {96}},
      {"events.encoder.2.weight", 1, {96, 96}},
      {"events.encoder.2.bias", 1, {96}},
      {"events.brake.weight", 1, {176, 96}},
      {"events.brake.bias", 1, {176}},
      {"events.frequency.weight", 1, {16, 96}},
      {"events.frequency.bias", 1, {16}},
      {"events.hazard.0.weight", 1, {64, 32}},
      {"events.hazard.0.bias", 1, {64}},
      {"events.hazard.2.weight", 1, {32, 64}},
      {"events.hazard.2.bias", 1, {32}},
      {"events.hazard.4.weight", 1, {2, 32}},
      {"events.hazard.4.bias", 1, {2}}};
  if (!doc.at("tensor_count").num(count) || count != std::size(expected) ||
      doc.at("tensors").members.size() != count)
    throw std::runtime_error("incompatible tensor inventory");
  for (const auto &e : expected) {
    const auto &t = model->tensor(e.name);
    const auto &metadata = doc.at("tensors").at(e.name);
    if (t.dtype != e.dtype || t.shape != e.shape ||
        !metadata.at("dtype").str(e.dtype == 1 ? "float32" : "float64") ||
        metadata.at("shape").kind != detail::Json::array ||
        metadata.at("shape").items.size() != t.shape.size())
      throw std::runtime_error(std::string("incompatible tensor layout: ") +
                               e.name);
    for (std::size_t j = 0; j < t.shape.size(); ++j)
      if (!metadata.at("shape").items[j].num(t.shape[j]))
        throw std::runtime_error(
            "manifest tensor dimensions differ from binary");
    Sha256 tensor_hash;
    tensor_hash.update(static_cast<const std::uint8_t *>(t.data), t.bytes);
    if (!metadata.at("sha256").str(tensor_hash.finish()))
      throw std::runtime_error("manifest tensor hash mismatch");
  }
  model->storage_.clear();
  model->storage_.shrink_to_fit();
  return std::shared_ptr<const Model>(std::move(model));
}

const TensorView &Model::tensor(const std::string &name) const {
  const auto it = tensors_.find(name);
  if (it == tensors_.end())
    throw std::out_of_range("native model tensor not found: " + name);
  return it->second;
}

} // namespace abc
