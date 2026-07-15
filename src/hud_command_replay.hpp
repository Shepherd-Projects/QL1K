#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace ql1k {

inline constexpr std::uint32_t k_hud_set_color_command = 1U;
inline constexpr std::uint32_t k_hud_stretch_pic_command = 2U;
inline constexpr std::size_t k_hud_set_color_size = 20U;
inline constexpr std::size_t k_hud_stretch_pic_size = 40U;
inline constexpr std::int64_t k_hud_replay_max_age_ms = 2;

struct HudSegmentValidation {
    bool valid{};
    std::size_t command_count{};
};

[[nodiscard]] inline HudSegmentValidation validate_hud_command_segment(
    const std::span<const std::uint8_t> segment) noexcept {
    if (segment.empty()) {
        return {};
    }

    std::size_t offset = 0;
    std::size_t command_count = 0;
    while (offset < segment.size()) {
        if (segment.size() - offset < sizeof(std::uint32_t)) {
            return {};
        }
        std::uint32_t command{};
        std::memcpy(&command, segment.data() + offset, sizeof(command));
        const std::size_t command_size =
            command == k_hud_set_color_command
                ? k_hud_set_color_size
                : (command == k_hud_stretch_pic_command
                       ? k_hud_stretch_pic_size
                       : 0U);
        if (command_size == 0U || segment.size() - offset < command_size) {
            return {};
        }
        offset += command_size;
        ++command_count;
    }
    return {offset == segment.size(), command_count};
}

[[nodiscard]] constexpr bool hud_replay_age_valid(
    const std::int64_t now, const std::int64_t captured,
    const std::int64_t frequency) noexcept {
    if (now <= 0 || captured <= 0 || frequency <= 0 || now < captured) {
        return false;
    }
    const std::int64_t elapsed = now - captured;
    return elapsed <= (frequency * k_hud_replay_max_age_ms) / 1000;
}

struct HudReplayGate {
    bool configured{};
    bool runtime_armed{};
    bool preview_frame{};
    bool gameplay_eligible{};
    bool renderer_ready{};
    bool cache_valid{};
    bool same_integer_time{};
    bool same_renderer_epoch{};
    bool same_module_epoch{};
    bool recent_capture{};
};

[[nodiscard]] constexpr bool should_replay_hud(
    const HudReplayGate& gate) noexcept {
    return gate.configured && gate.runtime_armed && gate.preview_frame &&
           gate.gameplay_eligible && gate.renderer_ready && gate.cache_valid &&
           gate.same_integer_time && gate.same_renderer_epoch &&
           gate.same_module_epoch && gate.recent_capture;
}

} // namespace ql1k
