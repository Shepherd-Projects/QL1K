#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>

namespace ql1k {

enum class HitregDisplayKind : std::uint8_t {
    none,
    percent,
    infinity,
    unavailable,
};

enum class ClientAccuracyDisplayKind : std::uint8_t {
    none,
    percent,
    unavailable,
};

enum class HitregUnavailableReason : std::uint8_t {
    none,
    inflight_hold_capacity,
    pending_tick_capacity,
    multiple_ticks_before_first_trace,
    cross_hold_feedback,
    unresolved_trace_on_close,
    client_ray_unavailable,
};

enum class HitregTraceKind : std::uint8_t {
    player,
    world,
    none,
    other,
};

inline bool hitreg_is_opponent(const std::int32_t local_team,
                               const std::int32_t candidate_team) noexcept {
    if (local_team == 0) {
        return candidate_team == 0;
    }
    if (local_team == 1 || local_team == 2) {
        return (candidate_team == 1 || candidate_team == 2) &&
               candidate_team != local_team;
    }
    return false;
}

struct HitregDisplay {
    HitregDisplayKind kind{HitregDisplayKind::none};
    ClientAccuracyDisplayKind client_accuracy_kind{ClientAccuracyDisplayKind::none};
    HitregUnavailableReason unavailable_reason{HitregUnavailableReason::none};
    std::uint64_t generation{};
    // Rounded whole-percent compatibility value; HUD uses exact hundredths.
    std::uint32_t percent{};
    std::uint32_t percent_hundredths{};
    std::uint32_t client_hits{};
    std::uint32_t server_hits{};
    std::uint32_t samples{};
    std::uint32_t fire_events{};
    std::uint32_t client_accuracy_percent_hundredths{};
    std::uint32_t client_accuracy_hits{};
    std::uint32_t client_accuracy_opportunities{};
    std::uint32_t trace_total{};
    std::uint32_t trace_player{};
    std::uint32_t trace_world{};
    std::uint32_t trace_none{};
    std::uint32_t trace_other{};
    std::uint32_t trace_client_player{};
    std::int32_t start_time{};
    std::int32_t end_time{};
};

struct HitregDiagnostics {
    std::size_t pending_holds{};
    std::size_t pending_server_ticks{};
    std::uint32_t pending_client_samples{};
    std::uint32_t feedback_positive_seen{};
    std::uint32_t feedback_positive_assigned{};
    std::uint32_t feedback_positive_unowned{};
    bool hold_open{};
};

inline void format_hitreg_text(const HitregDisplay& display, char* const text,
                               const std::size_t capacity) noexcept {
    if (text == nullptr || capacity == 0) {
        return;
    }
    if (display.client_accuracy_kind == ClientAccuracyDisplayKind::unavailable) {
        (void)std::snprintf(text, capacity, "client accuracy n/a");
    } else if (display.client_accuracy_kind == ClientAccuracyDisplayKind::percent) {
        const std::uint32_t whole = display.client_accuracy_percent_hundredths / 100U;
        const std::uint32_t fraction = display.client_accuracy_percent_hundredths % 100U;
        (void)std::snprintf(text, capacity, "client accuracy %u.%02u%%", whole, fraction);
    } else {
        text[0] = '\0';
    }
}

class HitregState final {
public:
    static constexpr std::int32_t k_lightning_weapon = 6;

    void reset() noexcept {
        holds_ = {};
        hold_count_ = 0;
        published_ = {};
        published_.generation = publication_generation_;
        have_last_fire_event_time_ = false;
        last_fire_event_time_ = 0;
        have_last_feedback_state_ = false;
        last_feedback_command_time_ = 0;
        last_feedback_hits_ = 0;
        feedback_transitions_seen_at_time_count_ = 0;
        feedback_positive_seen_ = 0;
        feedback_positive_assigned_ = 0;
        feedback_positive_unowned_ = 0;
    }

    [[nodiscard]] bool wants_client_ray(const std::int32_t weapon,
                                        const std::int32_t command_time) const noexcept {
        return weapon == k_lightning_weapon &&
               (!have_last_fire_event_time_ ||
                time_before(last_fire_event_time_, command_time));
    }

