#include "player_style_fast_path.hpp"

#include <array>
#include <cstdlib>
#include <cstdint>

namespace {

void expect(const bool condition) {
    if (!condition) {
        std::abort();
    }
}

ql1k::PlayerStyleImages seeded_images() {
    ql1k::PlayerStyleImages images{};
    std::uint8_t value{};
    for (auto& image : images) {
        for (std::byte& item : image) {
            item = static_cast<std::byte>(value++);
        }
    }
    return images;
}

} // namespace

int main() {
    std::array<std::uint8_t, 5> call_patch{};
    expect(ql1k::make_player_style_rel32_call(0x10041C71U, 0x100419F0U,
                                               call_patch));
    expect(call_patch == std::array<std::uint8_t, 5>{
                             0xE8U, 0x7AU, 0xFDU, 0xFFU, 0xFFU});
    expect(!ql1k::make_player_style_rel32_call(
        std::numeric_limits<std::uintptr_t>::max() - 3U, 0U, call_patch));

    expect(ql1k::player_style_rgba(0U, 1U) == 0xFFFFFFFFU);
    expect(ql1k::player_style_rgba(0x11223344U, 1U) == 0x44332211U);
    expect(ql1k::player_style_rgba(0x807010AAU, 2U) == 0xAA20E0FFU);

    const ql1k::PlayerStyleSource source{{0x11223344U, 0U, 0x807010AAU}, 2U};
    ql1k::PlayerStyleOutputs outputs{};
    expect(ql1k::resolve_player_style(source, outputs));
    expect(outputs == ql1k::PlayerStyleOutputs{0x44664422U, 0xFFFFFFFFU,
                                               0xAA20E0FFU});
    auto invalid_source = source;
    invalid_source.multiplier = 3U;
    expect(!ql1k::resolve_player_style(invalid_source, outputs));

    const auto before = seeded_images();
    auto after = before;
    for (std::size_t entity = 0; entity < after.size(); ++entity) {
        after[entity][ql1k::k_player_style_rgba_offset + entity] =
            static_cast<std::byte>(0xF0U + entity);
    }
    expect(ql1k::player_style_mutation_is_local(before, after));
    auto invalid = after;
    invalid[1][ql1k::k_player_style_rgba_offset - 1U] ^= std::byte{0x01U};
    expect(!ql1k::player_style_mutation_is_local(before, invalid));

    std::array<std::array<std::byte, ql1k::k_player_style_ref_entity_bytes>,
               ql1k::k_player_style_entity_count>
        entities{};
    std::array<void*, ql1k::k_player_style_entity_count> destinations{};
    std::array<const void*, ql1k::k_player_style_entity_count> sources{};
    for (std::size_t index = 0; index < entities.size(); ++index) {
        destinations[index] = entities[index].data();
        sources[index] = entities[index].data();
    }
    outputs = {0x11223344U, 0x55667788U, 0x99AABBCCU};
    expect(ql1k::apply_player_style_outputs(destinations, outputs));
    ql1k::PlayerStyleImages captured{};
    expect(ql1k::capture_player_style_images(sources, captured));
    expect(ql1k::player_style_outputs(captured) == outputs);
    destinations[2] = nullptr;
    expect(!ql1k::apply_player_style_outputs(destinations, outputs));
    return 0;
}
