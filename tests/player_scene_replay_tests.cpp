#include "player_scene_replay.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace {

using Products = ql1k::PlayerSceneProducts<4U, 2U, 8U, 2U>;

void require(const bool condition) {
    if (!condition) {
        std::abort();
    }
}

ql1k::PlayerScenePose pose(const float x, const float y, const float z,
                            const float yaw = 90.0F) {
    return {{x, y, z}, {0.0F, yaw, 0.0F}};
}

ql1k::PlayerSceneBasis identity_basis() {
    return {{{{{1.0F, 0.0F, 0.0F}},
               {{0.0F, 1.0F, 0.0F}},
               {{0.0F, 0.0F, 1.0F}}}}};
}

ql1k::PlayerSceneBasis yaw_90_basis() {
    return {{{{{0.0F, 1.0F, 0.0F}},
               {{-1.0F, 0.0F, 0.0F}},
               {{0.0F, 0.0F, 1.0F}}}}};
}

void write_vec3(ql1k::PlayerSceneRefEntity& ref, const std::size_t offset,
                const std::array<float, 3>& value) {
    std::memcpy(ref.data() + offset, value.data(), sizeof(value));
}

void write_int(ql1k::PlayerSceneRefEntity& ref, const std::size_t offset,
               const std::int32_t value) {
    std::memcpy(ref.data() + offset, &value, sizeof(value));
}

std::array<float, 3> read_vec3(const ql1k::PlayerSceneRefEntity& ref,
                               const std::size_t offset) {
    std::array<float, 3> value{};
    std::memcpy(value.data(), ref.data() + offset, sizeof(value));
    return value;
}

ql1k::PlayerSceneRefEntity ref(const ql1k::PlayerSceneRefType type) {
    ql1k::PlayerSceneRefEntity result{};
    const auto raw = static_cast<std::int32_t>(type);
    std::memcpy(result.data(), &raw, sizeof(raw));
    write_vec3(result, ql1k::k_player_scene_lighting_origin_offset,
               {9.0F, 19.0F, 29.0F});
    write_vec3(result, ql1k::k_player_scene_origin_offset,
               {10.0F, 20.0F, 30.0F});
    write_vec3(result, ql1k::k_player_scene_old_origin_offset,
               {100.0F, 200.0F, 300.0F});
    result[ql1k::k_player_scene_rgba_offset] = std::byte{0x11};
    return result;
}

ql1k::PlayerScenePolyVert vert(const float x, const float y, const float z) {
    ql1k::PlayerScenePolyVert result{};
    result.xyz[0] = x;
    result.xyz[1] = y;
    result.xyz[2] = z;
    result.st[0] = 0.25F;
    result.st[1] = 0.75F;
    result.modulate[0] = 4U;
    result.modulate[3] = 255U;
    return result;
}

void model_translation_preserves_style_and_axes() {
    const auto source_pose = pose(10.0F, 20.0F, 30.0F);
    const auto current_pose = pose(10.5F, 19.0F, 32.0F);
    auto source = ref(ql1k::PlayerSceneRefType::model);
    std::array<float, 9> axes{1.0F, 0.0F, 0.0F, 0.0F, 1.0F,
                              0.0F, 0.0F, 0.0F, 1.0F};
    std::memcpy(source.data() + ql1k::k_player_scene_axis_offset, axes.data(),
                sizeof(axes));
    ql1k::PlayerSceneRefEntity translated{};
    require(ql1k::translate_player_scene_ref_entity(
        source, source_pose, current_pose, translated));
    require(read_vec3(translated, ql1k::k_player_scene_lighting_origin_offset) ==
            std::array<float, 3>{9.5F, 18.0F, 31.0F});
    require(read_vec3(translated, ql1k::k_player_scene_origin_offset) ==
            std::array<float, 3>{10.5F, 19.0F, 32.0F});
    require(read_vec3(translated, ql1k::k_player_scene_old_origin_offset) ==
            std::array<float, 3>{100.0F, 200.0F, 300.0F});
    require(std::memcmp(translated.data() + ql1k::k_player_scene_axis_offset,
                        axes.data(), sizeof(axes)) == 0);
    require(translated[ql1k::k_player_scene_rgba_offset] == std::byte{0x11});
}

