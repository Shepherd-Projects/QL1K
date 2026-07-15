#pragma once

#include "shadow_mark_cache.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>

namespace ql1k {

constexpr std::size_t k_player_scene_ref_entity_bytes = 0x8CU;
// Everything before centity_t::lerpOrigin/lerpAngles is part of the stock
// presentation state.  This includes the 0xEC-byte network state plus the
// persistent animation and torso/legs smoothing state consumed by CG_Player.
constexpr std::size_t k_player_scene_nonpose_state_bytes = 0x2B8U;
constexpr std::size_t k_player_scene_ref_type_offset = 0x00U;
constexpr std::size_t k_player_scene_renderfx_offset = 0x04U;
constexpr std::size_t k_player_scene_hmodel_offset = 0x08U;
constexpr std::size_t k_player_scene_lighting_origin_offset = 0x0CU;
constexpr std::size_t k_player_scene_shadow_plane_offset = 0x18U;
constexpr std::size_t k_player_scene_axis_offset = 0x1CU;
constexpr std::size_t k_player_scene_origin_offset = 0x44U;
constexpr std::size_t k_player_scene_old_origin_offset = 0x54U;
constexpr std::size_t k_player_scene_backlerp_offset = 0x64U;
constexpr std::size_t k_player_scene_skin_number_offset = 0x68U;
constexpr std::size_t k_player_scene_custom_skin_offset = 0x6CU;
constexpr std::size_t k_player_scene_custom_shader_offset = 0x70U;
constexpr std::size_t k_player_scene_rgba_offset = 0x74U;
constexpr std::size_t k_player_scene_shader_texcoord_offset = 0x78U;
constexpr std::size_t k_player_scene_shader_time_offset = 0x80U;
constexpr std::size_t k_player_scene_radius_offset = 0x84U;
constexpr std::size_t k_player_scene_rotation_offset = 0x88U;

enum class PlayerSceneRefType : std::int32_t {
    model = 0,
    poly = 1,
    sprite = 2,
    beam = 3,
    rail_core = 4,
    rail_rings = 5,
    lightning = 6,
    portal_surface = 7,
};

struct PlayerScenePose {
    std::array<float, 3> origin{};
    std::array<float, 3> angles{};

    [[nodiscard]] bool operator==(const PlayerScenePose&) const noexcept = default;
};

struct PlayerSceneShapeSignature {
    std::uint64_t primary{};
    std::uint64_t secondary{};

    [[nodiscard]] bool operator==(
        const PlayerSceneShapeSignature&) const noexcept = default;
};

// Quake refEntity axes are three world-space basis vectors. A player-angle
// delta maps a captured world vector into captured-local coordinates, then
// expands those coordinates through the current basis.
struct PlayerSceneBasis {
    std::array<std::array<float, 3>, 3> axis{};
};

using PlayerSceneRefEntity =
    std::array<std::byte, k_player_scene_ref_entity_bytes>;
using PlayerScenePolyVert = ShadowPolyVert;

struct PlayerScenePoly {
    std::int32_t shader{};
    std::uint16_t first_vertex{};
    std::uint16_t vertex_count{};

    [[nodiscard]] bool operator==(const PlayerScenePoly&) const noexcept = default;
};

struct PlayerSceneDlight {
    std::array<float, 3> origin{};
    float radius{};
    std::array<float, 3> color{};
    std::int32_t additive{};

    [[nodiscard]] bool operator==(const PlayerSceneDlight&) const noexcept = default;
};

struct PlayerSceneKey {
    std::array<std::byte, k_player_scene_nonpose_state_bytes> nonpose_state{};
    std::uintptr_t entity_identity{};
    std::uintptr_t current_snapshot{};
    std::uintptr_t next_snapshot{};
    std::int32_t integer_time{};
    std::int32_t current_snapshot_time{};
    std::int32_t next_snapshot_time{};
    std::int32_t module_epoch{};
    std::int32_t renderer_epoch{};

