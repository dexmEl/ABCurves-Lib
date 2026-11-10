#pragma once

#include <cstdint>
#include <cstddef>
#include <limits>

namespace abc {

constexpr double COMMON_RADIANS_PER_COUNT = 0.0003509487083280618;
constexpr std::int64_t SAMPLE_US = 1000;
constexpr std::size_t HISTORY_SAMPLES = 640;
constexpr std::size_t WARM_HISTORY_SAMPLES = 160;

struct Vec2 {
    double x{0.0};
    double y{0.0};
};

constexpr Vec2 operator+(Vec2 a, Vec2 b) noexcept { return {a.x + b.x, a.y + b.y}; }
constexpr Vec2 operator-(Vec2 a, Vec2 b) noexcept { return {a.x - b.x, a.y - b.y}; }
constexpr Vec2 operator*(Vec2 a, double s) noexcept { return {a.x * s, a.y * s}; }
constexpr Vec2 operator/(Vec2 a, double s) noexcept { return {a.x / s, a.y / s}; }

struct Sample {
    std::int64_t timestamp_us{0};
    Vec2 position{};
    Vec2 displacement{};
};

enum class PlannerMode : std::uint8_t { moving = 0, braking = 1, holding = 2 };

struct Diagnostics {
    std::uint64_t decision_count{0};
    std::uint64_t motor_evaluations{0};
    std::uint64_t buffered_samples{0};
    PlannerMode mode{PlannerMode::holding};
    int selected_head{-1};
    int selected_brake_head{-1};
    std::int64_t numerical_time_us{0};
    std::int64_t last_decision_us{-1};
    std::int64_t last_target_receipt_us{-1};
    std::uint32_t pending_samples{0};
};

// Generation failures are latched until an explicit reset. These snapshots do
// not imply transactional rollback of target history, neural state or RNG.
enum class FailureCategory : std::uint32_t {
    none = 0, numerical = 1, inference = 2, resource = 3, internal = 4
};
enum class FailureReason : std::uint32_t {
    none = 0, nonfinite_policy = 1, invalid_primitive = 2,
    invalid_probabilities = 3, allocation = 4, native_exception = 5, unknown = 6
};
struct PlannerFailure {
    FailureCategory category{FailureCategory::none};
    FailureReason reason{FailureReason::none};
    bool reset_required{false};
    char message[256]{};
};

} // namespace abc