void body_axis_bits_extract_exact_selected_model_payload() {
    auto model = ref(ql1k::PlayerSceneRefType::model);
    const std::array<float, 9> axes{1.0F, 2.0F, 3.0F, 4.0F, 5.0F,
                                   6.0F, 7.0F, 8.0F, 9.0F};
    std::memcpy(model.data() + ql1k::k_player_scene_axis_offset,
                axes.data(), sizeof(axes));
    std::array<std::uint32_t, 9> bits{};
    require(ql1k::player_scene_ref_axis_bits(model, bits));
    require(std::memcmp(bits.data(), axes.data(), sizeof(bits)) == 0);
}

void beam_start_moves_while_endpoint_respects_trace_space() {
    const auto source_pose = pose(10.0F, 20.0F, 30.0F);
    const auto current_pose = pose(11.0F, 22.0F, 33.0F);
    const auto source = ref(ql1k::PlayerSceneRefType::lightning);
    ql1k::PlayerSceneRefEntity translated{};
    require(ql1k::translate_player_scene_ref_entity(
        source, source_pose, current_pose, translated));
    require(read_vec3(translated, ql1k::k_player_scene_origin_offset) ==
            std::array<float, 3>{11.0F, 22.0F, 33.0F});
    require(read_vec3(translated, ql1k::k_player_scene_old_origin_offset) ==
            std::array<float, 3>{100.0F, 200.0F, 300.0F});

    auto free_space = ref(ql1k::PlayerSceneRefType::lightning);
    write_vec3(free_space, ql1k::k_player_scene_old_origin_offset,
               {778.0F, 20.0F, 30.0F});
    require(ql1k::translate_player_scene_ref_entity(
        free_space, source_pose, current_pose, translated));
    require(read_vec3(translated, ql1k::k_player_scene_origin_offset) ==
            std::array<float, 3>{11.0F, 22.0F, 33.0F});
    require(read_vec3(translated, ql1k::k_player_scene_old_origin_offset) ==
            std::array<float, 3>{779.0F, 22.0F, 33.0F});
}

void rails_are_world_space_and_unknown_types_fail_closed() {
    const auto source_pose = pose(0.0F, 0.0F, 0.0F);
    const auto current_pose = pose(1.0F, 2.0F, 3.0F);
    const auto rail = ref(ql1k::PlayerSceneRefType::rail_core);
    ql1k::PlayerSceneRefEntity translated{};
    require(ql1k::translate_player_scene_ref_entity(
        rail, source_pose, current_pose, translated));
    require(translated == rail);
    const auto portal = ref(ql1k::PlayerSceneRefType::portal_surface);
    require(!ql1k::translate_player_scene_ref_entity(
        portal, source_pose, current_pose, translated));
}

void angle_changes_force_stock() {
    const auto source_pose = pose(0.0F, 0.0F, 0.0F, 90.0F);
    const auto current_pose = pose(1.0F, 0.0F, 0.0F, 90.01F);
    ql1k::PlayerSceneRefEntity translated{};
    require(!ql1k::translate_player_scene_ref_entity(
        ref(ql1k::PlayerSceneRefType::model), source_pose, current_pose,
        translated));
}

