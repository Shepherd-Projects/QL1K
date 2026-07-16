#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace ql1k {

enum class RendererDiagnosticEvent : std::uint8_t {
    session_start,
    endframe,
    font_direct_before,
    font_direct_after,
    font_capture,
    font_replay_before,
    font_replay_after,
};

[[nodiscard]] constexpr const char* renderer_diagnostic_event_name(
    const RendererDiagnosticEvent event) noexcept {
    switch (event) {
        case RendererDiagnosticEvent::session_start:
            return "session_start";
        case RendererDiagnosticEvent::endframe:
            return "endframe";
        case RendererDiagnosticEvent::font_direct_before:
            return "font_direct_before";
        case RendererDiagnosticEvent::font_direct_after:
            return "font_direct_after";
        case RendererDiagnosticEvent::font_capture:
            return "font_capture";
        case RendererDiagnosticEvent::font_replay_before:
            return "font_replay_before";
        case RendererDiagnosticEvent::font_replay_after:
            return "font_replay_after";
    }
    return "unknown";
}

struct RendererDiagnosticRecord {
    static constexpr std::uint32_t k_schema_version = 1U;

    std::uint32_t schema_version{k_schema_version};
    RendererDiagnosticEvent event{};
    std::int64_t qpc{};
    std::uint32_t process_id{};
    std::uint32_t thread_id{};
    std::uintptr_t wgl_context{};
    std::uintptr_t wgl_dc{};

    std::int32_t status{};
    std::int32_t runtime_armed{};
    std::int32_t renderer_epoch{};
    std::int32_t module_epoch{};
    std::int32_t smp_active{};
    std::int32_t smp_state{};
    std::int32_t smp_synchronous{};
    std::int32_t smp_persistent{};
    std::int32_t hud_replay_configured{};
    std::int32_t zero_bloom_configured{};
    std::int32_t color_correct_configured{};
    std::int32_t shadow_cache_configured{};
    std::int32_t client_state{};
    std::int32_t key_catchers{};
    std::int32_t registration_complete{};

    std::int32_t gl_valid{};
    std::int32_t active_texture{};
    std::int32_t binding_2d{};
    std::int32_t binding_rectangle{};
    std::int32_t current_program{};
    std::int32_t framebuffer{};
    std::int32_t array_buffer{};
    std::int32_t element_array_buffer{};
    std::array<std::int32_t, 4> viewport{};
    std::array<std::int32_t, 4> scissor_box{};
    std::int32_t blend_enabled{};
    std::int32_t depth_enabled{};
    std::int32_t scissor_enabled{};
    std::int32_t cull_enabled{};

    std::int32_t current_tmu{-1};
    std::array<std::int32_t, 2> cached_textures{-1, -1};
    std::int32_t binding_cache_match{-1};
    std::int32_t frame_index{-1};
    std::uintptr_t command_root{};
    std::int32_t command_used{-1};

    std::int32_t hud_cache_valid{};
    std::uint64_t hud_cache_bytes{};
    std::int32_t hud_integer_time{};
    std::int64_t hud_capture_total{};
    std::int64_t hud_replay_total{};
    std::int64_t hud_fallback_total{};
    std::int64_t hud_reject_total{};

    std::int32_t font_texture{};
    std::int32_t font_stride{};
    std::array<std::int32_t, 4> font_rectangle{};
    std::int32_t font_root{-1};
    std::int64_t font_capture_total{};
    std::int64_t font_replay_total{};
    std::int64_t font_drop_total{};

    std::int64_t lifecycle_sequence{};
    std::int64_t font_capture_sequence{};
    std::int64_t font_replay_sequence{};
    std::int64_t context_sync_total{};
    std::int64_t context_transfer_failure_total{};
    std::int32_t wgl_failure_total{};
};

template <std::size_t Capacity>
class RendererDiagnosticQueue {
public:
    static_assert(Capacity > 0U);

    [[nodiscard]] bool push(const RendererDiagnosticRecord& record) noexcept {
        if (size_ == Capacity) {
            ++dropped_;
            return false;
        }
        records_[tail_] = record;
        tail_ = (tail_ + 1U) % Capacity;
        ++size_;
        return true;
    }

    [[nodiscard]] std::size_t drain(
        const std::span<RendererDiagnosticRecord> destination) noexcept {
        const std::size_t count = (std::min)(size_, destination.size());
        for (std::size_t index = 0; index < count; ++index) {
            destination[index] = records_[head_];
            head_ = (head_ + 1U) % Capacity;
        }
        size_ -= count;
        return count;
    }

    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] std::uint64_t dropped() const noexcept { return dropped_; }

private:
    std::array<RendererDiagnosticRecord, Capacity> records_{};
    std::size_t head_{};
    std::size_t tail_{};
    std::size_t size_{};
    std::uint64_t dropped_{};
};

[[nodiscard]] constexpr bool renderer_diagnostic_sample_due(
    const std::int64_t now, const std::int64_t previous,
    const std::int64_t interval) noexcept {
    return now > 0 && interval > 0 &&
           (previous <= 0 || now - previous >= interval);
}

[[nodiscard]] constexpr bool renderer_diagnostic_log_needs_rotation(
    const std::uint64_t current_bytes, const std::uint64_t maximum_bytes) noexcept {
    return maximum_bytes > 0U && current_bytes >= maximum_bytes;
}

} // namespace ql1k