    [[nodiscard]] bool operator==(const PlayerSceneKey&) const noexcept = default;
};

[[nodiscard]] inline bool player_scene_finite(const float value) noexcept {
    return std::isfinite(value) && std::fabs(value) <= 2147483.0F;
}

[[nodiscard]] inline bool player_scene_pose_valid(
    const PlayerScenePose& pose) noexcept {
    for (const float component : pose.origin) {
        if (!player_scene_finite(component)) {
            return false;
        }
    }
    for (const float component : pose.angles) {
        if (!player_scene_finite(component)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] inline bool player_scene_angles_exact(
    const PlayerScenePose& captured, const PlayerScenePose& current) noexcept {
    return std::memcmp(captured.angles.data(), current.angles.data(),
                       sizeof(captured.angles)) == 0;
}

[[nodiscard]] inline bool player_scene_basis_valid(
    const PlayerSceneBasis& basis) noexcept {
    for (const auto& vector : basis.axis) {
        for (const float component : vector) {
            if (!player_scene_finite(component) ||
                std::fabs(component) > 1.001F) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] inline bool player_scene_interpolate_rotation_basis(
    const PlayerSceneBasis& current, const PlayerSceneBasis& next,
    const float fraction, PlayerSceneBasis& interpolated) noexcept {
    if (!player_scene_basis_valid(current) || !player_scene_basis_valid(next) ||
        !player_scene_finite(fraction) || fraction < 0.0F || fraction > 1.0F) {
        return false;
    }
    if (fraction == 0.0F) {
        interpolated = current;
        return true;
    }
    if (fraction == 1.0F) {
        interpolated = next;
        return true;
    }
    std::array<std::array<float, 3>, 3> blended{};
    for (std::size_t row = 0; row < blended.size(); ++row) {
        for (std::size_t axis = 0; axis < blended[row].size(); ++axis) {
            blended[row][axis] = current.axis[row][axis] +
                                 (next.axis[row][axis] -
                                  current.axis[row][axis]) *
                                     fraction;
        }
    }
    const auto dot = [](const std::array<float, 3>& left,
                        const std::array<float, 3>& right) noexcept {
        return left[0] * right[0] + left[1] * right[1] +
               left[2] * right[2];
    };
    const auto normalize = [&](std::array<float, 3>& vector) noexcept {
        const float squared_length = dot(vector, vector);
        if (!player_scene_finite(squared_length) ||
            squared_length < 0.000001F) {
            return false;
        }
        const float inverse_length = 1.0F / std::sqrt(squared_length);
        for (float& component : vector) {
            component *= inverse_length;
            if (!player_scene_finite(component)) {
                return false;
            }
        }
        return true;
    };
    if (!normalize(blended[0])) {
        return false;
    }
    const float projection = dot(blended[1], blended[0]);
    for (std::size_t axis = 0; axis < 3U; ++axis) {
        blended[1][axis] -= projection * blended[0][axis];
    }
    if (!normalize(blended[1])) {
        return false;
    }
    blended[2] = {
        blended[0][1] * blended[1][2] - blended[0][2] * blended[1][1],
        blended[0][2] * blended[1][0] - blended[0][0] * blended[1][2],
        blended[0][0] * blended[1][1] - blended[0][1] * blended[1][0]};
    if (!normalize(blended[2])) {
        return false;
    }
    interpolated.axis = blended;
    return player_scene_basis_valid(interpolated);
}

[[nodiscard]] inline bool player_scene_rotate_world_vector(
    const std::array<float, 3>& source, const PlayerSceneBasis& captured,
    const PlayerSceneBasis& current, std::array<float, 3>& destination) noexcept {
    if (!player_scene_basis_valid(captured) ||
        !player_scene_basis_valid(current)) {
        return false;
    }
    std::array<float, 3> local{};
    for (std::size_t basis = 0; basis < 3U; ++basis) {
        for (std::size_t world = 0; world < 3U; ++world) {
            if (!player_scene_finite(source[world])) {
                return false;
            }
            local[basis] += source[world] * captured.axis[basis][world];
        }
        if (!player_scene_finite(local[basis])) {
            return false;
        }
    }
    destination = {};
    for (std::size_t world = 0; world < 3U; ++world) {
        for (std::size_t basis = 0; basis < 3U; ++basis) {
            destination[world] += local[basis] * current.axis[basis][world];
        }
        if (!player_scene_finite(destination[world])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] inline bool player_scene_transform_world_point(
    const std::array<float, 3>& source, const PlayerScenePose& captured_pose,
    const PlayerScenePose& current_pose, const PlayerSceneBasis& captured_basis,
    const PlayerSceneBasis& current_basis,
    std::array<float, 3>& destination) noexcept {
    if (!player_scene_pose_valid(captured_pose) ||
        !player_scene_pose_valid(current_pose)) {
        return false;
    }
    std::array<float, 3> relative{};
    for (std::size_t axis = 0; axis < 3U; ++axis) {
        if (!player_scene_finite(source[axis])) {
            return false;
        }
        relative[axis] = source[axis] - captured_pose.origin[axis];
    }
    if (!player_scene_rotate_world_vector(relative, captured_basis,
                                          current_basis, destination)) {
        return false;
    }
    for (std::size_t axis = 0; axis < 3U; ++axis) {
        destination[axis] += current_pose.origin[axis];
        if (!player_scene_finite(destination[axis])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] inline std::array<float, 3> player_scene_origin_delta(
    const PlayerScenePose& captured, const PlayerScenePose& current) noexcept {
    return {current.origin[0] - captured.origin[0],
            current.origin[1] - captured.origin[1],
            current.origin[2] - captured.origin[2]};
}

[[nodiscard]] inline bool player_scene_delta_valid(
    const std::array<float, 3>& delta) noexcept {
    for (const float component : delta) {
        if (!std::isfinite(component) || std::fabs(component) > 4096.0F) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] inline std::int32_t player_scene_ref_type(
    const PlayerSceneRefEntity& entity) noexcept {
    std::int32_t result{};
    std::memcpy(&result, entity.data() + k_player_scene_ref_type_offset,
                sizeof(result));
    return result;
}

[[nodiscard]] inline std::int32_t player_scene_ref_hmodel(
    const PlayerSceneRefEntity& entity) noexcept {
    std::int32_t result{};
    std::memcpy(&result, entity.data() + k_player_scene_hmodel_offset,
                sizeof(result));
    return result;
}

[[nodiscard]] inline bool player_scene_ref_axis_bits(
    const PlayerSceneRefEntity& entity,
    std::array<std::uint32_t, 9>& bits) noexcept {
    if (k_player_scene_axis_offset + bits.size() * sizeof(float) >
        entity.size()) {
        return false;
    }
    std::memcpy(bits.data(), entity.data() + k_player_scene_axis_offset,
                bits.size() * sizeof(float));
    return true;
}

[[nodiscard]] inline bool player_scene_ref_type_supported(
    const std::int32_t type) noexcept {
    switch (static_cast<PlayerSceneRefType>(type)) {
    case PlayerSceneRefType::model:
    case PlayerSceneRefType::sprite:
    case PlayerSceneRefType::beam:
    case PlayerSceneRefType::rail_core:
    case PlayerSceneRefType::rail_rings:
    case PlayerSceneRefType::lightning:
        return true;
    case PlayerSceneRefType::poly:
    case PlayerSceneRefType::portal_surface:
    default:
        return false;
    }
}

[[nodiscard]] inline bool player_scene_translate_vec3(
    PlayerSceneRefEntity& entity, const std::size_t offset,
    const std::array<float, 3>& delta) noexcept {
    if (offset + 3U * sizeof(float) > entity.size()) {
        return false;
    }
    std::array<float, 3> value{};
    std::memcpy(value.data(), entity.data() + offset, sizeof(value));
    for (std::size_t axis = 0; axis < value.size(); ++axis) {
        if (!player_scene_finite(value[axis])) {
            return false;
        }
        value[axis] += delta[axis];
        if (!player_scene_finite(value[axis])) {
            return false;
        }
    }
    std::memcpy(entity.data() + offset, value.data(), sizeof(value));
    return true;
}

[[nodiscard]] inline bool player_scene_beam_endpoint_range_limited(
    const PlayerSceneRefEntity& entity, bool& range_limited) noexcept {
    std::array<float, 3> origin{};
    std::array<float, 3> endpoint{};
    std::memcpy(origin.data(), entity.data() + k_player_scene_origin_offset,
                sizeof(origin));
    std::memcpy(endpoint.data(),
                entity.data() + k_player_scene_old_origin_offset,
                sizeof(endpoint));
    float squared_distance{};
    for (std::size_t axis = 0; axis < origin.size(); ++axis) {
        if (!player_scene_finite(origin[axis]) ||
            !player_scene_finite(endpoint[axis])) {
            return false;
        }
        const float difference = endpoint[axis] - origin[axis];
        squared_distance += difference * difference;
    }
    if (!player_scene_finite(squared_distance)) {
        return false;
    }
    // CG_LightningBolt traces 768 units. The visual muzzle tag differs from
    // the trace start, so use a conservative 700-unit lower bound: clipped
    // endpoints stay on world geometry; only free-space range ends translate.
    range_limited = squared_distance >= 700.0F * 700.0F;
    return true;
}

[[nodiscard]] inline bool translate_player_scene_ref_entity(
    const PlayerSceneRefEntity& source, const PlayerScenePose& captured,
    const PlayerScenePose& current, PlayerSceneRefEntity& destination) noexcept {
    if (!player_scene_pose_valid(captured) || !player_scene_pose_valid(current) ||
        !player_scene_angles_exact(captured, current)) {
        return false;
    }
    const auto delta = player_scene_origin_delta(captured, current);
    if (!player_scene_delta_valid(delta)) {
        return false;
    }
    destination = source;
    const auto type = static_cast<PlayerSceneRefType>(player_scene_ref_type(source));
    switch (type) {
    case PlayerSceneRefType::model:
        return player_scene_translate_vec3(
                   destination, k_player_scene_lighting_origin_offset, delta) &&
               player_scene_translate_vec3(destination,
                                           k_player_scene_origin_offset, delta);
    case PlayerSceneRefType::sprite:
        return player_scene_translate_vec3(destination,
                                            k_player_scene_origin_offset, delta);
    case PlayerSceneRefType::beam:
    case PlayerSceneRefType::lightning: {
        bool range_limited{};
        if (!player_scene_beam_endpoint_range_limited(source, range_limited) ||
            !player_scene_translate_vec3(destination,
                                         k_player_scene_origin_offset, delta)) {
            return false;
        }
        return !range_limited ||
               player_scene_translate_vec3(destination,
                                           k_player_scene_old_origin_offset,
                                           delta);
    }
    case PlayerSceneRefType::rail_core:
    case PlayerSceneRefType::rail_rings:
        return true;
    case PlayerSceneRefType::poly:
    case PlayerSceneRefType::portal_surface:
    default:
        return false;
    }
}

[[nodiscard]] inline bool transform_player_scene_ref_entity(
    const PlayerSceneRefEntity& source, const PlayerScenePose& captured_pose,
    const PlayerScenePose& current_pose, const PlayerSceneBasis& captured_basis,
    const PlayerSceneBasis& current_basis,
    PlayerSceneRefEntity& destination) noexcept {
    if (!player_scene_pose_valid(captured_pose) ||
        !player_scene_pose_valid(current_pose) ||
        !player_scene_basis_valid(captured_basis) ||
        !player_scene_basis_valid(current_basis)) {
        return false;
    }
    destination = source;
    const auto transform_point = [&](const std::size_t offset) noexcept {
        if (offset + 3U * sizeof(float) > destination.size()) {
            return false;
        }
        std::array<float, 3> captured_point{};
        std::array<float, 3> transformed_point{};
        std::memcpy(captured_point.data(), source.data() + offset,
                    sizeof(captured_point));
        if (!player_scene_transform_world_point(
                captured_point, captured_pose, current_pose, captured_basis,
                current_basis, transformed_point)) {
            return false;
        }
        std::memcpy(destination.data() + offset, transformed_point.data(),
                    sizeof(transformed_point));
        return true;
    };
    const auto transform_axes = [&]() noexcept {
        for (std::size_t row = 0; row < 3U; ++row) {
            std::array<float, 3> captured_axis{};
            std::array<float, 3> transformed_axis{};
            const std::size_t offset =
                k_player_scene_axis_offset + row * 3U * sizeof(float);
            std::memcpy(captured_axis.data(), source.data() + offset,
                        sizeof(captured_axis));
            if (!player_scene_rotate_world_vector(
                    captured_axis, captured_basis, current_basis,
                    transformed_axis)) {
                return false;
            }
            std::memcpy(destination.data() + offset, transformed_axis.data(),
                        sizeof(transformed_axis));
        }
        return true;
    };
    const auto type = static_cast<PlayerSceneRefType>(player_scene_ref_type(source));
    switch (type) {
    case PlayerSceneRefType::model:
        return transform_point(k_player_scene_lighting_origin_offset) &&
               transform_point(k_player_scene_origin_offset) && transform_axes();
    case PlayerSceneRefType::sprite:
        // CG_Player's overhead connection/reward sprites use lerpOrigin plus a
        // world-up Z offset; player pitch/roll must not orbit that offset.
        return player_scene_translate_vec3(
            destination, k_player_scene_origin_offset,
            player_scene_origin_delta(captured_pose, current_pose));
    case PlayerSceneRefType::beam:
    case PlayerSceneRefType::lightning: {
        bool free_space_range_end{};
        if (!player_scene_beam_endpoint_range_limited(
                source, free_space_range_end) ||
            !transform_point(k_player_scene_origin_offset)) {
            return false;
        }
        return !free_space_range_end ||
               transform_point(k_player_scene_old_origin_offset);
    }
    case PlayerSceneRefType::rail_core:
    case PlayerSceneRefType::rail_rings:
        return true;
    case PlayerSceneRefType::poly:
    case PlayerSceneRefType::portal_surface:
    default:
        return false;
    }
}

[[nodiscard]] inline bool translate_player_scene_vertices(
    const std::span<const PlayerScenePolyVert> source,
    const PlayerScenePose& captured, const PlayerScenePose& current,
    const std::span<PlayerScenePolyVert> destination) noexcept {
    if (source.empty() || source.size() > destination.size() ||
        !player_scene_pose_valid(captured) || !player_scene_pose_valid(current) ||
        !player_scene_angles_exact(captured, current)) {
        return false;
    }
    const auto delta = player_scene_origin_delta(captured, current);
    if (!player_scene_delta_valid(delta)) {
        return false;
    }
    for (std::size_t index = 0; index < source.size(); ++index) {
        destination[index] = source[index];
        // CG_Player emits shadows and water wakes on traced world surfaces.
        // Follow the player laterally, but retain the captured floor/water Z;
        // translating Z would make the polygon float or sink.
        for (std::size_t axis = 0; axis < 2U; ++axis) {
            if (!player_scene_finite(destination[index].xyz[axis])) {
                return false;
            }
            destination[index].xyz[axis] += delta[axis];
            if (!player_scene_finite(destination[index].xyz[axis])) {
                return false;
            }
        }
        if (!player_scene_finite(destination[index].xyz[2])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] inline bool translate_player_scene_dlight(
    const PlayerSceneDlight& source, const PlayerScenePose& captured,
    const PlayerScenePose& current, PlayerSceneDlight& destination) noexcept {
    if (!player_scene_pose_valid(captured) || !player_scene_pose_valid(current) ||
        !player_scene_angles_exact(captured, current) ||
        !player_scene_finite(source.radius)) {
        return false;
    }
    const auto delta = player_scene_origin_delta(captured, current);
    if (!player_scene_delta_valid(delta)) {
        return false;
    }
    destination = source;
    for (std::size_t axis = 0; axis < 3U; ++axis) {
        if (!player_scene_finite(destination.origin[axis]) ||
            !player_scene_finite(destination.color[axis])) {
            return false;
        }
        destination.origin[axis] += delta[axis];
        if (!player_scene_finite(destination.origin[axis])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] inline bool transform_player_scene_dlight(
    const PlayerSceneDlight& source, const PlayerScenePose& captured_pose,
    const PlayerScenePose& current_pose, const PlayerSceneBasis& captured_basis,
    const PlayerSceneBasis& current_basis,
    PlayerSceneDlight& destination) noexcept {
    if (!player_scene_pose_valid(captured_pose) ||
        !player_scene_pose_valid(current_pose) ||
        !player_scene_basis_valid(captured_basis) ||
        !player_scene_basis_valid(current_basis) ||
        !player_scene_finite(source.radius)) {
        return false;
    }
    // The installed CG_Player's muzzle and powerup lights retain their
    // authoritative attachment sample during zero-time preview renders. Only
    // interpolated player translation moves that sample; applying the root
    // angle basis invents an orbit that stock does not submit.
    const auto delta = player_scene_origin_delta(captured_pose, current_pose);
    if (!player_scene_delta_valid(delta)) {
        return false;
    }
    destination = source;
    for (std::size_t axis = 0; axis < destination.origin.size(); ++axis) {
        if (!player_scene_finite(destination.origin[axis]) ||
            !player_scene_finite(destination.color[axis])) {
            return false;
        }
        destination.origin[axis] += delta[axis];
        if (!player_scene_finite(destination.origin[axis])) {
            return false;
        }
    }
    return true;
}

template <std::size_t MaxRefs, std::size_t MaxPolys,
          std::size_t MaxVertices, std::size_t MaxDlights>
struct PlayerSceneProducts {
    std::array<PlayerSceneRefEntity, MaxRefs> refs{};
    std::array<PlayerScenePoly, MaxPolys> polys{};
    std::array<PlayerScenePolyVert, MaxVertices> vertices{};
    std::array<PlayerSceneDlight, MaxDlights> dlights{};
    PlayerScenePose pose{};
    std::uint16_t ref_count{};
    std::uint16_t poly_count{};
    std::uint16_t vertex_count{};
    std::uint16_t dlight_count{};
    bool valid{};
    bool overflow{};

    void begin(const PlayerScenePose& source_pose) noexcept {
        ref_count = 0U;
        poly_count = 0U;
        vertex_count = 0U;
        dlight_count = 0U;
        valid = false;
        overflow = false;
        pose = source_pose;
    }

    [[nodiscard]] bool append_ref(const PlayerSceneRefEntity& ref) noexcept {
        if (overflow || ref_count >= refs.size() ||
            !player_scene_ref_type_supported(player_scene_ref_type(ref))) {
            overflow = true;
            return false;
        }
        refs[ref_count++] = ref;
        return true;
    }

    [[nodiscard]] bool append_poly(
        const std::int32_t shader,
        const std::span<const PlayerScenePolyVert> source) noexcept {
        if (overflow || shader == 0 || source.empty() ||
            poly_count >= polys.size() ||
            static_cast<std::size_t>(vertex_count) + source.size() >
                vertices.size()) {
            overflow = true;
            return false;
        }
        PlayerScenePoly& poly = polys[poly_count++];
        poly.shader = shader;
        poly.first_vertex = vertex_count;
        poly.vertex_count = static_cast<std::uint16_t>(source.size());
        std::memcpy(vertices.data() + vertex_count, source.data(),
                    source.size_bytes());
        vertex_count = static_cast<std::uint16_t>(vertex_count + source.size());
        return true;
    }

    [[nodiscard]] bool append_dlight(
        const PlayerSceneDlight& dlight) noexcept {
        if (overflow || dlight_count >= dlights.size()) {
            overflow = true;
            return false;
        }
        dlights[dlight_count++] = dlight;
        return true;
    }

    [[nodiscard]] bool finish() noexcept {
        valid = !overflow && player_scene_pose_valid(pose) &&
                (ref_count != 0U || poly_count != 0U || dlight_count != 0U);
        return valid;
    }
};

template <std::size_t MaxRefs, std::size_t MaxPolys,
          std::size_t MaxVertices, std::size_t MaxDlights>
[[nodiscard]] bool player_scene_shape_signature(
    const PlayerSceneProducts<MaxRefs, MaxPolys, MaxVertices, MaxDlights>& products,
    PlayerSceneShapeSignature& signature) noexcept {
    if (!products.valid || products.ref_count > products.refs.size() ||
        products.dlight_count > products.dlights.size()) {
        return false;
    }
    std::uint64_t primary = 1469598103934665603ULL;
    std::uint64_t secondary = 1099511628211ULL ^ 0x9E3779B97F4A7C15ULL;
    const auto mix_byte = [&](const std::uint8_t value) noexcept {
        primary = (primary ^ value) * 1099511628211ULL;
        secondary ^= static_cast<std::uint64_t>(value) +
                     0x9E3779B97F4A7C15ULL + (secondary << 6U) +
                     (secondary >> 2U);
    };
    const auto mix_u32 = [&](const std::uint32_t value) noexcept {
        for (std::size_t byte = 0; byte < sizeof(value); ++byte) {
            mix_byte(static_cast<std::uint8_t>(value >> (byte * 8U)));
        }
    };
    mix_u32(products.ref_count);
    mix_u32(products.dlight_count);
    for (std::size_t index = 0; index < products.ref_count; ++index) {
        const auto& ref = products.refs[index];
        const auto mix_ref_u32 = [&](const std::size_t offset) noexcept {
            std::uint32_t value{};
            std::memcpy(&value, ref.data() + offset, sizeof(value));
            mix_u32(value);
        };
        const std::int32_t type = player_scene_ref_type(ref);
        if (!player_scene_ref_type_supported(type)) {
            return false;
        }
        mix_u32(static_cast<std::uint32_t>(index));
        mix_ref_u32(k_player_scene_ref_type_offset);
        mix_ref_u32(k_player_scene_renderfx_offset);
        mix_ref_u32(k_player_scene_hmodel_offset);
        mix_ref_u32(k_player_scene_skin_number_offset);
        mix_ref_u32(k_player_scene_custom_skin_offset);
        mix_ref_u32(k_player_scene_custom_shader_offset);
    }
    for (std::size_t index = 0; index < products.dlight_count; ++index) {
        mix_u32(static_cast<std::uint32_t>(index));
        mix_u32(static_cast<std::uint32_t>(products.dlights[index].additive));
    }
    if (primary == 0U && secondary == 0U) {
        secondary = 1U;
    }
    signature = {primary, secondary};
    return true;
}

struct PlayerSceneNormalizationFailure {
    std::int32_t index{-1};
    std::int32_t offset{-1};
    std::uint32_t expected{};
    std::uint32_t actual{};
};

template <std::size_t MaxRefs, std::size_t MaxPolys,
          std::size_t MaxVertices, std::size_t MaxDlights>
[[nodiscard]] bool player_scene_normalize_model_tag_rounding(
    const PlayerSceneProducts<MaxRefs, MaxPolys, MaxVertices, MaxDlights>& expected,
    PlayerSceneProducts<MaxRefs, MaxPolys, MaxVertices, MaxDlights>& actual,
    const std::span<const std::int32_t> strict_body_models,
    PlayerSceneNormalizationFailure* const failure = nullptr,
    const std::span<const std::int16_t> trusted_tag_refs = {}) noexcept {
    constexpr float maximum_auxiliary_tag_rounding = 0.5F;
    if (failure != nullptr) {
        *failure = {};
    }
    if (expected.ref_count != actual.ref_count || strict_body_models.empty()) {
        if (failure != nullptr) {
            failure->offset = strict_body_models.empty() ? -2 : -3;
            failure->expected = expected.ref_count;
            failure->actual = actual.ref_count;
        }
        return false;
    }
    for (std::size_t index = 0; index < expected.ref_count; ++index) {
        const auto& source = expected.refs[index];
        auto& candidate = actual.refs[index];
        if (player_scene_ref_type(source) !=
            static_cast<std::int32_t>(PlayerSceneRefType::model)) {
            continue;
        }
        if (player_scene_ref_type(candidate) !=
            static_cast<std::int32_t>(PlayerSceneRefType::model)) {
            if (failure != nullptr) {
                failure->index = static_cast<std::int32_t>(index);
                failure->offset = static_cast<std::int32_t>(
                    k_player_scene_ref_type_offset);
                failure->expected = static_cast<std::uint32_t>(
                    PlayerSceneRefType::model);
                failure->actual = static_cast<std::uint32_t>(
                    player_scene_ref_type(candidate));
            }
            return false;
        }
        const std::int32_t source_model = player_scene_ref_hmodel(source);
        if (source_model == 0 || source_model != player_scene_ref_hmodel(candidate)) {
            continue;
        }
        const bool root_body_model = source_model == strict_body_models.front();
        const bool trusted_tag_ref =
            std::ranges::find(trusted_tag_refs,
                              static_cast<std::int16_t>(index)) !=
            trusted_tag_refs.end();
        std::size_t tagged_body_level = 0U;
        for (std::size_t body = 1; body < strict_body_models.size(); ++body) {
            if (source_model == strict_body_models[body]) {
                tagged_body_level = body;
                break;
            }
        }
        if (root_body_model) {
            continue;
        }
        const auto normalize_float = [&](const std::size_t offset,
                                         const float maximum_difference) noexcept {
            float expected_axis{};
            float actual_axis{};
            std::memcpy(&expected_axis, source.data() + offset,
                        sizeof(expected_axis));
            std::memcpy(&actual_axis, candidate.data() + offset,
                        sizeof(actual_axis));
            if (!player_scene_finite(expected_axis) ||
                !player_scene_finite(actual_axis) ||
                (maximum_difference >= 0.0F &&
                 std::fabs(expected_axis - actual_axis) > maximum_difference)) {
                if (failure != nullptr) {
                    failure->index = static_cast<std::int32_t>(index);
                    failure->offset = static_cast<std::int32_t>(offset);
                    std::memcpy(&failure->expected, &expected_axis,
                                sizeof(expected_axis));
                    std::memcpy(&failure->actual, &actual_axis,
                                sizeof(actual_axis));
                }
                return false;
            }
            std::memcpy(candidate.data() + offset, source.data() + offset,
                        sizeof(float));
            return true;
        };
        if (tagged_body_level == 0U) {
            for (std::size_t axis = 0; axis < 9U; ++axis) {
                // Muzzle-flash roll is selected from cosmetic rand() on every
                // stock call, so only finiteness is meaningful for auxiliary axes.
                if (!normalize_float(k_player_scene_axis_offset +
                                         axis * sizeof(float),
                                     -1.0F)) {
                    return false;
                }
            }
        }
        const float maximum_origin_difference =
            trusted_tag_ref || tagged_body_level != 0U
                ? -1.0F
                : maximum_auxiliary_tag_rounding;
        for (std::size_t axis = 0; axis < 3U; ++axis) {
            if (!normalize_float(k_player_scene_origin_offset +
                                     axis * sizeof(float),
                                 maximum_origin_difference)) {
                return false;
            }
        }
    }
    return true;
}

template <std::size_t MaxRefs, std::size_t MaxPolys,
          std::size_t MaxVertices, std::size_t MaxDlights>
[[nodiscard]] bool player_scene_normalize_dlight_cosmetics(
    const PlayerSceneProducts<MaxRefs, MaxPolys, MaxVertices, MaxDlights>& expected,
    PlayerSceneProducts<MaxRefs, MaxPolys, MaxVertices, MaxDlights>& actual,
    PlayerSceneNormalizationFailure* const failure = nullptr,
    const std::span<const std::int16_t> trusted_dlight_indices = {}) noexcept {
    if (failure != nullptr) {
        *failure = {};
    }
    if (expected.dlight_count != actual.dlight_count) {
        if (failure != nullptr) {
            failure->offset = -3;
            failure->expected = expected.dlight_count;
            failure->actual = actual.dlight_count;
        }
        return false;
    }
    for (std::size_t index = 0; index < expected.dlight_count; ++index) {
        if (!player_scene_finite(expected.dlights[index].radius) ||
            !player_scene_finite(actual.dlights[index].radius)) {
            if (failure != nullptr) {
                failure->index = static_cast<std::int32_t>(index);
                failure->offset = 3;
                std::memcpy(&failure->expected,
                            &expected.dlights[index].radius, sizeof(float));
                std::memcpy(&failure->actual,
                            &actual.dlights[index].radius, sizeof(float));
            }
            return false;
        }
        // CG_AddPlayerWeapon chooses 300 + (rand() & 31) on every call.
        // Keep the authoritative sample inside the same integer millisecond
        // instead of multiplying cosmetic RNG by the preview render rate.
        actual.dlights[index].radius = expected.dlights[index].radius;
        const bool trusted_dlight =
            std::ranges::find(trusted_dlight_indices,
                              static_cast<std::int16_t>(index)) !=
            trusted_dlight_indices.end();
        for (std::size_t axis = 0; axis < 3U; ++axis) {
            const float source = expected.dlights[index].origin[axis];
            float& candidate = actual.dlights[index].origin[axis];
            if (!player_scene_finite(source) || !player_scene_finite(candidate) ||
                (!trusted_dlight && std::fabs(source - candidate) > 0.1F)) {
                if (failure != nullptr) {
                    failure->index = static_cast<std::int32_t>(index);
                    failure->offset = static_cast<std::int32_t>(axis);
                    std::memcpy(&failure->expected, &source, sizeof(source));
                    std::memcpy(&failure->actual, &candidate,
                                sizeof(candidate));
                }
                return false;
            }
            // The light is attached to the same weapon tag as muzzle flashes.
            candidate = source;
        }
    }
    return true;
}

template <std::size_t MaxRefs, std::size_t MaxPolys,
          std::size_t MaxVertices, std::size_t MaxDlights>
[[nodiscard]] bool player_scene_normalize_model_shadow_planes(
    const PlayerSceneProducts<MaxRefs, MaxPolys, MaxVertices, MaxDlights>& expected,
    PlayerSceneProducts<MaxRefs, MaxPolys, MaxVertices, MaxDlights>& actual) noexcept {
    constexpr float maximum_trace_transition = 128.0F;
    if (expected.ref_count != actual.ref_count) {
        return false;
    }
    const auto normalize_height = [](const float source, float& candidate) noexcept {
        if (!player_scene_finite(source) || !player_scene_finite(candidate) ||
            std::fabs(source - candidate) > maximum_trace_transition) {
            return false;
        }
        candidate = source;
        return true;
    };
    for (std::size_t index = 0; index < expected.ref_count; ++index) {
        const auto& source = expected.refs[index];
        auto& candidate = actual.refs[index];
        const std::int32_t source_type = player_scene_ref_type(source);
        if (source_type != player_scene_ref_type(candidate)) {
            return false;
        }
        if (source_type != static_cast<std::int32_t>(PlayerSceneRefType::model)) {
            continue;
        }
        float source_plane{};
        float candidate_plane{};
        std::memcpy(&source_plane,
                    source.data() + k_player_scene_shadow_plane_offset,
                    sizeof(source_plane));
        std::memcpy(&candidate_plane,
                    candidate.data() + k_player_scene_shadow_plane_offset,
                    sizeof(candidate_plane));
        if (!normalize_height(source_plane, candidate_plane)) {
            return false;
        }
        std::memcpy(candidate.data() + k_player_scene_shadow_plane_offset,
                    &candidate_plane, sizeof(candidate_plane));
    }
    return true;
}

template <std::size_t MaxRefs, std::size_t MaxPolys,
          std::size_t MaxVertices, std::size_t MaxDlights>
[[nodiscard]] bool player_scene_normalize_temporal_world_surface_height(
    const PlayerSceneProducts<MaxRefs, MaxPolys, MaxVertices, MaxDlights>& expected,
    PlayerSceneProducts<MaxRefs, MaxPolys, MaxVertices, MaxDlights>& actual) noexcept {
    if (expected.vertex_count != actual.vertex_count ||
        !player_scene_normalize_model_shadow_planes(expected, actual)) {
        return false;
    }
    constexpr float maximum_trace_transition = 128.0F;
    const auto normalize_height = [](const float source, float& candidate) noexcept {
        if (!player_scene_finite(source) || !player_scene_finite(candidate) ||
            std::fabs(source - candidate) > maximum_trace_transition) {
            return false;
        }
        candidate = source;
        return true;
    };
    for (std::size_t index = 0; index < expected.vertex_count; ++index) {
        if (!normalize_height(expected.vertices[index].xyz[2],
                              actual.vertices[index].xyz[2])) {
            return false;
        }
        // Shadow-mark alpha is derived from the same 128-unit floor trace as
        // the world Z. Keep both from one canonical sample during the sub-2-ms
        // replay window; model shaderRGBA is compared separately and exactly.
        std::memcpy(actual.vertices[index].modulate,
                    expected.vertices[index].modulate,
                    sizeof(actual.vertices[index].modulate));
    }
    return true;
}

template <std::size_t MaxRefs, std::size_t MaxPolys,
          std::size_t MaxVertices, std::size_t MaxDlights>
void player_scene_discard_world_surfaces(
    PlayerSceneProducts<MaxRefs, MaxPolys, MaxVertices, MaxDlights>& products) noexcept {
    products.poly_count = 0U;
    products.vertex_count = 0U;
}

[[nodiscard]] inline bool player_scene_float_near(
    const float left, const float right, const float tolerance) noexcept {
    if (!player_scene_finite(left) || !player_scene_finite(right) ||
        !player_scene_finite(tolerance) || tolerance < 0.0F) {
        return false;
    }
    const float magnitude =
        std::fmax(1.0F, std::fmax(std::fabs(left), std::fabs(right)));
    const float ulp =
        std::nextafter(magnitude, std::numeric_limits<float>::infinity()) -
        magnitude;
    return std::fabs(left - right) <= tolerance + 2.0F * ulp;
}

template <std::size_t MaxRefs, std::size_t MaxPolys,
          std::size_t MaxVertices, std::size_t MaxDlights>
[[nodiscard]] bool player_scene_world_surfaces_near(
    const PlayerSceneProducts<MaxRefs, MaxPolys, MaxVertices, MaxDlights>& expected,
    const PlayerSceneProducts<MaxRefs, MaxPolys, MaxVertices, MaxDlights>& actual,
    const float tolerance) noexcept {
    if (expected.poly_count != actual.poly_count ||
        expected.vertex_count != actual.vertex_count ||
        expected.poly_count > expected.polys.size() ||
        actual.poly_count > actual.polys.size() ||
        expected.vertex_count > expected.vertices.size() ||
        actual.vertex_count > actual.vertices.size()) {
        return false;
    }
    for (std::size_t index = 0; index < expected.poly_count; ++index) {
        if (expected.polys[index] != actual.polys[index]) {
            return false;
        }
    }
    for (std::size_t index = 0; index < expected.vertex_count; ++index) {
        for (std::size_t axis = 0; axis < 3U; ++axis) {
            if (!player_scene_float_near(expected.vertices[index].xyz[axis],
                                         actual.vertices[index].xyz[axis],
                                         tolerance)) {
                return false;
            }
        }
        for (std::size_t axis = 0; axis < 2U; ++axis) {
            if (!player_scene_float_near(expected.vertices[index].st[axis],
                                         actual.vertices[index].st[axis],
                                         tolerance)) {
                return false;
            }
        }
        for (std::size_t channel = 0; channel < 4U; ++channel) {
            const int delta =
                static_cast<int>(expected.vertices[index].modulate[channel]) -
                static_cast<int>(actual.vertices[index].modulate[channel]);
            if (delta < -1 || delta > 1) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] inline float player_scene_ref_float_tolerance(
    const PlayerSceneRefType type, const std::size_t offset,
    const float tolerance) noexcept {
    // Repeated weapon/barrel-tag composition perturbs the visual muzzle more
    // than the collision trace. Keep the attachment under the same 0.5-unit
    // ceiling as other auxiliary model tags while the traced endpoint retains
    // a 0.1-unit ceiling. Model/body positions are not relaxed.
    const bool attached_beam = type == PlayerSceneRefType::beam ||
                               type == PlayerSceneRefType::lightning;
    const bool beam_start = offset >= k_player_scene_origin_offset &&
                            offset < k_player_scene_origin_offset +
                                         3U * sizeof(float);
    const bool endpoint = offset >= k_player_scene_old_origin_offset &&
                          offset < k_player_scene_old_origin_offset +
                                       3U * sizeof(float);
    if (attached_beam && beam_start) {
        return tolerance * 50.0F;
    }
    if (attached_beam && endpoint) {
        return tolerance * 10.0F;
    }
    const bool orientation_axis = offset >= k_player_scene_axis_offset &&
                                  offset < k_player_scene_axis_offset +
                                               9U * sizeof(float);
    // 0.035 is just over sin(2 degrees); positions retain the base 0.01-unit
    // bound while repeated x87 tag/swing composition gets a two-degree ceiling.
    return orientation_axis ? tolerance * 3.5F : tolerance;
}

[[nodiscard]] inline bool player_scene_ref_near(
    const PlayerSceneRefEntity& expected,
    const PlayerSceneRefEntity& actual, const float tolerance) noexcept {
    if (player_scene_ref_type(expected) != player_scene_ref_type(actual)) {
        return false;
    }
    std::array<bool, k_player_scene_ref_entity_bytes> near_float{};
    std::array<bool, k_player_scene_ref_entity_bytes> ignored{};
    const auto mark_float = [&near_float](const std::size_t offset) noexcept {
        for (std::size_t byte = 0; byte < sizeof(float); ++byte) {
            near_float[offset + byte] = true;
        }
    };
    const auto mark_vec3 = [&near_float](const std::size_t offset) noexcept {
        for (std::size_t byte = 0; byte < 3U * sizeof(float); ++byte) {
            near_float[offset + byte] = true;
        }
    };
    const auto ignore_vec3 = [&ignored](const std::size_t offset) noexcept {
        for (std::size_t byte = 0; byte < 3U * sizeof(float); ++byte) {
            ignored[offset + byte] = true;
        }
    };
    const auto type = static_cast<PlayerSceneRefType>(player_scene_ref_type(expected));
    mark_vec3(k_player_scene_lighting_origin_offset);
    mark_float(k_player_scene_shadow_plane_offset);
    for (std::size_t axis = 0; axis < 9U; ++axis) {
        mark_float(k_player_scene_axis_offset + axis * sizeof(float));
    }
    mark_vec3(k_player_scene_origin_offset);
    mark_vec3(k_player_scene_old_origin_offset);
    mark_float(k_player_scene_backlerp_offset);
    mark_float(k_player_scene_shader_texcoord_offset);
    mark_float(k_player_scene_shader_texcoord_offset + sizeof(float));
    mark_float(k_player_scene_shader_time_offset);
    mark_float(k_player_scene_radius_offset);
    mark_float(k_player_scene_rotation_offset);
    switch (type) {
    case PlayerSceneRefType::model:
        ignore_vec3(k_player_scene_old_origin_offset);
        break;
    case PlayerSceneRefType::sprite:
    case PlayerSceneRefType::beam:
    case PlayerSceneRefType::lightning:
    case PlayerSceneRefType::rail_core:
    case PlayerSceneRefType::rail_rings:
        break;
    case PlayerSceneRefType::poly:
    case PlayerSceneRefType::portal_surface:
    default:
        return false;
    }
    for (std::size_t offset = 0; offset < expected.size(); ++offset) {
        if (!near_float[offset] && !ignored[offset] &&
            expected[offset] != actual[offset]) {
            return false;
        }
    }
    for (std::size_t offset = 0; offset < expected.size(); offset += sizeof(float)) {
        if (!near_float[offset] || ignored[offset]) {
            continue;
        }
        float left{};
        float right{};
        std::memcpy(&left, expected.data() + offset, sizeof(left));
        std::memcpy(&right, actual.data() + offset, sizeof(right));
        const float field_tolerance =
            player_scene_ref_float_tolerance(type, offset, tolerance);
        if (!player_scene_float_near(left, right, field_tolerance)) {
            return false;
        }
    }
    return true;
}

template <std::size_t MaxRefs, std::size_t MaxPolys,
          std::size_t MaxVertices, std::size_t MaxDlights>
[[nodiscard]] bool player_scene_products_near(
    const PlayerSceneProducts<MaxRefs, MaxPolys, MaxVertices, MaxDlights>& expected,
    const PlayerSceneProducts<MaxRefs, MaxPolys, MaxVertices, MaxDlights>& actual,
    const float tolerance) noexcept {
    if (!expected.valid || !actual.valid || expected.ref_count != actual.ref_count ||
        expected.poly_count != actual.poly_count ||
        expected.vertex_count != actual.vertex_count ||
        expected.dlight_count != actual.dlight_count) {
        return false;
    }
    for (std::size_t index = 0; index < expected.ref_count; ++index) {
        if (!player_scene_ref_near(expected.refs[index], actual.refs[index],
                                   tolerance)) {
            return false;
        }
    }
    for (std::size_t index = 0; index < expected.poly_count; ++index) {
        if (expected.polys[index] != actual.polys[index]) {
            return false;
        }
    }
    for (std::size_t index = 0; index < expected.vertex_count; ++index) {
        for (std::size_t axis = 0; axis < 3U; ++axis) {
            if (!player_scene_float_near(expected.vertices[index].xyz[axis],
                                         actual.vertices[index].xyz[axis],
                                         tolerance)) {
                return false;
            }
        }
        for (std::size_t axis = 0; axis < 2U; ++axis) {
            if (!player_scene_float_near(expected.vertices[index].st[axis],
                                         actual.vertices[index].st[axis],
                                         tolerance)) {
                return false;
            }
        }
        for (std::size_t channel = 0; channel < 4U; ++channel) {
            const int delta =
                static_cast<int>(expected.vertices[index].modulate[channel]) -
                static_cast<int>(actual.vertices[index].modulate[channel]);
            if (delta < -1 || delta > 1) {
                return false;
            }
        }
    }
    for (std::size_t index = 0; index < expected.dlight_count; ++index) {
        const auto& left = expected.dlights[index];
        const auto& right = actual.dlights[index];
        if (left.additive != right.additive ||
            !player_scene_float_near(left.radius, right.radius, tolerance)) {
            return false;
        }
        for (std::size_t axis = 0; axis < 3U; ++axis) {
            if (!player_scene_float_near(left.origin[axis], right.origin[axis],
                                         tolerance) ||
                !player_scene_float_near(left.color[axis], right.color[axis],
                                         tolerance)) {
                return false;
            }
        }
    }
    return true;
}

} // namespace ql1k
