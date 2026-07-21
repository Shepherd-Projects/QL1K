#pragma once

#include <cstdint>
#include <limits>
#include <string_view>

namespace ql1k {

inline constexpr int k_minimum_fps = 30;
inline constexpr int k_maximum_fps = 1000;

constexpr int clamp_requested_fps(const int requested_fps) noexcept {
    return requested_fps < k_minimum_fps
               ? k_minimum_fps
               : (requested_fps > k_maximum_fps ? k_maximum_fps : requested_fps);
}

constexpr char ascii_lower(const char value) noexcept {
    return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : value;
}

constexpr bool legacy_uncapped_value(const std::string_view value) noexcept {
    constexpr std::string_view legacy = "250x";
    if (value.size() != legacy.size()) {
        return false;
    }
    for (std::size_t index = 0; index < legacy.size(); ++index) {
        if (ascii_lower(value[index]) != legacy[index]) {
            return false;
        }
    }
    return true;
}

constexpr int normalize_requested_fps(const int integer_value,
                                      const std::string_view string_value) noexcept {
    return legacy_uncapped_value(string_value) ? k_maximum_fps
                                                : clamp_requested_fps(integer_value);
}

class FramePacer final {
public:
    [[nodiscard]] int select_floor(const int requested_fps, const std::int64_t now,
                                   const std::int64_t frequency) noexcept {
        const int cap = clamp_requested_fps(requested_fps);
        if (!valid_clock(now, frequency)) {
            reset();
            return conservative_floor(cap);
        }

        if (!initialized_ || cap != cap_ || frequency != frequency_ || now < last_query_) {
            if (!rebase(cap, now, frequency)) {
                reset();
                return conservative_floor(cap);
            }
        }

        last_query_ = now;
        selection_pending_ = true;
        return selected_floor();
    }

    void commit(const int requested_fps, const std::int64_t now,
                const std::int64_t frequency) noexcept {
        const int cap = clamp_requested_fps(requested_fps);
        if (!valid_clock(now, frequency)) {
            reset();
            return;
        }
        if (!initialized_ || cap != cap_ || frequency != frequency_ || now < last_query_) {
            if (!rebase(cap, now, frequency)) {
                reset();
            }
            return;
        }
        if (!selection_pending_) {
            return;
        }

        last_accepted_ = now;
        last_query_ = now;
        selection_pending_ = false;
        advance_floor_phase();
        if (!advance_deadline() || deadline_ <= now) {
            if (!rebase(cap, now, frequency)) {
                reset();
            }
        }
    }

    void reset() noexcept {
        initialized_ = false;
        selection_pending_ = false;
        cap_ = 0;
        frequency_ = 0;
        last_accepted_ = 0;
        last_query_ = 0;
        deadline_ = 0;
        tick_phase_remainder_ = 0;
        floor_phase_remainder_ = 0;
    }

private:
    static constexpr bool valid_clock(const std::int64_t now,
                                      const std::int64_t frequency) noexcept {
        return now >= 0 && frequency > 0 &&
               frequency <= std::numeric_limits<std::int64_t>::max() / 1000;
    }

    static constexpr int conservative_floor(const int cap) noexcept {
        return (1000 + cap - 1) / cap;
    }

    bool rebase(const int cap, const std::int64_t now,
                const std::int64_t frequency) noexcept {
        initialized_ = true;
        selection_pending_ = false;
        cap_ = cap;
        frequency_ = frequency;
        last_accepted_ = now;
        last_query_ = now;
        deadline_ = now;
        tick_phase_remainder_ = cap - 1;
        floor_phase_remainder_ = cap - 1;
        return advance_deadline();
    }

    bool advance_deadline() noexcept {
        const std::int64_t whole_ticks = frequency_ / cap_;
        const std::int64_t fractional_ticks = frequency_ % cap_;
        const bool carry = tick_phase_remainder_ >= cap_ - fractional_ticks;
        const std::int64_t increment = whole_ticks + (carry ? 1 : 0);
        if (increment <= 0 || deadline_ > std::numeric_limits<std::int64_t>::max() - increment) {
            return false;
        }
        deadline_ += increment;
        tick_phase_remainder_ = carry
                                    ? tick_phase_remainder_ - (cap_ - fractional_ticks)
                                    : tick_phase_remainder_ + fractional_ticks;
        return true;
    }

    void advance_floor_phase() noexcept {
        const int fractional_milliseconds = 1000 % cap_;
        const bool carry = floor_phase_remainder_ >= cap_ - fractional_milliseconds;
        floor_phase_remainder_ = carry
                                     ? floor_phase_remainder_ -
                                           (cap_ - fractional_milliseconds)
                                     : floor_phase_remainder_ + fractional_milliseconds;
    }

    [[nodiscard]] int selected_floor() const noexcept {
        const int whole_milliseconds = 1000 / cap_;
        const int fractional_milliseconds = 1000 % cap_;
        const bool carry = floor_phase_remainder_ >= cap_ - fractional_milliseconds;
        return whole_milliseconds + (carry ? 1 : 0);
    }

    bool initialized_{};
    bool selection_pending_{};
    int cap_{};
    std::int64_t frequency_{};
    std::int64_t last_accepted_{};
    std::int64_t last_query_{};
    std::int64_t deadline_{};
    std::int64_t tick_phase_remainder_{};
    int floor_phase_remainder_{};
};

}  // namespace ql1k
