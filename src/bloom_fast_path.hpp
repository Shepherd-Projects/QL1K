#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace ql1k {

constexpr std::uint32_t k_bloom_command_id = 10U;
constexpr std::int32_t k_minimum_bloom_mode = 1;

struct BloomCommand10 {
    std::uint32_t command_id{};
    std::uint32_t scene_texture{};
    std::uint32_t half_blur_vertical_texture{};
    std::uint32_t half_downsample_texture{};
    std::uint32_t half_blur_horizontal_texture{};
    std::uint32_t quarter_bright_texture{};
    std::uint32_t quarter_blur_vertical_texture{};
    std::uint32_t quarter_blur_horizontal_texture{};
    std::uint32_t quarter_combine_texture{};
    std::uint32_t brightpass_program{};
    std::uint32_t downsample_program{};
    std::uint32_t blur_vertical_program{};
    std::uint32_t blur_horizontal_program{};
    std::uint32_t combine_program{};
};

static_assert(sizeof(BloomCommand10) == 0x38U);
static_assert(offsetof(BloomCommand10, scene_texture) == 0x04U);
static_assert(offsetof(BloomCommand10, brightpass_program) == 0x24U);
static_assert(offsetof(BloomCommand10, downsample_program) == 0x28U);
static_assert(offsetof(BloomCommand10, combine_program) == 0x34U);

struct ZeroBloomGate {
    bool configured{};
    bool runtime_armed{};
    bool gameplay_eligible{};
    bool renderer_ready{};
    bool context_current{};
    bool uniforms_current{};
    bool bindings_valid{};
    bool cvars_valid{};
    std::int32_t bloom_mode{};
    float bloom_intensity{};
    float bloom_saturation{};
    float scene_saturation{};
    float scene_intensity{};
    std::int32_t width{};
    std::int32_t height{};
};

[[nodiscard]] inline bool should_use_zero_bloom_fast_path(
    const ZeroBloomGate& gate, const BloomCommand10* command) noexcept {
    return gate.configured && gate.runtime_armed && gate.gameplay_eligible &&
           gate.renderer_ready && gate.context_current && gate.uniforms_current &&
           gate.bindings_valid && gate.cvars_valid &&
           gate.bloom_mode == k_minimum_bloom_mode &&
           gate.bloom_intensity == 0.0F && std::isfinite(gate.bloom_intensity) &&
           std::isfinite(gate.bloom_saturation) &&
           std::isfinite(gate.scene_saturation) &&
           std::isfinite(gate.scene_intensity) &&
           gate.width > 0 && gate.height > 0 && gate.width <= 16384 &&
           gate.height <= 16384 && command != nullptr &&
           command->command_id == k_bloom_command_id &&
           command->scene_texture != 0U && command->combine_program != 0U;
}

} // namespace ql1k