void angular_transform_rotates_attached_products_around_player() {
    const ql1k::PlayerScenePose captured{{0.0F, 0.0F, 0.0F},
                                          {0.0F, 0.0F, 0.0F}};
    const ql1k::PlayerScenePose current{{100.0F, 200.0F, 300.0F},
                                         {0.0F, 90.0F, 0.0F}};
    auto model = ref(ql1k::PlayerSceneRefType::model);
    write_vec3(model, ql1k::k_player_scene_lighting_origin_offset,
               {0.0F, 1.0F, 0.0F});
    write_vec3(model, ql1k::k_player_scene_origin_offset,
               {1.0F, 0.0F, 0.0F});
    const std::array<float, 9> identity_axes{
        1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F};
    std::memcpy(model.data() + ql1k::k_player_scene_axis_offset,
                identity_axes.data(), sizeof(identity_axes));

    ql1k::PlayerSceneRefEntity transformed{};
    require(ql1k::transform_player_scene_ref_entity(
        model, captured, current, identity_basis(), yaw_90_basis(),
        transformed));
    require(read_vec3(transformed, ql1k::k_player_scene_origin_offset) ==
            std::array<float, 3>{100.0F, 201.0F, 300.0F});
    require(read_vec3(transformed,
                      ql1k::k_player_scene_lighting_origin_offset) ==
            std::array<float, 3>{99.0F, 200.0F, 300.0F});
    const std::array<float, 9> yaw_axes{
        0.0F, 1.0F, 0.0F, -1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F};
    require(std::memcmp(transformed.data() + ql1k::k_player_scene_axis_offset,
                        yaw_axes.data(), sizeof(yaw_axes)) == 0);

    const ql1k::PlayerSceneDlight light{{2.0F, 0.0F, 0.0F},
                                         80.0F,
                                         {0.1F, 0.8F, 1.0F}, 1};
    ql1k::PlayerSceneDlight transformed_light{};
    require(ql1k::transform_player_scene_dlight(
        light, captured, current, identity_basis(), yaw_90_basis(),
        transformed_light));
    require(transformed_light.origin ==
            std::array<float, 3>{102.0F, 200.0F, 300.0F});
    require(transformed_light.color == light.color);

    auto clipped_beam = ref(ql1k::PlayerSceneRefType::lightning);
    write_vec3(clipped_beam, ql1k::k_player_scene_origin_offset,
               {1.0F, 0.0F, 0.0F});
    write_vec3(clipped_beam, ql1k::k_player_scene_old_origin_offset,
               {20.0F, 0.0F, 0.0F});
    require(ql1k::transform_player_scene_ref_entity(
        clipped_beam, captured, current, identity_basis(), yaw_90_basis(),
        transformed));
    require(read_vec3(transformed, ql1k::k_player_scene_origin_offset) ==
            std::array<float, 3>{100.0F, 201.0F, 300.0F});
    require(read_vec3(transformed, ql1k::k_player_scene_old_origin_offset) ==
            std::array<float, 3>{20.0F, 0.0F, 0.0F});
}

void body_rotation_basis_interpolates_at_render_fraction() {
    ql1k::PlayerSceneBasis interpolated{};
    require(ql1k::player_scene_interpolate_rotation_basis(
        identity_basis(), yaw_90_basis(), 0.5F, interpolated));
    constexpr float diagonal = 0.70710677F;
    require(std::fabs(interpolated.axis[0][0] - diagonal) < 0.00001F);
    require(std::fabs(interpolated.axis[0][1] - diagonal) < 0.00001F);
    require(std::fabs(interpolated.axis[1][0] + diagonal) < 0.00001F);
    require(std::fabs(interpolated.axis[1][1] - diagonal) < 0.00001F);
    require(std::fabs(interpolated.axis[2][2] - 1.0F) < 0.00001F);

    ql1k::PlayerSceneBasis endpoint{};
    require(ql1k::player_scene_interpolate_rotation_basis(
        identity_basis(), yaw_90_basis(), 1.0F, endpoint));
    require(endpoint.axis == yaw_90_basis().axis);
}

void polys_and_dlights_translate_without_visual_mutation() {
    const auto source_pose = pose(4.0F, 5.0F, 6.0F);
    const auto current_pose = pose(5.0F, 3.0F, 9.0F);
    const std::array source_vertices{vert(3.0F, 4.0F, 0.0F),
                                     vert(5.0F, 6.0F, 0.0F)};
    std::array<ql1k::PlayerScenePolyVert, 2> translated_vertices{};
    require(ql1k::translate_player_scene_vertices(
        source_vertices, source_pose, current_pose, translated_vertices));
    require(translated_vertices[0].xyz[0] == 4.0F);
    require(translated_vertices[0].xyz[1] == 2.0F);
    require(translated_vertices[0].xyz[2] == 0.0F);
    require(translated_vertices[0].st[0] == source_vertices[0].st[0]);
    require(translated_vertices[0].modulate[3] == 255U);

    const ql1k::PlayerSceneDlight source_light{{4.0F, 5.0F, 7.0F},
                                               80.0F,
                                               {0.1F, 0.8F, 1.0F}, 1};
    ql1k::PlayerSceneDlight translated_light{};
    require(ql1k::translate_player_scene_dlight(
        source_light, source_pose, current_pose, translated_light));
    require(translated_light.origin ==
            std::array<float, 3>{5.0F, 3.0F, 10.0F});
    require(translated_light.color == source_light.color);
    require(translated_light.radius == source_light.radius);
    require(translated_light.additive == 1);
}