    void on_weapon_fire(const std::int32_t weapon, const std::int32_t command_time,
                        const HitregTraceKind trace_kind,
                        const bool client_player_contact,
                        const bool client_ray_available) noexcept {
        // Prediction rebuilds old playerstates repeatedly, including events
        // already acknowledged by a later snapshot. Command time is the
        // generating event identity: accept it once for the whole lifecycle,
        // not merely while its server attribution remains pending.
        if (have_last_fire_event_time_ && !time_before(last_fire_event_time_, command_time)) {
            return;
        }
        have_last_fire_event_time_ = true;
        last_fire_event_time_ = command_time;

        if (weapon != k_lightning_weapon) {
            close_open_hold(command_time);
            return;
        }

        Hold* hold = open_hold();
        if (hold == nullptr) {
            if (hold_count_ == holds_.size()) {
                // Never silently omit a physical hold. Collapse an impossible-to-
                // retain backlog into one explicitly unavailable current hold.
                holds_ = {};
                hold_count_ = 1;
                hold = &holds_[0];
                hold->start_time = command_time;
                hold->open = true;
                mark_unavailable(*hold, HitregUnavailableReason::inflight_hold_capacity);
            } else {
                hold = &holds_[hold_count_++];
                *hold = {};
                hold->start_time = command_time;
                hold->open = true;
            }
        }

        increment_saturated(hold->fire_events);
        increment_saturated(hold->client_accuracy_opportunities);
        if (!client_ray_available) {
            mark_unavailable(*hold, HitregUnavailableReason::client_ray_unavailable);
        } else {
            increment_saturated(hold->trace_total);
            switch (trace_kind) {
                case HitregTraceKind::player:
                    increment_saturated(hold->trace_player);
                    break;
                case HitregTraceKind::world:
                    increment_saturated(hold->trace_world);
                    break;
                case HitregTraceKind::none:
                    increment_saturated(hold->trace_none);
                    break;
                case HitregTraceKind::other:
                    increment_saturated(hold->trace_other);
                    break;
            }
            if (client_player_contact) {
                increment_saturated(hold->client_accuracy_hits);
                increment_saturated(hold->trace_client_player);
            }
        }
        if (hold->pending_server_count == hold->pending_server_times.size()) {
            mark_unavailable(*hold, HitregUnavailableReason::pending_tick_capacity);
        } else {
            PendingTick& tick = hold->pending_server_times[hold->pending_server_count++];
            tick = {};
            tick.command_time = command_time;
            tick.client_contact = client_player_contact;
        }
    }

    void on_usercmd(const std::int32_t command_time, const bool attack_held) noexcept {
        if (!attack_held) {
            close_open_hold(command_time);
        }
    }

    void on_server_feedback_transition(const std::int32_t old_command_time,
                                       const std::int32_t new_command_time,
                                       const std::int32_t old_hits,
                                       const std::int32_t new_hits) noexcept {
        // Prediction can replay the same old/new playerstate pair once per
        // rendered frame. Command time orders authoritative states; the
        // absolute counter, not a potentially stale caller delta, is the
        // feedback baseline.
        std::int32_t baseline_hits = old_hits;
        if (have_last_feedback_state_) {
            if (time_before(new_command_time, last_feedback_command_time_)) {
                return;
            }
            baseline_hits = last_feedback_hits_;
            if (new_command_time == last_feedback_command_time_) {
                for (std::size_t index = 0; index < feedback_transitions_seen_at_time_count_;
                     ++index) {
                    const FeedbackTransitionIdentity& seen =
                        feedback_transitions_seen_at_time_[index];
                    if (seen.old_command_time == old_command_time &&
                        seen.old_hits == old_hits && seen.new_hits == new_hits) {
                        return;
                    }
                }
                if (feedback_transitions_seen_at_time_count_ ==
                    feedback_transitions_seen_at_time_.size()) {
                    return;
                }
            } else {
                feedback_transitions_seen_at_time_count_ = 0;
            }
        }

        have_last_feedback_state_ = true;
        last_feedback_command_time_ = new_command_time;
        last_feedback_hits_ = new_hits;
        feedback_transitions_seen_at_time_[feedback_transitions_seen_at_time_count_++] =
            {old_command_time, old_hits, new_hits};

        const std::int64_t signed_hit_delta =
            static_cast<std::int64_t>(new_hits) - static_cast<std::int64_t>(baseline_hits);
        const std::uint32_t hit_delta =
            signed_hit_delta <= 0
                ? 0U
                : signed_hit_delta > std::numeric_limits<std::uint32_t>::max()
                      ? std::numeric_limits<std::uint32_t>::max()
                      : static_cast<std::uint32_t>(signed_hit_delta);
        on_server_feedback(old_command_time, new_command_time, hit_delta);
    }

