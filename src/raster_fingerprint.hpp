#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace ql1k {

struct RasterHash128 {
    std::array<std::uint32_t, 4> words{};

    friend bool operator==(const RasterHash128&, const RasterHash128&) noexcept = default;
};

namespace raster_fingerprint_detail {

[[nodiscard]] inline std::uint32_t load_u32(const std::uint8_t* const bytes) noexcept {
    static_assert(std::endian::native == std::endian::little);
    std::uint32_t value{};
    std::memcpy(&value, bytes, sizeof(value));
    return value;
}

[[nodiscard]] constexpr std::uint32_t mix_final(std::uint32_t value) noexcept {
    value ^= value >> 16U;
    value *= 0x85EBCA6BU;
    value ^= value >> 13U;
    value *= 0xC2B2AE35U;
    value ^= value >> 16U;
    return value;
}

} // namespace raster_fingerprint_detail

// MurmurHash3 x86-128 is intentionally used here: the patch is a 32-bit x86
// DLL, the input is a small RGB readback, and four 32-bit
// lanes avoid the cost of a platform cryptographic provider on every frame.
[[nodiscard]] inline RasterHash128 hash_rgb24(
    const std::span<const std::uint8_t> rgb_bytes) noexcept {
    constexpr std::uint32_t seed = 0x514C314BU;
    constexpr std::uint32_t c1 = 0x239B961BU;
    constexpr std::uint32_t c2 = 0xAB0E9789U;
    constexpr std::uint32_t c3 = 0x38B34AE5U;
    constexpr std::uint32_t c4 = 0xA1E38B93U;

    std::uint32_t h1 = seed;
    std::uint32_t h2 = seed;
    std::uint32_t h3 = seed;
    std::uint32_t h4 = seed;

    const std::size_t block_count = rgb_bytes.size() / 16U;
    for (std::size_t block = 0; block < block_count; ++block) {
        const std::uint8_t* const bytes = rgb_bytes.data() + block * 16U;
        std::uint32_t k1 = raster_fingerprint_detail::load_u32(bytes);
        std::uint32_t k2 = raster_fingerprint_detail::load_u32(bytes + 4U);
        std::uint32_t k3 = raster_fingerprint_detail::load_u32(bytes + 8U);
        std::uint32_t k4 = raster_fingerprint_detail::load_u32(bytes + 12U);

        k1 *= c1;
        k1 = std::rotl(k1, 15);
        k1 *= c2;
        h1 ^= k1;
        h1 = std::rotl(h1, 19);
        h1 += h2;
        h1 = h1 * 5U + 0x561CCD1BU;

        k2 *= c2;
        k2 = std::rotl(k2, 16);
        k2 *= c3;
        h2 ^= k2;
        h2 = std::rotl(h2, 17);
        h2 += h3;
        h2 = h2 * 5U + 0x0BCAA747U;

        k3 *= c3;
        k3 = std::rotl(k3, 17);
        k3 *= c4;
        h3 ^= k3;
        h3 = std::rotl(h3, 15);
        h3 += h4;
        h3 = h3 * 5U + 0x96CD1C35U;

        k4 *= c4;
        k4 = std::rotl(k4, 18);
        k4 *= c1;
        h4 ^= k4;
        h4 = std::rotl(h4, 13);
        h4 += h1;
        h4 = h4 * 5U + 0x32AC3B17U;
    }

    const std::uint8_t* const tail =
        rgb_bytes.empty() ? nullptr : rgb_bytes.data() + block_count * 16U;
    std::uint32_t k1{};
    std::uint32_t k2{};
    std::uint32_t k3{};
    std::uint32_t k4{};
    switch (rgb_bytes.size() & 15U) {
    case 15:
        k4 ^= static_cast<std::uint32_t>(tail[14]) << 16U;
        [[fallthrough]];
    case 14:
        k4 ^= static_cast<std::uint32_t>(tail[13]) << 8U;
        [[fallthrough]];
    case 13:
        k4 ^= static_cast<std::uint32_t>(tail[12]);
        k4 *= c4;
        k4 = std::rotl(k4, 18);
        k4 *= c1;
        h4 ^= k4;
        [[fallthrough]];
    case 12:
        k3 ^= static_cast<std::uint32_t>(tail[11]) << 24U;
        [[fallthrough]];
    case 11:
        k3 ^= static_cast<std::uint32_t>(tail[10]) << 16U;
        [[fallthrough]];
    case 10:
        k3 ^= static_cast<std::uint32_t>(tail[9]) << 8U;
        [[fallthrough]];
    case 9:
        k3 ^= static_cast<std::uint32_t>(tail[8]);
        k3 *= c3;
        k3 = std::rotl(k3, 17);
        k3 *= c4;
        h3 ^= k3;
        [[fallthrough]];
    case 8:
        k2 ^= static_cast<std::uint32_t>(tail[7]) << 24U;
        [[fallthrough]];
    case 7:
        k2 ^= static_cast<std::uint32_t>(tail[6]) << 16U;
        [[fallthrough]];
    case 6:
        k2 ^= static_cast<std::uint32_t>(tail[5]) << 8U;
        [[fallthrough]];
    case 5:
        k2 ^= static_cast<std::uint32_t>(tail[4]);
        k2 *= c2;
        k2 = std::rotl(k2, 16);
        k2 *= c3;
        h2 ^= k2;
        [[fallthrough]];
    case 4:
        k1 ^= static_cast<std::uint32_t>(tail[3]) << 24U;
        [[fallthrough]];
    case 3:
        k1 ^= static_cast<std::uint32_t>(tail[2]) << 16U;
        [[fallthrough]];
    case 2:
        k1 ^= static_cast<std::uint32_t>(tail[1]) << 8U;
        [[fallthrough]];
    case 1:
        k1 ^= static_cast<std::uint32_t>(tail[0]);
        k1 *= c1;
        k1 = std::rotl(k1, 15);
        k1 *= c2;
        h1 ^= k1;
        [[fallthrough]];
    case 0:
        break;
    }

    const std::uint32_t length = static_cast<std::uint32_t>(rgb_bytes.size());
    h1 ^= length;
    h2 ^= length;
    h3 ^= length;
    h4 ^= length;

    h1 += h2 + h3 + h4;
    h2 += h1;
    h3 += h1;
    h4 += h1;

    h1 = raster_fingerprint_detail::mix_final(h1);
    h2 = raster_fingerprint_detail::mix_final(h2);
    h3 = raster_fingerprint_detail::mix_final(h3);
    h4 = raster_fingerprint_detail::mix_final(h4);

    h1 += h2 + h3 + h4;
    h2 += h1;
    h3 += h1;
    h4 += h1;
    return RasterHash128{{h1, h2, h3, h4}};
}

