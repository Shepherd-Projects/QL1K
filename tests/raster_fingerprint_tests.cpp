#include "raster_fingerprint.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>

namespace {

void require(const bool condition) {
    if (!condition) {
        std::abort();
    }
}

void rgb_hash_is_deterministic_and_byte_sensitive() {
    std::array<std::uint8_t, 64U * 64U * 3U> rgb{};
    for (std::size_t index = 0; index < rgb.size(); ++index) {
        rgb[index] = static_cast<std::uint8_t>((index * 37U + index / 7U) & 0xFFU);
    }

    const ql1k::RasterHash128 baseline = ql1k::hash_rgb24(rgb);
    require(ql1k::hash_rgb24(rgb) == baseline);

    auto changed = rgb;
    changed.front() ^= 0x01U;
    require(!(ql1k::hash_rgb24(changed) == baseline));
    changed = rgb;
    changed[changed.size() / 2U] ^= 0x80U;
    require(!(ql1k::hash_rgb24(changed) == baseline));
    changed = rgb;
    changed.back() ^= 0x01U;
    require(!(ql1k::hash_rgb24(changed) == baseline));

    const std::span<const std::uint8_t> empty{};
    require(ql1k::hash_rgb24(empty) == ql1k::hash_rgb24(empty));

    std::array<std::uint8_t, 16> tail{};
    for (std::size_t length = 1U; length < tail.size(); ++length) {
        tail[length - 1U] = static_cast<std::uint8_t>(length * 11U);
        const std::span<const std::uint8_t> input{tail.data(), length};
        const ql1k::RasterHash128 tail_baseline = ql1k::hash_rgb24(input);
        tail[length - 1U] ^= 1U;
        require(!(ql1k::hash_rgb24(input) == tail_baseline));
        tail[length - 1U] ^= 1U;
    }
}

void accounting_classifies_first_repeat_and_change() {
    ql1k::RasterFingerprintAccounting<4> accounting;
    const ql1k::RasterHash128 first{{1U, 2U, 3U, 4U}};
    const ql1k::RasterHash128 changed{{1U, 2U, 3U, 5U}};

    require(accounting.note_issued(1U));
    require(accounting.note_ready(1U, first) == ql1k::RasterSampleClass::first);
    require(accounting.note_issued(2U));
    require(accounting.note_ready(2U, first) == ql1k::RasterSampleClass::repeated);
    require(accounting.note_issued(3U));
    require(accounting.note_ready(3U, changed) == ql1k::RasterSampleClass::changed);

    const ql1k::RasterFingerprintCounters& counters = accounting.counters();
    require(counters.issued == 3U);
    require(counters.ready == 3U);
    require(counters.first == 1U);
    require(counters.repeated == 1U);
    require(counters.changed == 1U);
    require(counters.pending == 0U);
    require(counters.gaps == 0U);
    require(counters.accounting_faults == 0U);
}

void accounting_is_fixed_capacity_and_rejects_out_of_order_completion() {
    ql1k::RasterFingerprintAccounting<2> accounting;
    const ql1k::RasterHash128 fingerprint{{9U, 8U, 7U, 6U}};

    require(accounting.note_issued(10U));
    require(accounting.note_issued(11U));
    require(!accounting.has_capacity());
    require(!accounting.note_issued(12U));
    require(accounting.note_ready(11U, fingerprint) ==
            ql1k::RasterSampleClass::rejected);
    require(accounting.note_ready(10U, fingerprint) == ql1k::RasterSampleClass::first);
    require(accounting.note_ready(11U, fingerprint) ==
            ql1k::RasterSampleClass::repeated);
    require(accounting.note_issued(12U));
    require(accounting.note_ready(12U, fingerprint) ==
            ql1k::RasterSampleClass::repeated);

    accounting.note_gap();
    accounting.note_wait_failure();
    const ql1k::RasterFingerprintCounters& counters = accounting.counters();
    require(counters.issued == 3U);
    require(counters.ready == 3U);
    require(counters.gaps == 2U);
    require(counters.wait_failures == 1U);
    require(counters.accounting_faults == 1U);
    require(counters.pending == 0U);

    accounting.reset();
    require(accounting.has_capacity());
    require(accounting.counters().issued == 0U);
    require(accounting.counters().ready == 0U);
    require(accounting.counters().gaps == 0U);
    require(accounting.counters().wait_failures == 0U);
    require(accounting.counters().accounting_faults == 0U);
}

} // namespace

int main() {
    rgb_hash_is_deterministic_and_byte_sensitive();
    accounting_classifies_first_repeat_and_change();
    accounting_is_fixed_capacity_and_rejects_out_of_order_completion();
    return 0;
}