void fixed_products_reject_overflow_empty_and_unsafe_types() {
    Products products;
    products.begin(pose(0.0F, 0.0F, 0.0F));
    require(!products.finish());
    products.begin(pose(0.0F, 0.0F, 0.0F));
    require(!products.append_ref(ref(ql1k::PlayerSceneRefType::poly)));
    require(!products.finish());
    products.begin(pose(0.0F, 0.0F, 0.0F));
    for (std::size_t index = 0; index < 4U; ++index) {
        require(products.append_ref(ref(ql1k::PlayerSceneRefType::model)));
    }
    require(!products.append_ref(ref(ql1k::PlayerSceneRefType::model)));
    require(!products.finish());
}

void product_shape_signature_tracks_semantics_not_pose() {
    Products source;
    source.begin(pose(0.0F, 0.0F, 0.0F));
    auto model = ref(ql1k::PlayerSceneRefType::model);
    write_int(model, ql1k::k_player_scene_hmodel_offset, 7);
    write_int(model, ql1k::k_player_scene_custom_shader_offset, 9);
    require(source.append_ref(model));
    require(source.finish());
    ql1k::PlayerSceneShapeSignature baseline{};
    require(ql1k::player_scene_shape_signature(source, baseline));

    Products pose_only = source;
    write_vec3(pose_only.refs[0], ql1k::k_player_scene_origin_offset,
               {100.0F, 200.0F, 300.0F});
    write_int(pose_only.refs[0], 0x50U, 1234);
    ql1k::PlayerSceneShapeSignature pose_only_signature{};
    require(ql1k::player_scene_shape_signature(pose_only,
                                                pose_only_signature));
    require(pose_only_signature == baseline);

    Products different_model = source;
    write_int(different_model.refs[0], ql1k::k_player_scene_hmodel_offset, 8);
    ql1k::PlayerSceneShapeSignature different_model_signature{};
    require(ql1k::player_scene_shape_signature(
        different_model, different_model_signature));
    require(!(different_model_signature == baseline));

    Products with_light = source;
    require(with_light.append_dlight(
        {{{1.0F, 2.0F, 3.0F}}, 300.0F, {{1.0F, 0.0F, 0.0F}}, 0}));
    ql1k::PlayerSceneShapeSignature light_signature{};
    require(with_light.finish());
    require(ql1k::player_scene_shape_signature(with_light, light_signature));
    require(!(light_signature == baseline));
}

