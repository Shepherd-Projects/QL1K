#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace ql1k {

template <std::size_t PlayerCount>
struct SubmittedPoseSignature {
    static_assert(PlayerCount <= 64U);

    std::array<std::uint32_t, 12> camera_bits{};
    std::array<std::array<std::uint32_t, 3>, PlayerCount> player_position_bits{};
    std::uint64_t player_presence{};
    bool camera_valid{};

    [[nodiscard]] bool valid() const noexcept {
        return camera_valid || player_presence != 0U;
    }

    friend bool operator==(const SubmittedPoseSignature&,
                           const SubmittedPoseSignature&) noexcept = default;
};

class SubmillisecondRenderClock {
  public:
    double sample(const std::int32_t server_time, const std::int64_t counter,
                  const std::int64_t frequency, const bool render_only) noexcept {
        if (counter <= 0 || frequency <= 0) {
            reset();
            return 0.0;
        }
        if (!valid_ || !render_only || server_time != server_time_) {
            valid_ = true;
            server_time_ = server_time;
            anchor_counter_ = counter;
            return 0.0;
        }
        if (counter <= anchor_counter_) {
            return 0.0;
        }
        const long double elapsed_ms =
            (static_cast<long double>(counter - anchor_counter_) * 1000.0L) /
            static_cast<long double>(frequency);
        constexpr long double k_max_fractional_ms = 0.999999L;
        return static_cast<double>(
            std::clamp(elapsed_ms, 0.0L, k_max_fractional_ms));
    }

    void reset() noexcept {
        valid_ = false;
        server_time_ = 0;
        anchor_counter_ = 0;
    }

  private:
    bool valid_{};
    std::int32_t server_time_{};
    std::int64_t anchor_counter_{};
};

inline float refine_snapshot_fraction(const float stock_fraction,
                                      const double fractional_ms,
                                      const std::int32_t render_time,
                                      const std::int32_t current_snapshot_time,
                                      const std::int32_t next_snapshot_time) noexcept {
    const std::int64_t interval = static_cast<std::int64_t>(next_snapshot_time) -
                                  static_cast<std::int64_t>(current_snapshot_time);
    if (!std::isfinite(stock_fraction) || !std::isfinite(fractional_ms) ||
        fractional_ms <= 0.0 || fractional_ms >= 1.0 || interval <= 0 ||
        render_time < current_snapshot_time || render_time >= next_snapshot_time) {
        return stock_fraction;
    }
    const long double refined =
        (static_cast<long double>(render_time) -
             static_cast<long double>(current_snapshot_time) +
         static_cast<long double>(fractional_ms)) /
        static_cast<long double>(interval);
    if (refined < 0.0L || refined >= 1.0L) {
        return stock_fraction;
    }
    const float result = static_cast<float>(refined);
    return std::isfinite(result) && result >= stock_fraction ? result : stock_fraction;
}

template <std::size_t EntityCount, std::size_t FieldCount>
class StridedFieldTransaction {
  public:
    bool capture(const std::uint8_t* base, const std::size_t stride,
                 const std::size_t field_offset) noexcept {
        clear();
        if (base == nullptr || stride < field_offset + FieldCount * sizeof(std::uint32_t)) {
            return false;
        }
        base_ = base;
        stride_ = stride;
        field_offset_ = field_offset;
        for (std::size_t entity = 0; entity < EntityCount; ++entity) {
            std::memcpy(values_[entity].data(), base + entity * stride + field_offset,
                        FieldCount * sizeof(std::uint32_t));
        }
        valid_ = true;
        return true;
    }

    bool restore(std::uint8_t* base, const std::size_t stride,
                 const std::size_t field_offset) noexcept {
        if (!valid_ || base == nullptr || base != base_ || stride != stride_ ||
            field_offset != field_offset_) {
            return false;
        }
        for (std::size_t entity = 0; entity < EntityCount; ++entity) {
            auto* const destination = base + entity * stride + field_offset;
            std::memcpy(destination, values_[entity].data(),
                        FieldCount * sizeof(std::uint32_t));
            if (std::memcmp(destination, values_[entity].data(),
                            FieldCount * sizeof(std::uint32_t)) != 0) {
                return false;
            }
        }
        clear();
        return true;
    }

    void clear() noexcept {
        valid_ = false;
        base_ = nullptr;
        stride_ = 0;
        field_offset_ = 0;
    }

    [[nodiscard]] bool valid() const noexcept { return valid_; }

  private:
    std::array<std::array<std::uint32_t, FieldCount>, EntityCount> values_{};
    const std::uint8_t* base_{};
    std::size_t stride_{};
    std::size_t field_offset_{};
    bool valid_{};
};

template <std::size_t Capacity, std::size_t FieldCount>
class IndexedFieldTransaction {
  public:
    void begin() noexcept {
        clear();
        valid_ = true;
    }

    bool capture(std::uint8_t* fields) noexcept {
        if (!valid_ || fields == nullptr) {
            return false;
        }
        for (std::size_t index = 0; index < count_; ++index) {
            if (entries_[index].fields == fields) {
                return true;
            }
        }
        return capture_unique(fields);
    }

    bool capture_unique(std::uint8_t* fields) noexcept {
        if (!valid_ || fields == nullptr || count_ >= Capacity) {
            return false;
        }
        Entry& entry = entries_[count_++];
        entry.fields = fields;
        std::memcpy(entry.values.data(), fields,
                    FieldCount * sizeof(std::uint32_t));
        return true;
    }

    bool restore() noexcept {
        if (!valid_) {
            return false;
        }
        for (std::size_t index = 0; index < count_; ++index) {
            Entry& entry = entries_[index];
            if (entry.fields == nullptr) {
                return false;
            }
            std::memcpy(entry.fields, entry.values.data(),
                        FieldCount * sizeof(std::uint32_t));
            if (std::memcmp(entry.fields, entry.values.data(),
                            FieldCount * sizeof(std::uint32_t)) != 0) {
                return false;
            }
        }
        clear();
        return true;
    }

    void clear() noexcept {
        valid_ = false;
        count_ = 0;
    }

    [[nodiscard]] bool valid() const noexcept { return valid_; }
    [[nodiscard]] std::size_t count() const noexcept { return count_; }

  private:
    struct Entry {
        std::uint8_t* fields{};
        std::array<std::uint32_t, FieldCount> values{};
    };

    std::array<Entry, Capacity> entries_{};
    std::size_t count_{};
    bool valid_{};
};

} // namespace ql1k