    void on_server_feedback(const std::int32_t old_command_time,
                            const std::int32_t new_command_time,
                            const std::uint32_t hit_damage_delta) noexcept {
        if (hit_damage_delta != 0) {
            increment_saturated(feedback_positive_seen_);
        }
        std::array<std::size_t, k_max_inflight_holds> interval_ticks_by_hold{};
        std::array<bool, k_max_inflight_holds> interval_client_contact_by_hold{};
        std::array<bool, k_max_inflight_holds> acknowledged_before_interval{};
        for (std::size_t hold_index = 0; hold_index < hold_count_; ++hold_index) {
            Hold& hold = holds_[hold_index];
            acknowledged_before_interval[hold_index] = hold.have_acknowledged_tick;
            for (std::size_t tick_index = 0; tick_index < hold.pending_server_count; ++tick_index) {
                const PendingTick& tick = hold.pending_server_times[tick_index];
                const std::int32_t tick_time = tick.command_time;
                if (time_before(old_command_time, tick_time) &&
                    time_reached(new_command_time, tick_time)) {
                    ++interval_ticks_by_hold[hold_index];
                    interval_client_contact_by_hold[hold_index] =
                        interval_client_contact_by_hold[hold_index] || tick.client_contact;
                }
            }
        }

        for (std::size_t hold_index = 0; hold_index < hold_count_; ++hold_index) {
            if (interval_ticks_by_hold[hold_index] == 0) {
                continue;
            }
            Hold& hold = holds_[hold_index];
            hold.have_acknowledged_tick = true;
            record_client_window(hold, interval_client_contact_by_hold[hold_index]);
        }

        if (hit_damage_delta != 0) {
            std::size_t owner_count = 0;
            std::size_t owner_index = 0;
            for (std::size_t hold_index = 0; hold_index < hold_count_; ++hold_index) {
                if (interval_ticks_by_hold[hold_index] != 0 ||
                    (acknowledged_before_interval[hold_index] &&
                     !holds_[hold_index].open)) {
                    ++owner_count;
                    owner_index = hold_index;
                }
            }

            // Prefer exact command-interval ownership. Only fall back to the
            // sole live LG hold when feedback leads the first acknowledgement
            // or trails release and the transition interval contains no tick.
            if (owner_count == 0) {
                for (std::size_t hold_index = 0; hold_index < hold_count_; ++hold_index) {
                    const Hold& hold = holds_[hold_index];
                    if (hold.fire_events != 0 &&
                        (hold.open || hold.pending_server_count != 0 ||
                         hold.have_acknowledged_tick)) {
                        ++owner_count;
                        owner_index = hold_index;
                    }
                }
            }

            if (owner_count > 1) {
                for (std::size_t hold_index = 0; hold_index < hold_count_; ++hold_index) {
                    Hold& hold = holds_[hold_index];
                    if (interval_ticks_by_hold[hold_index] != 0 ||
                        (interval_ticks_by_hold[hold_index] == 0 &&
                         hold.fire_events != 0 &&
                         (hold.open || hold.pending_server_count != 0 ||
                          hold.have_acknowledged_tick))) {
                        mark_unavailable(hold, HitregUnavailableReason::cross_hold_feedback);
                    }
                }
                increment_saturated(feedback_positive_unowned_);
            } else if (owner_count == 1) {
                Hold& hold = holds_[owner_index];
                increment_saturated(hold.server_hits);
                increment_saturated(feedback_positive_assigned_);
            } else {
                increment_saturated(feedback_positive_unowned_);
            }
        }

        for (std::size_t hold_index = 0; hold_index < hold_count_; ++hold_index) {
            Hold& hold = holds_[hold_index];
            std::size_t retained = 0;
            for (std::size_t tick_index = 0; tick_index < hold.pending_server_count; ++tick_index) {
                const PendingTick& tick = hold.pending_server_times[tick_index];
                const std::int32_t tick_time = tick.command_time;
                if (!time_reached(new_command_time, tick_time)) {
                    hold.pending_server_times[retained++] = tick;
                }
            }
            hold.pending_server_count = retained;
        }

        std::size_t finalized = 0;
        while (finalized < hold_count_) {
            const Hold& hold = holds_[finalized];
            if (hold.open || !time_reached(new_command_time, hold.end_time)) {
                break;
            }
            publish(hold);
            ++finalized;
        }
        if (finalized != 0) {
            for (std::size_t index = finalized; index < hold_count_; ++index) {
                holds_[index - finalized] = holds_[index];
            }
            for (std::size_t index = hold_count_ - finalized; index < hold_count_; ++index) {
                holds_[index] = {};
            }
            hold_count_ -= finalized;
        }
    }

