#pragma once

#include "types.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace abc {

struct TensorView {
    const void* data{nullptr};
    std::vector<std::uint32_t> shape;
    std::uint8_t dtype{0}; // 1 = float32, 2 = float64
    std::size_t bytes{0};
};

class Model final {
public:
    Model(const Model&) = delete;
    Model& operator=(const Model&) = delete;
    static std::shared_ptr<const Model> load(const std::filesystem::path& directory);
    static std::shared_ptr<const Model> load_memory(std::span<const std::byte> binary,
                                                    std::string_view manifest);

    const std::string& sha256() const noexcept { return sha256_; }
    int commitment_ms() const noexcept { return commitment_ms_; }
    const std::string& variant() const noexcept { return variant_; }
    const TensorView& tensor(const std::string& name) const;
    const std::filesystem::path& source_directory() const noexcept { return directory_; }

private:
    Model() = default;
    static std::shared_ptr<const Model> parse(std::span<const std::byte> binary,
                                               std::string_view manifest,
                                               const std::filesystem::path& directory);
    std::filesystem::path directory_;
    std::string variant_;
    std::string sha256_;
    int commitment_ms_{0};
    std::vector<std::byte> storage_;
    std::vector<std::vector<std::uint64_t>> aligned_tensors_;
    std::unordered_map<std::string, TensorView> tensors_;
};

} // namespace abc