enum class RasterSampleClass : std::uint8_t {
    first,
    changed,
    repeated,
    rejected,
};

struct RasterFingerprintCounters {
    std::uint64_t issued{};
    std::uint64_t ready{};
    std::uint64_t first{};
    std::uint64_t changed{};
    std::uint64_t repeated{};
    std::uint64_t gaps{};
    std::uint64_t wait_failures{};
    std::uint64_t accounting_faults{};
    std::size_t pending{};
};

// Tracks issue/ready ordering without owning PBOs, fences, or any OpenGL type.
// The GL ring remains responsible only for storage and readiness polling.
template <std::size_t Capacity>
class RasterFingerprintAccounting {
  public:
    static_assert(Capacity > 0U);

    [[nodiscard]] bool note_issued(const std::uint64_t frame_id) noexcept {
        if (pending_ >= Capacity) {
            ++counters_.gaps;
            return false;
        }
        if (frame_id == 0U || frame_id <= last_issued_frame_) {
            ++counters_.gaps;
            ++counters_.accounting_faults;
            return false;
        }
        pending_frame_ids_[(head_ + pending_) % Capacity] = frame_id;
        ++pending_;
        last_issued_frame_ = frame_id;
        ++counters_.issued;
        counters_.pending = pending_;
        return true;
    }

    [[nodiscard]] RasterSampleClass note_ready(
        const std::uint64_t frame_id, const RasterHash128 fingerprint) noexcept {
        if (pending_ == 0U || pending_frame_ids_[head_] != frame_id) {
            ++counters_.accounting_faults;
            return RasterSampleClass::rejected;
        }

        head_ = (head_ + 1U) % Capacity;
        --pending_;
        ++counters_.ready;
        counters_.pending = pending_;

        RasterSampleClass classification{};
        if (!has_previous_fingerprint_) {
            classification = RasterSampleClass::first;
            ++counters_.first;
            has_previous_fingerprint_ = true;
        } else if (fingerprint == previous_fingerprint_) {
            classification = RasterSampleClass::repeated;
            ++counters_.repeated;
        } else {
            classification = RasterSampleClass::changed;
            ++counters_.changed;
        }
        previous_fingerprint_ = fingerprint;
        return classification;
    }

    void note_gap() noexcept { ++counters_.gaps; }

    void note_wait_failure() noexcept { ++counters_.wait_failures; }

    [[nodiscard]] bool has_capacity() const noexcept { return pending_ < Capacity; }

    [[nodiscard]] const RasterFingerprintCounters& counters() const noexcept {
        return counters_;
    }

    void reset() noexcept {
        pending_frame_ids_ = {};
        counters_ = {};
        previous_fingerprint_ = {};
        last_issued_frame_ = 0U;
        head_ = 0U;
        pending_ = 0U;
        has_previous_fingerprint_ = false;
    }

  private:
    std::array<std::uint64_t, Capacity> pending_frame_ids_{};
    RasterFingerprintCounters counters_{};
    RasterHash128 previous_fingerprint_{};
    std::uint64_t last_issued_frame_{};
    std::size_t head_{};
    std::size_t pending_{};
    bool has_previous_fingerprint_{};
};

} // namespace ql1k