void comparison_allows_only_bounded_translated_float_error() {
    require(ql1k::player_scene_float_near(570.5734F, 570.5834F, 0.01F));
    require(!ql1k::player_scene_float_near(570.5734F, 570.5844F, 0.01F));
    Products expected;
    expected.begin(pose(1.0F, 2.0F, 3.0F));
    require(expected.append_ref(ref(ql1k::PlayerSceneRefType::model)));
    const std::array vertices{vert(1.0F, 2.0F, 3.0F)};
    require(expected.append_poly(7, vertices));
    require(expected.finish());
    Products actual = expected;
    float origin_x{};
    std::memcpy(&origin_x,
                actual.refs[0].data() + ql1k::k_player_scene_origin_offset,
                sizeof(origin_x));
    origin_x += 0.0005F;
    std::memcpy(actual.refs[0].data() + ql1k::k_player_scene_origin_offset,
                &origin_x, sizeof(origin_x));
    actual.vertices[0].xyz[2] += 0.0005F;
    actual.vertices[0].st[0] += 0.0005F;
    require(ql1k::player_scene_products_near(expected, actual, 0.001F));
    float axis{};
    std::memcpy(&axis,
                actual.refs[0].data() + ql1k::k_player_scene_axis_offset,
                sizeof(axis));
    axis += 0.0005F;
    std::memcpy(actual.refs[0].data() + ql1k::k_player_scene_axis_offset,
                &axis, sizeof(axis));
    require(ql1k::player_scene_products_near(expected, actual, 0.001F));
    axis += 0.0025F;
    std::memcpy(actual.refs[0].data() + ql1k::k_player_scene_axis_offset,
                &axis, sizeof(axis));
    require(ql1k::player_scene_products_near(expected, actual, 0.001F));
    axis += 0.001F;
    std::memcpy(actual.refs[0].data() + ql1k::k_player_scene_axis_offset,
                &axis, sizeof(axis));
    require(!ql1k::player_scene_products_near(expected, actual, 0.001F));
    std::memcpy(actual.refs[0].data() + ql1k::k_player_scene_axis_offset,
                expected.refs[0].data() + ql1k::k_player_scene_axis_offset,
                sizeof(axis));
    write_vec3(actual.refs[0], ql1k::k_player_scene_old_origin_offset,
               {-999.0F, 777.0F, 123.0F});
    require(ql1k::player_scene_products_near(expected, actual, 0.001F));
    actual.vertices[0].st[0] += 0.01F;
    require(!ql1k::player_scene_products_near(expected, actual, 0.001F));
    actual.vertices[0].st[0] = expected.vertices[0].st[0];
    actual.vertices[0].modulate[3] ^= 0x01U;
    require(ql1k::player_scene_products_near(expected, actual, 0.001F));
    actual.vertices[0].modulate[3] =
        static_cast<std::uint8_t>(expected.vertices[0].modulate[3] - 2U);
    require(!ql1k::player_scene_products_near(expected, actual, 0.001F));
    actual.vertices[0].modulate[3] = expected.vertices[0].modulate[3];
    actual.refs[0][ql1k::k_player_scene_rgba_offset] ^= std::byte{0x01};
    require(!ql1k::player_scene_products_near(expected, actual, 0.001F));
}

void beam_endpoint_has_a_narrow_field_specific_tolerance() {
    auto expected = ref(ql1k::PlayerSceneRefType::lightning);
    auto actual = expected;
    auto endpoint = read_vec3(actual, ql1k::k_player_scene_old_origin_offset);
    endpoint[0] += 0.05F;
    write_vec3(actual, ql1k::k_player_scene_old_origin_offset, endpoint);
    require(ql1k::player_scene_ref_near(expected, actual, 0.01F));

    endpoint[0] += 0.15F;
    write_vec3(actual, ql1k::k_player_scene_old_origin_offset, endpoint);
    require(!ql1k::player_scene_ref_near(expected, actual, 0.01F));

    actual = expected;
    auto start = read_vec3(actual, ql1k::k_player_scene_origin_offset);
    start[0] += 0.45F;
    write_vec3(actual, ql1k::k_player_scene_origin_offset, start);
    require(ql1k::player_scene_ref_near(expected, actual, 0.01F));
    start[0] += 0.10F;
    write_vec3(actual, ql1k::k_player_scene_origin_offset, start);
    require(!ql1k::player_scene_ref_near(expected, actual, 0.01F));

    auto model_expected = ref(ql1k::PlayerSceneRefType::model);
    auto model_actual = model_expected;
    start = read_vec3(model_actual, ql1k::k_player_scene_origin_offset);
    start[0] += 0.05F;
    write_vec3(model_actual, ql1k::k_player_scene_origin_offset, start);
    require(!ql1k::player_scene_ref_near(model_expected, model_actual, 0.01F));

    auto rail_expected = ref(ql1k::PlayerSceneRefType::rail_core);
    auto rail_actual = rail_expected;
    endpoint = read_vec3(rail_actual, ql1k::k_player_scene_old_origin_offset);
    endpoint[0] += 0.05F;
    write_vec3(rail_actual, ql1k::k_player_scene_old_origin_offset, endpoint);
    require(!ql1k::player_scene_ref_near(rail_expected, rail_actual, 0.01F));
}

void key_is_exact_and_snapshot_sensitive() {
    ql1k::PlayerSceneKey first{};
    first.entity_identity = 2U;
    first.current_snapshot = 0x1000U;
    first.next_snapshot = 0x2000U;
    first.integer_time = 100;
    first.nonpose_state[4] = std::byte{0x55};
    auto second = first;
    require(first == second);
    second.nonpose_state[4] ^= std::byte{0x01};
    require(!(first == second));
    second = first;
    second.nonpose_state.back() ^= std::byte{0x01};
    require(!(first == second));
    second = first;
    ++second.integer_time;
    require(!(first == second));
}

