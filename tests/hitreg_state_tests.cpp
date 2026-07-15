#include "hitreg_state.hpp"

#include <cstdint>
#include <cstdio>
#include <limits>
#include <string_view>

namespace {

int g_failures{};

void expect(const bool condition, const char* const message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++g_failures;
    }
}

void fire(ql1k::HitregState& state, const std::int32_t time,
          const ql1k::HitregTraceKind kind, const bool opponent_contact) {
    expect(state.wants_client_ray(ql1k::HitregState::k_lightning_weapon, time),
           "a new LG event requests exactly one native client ray");
    state.on_weapon_fire(ql1k::HitregState::k_lightning_weapon, time, kind,
                         opponent_contact, true);
}

void close_and_ack(ql1k::HitregState& state, const std::int32_t release_time,
                   const std::int32_t old_time, const std::int32_t new_time,
                   const std::uint32_t damage = 0) {
    state.on_usercmd(release_time, false);
    state.on_server_feedback(old_time, new_time, damage);
}

void one_native_ray_per_event() {
    ql1k::HitregState state;
    fire(state, 100, ql1k::HitregTraceKind::world, false);
    fire(state, 108, ql1k::HitregTraceKind::player, true);
    fire(state, 116, ql1k::HitregTraceKind::none, false);
    close_and_ack(state, 120, 90, 130);

    const auto& display = state.published();
    expect(display.fire_events == 3, "three physical LG events are retained");
    expect(display.client_accuracy_opportunities == 3,
           "each LG event contributes one client opportunity");
    expect(display.trace_total == 3, "each LG event contributes one trace");
    expect(display.trace_world == 1 && display.trace_player == 1 &&
               display.trace_none == 1 && display.trace_other == 0,
           "native trace classifications are mutually exclusive");
    expect(display.client_accuracy_hits == 1 && display.trace_client_player == 1,
           "only the native opponent contact counts as a client hit");
    expect(display.client_accuracy_percent_hundredths == 3333,
           "client accuracy preserves two-decimal precision");
}

void no_render_window_or_bias() {
    ql1k::HitregState state;
    fire(state, 200, ql1k::HitregTraceKind::world, false);
    fire(state, 208, ql1k::HitregTraceKind::player, true);
    fire(state, 216, ql1k::HitregTraceKind::world, false);
    close_and_ack(state, 220, 190, 230);
    expect(state.published().client_accuracy_hits == 1 &&
               state.published().client_accuracy_opportunities == 3,
           "nearby render observations cannot turn adjacent misses into hits");
}

void wall_and_stationary_extremes() {
    ql1k::HitregState wall;
    fire(wall, 300, ql1k::HitregTraceKind::world, false);
    fire(wall, 308, ql1k::HitregTraceKind::world, false);
    close_and_ack(wall, 312, 290, 320);
    expect(wall.published().client_accuracy_percent_hundredths == 0,
           "wall-only fire is zero client accuracy");

    ql1k::HitregState target;
    fire(target, 400, ql1k::HitregTraceKind::player, true);
    fire(target, 408, ql1k::HitregTraceKind::player, true);
    close_and_ack(target, 412, 390, 420);
    expect(target.published().client_accuracy_percent_hundredths == 10000,
           "continuous native opponent contact is 100 percent");
}

void prediction_replay_is_not_resampled() {
    ql1k::HitregState state;
    fire(state, 500, ql1k::HitregTraceKind::player, true);
    expect(!state.wants_client_ray(6, 500), "the same predicted event is not traced twice");
    state.on_weapon_fire(6, 500, ql1k::HitregTraceKind::world, false, true);
    close_and_ack(state, 504, 490, 510);
    expect(state.published().fire_events == 1 && state.published().trace_total == 1 &&
               state.published().client_accuracy_hits == 1,
           "prediction replay cannot overwrite the event's original trace result");
}

void command_time_wrap_remains_ordered() {
    ql1k::HitregState state;
    const auto first = std::numeric_limits<std::int32_t>::max() - 3;
    const auto second = std::numeric_limits<std::int32_t>::min() + 4;
    fire(state, first, ql1k::HitregTraceKind::none, false);
    fire(state, second, ql1k::HitregTraceKind::player, true);
    state.on_usercmd(second + 4, false);
    state.on_server_feedback(first - 8, second + 8, 0);
    expect(state.published().fire_events == 2,
           "signed command-time wrap does not lose a physical LG event");
}

