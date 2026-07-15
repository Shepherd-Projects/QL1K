#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace ql1k {

enum class FrameOwnership : std::uint8_t {
    stock,
    render_only,
    positive_simulation,
};

constexpr int scheduler_floor(const bool runtime_armed,
                              const bool preview_chain_armed) noexcept {
    return runtime_armed && preview_chain_armed ? 0 : 1;
}

constexpr FrameOwnership classify_frame(const int delta,
                                        const bool zero_token,
                                        const bool preview_chain_armed) noexcept {
    if (delta == 0 && zero_token && preview_chain_armed) {
        return FrameOwnership::render_only;
    }
    return delta > 0 ? FrameOwnership::positive_simulation : FrameOwnership::stock;
}

constexpr bool owns_gameplay_cadence(const FrameOwnership ownership) noexcept {
    return ownership == FrameOwnership::positive_simulation;
}

constexpr bool should_reuse_cgame_cvars(const bool runtime_armed,
                                        const bool preview_active,
                                        const bool cache_warmed) noexcept {
    return runtime_armed && preview_active && cache_warmed;
}

inline float wrapped_angle_delta(const float preview, const float committed) noexcept {
    if (!std::isfinite(preview) || !std::isfinite(committed)) {
        return 0.0F;
    }
    return std::remainder(preview - committed, 360.0F);
}

inline bool materially_changed_view(const float current_pitch, const float current_yaw,
                                    const float previous_pitch, const float previous_yaw,
                                    const float epsilon_degrees = 0.001F) noexcept {
    if (!std::isfinite(current_pitch) || !std::isfinite(current_yaw) ||
        !std::isfinite(previous_pitch) || !std::isfinite(previous_yaw) ||
        !std::isfinite(epsilon_degrees) || epsilon_degrees < 0.0F) {
        return false;
    }
    return std::fabs(wrapped_angle_delta(current_pitch, previous_pitch)) > epsilon_degrees ||
           std::fabs(wrapped_angle_delta(current_yaw, previous_yaw)) > epsilon_degrees;
}

struct AxisViewangles final {
    float pitch{};
    float yaw{};
    bool valid{};
};

inline AxisViewangles viewangles_from_forward_axis(const float* const forward) noexcept {
    if (forward == nullptr || !std::isfinite(forward[0]) || !std::isfinite(forward[1]) ||
        !std::isfinite(forward[2])) {
        return {};
    }
    const float horizontal = std::hypot(forward[0], forward[1]);
    const float magnitude = std::hypot(horizontal, forward[2]);
    if (!std::isfinite(magnitude) || magnitude < 0.5F) {
        return {};
    }
    constexpr float radians_to_degrees = 57.295779513082320876F;
    return {
        std::atan2(-forward[2], horizontal) * radians_to_degrees,
        std::atan2(forward[1], forward[0]) * radians_to_degrees,
        true,
    };
}

template <std::size_t Size>
class ByteSnapshot final {
public:
    void capture(const void* source) noexcept {
        std::memcpy(bytes_.data(), source, Size);
        captured_ = true;
    }

    bool restore(void* destination) const noexcept {
        if (!captured_) {
            return false;
        }
        std::memcpy(destination, bytes_.data(), Size);
        return true;
    }

    [[nodiscard]] bool matches(const void* candidate) const noexcept {
        return captured_ && std::memcmp(bytes_.data(), candidate, Size) == 0;
    }

    [[nodiscard]] bool captured() const noexcept {
        return captured_;
    }

private:
    std::array<std::uint8_t, Size> bytes_{};
    bool captured_{};
};

class ViewOverlay final {
public:
    bool apply(float* viewangles, const float pitch_delta, const float yaw_delta) noexcept {
        if (viewangles == nullptr || applied_ || !std::isfinite(pitch_delta) ||
            !std::isfinite(yaw_delta)) {
            return false;
        }
        std::memcpy(saved_.data(), viewangles, sizeof(saved_));
        viewangles[0] += pitch_delta;
        viewangles[1] += yaw_delta;
        applied_ = true;
        return true;
    }

    bool restore(float* viewangles) noexcept {
        if (viewangles == nullptr || !applied_) {
            return false;
        }
        std::memcpy(viewangles, saved_.data(), sizeof(saved_));
        applied_ = false;
        return true;
    }

    [[nodiscard]] bool applied() const noexcept {
        return applied_;
    }

private:
    std::array<float, 3> saved_{};
    bool applied_{};
};

}  // namespace ql1k