void only_bounded_model_tag_rounding_is_normalized() {
    Products expected;
    expected.begin(pose(0.0F, 0.0F, 0.0F));
    auto body = ref(ql1k::PlayerSceneRefType::model);
    auto torso = ref(ql1k::PlayerSceneRefType::model);
    auto head = ref(ql1k::PlayerSceneRefType::model);
    auto flash = ref(ql1k::PlayerSceneRefType::model);
    write_int(body, ql1k::k_player_scene_hmodel_offset, 10);
    write_int(torso, ql1k::k_player_scene_hmodel_offset, 11);
    write_int(head, ql1k::k_player_scene_hmodel_offset, 12);
    write_int(flash, ql1k::k_player_scene_hmodel_offset, 20);
    require(expected.append_ref(body));
    require(expected.append_ref(torso));
    require(expected.append_ref(head));
    require(expected.append_ref(flash));
    require(expected.finish());
    Products actual = expected;
    const float body_changed = 0.25F;
    const float auxiliary_axis_changed = 0.25F;
    const float auxiliary_origin_changed = 0.025F;
    std::memcpy(actual.refs[0].data() + ql1k::k_player_scene_axis_offset,
                &body_changed, sizeof(body_changed));
    std::memcpy(actual.refs[3].data() + ql1k::k_player_scene_axis_offset,
                &auxiliary_axis_changed, sizeof(auxiliary_axis_changed));
    float auxiliary_origin_x =
        read_vec3(actual.refs[3], ql1k::k_player_scene_origin_offset)[0] +
        auxiliary_origin_changed;
    std::memcpy(actual.refs[3].data() + ql1k::k_player_scene_origin_offset,
                &auxiliary_origin_x, sizeof(auxiliary_origin_x));
    float torso_origin_x =
        read_vec3(actual.refs[1], ql1k::k_player_scene_origin_offset)[0] +
        0.015F;
    std::memcpy(actual.refs[1].data() + ql1k::k_player_scene_origin_offset,
                &torso_origin_x, sizeof(torso_origin_x));
    float head_origin_x =
        read_vec3(actual.refs[2], ql1k::k_player_scene_origin_offset)[0] +
        0.15F;
    std::memcpy(actual.refs[2].data() + ql1k::k_player_scene_origin_offset,
                &head_origin_x, sizeof(head_origin_x));
    const std::array<std::int32_t, 3> body_models{10, 11, 12};
    require(ql1k::player_scene_normalize_model_tag_rounding(
        expected, actual, body_models));
    require(!ql1k::player_scene_products_near(expected, actual, 0.01F));
    std::memcpy(actual.refs[0].data() + ql1k::k_player_scene_axis_offset,
                expected.refs[0].data() + ql1k::k_player_scene_axis_offset,
                sizeof(body_changed));
    require(ql1k::player_scene_products_near(expected, actual, 0.01F));

    actual = expected;
    const float unsafe_tag_drift =
        read_vec3(actual.refs[3], ql1k::k_player_scene_origin_offset)[0] +
        0.501F;
    std::memcpy(actual.refs[3].data() + ql1k::k_player_scene_origin_offset,
                &unsafe_tag_drift, sizeof(unsafe_tag_drift));
    ql1k::PlayerSceneNormalizationFailure failure{};
    require(!ql1k::player_scene_normalize_model_tag_rounding(
        expected, actual, body_models, &failure));
    require(failure.index == 3);
    require(failure.offset ==
            static_cast<std::int32_t>(ql1k::k_player_scene_origin_offset));
    require(failure.expected != failure.actual);

    actual = expected;
    const float recomposed_tag_drift =
        read_vec3(actual.refs[3], ql1k::k_player_scene_origin_offset)[0] +
        0.9F;
    std::memcpy(actual.refs[3].data() + ql1k::k_player_scene_origin_offset,
                &recomposed_tag_drift, sizeof(recomposed_tag_drift));
    const std::array<std::int16_t, 1> trusted_tag_refs{3};
    require(ql1k::player_scene_normalize_model_tag_rounding(
        expected, actual, body_models, &failure, trusted_tag_refs));
    require(ql1k::player_scene_products_near(expected, actual, 0.01F));

    actual = expected;
    const float unsafe_body_tag_drift =
        read_vec3(actual.refs[1], ql1k::k_player_scene_origin_offset)[0] +
        0.101F;
    std::memcpy(actual.refs[1].data() + ql1k::k_player_scene_origin_offset,
                &unsafe_body_tag_drift, sizeof(unsafe_body_tag_drift));
    require(ql1k::player_scene_normalize_model_tag_rounding(
        expected, actual, body_models));
    require(ql1k::player_scene_products_near(expected, actual, 0.01F));

    actual = expected;
    const float unsafe_head_tag_drift =
        read_vec3(actual.refs[2], ql1k::k_player_scene_origin_offset)[0] +
        0.201F;
    std::memcpy(actual.refs[2].data() + ql1k::k_player_scene_origin_offset,
                &unsafe_head_tag_drift, sizeof(unsafe_head_tag_drift));
    require(ql1k::player_scene_normalize_model_tag_rounding(
        expected, actual, body_models));
    require(ql1k::player_scene_products_near(expected, actual, 0.01F));

    actual = expected;
    const float nonfinite_tag = std::numeric_limits<float>::quiet_NaN();
    std::memcpy(actual.refs[1].data() + ql1k::k_player_scene_origin_offset,
                &nonfinite_tag, sizeof(nonfinite_tag));
    require(!ql1k::player_scene_normalize_model_tag_rounding(
        expected, actual, body_models));
}

