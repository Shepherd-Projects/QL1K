#include "render_interpolation.hpp"
#include "snapshot_entity_cache.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

namespace {

void require(const bool condition) {
    if (!condition) {
        std::abort();
    }
}

void render_clock_anchors_and_clamps() {
    ql1k::SubmillisecondRenderClock clock;
    constexpr std::int64_t frequency = 10'000'000;
    require(clock.sample(100, 1'000'000, frequency, false) == 0.0);
    const double half_ms = clock.sample(100, 1'005'000, frequency, true);
    require(std::fabs(half_ms - 0.5) < 0.000001);
    const double clamped = clock.sample(100, 1'020'000, frequency, true);
    require(clamped > 0.999 && clamped < 1.0);
    require(clock.sample(101, 1'021'000, frequency, true) == 0.0);
    require(clock.sample(101, 1'026'000, frequency, true) > 0.49);
    require(clock.sample(101, 1'027'000, 0, true) == 0.0);
}

void snapshot_fraction_is_refined_without_crossing_next_snapshot() {
    const float refined = ql1k::refine_snapshot_fraction(0.4F, 0.5, 1010, 1000, 1025);
    require(std::fabs(refined - 0.42F) < 0.000001F);
    require(ql1k::refine_snapshot_fraction(0.96F, 0.5, 1024, 1000, 1025) > 0.97F);
    require(ql1k::refine_snapshot_fraction(1.0F, 0.5, 1025, 1000, 1025) == 1.0F);
    require(ql1k::refine_snapshot_fraction(0.4F, 1.0, 1010, 1000, 1025) == 0.4F);
    require(ql1k::refine_snapshot_fraction(0.4F, 0.5, 1010, 1025, 1000) == 0.4F);
}

void submitted_pose_signature_is_exact_and_presence_sensitive() {
    ql1k::SubmittedPoseSignature<4> baseline{};
    require(!baseline.valid());
    baseline.camera_valid = true;
    baseline.camera_bits[0] = 0x3f800000U;
    baseline.player_presence = 1ULL << 2U;
    baseline.player_position_bits[2] = {1U, 2U, 3U};
    require(baseline.valid());

    auto candidate = baseline;
    require(candidate == baseline);
    candidate.player_position_bits[2][1] ^= 1U;
    require(!(candidate == baseline));
    candidate = baseline;
    candidate.player_presence = 0U;
    require(!(candidate == baseline));
    candidate = baseline;
    candidate.camera_bits[11] = 1U;
    require(!(candidate == baseline));
}

void strided_transaction_restores_only_selected_fields() {
    constexpr std::size_t entities = 4;
    constexpr std::size_t stride = 64;
    constexpr std::size_t offset = 12;
    std::array<std::uint8_t, entities * stride> storage{};
    for (std::size_t index = 0; index < storage.size(); ++index) {
        storage[index] = static_cast<std::uint8_t>(index);
    }
    const auto original = storage;
    ql1k::StridedFieldTransaction<entities, 6> transaction;
    require(transaction.capture(storage.data(), stride, offset));
    require(transaction.valid());
    for (std::size_t entity = 0; entity < entities; ++entity) {
        for (std::size_t byte = 0; byte < 6 * sizeof(std::uint32_t); ++byte) {
            storage[entity * stride + offset + byte] ^= 0x5a;
        }
        storage[entity * stride + offset - 1] ^= 0xff;
    }
    require(transaction.restore(storage.data(), stride, offset));
    require(!transaction.valid());
    for (std::size_t entity = 0; entity < entities; ++entity) {
        for (std::size_t byte = 0; byte < 6 * sizeof(std::uint32_t); ++byte) {
            require(storage[entity * stride + offset + byte] ==
                    original[entity * stride + offset + byte]);
        }
        require(storage[entity * stride + offset - 1] !=
                original[entity * stride + offset - 1]);
    }
}

void indexed_transaction_restores_exact_unique_addresses() {
    std::array<std::array<std::uint32_t, 8>, 3> entities{};
    for (std::size_t entity = 0; entity < entities.size(); ++entity) {
        for (std::size_t field = 0; field < entities[entity].size(); ++field) {
            entities[entity][field] = static_cast<std::uint32_t>(entity * 100 + field);
        }
    }
    const auto original = entities;
    ql1k::IndexedFieldTransaction<3, 6> transaction;
    transaction.begin();
    require(transaction.capture(reinterpret_cast<std::uint8_t*>(entities[0].data())));
    require(transaction.capture(reinterpret_cast<std::uint8_t*>(entities[1].data())));
    require(transaction.capture(reinterpret_cast<std::uint8_t*>(entities[0].data())));
    require(transaction.count() == 2);
    for (std::size_t field = 0; field < 6; ++field) {
        entities[0][field] ^= 0xffffffffU;
        entities[1][field] ^= 0xffffffffU;
    }
    entities[2][0] ^= 0xffffffffU;
    require(transaction.restore());
    require(entities[0] == original[0]);
    require(entities[1] == original[1]);
    require(entities[2][0] != original[2][0]);
}

void snapshot_entity_cache_deduplicates_and_reuses_immutable_snapshots() {
    ql1k::SnapshotEntityIndexCache<8, 4> cache;
    constexpr std::uintptr_t token = 0x1234U;
    const std::array<std::int32_t, 6> source{2, 1, 2, 7, 3, 1};
    require(cache.rebuild(reinterpret_cast<const void*>(token), 100, 1, source));
    require(cache.matches(reinterpret_cast<const void*>(token), 100, 1, source.size()));
    const std::array<std::int32_t, 4> expected_entities{1, 2, 7, 3};
    require(cache.entity_numbers().size() == 4U);
    for (std::size_t index = 0; index < cache.entity_numbers().size(); ++index) {
        require(cache.entity_numbers()[index] == expected_entities[index]);
    }
    require(cache.player_numbers().size() == 2U);
    require(cache.player_numbers()[0] == 2);
    require(cache.player_numbers()[1] == 3);
    require(!cache.matches(reinterpret_cast<const void*>(token), 101, 1, source.size()));
    const std::array<std::int32_t, 1> invalid{8};
    require(!cache.rebuild(reinterpret_cast<const void*>(token), 100, 1, invalid));
    require(cache.entity_numbers().empty());
}

} // namespace

int main() {
    render_clock_anchors_and_clamps();
    snapshot_fraction_is_refined_without_crossing_next_snapshot();
    submitted_pose_signature_is_exact_and_presence_sensitive();
    strided_transaction_restores_only_selected_fields();
    indexed_transaction_restores_exact_unique_addresses();
    snapshot_entity_cache_deduplicates_and_reuses_immutable_snapshots();
    return 0;
}
