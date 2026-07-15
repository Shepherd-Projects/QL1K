#pragma once

#include <bit>
#include <cstdint>

namespace ql1k {

struct ColorCorrectCommand9 {
    std::uint32_t command_id{};
    std::uint32_t scene_texture{};
    std::uint32_t program{};
    std::uint32_t reserved{};
};

static_assert(sizeof(ColorCorrectCommand9) == 16U);

struct ColorCorrectIdentityGate {
    bool configured{};
    bool runtime_armed{};
    bool gameplay_eligible{};
    bool renderer_ready{};
    bool current_context{};
    bool cvars_valid{};
    bool color_correct_enabled{};
    bool color_correct_active{};
    bool zero_bloom_fast_path_ran{};
    float gamma{};
    float contrast{};
    std::int32_t overbright_bits{};
};

[[nodiscard]] constexpr bool exact_float_one(const float value) noexcept {
    return std::bit_cast<std::uint32_t>(value) == 0x3F800000U;
}

[[nodiscard]] constexpr bool should_skip_identity_color_correct(
    const ColorCorrectIdentityGate& gate,
    const ColorCorrectCommand9* const command) noexcept {
    return gate.configured && gate.runtime_armed && gate.gameplay_eligible &&
           gate.renderer_ready && gate.current_context && gate.cvars_valid &&
           gate.color_correct_enabled && gate.color_correct_active &&
           gate.zero_bloom_fast_path_ran && exact_float_one(gate.gamma) &&
           exact_float_one(gate.contrast) && gate.overbright_bits <= 0 &&
           command != nullptr && command->command_id == 9U &&
           command->scene_texture != 0U && command->program != 0U;
}

} // namespace ql1k