    [[nodiscard]] const HitregDisplay& published() const noexcept {
        return published_;
    }

    [[nodiscard]] std::size_t pending_holds() const noexcept {
        return hold_count_;
    }

    [[nodiscard]] HitregDiagnostics diagnostics() const noexcept {
        HitregDiagnostics result{};
        result.pending_holds = hold_count_;
        for (std::size_t index = 0; index < hold_count_; ++index) {
            result.pending_server_ticks += holds_[index].pending_server_count;
        }
        result.hold_open = hold_count_ != 0 && holds_[hold_count_ - 1].open;
        result.feedback_positive_seen = feedback_positive_seen_;
        result.feedback_positive_assigned = feedback_positive_assigned_;
        result.feedback_positive_unowned = feedback_positive_unowned_;
        return result;
    }

private:
    static constexpr std::size_t k_max_unacknowledged_ticks = 64;
    static constexpr std::size_t k_max_feedback_transitions_at_one_time = 64;

    struct FeedbackTransitionIdentity {
        std::int32_t old_command_time{};
        std::int32_t old_hits{};
        std::int32_t new_hits{};
    };

    struct PendingTick {
        std::int32_t command_time{};
        bool client_contact{};
    };

    struct Hold {
        std::int32_t start_time{};
        std::int32_t end_time{};
        std::uint32_t client_hits{};
        std::uint32_t server_hits{};
        std::uint32_t samples{};
        std::uint32_t fire_events{};
        std::uint32_t client_accuracy_hits{};
        std::uint32_t client_accuracy_opportunities{};
        std::uint32_t trace_total{};
        std::uint32_t trace_player{};
        std::uint32_t trace_world{};
        std::uint32_t trace_none{};
        std::uint32_t trace_other{};
        std::uint32_t trace_client_player{};
        std::array<PendingTick, k_max_unacknowledged_ticks> pending_server_times{};
        std::size_t pending_server_count{};
        bool open{};
        HitregUnavailableReason unavailable_reason{HitregUnavailableReason::none};
        bool have_acknowledged_tick{};
    };

    static constexpr std::size_t k_max_inflight_holds = 16;

    [[nodiscard]] static bool time_reached(const std::int32_t value,
                                           const std::int32_t target) noexcept {
        return static_cast<std::int32_t>(static_cast<std::uint32_t>(value) -
                                         static_cast<std::uint32_t>(target)) >= 0;
    }

    [[nodiscard]] static bool time_before(const std::int32_t value,
                                          const std::int32_t target) noexcept {
        return !time_reached(value, target);
    }

    static void increment_saturated(std::uint32_t& value) noexcept {
        if (value != std::numeric_limits<std::uint32_t>::max()) {
            ++value;
        }
    }

    static void record_client_window(Hold& hold, const bool player_contact) noexcept {
        increment_saturated(hold.samples);
        if (player_contact) {
            increment_saturated(hold.client_hits);
        }
    }

    [[nodiscard]] static bool client_accuracy_unavailable(
        const HitregUnavailableReason reason) noexcept {
        return reason != HitregUnavailableReason::none &&
               reason != HitregUnavailableReason::cross_hold_feedback;
    }

    static void mark_unavailable(Hold& hold, const HitregUnavailableReason reason) noexcept {
        if (hold.unavailable_reason == HitregUnavailableReason::none) {
            hold.unavailable_reason = reason;
        }
    }

    [[nodiscard]] Hold* open_hold() noexcept {
        if (hold_count_ == 0 || !holds_[hold_count_ - 1].open) {
            return nullptr;
        }
        return &holds_[hold_count_ - 1];
    }

