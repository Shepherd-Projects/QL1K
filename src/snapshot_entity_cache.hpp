#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace ql1k {

template <std::size_t MaxEntities, std::size_t MaxPlayers>
class SnapshotEntityIndexCache {
  public:
    static_assert(MaxEntities != 0U);
    static_assert(MaxPlayers <= MaxEntities);

    [[nodiscard]] bool matches(const void* const identity,
                               const std::int32_t snapshot_time,
                               const std::int32_t local_player,
                               const std::size_t source_count) const noexcept {
        return valid_ && identity != nullptr && identity_ == identity &&
               snapshot_time_ == snapshot_time && local_player_ == local_player &&
               source_count_ == source_count;
    }

    bool rebuild(const void* const identity, const std::int32_t snapshot_time,
                 const std::int32_t local_player,
                 const std::span<const std::int32_t> source_entities) noexcept {
        clear();
        if (identity == nullptr || local_player < 0 ||
            local_player >= static_cast<std::int32_t>(MaxEntities) ||
            source_entities.size() > MaxEntities) {
            return false;
        }

        std::array<std::uint64_t, (MaxEntities + 63U) / 64U> seen{};
        const auto add_unique = [&](const std::int32_t number) noexcept {
            if (number < 0 || number >= static_cast<std::int32_t>(MaxEntities)) {
                return false;
            }
            const std::size_t index = static_cast<std::size_t>(number);
            const std::uint64_t bit = 1ULL << (index & 63U);
            std::uint64_t& word = seen[index >> 6U];
            if ((word & bit) != 0U) {
                return true;
            }
            word |= bit;
            entity_numbers_[entity_count_++] = number;
            if (number != local_player && number < static_cast<std::int32_t>(MaxPlayers)) {
                player_numbers_[player_count_++] = number;
            }
            return true;
        };

        if (!add_unique(local_player)) {
            return false;
        }
        for (const std::int32_t number : source_entities) {
            if (!add_unique(number)) {
                clear();
                return false;
            }
        }
        identity_ = identity;
        snapshot_time_ = snapshot_time;
        local_player_ = local_player;
        source_count_ = source_entities.size();
        valid_ = true;
        return true;
    }

    void clear() noexcept {
        identity_ = nullptr;
        snapshot_time_ = 0;
        local_player_ = 0;
        source_count_ = 0U;
        entity_count_ = 0U;
        player_count_ = 0U;
        valid_ = false;
    }

    [[nodiscard]] std::span<const std::int32_t> entity_numbers() const noexcept {
        return {entity_numbers_.data(), entity_count_};
    }

    [[nodiscard]] std::span<const std::int32_t> player_numbers() const noexcept {
        return {player_numbers_.data(), player_count_};
    }

  private:
    const void* identity_{};
    std::int32_t snapshot_time_{};
    std::int32_t local_player_{};
    std::size_t source_count_{};
    std::array<std::int32_t, MaxEntities> entity_numbers_{};
    std::array<std::int32_t, MaxPlayers> player_numbers_{};
    std::size_t entity_count_{};
    std::size_t player_count_{};
    bool valid_{};
};

} // namespace ql1k