void latest_completed_hold_replaces_previous_hold() {
    ql1k::HitregState state;
    fire(state, 600, ql1k::HitregTraceKind::player, true);
    close_and_ack(state, 604, 590, 610);
    const auto first_generation = state.published().generation;
    expect(state.published().client_accuracy_percent_hundredths == 10000,
           "first hold publishes independently");

    fire(state, 620, ql1k::HitregTraceKind::world, false);
    fire(state, 628, ql1k::HitregTraceKind::world, false);
    close_and_ack(state, 632, 610, 640);
    expect(state.published().generation > first_generation &&
               state.published().client_accuracy_hits == 0 &&
               state.published().client_accuracy_opportunities == 2,
           "the newest completed hold replaces rather than accumulates prior holds");
}

void capacity_failure_is_explicit() {
    ql1k::HitregState state;
    for (std::int32_t index = 0; index < 65; ++index) {
        fire(state, 700 + index * 8, ql1k::HitregTraceKind::none, false);
    }
    close_and_ack(state, 1220, 690, 1230);
    expect(state.published().client_accuracy_kind ==
               ql1k::ClientAccuracyDisplayKind::unavailable,
           "an impossible pending-tick overflow fails closed instead of publishing a ratio");
    expect(state.published().unavailable_reason ==
               ql1k::HitregUnavailableReason::pending_tick_capacity,
           "capacity failure retains its diagnostic cause");
}

void feedback_damage_is_a_hit_signal_not_a_sample_count() {
    ql1k::HitregState state;
    fire(state, 1300, ql1k::HitregTraceKind::player, true);
    state.on_usercmd(1304, false);
    state.on_server_feedback(1290, 1310, 1000);
    expect(state.published().server_hits == 1,
           "arbitrary damage magnitude contributes one server hit signal");
}

void native_ray_failure_is_explicit() {
    ql1k::HitregState state;
    expect(state.wants_client_ray(6, 1250), "failed native ray still owns a physical event");
    state.on_weapon_fire(6, 1250, ql1k::HitregTraceKind::other, false, false);
    close_and_ack(state, 1254, 1240, 1260);
    expect(state.published().client_accuracy_kind ==
               ql1k::ClientAccuracyDisplayKind::unavailable &&
               state.published().unavailable_reason ==
                   ql1k::HitregUnavailableReason::client_ray_unavailable,
           "native dependency failure publishes n/a rather than a false miss");
    expect(state.published().fire_events == 1 && state.published().trace_total == 0,
           "failed ray retains the event without inventing a trace classification");
}

void opponent_team_rules_match_game_modes() {
    expect(ql1k::hitreg_is_opponent(0, 0), "FFA clients are opponents");
    expect(ql1k::hitreg_is_opponent(1, 2) && ql1k::hitreg_is_opponent(2, 1),
           "red and blue are opponents");
    expect(!ql1k::hitreg_is_opponent(1, 1) && !ql1k::hitreg_is_opponent(2, 2),
           "teammates do not count as client hits");
    expect(!ql1k::hitreg_is_opponent(3, 1), "spectators do not count as opponents");
}

void display_text_has_two_decimals() {
    ql1k::HitregState state;
    fire(state, 1400, ql1k::HitregTraceKind::player, true);
    fire(state, 1408, ql1k::HitregTraceKind::player, true);
    fire(state, 1416, ql1k::HitregTraceKind::world, false);
    close_and_ack(state, 1420, 1390, 1430);
    char text[64]{};
    ql1k::format_hitreg_text(state.published(), text, sizeof(text));
    expect(std::string_view(text) == "client accuracy 66.67%",
           "HUD text exposes the retained hundredths");
}

void reset_clears_all_state() {
    ql1k::HitregState state;
    fire(state, 1500, ql1k::HitregTraceKind::player, true);
    close_and_ack(state, 1504, 1490, 1510);
    state.reset();
    expect(state.published().kind == ql1k::HitregDisplayKind::none &&
               state.published().client_accuracy_kind ==
                   ql1k::ClientAccuracyDisplayKind::none &&
               state.pending_holds() == 0,
           "reset clears published and in-flight hitreg state");
}

}  // namespace

int main() {
    one_native_ray_per_event();
    no_render_window_or_bias();
    wall_and_stationary_extremes();
    prediction_replay_is_not_resampled();
    command_time_wrap_remains_ordered();
    latest_completed_hold_replaces_previous_hold();
    capacity_failure_is_explicit();
    feedback_damage_is_a_hit_signal_not_a_sample_count();
    native_ray_failure_is_explicit();
    opponent_team_rules_match_game_modes();
    display_text_has_two_decimals();
    reset_clears_all_state();
    if (g_failures != 0) {
        std::fprintf(stderr, "%d test(s) failed\n", g_failures);
        return 1;
    }
    std::puts("hitreg state tests passed");
    return 0;
}
