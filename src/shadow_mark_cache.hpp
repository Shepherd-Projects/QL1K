#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace ql1k {

struct ShadowPolyVert {
    float xyz[3]{};
    float st[2]{};
    std::uint8_t modulate[4]{};
};

static_assert(sizeof(ShadowPolyVert) == 24U);

struct ShadowMarkKey {
    std::int32_t integer_time{};
    std::int32_t shader{};
    std::int32_t alpha_fade{};
    std::int32_t temporary{};
    std::array<std::uint32_t, 4> topology_bits{};

    [[nodiscard]] bool operator==(const ShadowMarkKey&) const noexcept = default;
};

[[nodiscard]] inline ShadowMarkKey make_shadow_mark_key(
    const std::int32_t integer_time, const std::int32_t shader, const float* const direction,
    const std::int32_t alpha_fade, const float radius, const std::int32_t temporary) noexcept {
    ShadowMarkKey key{};
    key.integer_time = integer_time;
    key.shader = shader;
    key.alpha_fade = alpha_fade;
    key.temporary = temporary;
    if (direction == nullptr) {
        return key;
    }
    key.topology_bits = {
        std::bit_cast<std::uint32_t>(direction[0]),
        std::bit_cast<std::uint32_t>(direction[1]),
        std::bit_cast<std::uint32_t>(direction[2]),
        std::bit_cast<std::uint32_t>(radius),
    };
    return key;
}

template <std::size_t SlotCount, std::size_t MaxPolys, std::size_t MaxVertices>
class ShadowMarkCache {
public:
    struct CachedPoly {
        std::int32_t shader{};
        std::uint16_t first_vertex{};
        std::uint16_t vertex_count{};
    };

    struct Entry {
        ShadowMarkKey key{};
        float origin[3]{};
        std::array<CachedPoly, MaxPolys> polys{};
        std::array<ShadowPolyVert, MaxVertices> vertices{};
        std::uint16_t poly_count{};
        std::uint16_t vertex_count{};
        bool valid{};
        bool overflow{};
    };

    void clear() noexcept { entries_ = {}; }

    [[nodiscard]] Entry* find(const std::size_t slot,
                              const ShadowMarkKey& key) noexcept {
        if (slot >= SlotCount) {
            return nullptr;
        }
        Entry& entry = entries_[slot];
        return entry.valid && entry.key == key ? &entry : nullptr;
    }

    [[nodiscard]] Entry* begin_capture(const std::size_t slot,
                                       const ShadowMarkKey& key,
                                       const float* const origin) noexcept {
        if (slot >= SlotCount || origin == nullptr) {
            return nullptr;
        }
        Entry& entry = entries_[slot];
        entry = {};
        entry.key = key;
        std::memcpy(entry.origin, origin, sizeof(entry.origin));
        return &entry;
    }

    [[nodiscard]] bool append(Entry* const entry, const std::int32_t shader,
                              const std::int32_t vertex_count,
                              const ShadowPolyVert* const vertices) noexcept {
        if (entry == nullptr || vertex_count <= 0 || vertices == nullptr ||
            entry->poly_count >= MaxPolys ||
            static_cast<std::size_t>(entry->vertex_count) +
                    static_cast<std::size_t>(vertex_count) >
                MaxVertices) {
            if (entry != nullptr) {
                entry->overflow = true;
                entry->valid = false;
            }
            return false;
        }
        CachedPoly& poly = entry->polys[entry->poly_count++];
        poly.shader = shader;
        poly.first_vertex = entry->vertex_count;
        poly.vertex_count = static_cast<std::uint16_t>(vertex_count);
        std::memcpy(entry->vertices.data() + entry->vertex_count, vertices,
                    static_cast<std::size_t>(vertex_count) * sizeof(ShadowPolyVert));
        entry->vertex_count = static_cast<std::uint16_t>(
            entry->vertex_count + static_cast<std::uint16_t>(vertex_count));
        return true;
    }

    [[nodiscard]] bool finish_capture(Entry* const entry) noexcept {
        if (entry == nullptr || entry->overflow) {
            return false;
        }
        entry->valid = true;
        return true;
    }

    [[nodiscard]] static bool translated_poly(
        const Entry& entry, const std::size_t poly_index, const float* const origin,
        const std::span<ShadowPolyVert> destination) noexcept {
        if (!entry.valid || origin == nullptr || poly_index >= entry.poly_count) {
            return false;
        }
        const CachedPoly& poly = entry.polys[poly_index];
        if (poly.vertex_count > destination.size() ||
            static_cast<std::size_t>(poly.first_vertex) + poly.vertex_count >
                entry.vertex_count) {
            return false;
        }
        const float delta[3]{origin[0] - entry.origin[0], origin[1] - entry.origin[1],
                             origin[2] - entry.origin[2]};
        for (std::size_t index = 0; index < poly.vertex_count; ++index) {
            destination[index] = entry.vertices[poly.first_vertex + index];
            destination[index].xyz[0] += delta[0];
            destination[index].xyz[1] += delta[1];
            destination[index].xyz[2] += delta[2];
        }
        return true;
    }

private:
    std::array<Entry, SlotCount> entries_{};
};

} // namespace ql1k