    void close_open_hold(const std::int32_t command_time) noexcept {
        Hold* hold = open_hold();
        if (hold == nullptr) {
            return;
        }
        hold->open = false;
        hold->end_time = command_time;
    }

    void publish(const Hold& hold) noexcept {
        if (publication_generation_ != std::numeric_limits<std::uint64_t>::max()) {
            ++publication_generation_;
        }
        published_.generation = publication_generation_;
        published_.client_hits = hold.client_hits;
        published_.server_hits = hold.server_hits;
        published_.samples = hold.samples;
        published_.fire_events = hold.fire_events;
        published_.client_accuracy_hits = hold.client_accuracy_hits;
        published_.client_accuracy_opportunities = hold.client_accuracy_opportunities;
        published_.trace_total = hold.trace_total;
        published_.trace_player = hold.trace_player;
        published_.trace_world = hold.trace_world;
        published_.trace_none = hold.trace_none;
        published_.trace_other = hold.trace_other;
        published_.trace_client_player = hold.trace_client_player;
        published_.start_time = hold.start_time;
        published_.end_time = hold.end_time;
        published_.unavailable_reason = hold.unavailable_reason;
        if (client_accuracy_unavailable(hold.unavailable_reason) ||
            hold.client_accuracy_opportunities == 0) {
            published_.client_accuracy_kind = ClientAccuracyDisplayKind::unavailable;
            published_.client_accuracy_percent_hundredths = 0;
        } else {
            const std::uint64_t client_numerator =
                static_cast<std::uint64_t>(hold.client_accuracy_hits) * 10000ULL +
                static_cast<std::uint64_t>(hold.client_accuracy_opportunities / 2U);
            const std::uint64_t client_ratio =
                client_numerator / hold.client_accuracy_opportunities;
            published_.client_accuracy_kind = ClientAccuracyDisplayKind::percent;
            published_.client_accuracy_percent_hundredths =
                static_cast<std::uint32_t>(client_ratio);
        }
        if (hold.unavailable_reason != HitregUnavailableReason::none) {
            published_.kind = HitregDisplayKind::unavailable;
            published_.percent = 0;
            published_.percent_hundredths = 0;
            return;
        }
        if (hold.client_hits == 0) {
            published_.kind = hold.server_hits == 0 ? HitregDisplayKind::percent
                                                    : HitregDisplayKind::infinity;
            published_.percent = hold.server_hits == 0 ? 100U : 0U;
            published_.percent_hundredths = hold.server_hits == 0 ? 10000U : 0U;
            return;
        }

        const std::uint64_t numerator = static_cast<std::uint64_t>(hold.server_hits) * 10000ULL +
                                        static_cast<std::uint64_t>(hold.client_hits / 2U);
        const std::uint64_t ratio = numerator / hold.client_hits;
        published_.kind = HitregDisplayKind::percent;
        published_.percent_hundredths = ratio > std::numeric_limits<std::uint32_t>::max()
                                            ? std::numeric_limits<std::uint32_t>::max()
                                            : static_cast<std::uint32_t>(ratio);
        const std::uint64_t rounded_whole = (ratio + 50ULL) / 100ULL;
        published_.percent = rounded_whole > std::numeric_limits<std::uint32_t>::max()
                                 ? std::numeric_limits<std::uint32_t>::max()
                                 : static_cast<std::uint32_t>(rounded_whole);
    }

    std::array<Hold, k_max_inflight_holds> holds_{};
    std::size_t hold_count_{};
    HitregDisplay published_{};
    std::uint64_t publication_generation_{};
    std::int32_t last_fire_event_time_{};
    bool have_last_fire_event_time_{};
    std::int32_t last_feedback_command_time_{};
    std::int32_t last_feedback_hits_{};
    bool have_last_feedback_state_{};
    std::array<FeedbackTransitionIdentity, k_max_feedback_transitions_at_one_time>
        feedback_transitions_seen_at_time_{};
    std::size_t feedback_transitions_seen_at_time_count_{};
    std::uint32_t feedback_positive_seen_{};
    std::uint32_t feedback_positive_assigned_{};
    std::uint32_t feedback_positive_unowned_{};
};

}  // namespace ql1k
