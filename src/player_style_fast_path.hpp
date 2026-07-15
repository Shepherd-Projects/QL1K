#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace ql1k {

constexpr std::size_t k_player_style_entity_count = 3U;
constexpr std::size_t k_player_style_ref_entity_bytes = 0x8CU;
constexpr std::size_t k_player_style_rgba_offset = 0x74U;
constexpr std::size_t k_player_style_rgba_bytes = sizeof(std::uint32_t);

using PlayerStyleImage =
    std::array<std::byte, k_player_style_ref_entity_bytes>;
using PlayerStyleImages =
    std::array<PlayerStyleImage, k_player_style_entity_count>;
using PlayerStyleOutputs =
    std::array<std::uint32_t, k_player_style_entity_count>;

[[nodiscard]] inline bool make_player_style_rel32_call(
    const std::uintptr_t call_site, const std::uintptr_t target,
    std::array<std::uint8_t, 5>& patch) noexcept {
    if (call_site > std::numeric_limits<std::uintptr_t>::max() - patch.size()) {
        return false;
    }
    const std::int64_t displacement =
        static_cast<std::int64_t>(target) -
        static_cast<std::int64_t>(call_site + patch.size());
    if (displacement < std::numeric_limits<std::int32_t>::min() ||
        displacement > std::numeric_limits<std::int32_t>::max()) {
        return false;
    }
    patch[0] = 0xE8U;
    const auto encoded = static_cast<std::int32_t>(displacement);
    std::memcpy(patch.data() + 1U, &encoded, sizeof(encoded));
    return true;
}

struct PlayerStyleSource {
    std::array<std::uint32_t, k_player_style_entity_count> packed_colors{};
    std::uint32_t multiplier{};

    [[nodiscard]] bool valid() const noexcept {
        return multiplier == 1U || multiplier == 2U;
    }
};

[[nodiscard]] inline std::uint32_t player_style_rgba(
    const std::uint32_t packed, const std::uint32_t multiplier) noexcept {
    if (packed == 0U) {
        return 0xFFFFFFFFU;
    }
    const auto saturate = [multiplier](const std::uint32_t channel) noexcept {
        const std::uint32_t scaled = channel * multiplier;
        return scaled > 255U ? 255U : scaled;
    };
    const std::uint32_t red = saturate((packed >> 24U) & 0xFFU);
    const std::uint32_t green = saturate((packed >> 16U) & 0xFFU);
    const std::uint32_t blue = saturate((packed >> 8U) & 0xFFU);
    const std::uint32_t alpha = packed & 0xFFU;
    return red | (green << 8U) | (blue << 16U) | (alpha << 24U);
}

[[nodiscard]] inline bool resolve_player_style(
    const PlayerStyleSource& source, PlayerStyleOutputs& outputs) noexcept {
    if (!source.valid()) {
        return false;
    }
    for (std::size_t index = 0; index < outputs.size(); ++index) {
        outputs[index] = player_style_rgba(source.packed_colors[index],
                                           source.multiplier);
    }
    return true;
}

[[nodiscard]] inline bool capture_player_style_images(
    const std::array<const void*, k_player_style_entity_count>& entities,
    PlayerStyleImages& images) noexcept {
    for (std::size_t index = 0; index < entities.size(); ++index) {
        if (entities[index] == nullptr) {
            return false;
        }
        std::memcpy(images[index].data(), entities[index], images[index].size());
    }
    return true;
}

[[nodiscard]] inline PlayerStyleOutputs player_style_outputs(
    const PlayerStyleImages& images) noexcept {
    PlayerStyleOutputs outputs{};
    for (std::size_t index = 0; index < images.size(); ++index) {
        std::memcpy(&outputs[index],
                    images[index].data() + k_player_style_rgba_offset,
                    sizeof(outputs[index]));
    }
    return outputs;
}

[[nodiscard]] inline bool player_style_mutation_is_local(
    const PlayerStyleImages& before, const PlayerStyleImages& after) noexcept {
    for (std::size_t entity = 0; entity < before.size(); ++entity) {
        for (std::size_t offset = 0; offset < before[entity].size(); ++offset) {
            const bool rgba = offset >= k_player_style_rgba_offset &&
                              offset < k_player_style_rgba_offset +
                                           k_player_style_rgba_bytes;
            if (!rgba && before[entity][offset] != after[entity][offset]) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] inline bool apply_player_style_outputs(
    const std::array<void*, k_player_style_entity_count>& entities,
    const PlayerStyleOutputs& outputs) noexcept {
    for (std::size_t index = 0; index < entities.size(); ++index) {
        if (entities[index] == nullptr) {
            return false;
        }
        auto* const bytes = static_cast<std::byte*>(entities[index]);
        std::memcpy(bytes + k_player_style_rgba_offset, &outputs[index],
                    sizeof(outputs[index]));
    }
    return true;
}

} // namespace ql1k