void only_bounded_cosmetic_dlight_variance_is_normalized() {
    Products expected;
    expected.begin(pose(0.0F, 0.0F, 0.0F));
    require(expected.append_ref(ref(ql1k::PlayerSceneRefType::model)));
    require(expected.append_dlight(
        {{{1.0F, 2.0F, 3.0F}}, 300.0F, {{0.2F, 0.8F, 1.0F}}, 0}));
    require(expected.finish());
    Products actual = expected;
    actual.dlights[0].radius = 331.0F;
    actual.dlights[0].origin[1] += 0.05F;
    require(!ql1k::player_scene_products_near(expected, actual, 0.01F));
    require(ql1k::player_scene_normalize_dlight_cosmetics(expected, actual));
    require(ql1k::player_scene_products_near(expected, actual, 0.01F));
    actual = expected;
    actual.dlights[0].origin[0] += 0.101F;
    ql1k::PlayerSceneNormalizationFailure failure{};
    require(!ql1k::player_scene_normalize_dlight_cosmetics(
        expected, actual, &failure));
    require(failure.index == 0 && failure.offset == 0);
    const std::array<std::int16_t, 1> trusted_dlights{0};
    require(ql1k::player_scene_normalize_dlight_cosmetics(
        expected, actual, &failure, trusted_dlights));
    require(ql1k::player_scene_products_near(expected, actual, 0.01F));
}

void only_bounded_world_surface_height_is_temporally_normalized() {
    Products expected;
    expected.begin(pose(0.0F, 0.0F, 0.0F));
    auto body = ref(ql1k::PlayerSceneRefType::model);
    const float expected_plane = -142.875F;
    std::memcpy(body.data() + ql1k::k_player_scene_shadow_plane_offset,
                &expected_plane, sizeof(expected_plane));
    require(expected.append_ref(body));
    const std::array vertices{vert(1.0F, 2.0F, -143.875F)};
    require(expected.append_poly(7, vertices));
    require(expected.finish());

    Products actual = expected;
    const float stepped_plane = -150.875F;
    std::memcpy(actual.refs[0].data() + ql1k::k_player_scene_shadow_plane_offset,
                &stepped_plane, sizeof(stepped_plane));
    actual.vertices[0].xyz[2] -= 8.0F;
    actual.vertices[0].modulate[0] = 201U;
    require(!ql1k::player_scene_products_near(expected, actual, 0.01F));
    require(ql1k::player_scene_normalize_temporal_world_surface_height(
        expected, actual));
    require(ql1k::player_scene_products_near(expected, actual, 0.01F));

    actual = expected;
    const float unsafe_plane = expected_plane - 128.01F;
    std::memcpy(actual.refs[0].data() + ql1k::k_player_scene_shadow_plane_offset,
                &unsafe_plane, sizeof(unsafe_plane));
    require(!ql1k::player_scene_normalize_temporal_world_surface_height(
        expected, actual));

    actual = expected;
    actual.vertices[0].xyz[0] += 1.0F;
    require(ql1k::player_scene_normalize_temporal_world_surface_height(
        expected, actual));
    require(!ql1k::player_scene_products_near(expected, actual, 0.01F));
}

