#include "frame_pacer.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <limits>

namespace {

constexpr std::array<std::int64_t, 4> k_frequencies{
    1'000'003,
    3'579'545,
    10'000'000,
    24'000'000,
};

void require(const bool condition) {
    if (!condition) {
        std::abort();
    }
}

std::int64_t qpc_at_millisecond(const std::int64_t elapsed_milliseconds,
                                const std::int64_t frequency) {
    const std::int64_t scaled = elapsed_milliseconds * frequency;
    return scaled / 1000 + (scaled % 1000 != 0 ? 1 : 0);
}

void commit_after_floor(ql1k::FramePacer& pacer, const int cap, const int floor,
                        std::int64_t& elapsed_milliseconds,
                        const std::int64_t frequency) {
    elapsed_milliseconds += floor;
    pacer.commit(cap, qpc_at_millisecond(elapsed_milliseconds, frequency), frequency);
}

void numeric_and_legacy_values_normalize() {
    require(ql1k::normalize_requested_fps(500, "500") == 500);
    require(ql1k::normalize_requested_fps(600, "600") == 600);
    require(ql1k::normalize_requested_fps(250, "250") == 250);
    require(ql1k::normalize_requested_fps(250, "250x") == 1000);
    require(ql1k::normalize_requested_fps(250, "250X") == 1000);
    require(ql1k::normalize_requested_fps(0, "0") == 30);
    require(ql1k::normalize_requested_fps(2000, "2000") == 1000);
}

void qpc_frequency_does_not_change_whole_millisecond_floor() {
    for (const std::int64_t frequency : k_frequencies) {
        ql1k::FramePacer cap_250;
        require(cap_250.select_floor(250, 0, frequency) == 4);

        ql1k::FramePacer cap_500;
        require(cap_500.select_floor(500, 0, frequency) == 2);

        ql1k::FramePacer cap_1000;
        require(cap_1000.select_floor(1000, 0, frequency) == 1);
    }
}

void every_supported_cap_has_bounded_whole_millisecond_phase() {
    constexpr std::int64_t frames = 5000;
    for (const std::int64_t frequency : k_frequencies) {
        for (int cap = ql1k::k_minimum_fps; cap <= ql1k::k_maximum_fps; ++cap) {
            ql1k::FramePacer pacer;
            std::int64_t elapsed_milliseconds = 0;
            for (std::int64_t frame = 0; frame < frames; ++frame) {
                const std::int64_t now =
                    qpc_at_millisecond(elapsed_milliseconds, frequency);
                const int floor = pacer.select_floor(cap, now, frequency);
                require(floor >= 1 && floor <= 34);

                const std::int64_t recheck = qpc_at_millisecond(
                    elapsed_milliseconds + floor - 1, frequency);
                require(pacer.select_floor(cap, recheck, frequency) == floor);
                commit_after_floor(pacer, cap, floor, elapsed_milliseconds, frequency);
            }

            const std::int64_t ideal_scaled = frames * 1000;
            const std::int64_t actual_scaled =
                static_cast<std::int64_t>(cap) * elapsed_milliseconds;
            require(ideal_scaled <= actual_scaled);
            require(actual_scaled - ideal_scaled < cap);
        }
    }
}

void reference_caps_have_exact_frequency_independent_patterns() {
    for (const std::int64_t frequency : k_frequencies) {
        {
            ql1k::FramePacer pacer;
            std::int64_t elapsed_milliseconds = 0;
            for (int frame = 0; frame < 250; ++frame) {
                const int floor = pacer.select_floor(
                    250, qpc_at_millisecond(elapsed_milliseconds, frequency), frequency);
                require(floor == 4);
                commit_after_floor(pacer, 250, floor, elapsed_milliseconds, frequency);
            }
            require(elapsed_milliseconds == 1000);
        }

        {
            ql1k::FramePacer pacer;
            std::int64_t elapsed_milliseconds = 0;
            for (int frame = 0; frame < 500; ++frame) {
                const int floor = pacer.select_floor(
                    500, qpc_at_millisecond(elapsed_milliseconds, frequency), frequency);
                require(floor == 2);
                commit_after_floor(pacer, 500, floor, elapsed_milliseconds, frequency);
            }
            require(elapsed_milliseconds == 1000);
        }

        {
            ql1k::FramePacer pacer;
            std::int64_t elapsed_milliseconds = 0;
            int one_millisecond = 0;
            int two_milliseconds = 0;
            for (int frame = 0; frame < 600; ++frame) {
                const int floor = pacer.select_floor(
                    600, qpc_at_millisecond(elapsed_milliseconds, frequency), frequency);
                require(floor == (frame % 3 == 2 ? 1 : 2));
                one_millisecond += floor == 1 ? 1 : 0;
                two_milliseconds += floor == 2 ? 1 : 0;
                commit_after_floor(pacer, 600, floor, elapsed_milliseconds, frequency);
            }
            require(one_millisecond == 200);
            require(two_milliseconds == 400);
            require(elapsed_milliseconds == 1000);
        }

        {
            ql1k::FramePacer pacer;
            std::int64_t elapsed_milliseconds = 0;
            for (int frame = 0; frame < 1000; ++frame) {
                const int floor = pacer.select_floor(
                    1000, qpc_at_millisecond(elapsed_milliseconds, frequency), frequency);
                require(floor == 1);
                commit_after_floor(pacer, 1000, floor, elapsed_milliseconds, frequency);
            }
            require(elapsed_milliseconds == 1000);
        }
    }
}

void cap_and_frequency_changes_rebase_without_catch_up() {
    ql1k::FramePacer pacer;
    std::int64_t elapsed_milliseconds = 0;
    constexpr std::int64_t first_frequency = 3'579'545;
    constexpr std::int64_t second_frequency = 1'000'003;

    int floor = pacer.select_floor(600, 0, first_frequency);
    require(floor == 2);
    commit_after_floor(pacer, 600, floor, elapsed_milliseconds, first_frequency);

    floor = pacer.select_floor(
        500, qpc_at_millisecond(elapsed_milliseconds, first_frequency), first_frequency);
    require(floor == 2);
    commit_after_floor(pacer, 500, floor, elapsed_milliseconds, first_frequency);

    require(pacer.select_floor(
                1000, qpc_at_millisecond(elapsed_milliseconds, first_frequency),
                first_frequency) == 1);
    require(pacer.select_floor(
                600, qpc_at_millisecond(elapsed_milliseconds, second_frequency),
                second_frequency) == 2);
}

void hitches_and_backward_time_rebase() {
    constexpr std::int64_t frequency = 3'579'545;
    ql1k::FramePacer pacer;
    require(pacer.select_floor(600, 0, frequency) == 2);

    const std::int64_t hitch = qpc_at_millisecond(100, frequency);
    pacer.commit(600, hitch, frequency);
    require(pacer.select_floor(600, hitch, frequency) == 2);

    require(pacer.select_floor(600, qpc_at_millisecond(99, frequency), frequency) == 2);
}

void invalid_clock_is_conservative_and_resets() {
    constexpr std::int64_t frequency = 3'579'545;
    ql1k::FramePacer pacer;
    require(pacer.select_floor(600, 0, 0) == 2);
    require(pacer.select_floor(999, -1, frequency) == 2);
    require(pacer.select_floor(600, std::numeric_limits<std::int64_t>::max() - 1,
                               frequency) == 2);
    require(pacer.select_floor(600, 0, frequency) == 2);
}

void duplicate_commit_does_not_advance_either_phase() {
    constexpr std::int64_t frequency = 3'579'545;
    ql1k::FramePacer pacer;
    std::int64_t elapsed_milliseconds = 0;
    const int floor = pacer.select_floor(600, 0, frequency);
    require(floor == 2);
    commit_after_floor(pacer, 600, floor, elapsed_milliseconds, frequency);

    const std::int64_t now = qpc_at_millisecond(elapsed_milliseconds, frequency);
    pacer.commit(600, now, frequency);
    require(pacer.select_floor(600, now, frequency) == 2);
}

}  // namespace

int main() {
    numeric_and_legacy_values_normalize();
    qpc_frequency_does_not_change_whole_millisecond_floor();
    every_supported_cap_has_bounded_whole_millisecond_phase();
    reference_caps_have_exact_frequency_independent_patterns();
    cap_and_frequency_changes_rebase_without_catch_up();
    hitches_and_backward_time_rebase();
    invalid_clock_is_conservative_and_resets();
    duplicate_commit_does_not_advance_either_phase();
    return 0;
}
