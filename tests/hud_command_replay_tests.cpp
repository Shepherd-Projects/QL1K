#include "hud_command_replay.hpp"

#include <array>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <span>

namespace {

void require(const bool condition) {
    if (!condition) {
        std::abort();
    }
}

template <std::size_t Size>
void write_command(std::array<std::uint8_t, Size>& bytes,
                   const std::size_t offset, const std::uint32_t command) {
    std::memcpy(bytes.data() + offset, &command, sizeof(command));
}

void parser_accepts_only_exact_draw_commands() {
    std::array<std::uint8_t, 60> valid{};
    write_command(valid, 0, ql1k::k_hud_set_color_command);
    write_command(valid, 20, ql1k::k_hud_stretch_pic_command);
    const auto accepted = ql1k::validate_hud_command_segment(valid);
    require(accepted.valid);
    require(accepted.command_count == 2U);

    require(!ql1k::validate_hud_command_segment(
                 std::span<const std::uint8_t>(valid.data(), 59))
                 .valid);
    write_command(valid, 20, 99U);
    require(!ql1k::validate_hud_command_segment(valid).valid);
    require(!ql1k::validate_hud_command_segment({}).valid);
}

void replay_gate_fails_closed() {
    ql1k::HudReplayGate gate{true, true, true, true, true,
                             true, true, true, true, true};
    require(ql1k::should_replay_hud(gate));
    gate.preview_frame = false;
    require(!ql1k::should_replay_hud(gate));
    gate.preview_frame = true;
    gate.same_integer_time = false;
    require(!ql1k::should_replay_hud(gate));
    gate.same_integer_time = true;
    gate.gameplay_eligible = false;
    require(!ql1k::should_replay_hud(gate));
}

void replay_ttl_is_bounded() {
    constexpr std::int64_t frequency = 10'000'000;
    constexpr std::int64_t captured = 50'000'000;
    require(ql1k::hud_replay_age_valid(captured + 20'000, captured, frequency));
    require(!ql1k::hud_replay_age_valid(captured + 20'001, captured, frequency));
    require(!ql1k::hud_replay_age_valid(captured - 1, captured, frequency));
    require(!ql1k::hud_replay_age_valid(captured, captured, 0));
}

} // namespace

int main() {
    parser_accepts_only_exact_draw_commands();
    replay_gate_fails_closed();
    replay_ttl_is_bounded();
    return 0;
}