void native_world_surfaces_are_excluded_without_relaxing_model_products() {
    Products expected;
    expected.begin(pose(0.0F, 0.0F, 0.0F));
    require(expected.append_ref(ref(ql1k::PlayerSceneRefType::model)));
    require(expected.finish());

    Products actual;
    actual.begin(expected.pose);
    auto body = expected.refs[0];
    const float current_shadow_plane = 8.0F;
    std::memcpy(body.data() + ql1k::k_player_scene_shadow_plane_offset,
                &current_shadow_plane, sizeof(current_shadow_plane));
    require(actual.append_ref(body));
    const std::array vertices{vert(1.0F, 2.0F, 7.0F)};
    require(actual.append_poly(7, vertices));
    require(actual.finish());

    require(!ql1k::player_scene_products_near(expected, actual, 0.01F));
    require(ql1k::player_scene_normalize_model_shadow_planes(expected, actual));
    ql1k::player_scene_discard_world_surfaces(actual);
    require(ql1k::player_scene_products_near(expected, actual, 0.01F));

    actual.refs[0][ql1k::k_player_scene_rgba_offset] ^= std::byte{0x01};
    require(!ql1k::player_scene_products_near(expected, actual, 0.01F));
}

void native_world_surface_subview_stays_structurally_strict() {
    Products stock;
    stock.begin(pose(0.0F, 0.0F, 0.0F));
    const std::array vertices{vert(1.0F, 2.0F, 3.0F),
                              vert(4.0F, 5.0F, 6.0F)};
    require(stock.append_poly(7, vertices));
    require(stock.finish());

    Products native = stock;
    native.vertices[0].xyz[0] += 0.005F;
    native.vertices[1].modulate[2] += 1U;
    require(ql1k::player_scene_world_surfaces_near(stock, native, 0.01F));

    native = stock;
    native.polys[0].shader += 1;
    require(!ql1k::player_scene_world_surfaces_near(stock, native, 0.01F));
    native = stock;
    native.vertices[0].xyz[0] += 0.011F;
    require(!ql1k::player_scene_world_surfaces_near(stock, native, 0.01F));
    native = stock;
    native.vertex_count -= 1U;
    require(!ql1k::player_scene_world_surfaces_near(stock, native, 0.01F));

    Products empty_left;
    Products empty_right;
    require(ql1k::player_scene_world_surfaces_near(
        empty_left, empty_right, 0.01F));
}

} // namespace

int main() {
    model_translation_preserves_style_and_axes();
    body_axis_bits_extract_exact_selected_model_payload();
    beam_start_moves_while_endpoint_respects_trace_space();
    rails_are_world_space_and_unknown_types_fail_closed();
    angle_changes_force_stock();
    angular_transform_rotates_attached_products_around_player();
    body_rotation_basis_interpolates_at_render_fraction();
    polys_and_dlights_translate_without_visual_mutation();
    fixed_products_reject_overflow_empty_and_unsafe_types();
    product_shape_signature_tracks_semantics_not_pose();
    comparison_allows_only_bounded_translated_float_error();
    key_is_exact_and_snapshot_sensitive();
    only_bounded_model_tag_rounding_is_normalized();
    only_bounded_cosmetic_dlight_variance_is_normalized();
    only_bounded_world_surface_height_is_temporally_normalized();
    native_world_surfaces_are_excluded_without_relaxing_model_products();
    native_world_surface_subview_stays_structurally_strict();
    beam_endpoint_has_a_narrow_field_specific_tolerance();
    return 0;
}
