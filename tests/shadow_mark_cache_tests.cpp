#include "shadow_mark_cache.hpp"

#include <array>
#include <cstdlib>
#include <cstdint>

namespace {

using Cache = ql1k::ShadowMarkCache<2U, 2U, 4U>;

void require(const bool condition) {
    if (!condition) {
        std::abort();
    }
}

ql1k::ShadowMarkKey key(const std::int32_t time = 100) {
    const float direction[3]{0.0F, 0.0F, 1.0F};
    return ql1k::make_shadow_mark_key(time, 7, direction, 0, 24.0F, 1);
}

ql1k::ShadowPolyVert vertex(const float x, const float y, const float z) {
    ql1k::ShadowPolyVert result{};
    result.xyz[0] = x;
    result.xyz[1] = y;
    result.xyz[2] = z;
    result.st[0] = 0.25F;
    result.st[1] = 0.75F;
    result.modulate[3] = 255;
    return result;
}

void capture_replay_translates_positions_only() {
    Cache cache;
    const float captured_origin[3]{10.0F, 20.0F, 30.0F};
    auto* entry = cache.begin_capture(0, key(), captured_origin);
    const std::array vertices{vertex(9.0F, 19.0F, 30.0F),
                              vertex(11.0F, 21.0F, 30.0F)};
    require(cache.append(entry, 7, static_cast<std::int32_t>(vertices.size()),
                         vertices.data()));
    require(cache.finish_capture(entry));
    require(cache.find(0, key()) == entry);

    const float replay_origin[3]{10.5F, 19.0F, 32.0F};
    std::array<ql1k::ShadowPolyVert, 2> translated{};
    require(Cache::translated_poly(*entry, 0, replay_origin, translated));
    require(translated[0].xyz[0] == 9.5F);
    require(translated[0].xyz[1] == 18.0F);
    require(translated[0].xyz[2] == 32.0F);
    require(translated[1].st[0] == vertices[1].st[0]);
    require(translated[1].modulate[3] == 255);
}

void key_and_slot_mismatches_miss() {
    Cache cache;
    const float origin[3]{};
    auto* entry = cache.begin_capture(0, key(), origin);
    require(cache.finish_capture(entry));
    require(cache.find(0, key(101)) == nullptr);
    require(cache.find(1, key()) == nullptr);
    require(cache.find(2, key()) == nullptr);
}

void overflow_never_becomes_valid() {
    Cache cache;
    const float origin[3]{};
    auto* entry = cache.begin_capture(0, key(), origin);
    const std::array vertices{vertex(0, 0, 0), vertex(1, 0, 0), vertex(2, 0, 0),
                              vertex(3, 0, 0), vertex(4, 0, 0)};
    require(!cache.append(entry, 7, static_cast<std::int32_t>(vertices.size()),
                          vertices.data()));
    require(!cache.finish_capture(entry));
    require(cache.find(0, key()) == nullptr);
}

void empty_capture_is_a_valid_no_geometry_result() {
    Cache cache;
    const float origin[3]{1.0F, 2.0F, 3.0F};
    auto* entry = cache.begin_capture(0, key(), origin);
    require(cache.finish_capture(entry));
    require(cache.find(0, key()) == entry);
    require(entry->poly_count == 0);
}

} // namespace

int main() {
    capture_replay_translates_positions_only();
    key_and_slot_mismatches_miss();
    overflow_never_becomes_valid();
    empty_capture_is_a_valid_no_geometry_result();
    return 0;
}
