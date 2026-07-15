#include <Windows.h>
#include <GL/gl.h>
#include <bcrypt.h>
#include <float.h>
#include <intrin.h>

#include <safetyhook.hpp>

#include "bloom_fast_path.hpp"
#include "color_correct_fast_path.hpp"
#include "font_upload.hpp"
#include "hitreg_state.hpp"
#include "hud_command_replay.hpp"
#include "player_scene_replay.hpp"
#include "player_style_fast_path.hpp"
#include "raster_fingerprint.hpp"
#include "render_interpolation.hpp"
#include "shadow_mark_cache.hpp"
#include "snapshot_entity_cache.hpp"
#include "smp_context_handoff.hpp"
#include "transient_camera.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <new>
#include <string>
#include <string_view>
#include <utility>

extern "C" IMAGE_DOS_HEADER __ImageBase;

#pragma comment(lib, "bcrypt.lib")

namespace {

constexpr char k_engine_hash[] =
    "C926FE9F6C851E00B3B9332E88903AD01F28FDD60454873891C0158F5DED1299";
constexpr char k_cgame_hash[] =
    "310542161AE03CC09A2EDF7C5933A3CB5E2F13D791F59D5A33A62721BBF39953";

constexpr std::uintptr_t k_engine_preferred_base = 0x00400000U;
constexpr std::uintptr_t k_cgame_preferred_base = 0x10000000U;
constexpr std::uintptr_t k_engine_s1 = 0x004CC83EU;
constexpr std::uintptr_t k_engine_s2 = 0x004CB710U;
constexpr std::uintptr_t k_engine_s3 = 0x004BC3E0U;
constexpr std::uintptr_t k_engine_s4 = 0x004B5DE0U;
constexpr std::uintptr_t k_engine_s8 = 0x004B8940U;
constexpr std::uintptr_t k_engine_s9_pre = 0x004AFB69U;
constexpr std::uintptr_t k_engine_s9_post = 0x004AFB7FU;
constexpr std::uintptr_t k_engine_s10 = 0x004E9FF0U;
constexpr std::uintptr_t k_engine_s11 = 0x004AFBF0U;
constexpr std::uintptr_t k_engine_present = 0x004BE3A0U;
constexpr std::uintptr_t k_engine_mouse_transform = 0x004B5800U;
constexpr std::uintptr_t k_engine_packet_write = 0x004B5F70U;
constexpr std::uintptr_t k_engine_endframe = 0x0046B840U;
constexpr std::uintptr_t k_engine_re_endframe = 0x0043CAC0U;
constexpr std::uintptr_t k_engine_smp_default_push = 0x00449C15U;
constexpr std::uintptr_t k_engine_smp_frontend_sleep = 0x0046BBF0U;
constexpr std::uintptr_t k_engine_smp_renderer_sleep = 0x0046BB60U;
constexpr std::uintptr_t k_engine_smp_wake = 0x0046BC20U;
constexpr std::uintptr_t k_engine_smp_spawn_thread = 0x0046BAF0U;
constexpr std::uintptr_t k_engine_smp_renderer_loop = 0x00437BC0U;
constexpr std::uintptr_t k_engine_backend_allocation = 0x0044A1F0U;
constexpr std::uintptr_t k_engine_font_atlas_upload = 0x00442160U;
constexpr std::uintptr_t k_engine_smp_frame_toggle = 0x004507E0U;
constexpr std::uintptr_t k_engine_add_poly_to_scene = 0x004508C0U;
constexpr std::uintptr_t k_engine_add_ref_entity_to_scene = 0x00450CD0U;
constexpr std::uintptr_t k_engine_add_dlight_to_scene = 0x00450D70U;
constexpr std::uintptr_t k_engine_renderer_command_issue = 0x0043C480U;
constexpr std::uintptr_t k_engine_renderer_frontend_sync = 0x0043C540U;
constexpr std::uintptr_t k_engine_renderer_command_buffer = 0x0043C570U;
constexpr std::uintptr_t k_engine_color_correct_backend = 0x00436DC0U;
constexpr std::uintptr_t k_engine_bloom_backend = 0x00436EC0U;
constexpr std::uintptr_t k_engine_postprocess_quad = 0x00436C50U;
constexpr std::uintptr_t k_engine_postprocess_state_reset = 0x00436280U;
constexpr std::uintptr_t k_engine_postprocess_bind_destination = 0x004387D0U;
constexpr std::uintptr_t k_engine_bloom_uniform_refresh = 0x00438590U;
constexpr std::uintptr_t k_engine_renderer_bind_image = 0x00435730U;
constexpr std::uintptr_t k_engine_renderer_begin_registration = 0x0044F550U;
constexpr std::uintptr_t k_engine_renderer_end_registration = 0x00449EF0U;
constexpr std::uintptr_t k_engine_renderer_shutdown = 0x00449E40U;
constexpr std::uintptr_t k_engine_smp_command_buffers_init = 0x0043C400U;
constexpr std::uintptr_t k_engine_smp_command_buffers_shutdown = 0x0043C460U;
constexpr std::uintptr_t k_engine_cmd_number = 0x014725D0U;
constexpr std::uintptr_t k_engine_cmd_ring = 0x01471ED0U;
constexpr std::uintptr_t k_engine_getter_slot = 0x00565AC0U;
constexpr std::uintptr_t k_engine_stock_getter = 0x004B0180U;
constexpr std::uintptr_t k_engine_interface = 0x0146CC38U;
constexpr std::uintptr_t k_engine_ui_interface = 0x0146CC18U;
constexpr std::uintptr_t k_engine_ui_loaded = 0x0146CC1CU;
constexpr std::uintptr_t k_engine_vm_pointer = 0x01647F0CU;
constexpr std::uintptr_t k_engine_s2_absolute = 0x0145B950U;
constexpr std::uintptr_t k_engine_time_absolute = 0x01205E30U;
constexpr std::uintptr_t k_engine_s11_absolute = 0x01528BA4U;
constexpr std::uintptr_t k_engine_present_absolute = 0x0146CC34U;
constexpr std::uintptr_t k_engine_endframe_absolute = 0x01740F74U;
constexpr std::uintptr_t k_engine_smp_active = 0x01743BE4U;
constexpr std::uintptr_t k_engine_smp_wgl_failures = 0x00E411D4U;
constexpr std::uintptr_t k_engine_smp_command_data = 0x00E411D0U;
constexpr std::uintptr_t k_engine_smp_cvar_slot = 0x01740F90U;
constexpr std::uintptr_t k_engine_renderer_registered = 0x01716EA0U;
constexpr std::uintptr_t k_engine_wgl_make_current_slot = 0x016E408CU;
constexpr std::uintptr_t k_engine_wgl_hdc = 0x016E4104U;
constexpr std::uintptr_t k_engine_wgl_context = 0x016E4108U;
constexpr std::uintptr_t k_engine_smp_render_completed_event = 0x016E40E0U;
constexpr std::uintptr_t k_engine_smp_command_event = 0x016E40E8U;
constexpr std::uintptr_t k_engine_smp_renderer_idle_event = 0x016E4134U;
constexpr std::uintptr_t k_engine_smp_thread_handle = 0x016E40DCU;
constexpr std::uintptr_t k_engine_smp_thread_id = 0x016E4130U;
constexpr std::uintptr_t k_engine_smp_frame_index = 0x01716EB8U;
constexpr std::uintptr_t k_engine_backend_data_roots = 0x01745A78U;
constexpr std::uintptr_t k_engine_scene_ref_count = 0x01716DE4U;
constexpr std::uintptr_t k_engine_scene_dlight_count = 0x01716DE8U;
constexpr std::uintptr_t k_engine_scene_polyvert_count = 0x01716DF0U;
constexpr std::uintptr_t k_engine_scene_poly_count = 0x01716E00U;
constexpr std::uintptr_t k_engine_max_polys = 0x01740EB0U;
constexpr std::uintptr_t k_engine_max_polyverts = 0x01740EACU;
constexpr std::uintptr_t k_engine_renderer_default_image = 0x01716ECCU;
constexpr std::uintptr_t k_engine_renderer_width = 0x01743BCCU;
constexpr std::uintptr_t k_engine_renderer_height = 0x01743BD0U;
constexpr std::uintptr_t k_engine_renderer_blend_enabled = 0x01740F40U;
constexpr std::uintptr_t k_engine_bloom_mode = 0x0055C308U;
constexpr std::uintptr_t k_engine_bloom_uniform_update_pending = 0x01740D08U;
constexpr std::uintptr_t k_engine_bloom_intensity_cvar = 0x01743C10U;
constexpr std::uintptr_t k_engine_bloom_saturation_cvar = 0x01740E44U;
constexpr std::uintptr_t k_engine_bloom_scene_saturation_cvar = 0x01740E5CU;
constexpr std::uintptr_t k_engine_bloom_scene_intensity_cvar = 0x01740E78U;
constexpr std::uintptr_t k_engine_color_correct_active_cvar = 0x01740D90U;
constexpr std::uintptr_t k_engine_gamma_cvar = 0x01740DDCU;
constexpr std::uintptr_t k_engine_contrast_cvar = 0x01740E40U;
constexpr std::uintptr_t k_engine_overbright_bits_cvar = 0x01740F00U;
constexpr std::uintptr_t k_engine_enable_color_correct_cvar = 0x01740F04U;
constexpr std::uintptr_t k_engine_gl_active_texture_slot = 0x01740E28U;
constexpr std::uintptr_t k_engine_gl_bind_texture_slot = 0x016E3DFCU;
constexpr std::uintptr_t k_engine_gl_use_program_slot = 0x016E3D14U;
constexpr std::uintptr_t k_engine_client_state = 0x01528BA0U;
constexpr std::uintptr_t k_engine_key_catchers = 0x01528BA4U;
constexpr std::uintptr_t k_engine_create_event_slot = 0x0052C154U;
constexpr std::uintptr_t k_engine_zero_string = 0x0054FFE0U;
constexpr std::uintptr_t k_engine_one_string = 0x00551624U;
constexpr std::uintptr_t k_engine_pending_mouse_x = 0x01471E9CU;
constexpr std::uintptr_t k_engine_pending_mouse_y = 0x01471EA0U;
constexpr std::uintptr_t k_engine_mouse_state = 0x01472754U;
constexpr std::uintptr_t k_engine_m_filter = 0x01647F00U;
constexpr std::uintptr_t k_engine_mouse_accel_debug = 0x015F6724U;
constexpr std::uintptr_t k_engine_mouse_debug_file = 0x0114C130U;
constexpr std::uintptr_t k_cgame_warning_compare = 0x1000AF2EU;
constexpr std::uintptr_t k_cgame_predict_compare = 0x10044884U;
constexpr std::uintptr_t k_cgame_warning_entry = 0x1000AEF0U;
constexpr std::uintptr_t k_cgame_predict_entry = 0x100446E0U;
constexpr std::uintptr_t k_cgame_fps_value = 0x10009C83U;
constexpr std::uintptr_t k_cgame_hitreg_fire = 0x10006083U;
constexpr std::uintptr_t k_cgame_hitreg_feedback = 0x100435CBU;
constexpr std::uintptr_t k_cgame_hitreg_draw = 0x10009D3BU;
constexpr std::uintptr_t k_cgame_angle_vectors = 0x10056E40U;
constexpr std::uintptr_t k_cgame_point_trace = 0x10044040U;
constexpr std::uintptr_t k_cgame_alternate_point_trace = 0x10044100U;
constexpr std::uintptr_t k_cgame_trace_mode = 0x10A5FD9CU;
constexpr std::uintptr_t k_cgame_player_shadow = 0x10041490U;
constexpr std::uintptr_t k_cgame_player_wake = 0x10041630U;
constexpr std::uintptr_t k_cgame_player_angles = 0x1003F350U;
constexpr std::uintptr_t k_cgame_position_on_tag = 0x10015240U;
constexpr std::uintptr_t k_cgame_position_rotated_on_tag = 0x10015340U;
constexpr std::uintptr_t k_cgame_shadow_mode = 0x10A621CCU;
constexpr std::uintptr_t k_cgame_text_measure = 0x100082B0U;
constexpr std::uintptr_t k_cgame_text_paint = 0x10008440U;
constexpr std::uintptr_t k_cgame_client_info_teams = 0x10A41DF8U;
constexpr std::uintptr_t k_cgame_style_gametype = 0x10A3FF14U;
constexpr std::uintptr_t k_cgame_style_player_flag = 0x10A9C214U;
constexpr std::uintptr_t k_cgame_style_player_state_flags = 0x10A9C21CU;
constexpr std::uintptr_t k_cgame_style_local_client = 0x10A9C298U;
constexpr std::uintptr_t k_cgame_style_force_red = 0x10A65594U;
constexpr std::uintptr_t k_cgame_style_force_blue = 0x10B70CD4U;
constexpr std::uintptr_t k_cgame_style_dead_darken = 0x10ABB88CU;
constexpr std::uintptr_t k_cgame_style_dead_color = 0x10A6E58CU;
constexpr std::uintptr_t k_cgame_style_enemy_lower = 0x10A6E22CU;
constexpr std::uintptr_t k_cgame_style_enemy_upper = 0x10A6EFACU;
constexpr std::uintptr_t k_cgame_style_enemy_head = 0x10A6D5CCU;
constexpr std::uintptr_t k_cgame_style_team_lower = 0x10A23A8CU;
constexpr std::uintptr_t k_cgame_style_team_upper = 0x10A377CCU;
constexpr std::uintptr_t k_cgame_style_team_head = 0x10A6318CU;
constexpr std::uintptr_t k_cgame_client_legs_model = 0x10A41FECU;
constexpr std::uintptr_t k_cgame_client_torso_model = 0x10A41FF4U;
constexpr std::uintptr_t k_cgame_client_head_model = 0x10A41FFCU;
constexpr std::uintptr_t k_cgame_serverinfo_flags = 0x10A3FF28U;
constexpr std::uintptr_t k_cgame_replay_return = 0x10044953U;
constexpr std::uintptr_t k_cgame_draw_active_frame = 0x1004E4E0U;
constexpr std::uintptr_t k_cgame_draw_2d = 0x10010D90U;
constexpr std::uintptr_t k_cgame_update_cvars = 0x10020CA0U;
constexpr std::uintptr_t k_cgame_scene_submission_call = 0x1004E8CBU;
constexpr std::uintptr_t k_cgame_shadow_impact_return = 0x100415FFU;
constexpr std::uintptr_t k_cgame_impact_mark = 0x1002A740U;
constexpr std::uintptr_t k_cgame_impact_add_poly_call = 0x1002ABF7U;
constexpr std::uintptr_t k_cgame_player_renderer = 0x10041B80U;
constexpr std::uintptr_t k_cgame_player_style_call = 0x10041C71U;
constexpr std::uintptr_t k_cgame_player_style_stock = 0x100419F0U;
constexpr std::uintptr_t k_cgame_frame_interpolation_seam = 0x10018C2DU;
constexpr std::uintptr_t k_cgame_frame_interpolation = 0x10A9C1DCU;
constexpr std::uintptr_t k_cgame_integer_time = 0x10A9C1ECU;
constexpr std::uintptr_t k_cgame_frame_time = 0x10A9C1E8U;
constexpr std::uintptr_t k_cgame_current_snapshot = 0x10A6F8C4U;
constexpr std::uintptr_t k_cgame_next_snapshot = 0x10A6F8C8U;
constexpr std::uintptr_t k_cgame_frame_interpolation_followup = 0x10A9C7CCU;
constexpr std::uintptr_t k_cgame_predicted_player_centity = 0x10A9C460U;
constexpr std::uintptr_t k_cgame_predicted_viewangles = 0x10A9C2B0U;
constexpr std::uintptr_t k_cgame_refdef_viewangles = 0x10A9C990U;
constexpr std::uintptr_t k_cgame_refdef_vieworg = 0x10A9C838U;
constexpr std::uintptr_t k_cgame_refdef_viewaxis = 0x10A9C844U;
constexpr std::uintptr_t k_cgame_solid_count = 0x100D1748U;
constexpr std::uintptr_t k_cgame_solid_list = 0x100D1750U;
constexpr std::uintptr_t k_cgame_centities = 0x10ABBAC0U;
constexpr std::uintptr_t k_cgame_warning_absolute = 0x10075000U;
constexpr std::uintptr_t k_cgame_cvar_table = 0x10076A00U;
constexpr std::uintptr_t k_cgame_fps_absolute = 0x10ABAF8CU;
constexpr std::size_t k_centity_stride = 0x2D0U;
constexpr std::size_t k_centity_style_client_offset = 0xB0U;
constexpr std::size_t k_centity_position_offset = 0x2B8U;
constexpr std::size_t k_max_player_entities = 64U;
constexpr std::size_t k_max_cgame_entities = 1024U;
constexpr std::size_t k_centity_pose_field_count = 6U;
constexpr std::size_t k_snapshot_player_number_offset = 0xB4U;
constexpr std::size_t k_snapshot_entity_count_offset = 0x27CU;
constexpr std::size_t k_snapshot_entities_offset = 0x280U;
constexpr std::size_t k_snapshot_entity_stride = 0xECU;
constexpr std::size_t k_entity_pose_transaction_capacity = k_max_cgame_entities + 2U;
static_assert(ql1k::k_player_scene_nonpose_state_bytes ==
              k_centity_position_offset);
constexpr LONG64 k_player_style_validation_threshold = 8192;
// One locked demo/renderer epoch supplies roughly 4.4k eligible comparisons.
// Two clean validation processes supplied 8,067 exact comparisons with no
// mismatch; require 4,096 consecutive comparisons in the current epoch before
// bypass can activate.
constexpr LONG k_player_scene_validation_threshold = 4096;
constexpr std::size_t k_player_scene_cache_slots = k_max_player_entities + 1U;
constexpr std::size_t k_player_scene_max_refs = 64U;
constexpr std::size_t k_player_scene_max_polys = 32U;
constexpr std::size_t k_player_scene_max_vertices = 384U;
constexpr std::size_t k_player_scene_max_dlights = 8U;
constexpr std::size_t k_player_scene_shape_proof_slots = 64U;
constexpr std::uint32_t k_player_scene_shape_validation_threshold = 32U;
constexpr std::size_t k_engine_scene_ref_stride = 0xC0U;
constexpr std::size_t k_engine_scene_ref_offset = 0x80580U;
constexpr std::size_t k_engine_scene_dlight_stride = 0x2CU;
constexpr std::size_t k_engine_scene_dlight_offset = 0x80000U;
constexpr std::size_t k_engine_scene_poly_descriptor_stride = 0x14U;
constexpr std::size_t k_engine_scene_poly_descriptor_pointer_offset = 0xB04C0U;
constexpr std::size_t k_engine_scene_polyvert_pointer_offset = 0xB04C4U;
constexpr std::int32_t k_engine_scene_ref_limit = 0x3FE;
constexpr std::int32_t k_engine_scene_dlight_limit = 0x20;

constexpr std::array<std::uint8_t, 5> k_s1_signature{0xE8, 0x4D, 0xF4, 0xFF, 0xFF};
constexpr std::array<std::uint8_t, 8> k_s2_signature{
    0x55, 0x8B, 0xEC, 0xA1, 0x50, 0xB9, 0x45, 0x01};
constexpr std::array<std::uint8_t, 9> k_s3_signature{
    0x55, 0x8B, 0xEC, 0x51, 0xA1, 0x30, 0x5E, 0x20, 0x01};
constexpr std::array<std::uint8_t, 6> k_s4_signature{0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x1C};
constexpr std::array<std::uint8_t, 8> k_s8_signature{
    0x55, 0x8B, 0xEC, 0xA1, 0x30, 0x5E, 0x20, 0x01};
constexpr std::array<std::uint8_t, 5> k_s9_pre_signature{0xE8, 0xC2, 0x3E, 0x00, 0x00};
constexpr std::array<std::uint8_t, 5> k_s9_post_signature{0x83, 0xC4, 0x0C, 0x5E, 0x5B};
constexpr std::array<std::uint8_t, 5> k_player_style_call_signature{
    0xE8, 0x7A, 0xFD, 0xFF, 0xFF};
constexpr std::array<std::uint8_t, 7> k_s10_signature{
    0x55, 0x8B, 0xEC, 0x53, 0x8B, 0x5D, 0x08};
constexpr std::array<std::uint8_t, 7> k_s11_signature{
    0x83, 0x25, 0xA4, 0x8B, 0x52, 0x01, 0xF7};
constexpr std::array<std::uint8_t, 7> k_present_signature{
    0x83, 0x3D, 0x34, 0xCC, 0x46, 0x01, 0x00};
constexpr std::array<std::uint8_t, 6> k_mouse_transform_signature{
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x1C};
constexpr std::array<std::uint8_t, 8> k_packet_write_signature{
    0x55, 0x8B, 0xEC, 0xB8, 0x44, 0x80, 0x00, 0x00};
constexpr std::array<std::uint8_t, 9> k_endframe_signature{
    0xA1, 0x74, 0x0F, 0x74, 0x01, 0x83, 0x78, 0x20, 0x00};
constexpr std::array<std::uint8_t, 5> k_re_endframe_signature{
    0x55, 0x8B, 0xEC, 0x83, 0x3D};
constexpr std::array<std::uint8_t, 5> k_smp_default_push_signature{
    0x68, 0xE0, 0xFF, 0x54, 0x00};
constexpr std::array<std::uint8_t, 9> k_smp_wake_signature{
    0x55, 0x8B, 0xEC, 0x8B, 0x0D, 0x04, 0x41, 0x6E, 0x01};
constexpr std::array<std::uint8_t, 5> k_smp_frontend_sleep_signature{
    0xA1, 0x34, 0x41, 0x6E, 0x01};
constexpr std::array<std::uint8_t, 5> k_smp_renderer_sleep_signature{
    0xA1, 0x04, 0x41, 0x6E, 0x01};
constexpr std::array<std::uint8_t, 10> k_smp_spawn_thread_signature{
    0x55, 0x8B, 0xEC, 0x56, 0x8B, 0x35, 0x54, 0xC1, 0x52, 0x00};
constexpr std::array<std::uint8_t, 5> k_smp_renderer_loop_signature{
    0x55, 0x8B, 0xEC, 0x51, 0x53};
constexpr std::array<std::uint8_t, 6> k_backend_allocation_signature{
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08};
constexpr std::array<std::uint8_t, 10> k_font_atlas_upload_signature{
    0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x08, 0x53, 0x56, 0x8B, 0x75};
constexpr std::array<std::uint8_t, 6> k_smp_frame_toggle_signature{
    0x8B, 0x0D, 0x90, 0x0F, 0x74, 0x01};
constexpr std::array<std::uint8_t, 8> k_renderer_command_issue_signature{
    0x55, 0x8B, 0xEC, 0xA1, 0xB8, 0x6E, 0x71, 0x01};
constexpr std::array<std::uint8_t, 7> k_renderer_frontend_sync_signature{
    0x83, 0x3D, 0xA0, 0x6E, 0x71, 0x01, 0x00};
constexpr std::array<std::uint8_t, 8> k_renderer_begin_registration_signature{
    0x55, 0x8B, 0xEC, 0xE8, 0x98, 0xAC, 0xFF, 0xFF};
constexpr std::array<std::uint8_t, 10> k_renderer_end_registration_signature{
    0xE8, 0x4B, 0x26, 0xFF, 0xFF, 0xE8, 0x06, 0x27, 0x0A, 0x00};
constexpr std::array<std::uint8_t, 14> k_renderer_shutdown_signature{
    0x55, 0x8B, 0xEC, 0x56, 0xE8, 0x37, 0x69,
    0x00, 0x00, 0xE8, 0x52, 0xB1, 0xFE, 0xFF};
constexpr std::array<std::uint8_t, 5> k_smp_command_buffers_init_signature{
    0xA1, 0x90, 0x0F, 0x74, 0x01};
constexpr std::array<std::uint8_t, 7> k_smp_command_buffers_shutdown_signature{
    0x83, 0x3D, 0xE4, 0x3B, 0x74, 0x01, 0x00};
constexpr std::array<std::uint8_t, 5> k_warning_signature{0x3B, 0x4A, 0x2C, 0x0F, 0x8E};
constexpr std::array<std::uint8_t, 5> k_predict_signature{0x3B, 0x41, 0x2C, 0x7E, 0x24};
constexpr std::array<std::uint8_t, 8> k_warning_entry_signature{
    0x83, 0xEC, 0x24, 0xA1, 0x00, 0x50, 0x07, 0x10};
constexpr std::array<std::uint8_t, 6> k_predict_entry_signature{
    0x81, 0xEC, 0xB4, 0x02, 0x00, 0x00};
constexpr std::array<std::uint8_t, 8> k_fps_value_signature{
    0x53, 0x56, 0x8B, 0x35, 0x8C, 0xAF, 0xAB, 0x10};
constexpr std::array<std::uint8_t, 5> k_hitreg_fire_signature{
    0xE8, 0x38, 0xBF, 0xFF, 0xFF};
constexpr std::array<std::uint8_t, 19> k_hitreg_feedback_signature{
    0x8B, 0x86, 0x04, 0x01, 0x00, 0x00, 0x8B, 0x8D, 0x04, 0x01,
    0x00, 0x00, 0x3B, 0xC1, 0x0F, 0x8E, 0xE0, 0x00, 0x00};
constexpr std::array<std::uint8_t, 9> k_hitreg_draw_signature{
    0x83, 0xC4, 0x24, 0x5F, 0x5E, 0x5B, 0xDB, 0x44, 0x24};
constexpr std::array<std::uint8_t, 3> k_text_measure_signature{0x83, 0xEC, 0x20};
constexpr std::array<std::uint8_t, 3> k_text_paint_signature{0x83, 0xEC, 0x24};
constexpr std::array<std::uint8_t, 9> k_angle_vectors_signature{
    0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0x83, 0xEC, 0x08};
constexpr std::array<std::uint8_t, 8> k_draw_active_frame_signature{
    0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xC0, 0x8B, 0x45};
constexpr std::array<std::uint8_t, 15> k_draw_2d_signature{
    0x83, 0xEC, 0x44, 0xA1, 0x00, 0x50, 0x07, 0x10,
    0x33, 0xC4, 0x89, 0x44, 0x24, 0x3C, 0xA1};
constexpr std::array<std::uint8_t, 16> k_update_cvars_signature{
    0x53, 0x56, 0x57, 0xBE, 0x00, 0x6A, 0x07, 0x10,
    0xBF, 0x27, 0x01, 0x00, 0x00, 0x8D, 0x49, 0x00};
constexpr std::array<std::uint8_t, 5> k_scene_submission_call_signature{
    0xE8, 0xC0, 0x2B, 0xFC, 0xFF};
constexpr std::array<std::uint8_t, 5> k_frame_interpolation_seam_signature{
    0x68, 0xCC, 0xC7, 0xA9, 0x10};
constexpr std::array<std::uint8_t, 3> k_shadow_impact_return_signature{0x83, 0xC4, 0x2C};
constexpr std::array<std::uint8_t, 5> k_impact_mark_signature{
    0xB8, 0x88, 0x17, 0x00, 0x00};
constexpr std::array<std::uint8_t, 2> k_impact_add_poly_call_signature{0xFF, 0xD1};
constexpr std::array<std::uint8_t, 6> k_player_renderer_signature{
    0x81, 0xEC, 0x00, 0x03, 0x00, 0x00};
constexpr std::array<std::uint8_t, 8> k_player_shadow_signature{
    0x83, 0xEC, 0x6C, 0xA1, 0x00, 0x50, 0x07, 0x10};
constexpr std::array<std::uint8_t, 11> k_player_wake_signature{
    0x81, 0xEC, 0xC4, 0x00, 0x00, 0x00,
    0xA1, 0x00, 0x50, 0x07, 0x10};
constexpr std::array<std::uint8_t, 8> k_player_angles_signature{
    0x83, 0xEC, 0x64, 0x53, 0x55, 0x8B, 0x6C, 0x24};
constexpr std::array<std::uint8_t, 8> k_position_on_tag_signature{
    0x83, 0xEC, 0x38, 0xA1, 0x00, 0x50, 0x07, 0x10};
constexpr std::array<std::uint8_t, 10> k_position_rotated_on_tag_signature{
    0x83, 0xEC, 0x5C, 0xA1, 0x00, 0x50, 0x07, 0x10, 0x33, 0xC4};
constexpr std::array<std::uint8_t, 8> k_renderer_command_buffer_signature{
    0x55, 0x8B, 0xEC, 0xA1, 0xB8, 0x6E, 0x71, 0x01};
constexpr std::array<std::uint8_t, 7> k_color_correct_backend_signature{
    0x55, 0x8B, 0xEC, 0x57, 0x8B, 0x7D, 0x08};
constexpr std::array<std::uint8_t, 12> k_bloom_backend_signature{
    0x55, 0x8B, 0xEC, 0x51, 0x53, 0x56, 0x57, 0x68, 0xC0, 0x84, 0x00, 0x00};
constexpr std::array<std::uint8_t, 9> k_postprocess_quad_signature{
    0x55, 0x8B, 0xEC, 0x51, 0xD9, 0xE8, 0x83, 0xEC, 0x10};
constexpr std::array<std::uint8_t, 5> k_postprocess_state_reset_signature{
    0xA1, 0xD0, 0x3B, 0x74, 0x01};
constexpr std::array<std::uint8_t, 7> k_postprocess_bind_destination_signature{
    0x6A, 0x00, 0x68, 0x40, 0x8D, 0x00, 0x00};
constexpr std::array<std::uint8_t, 8> k_bloom_uniform_refresh_signature{
    0x55, 0x8B, 0xEC, 0x51, 0xE8, 0x27, 0x82, 0x01};
constexpr std::array<std::uint8_t, 8> k_renderer_bind_image_signature{
    0x55, 0x8B, 0xEC, 0x56, 0x8B, 0x75, 0x08, 0x85};

constexpr std::size_t k_record_size = 28U;
constexpr std::size_t k_shadow_mark_cache_slots = k_max_player_entities + 1U;
constexpr std::size_t k_shadow_mark_cache_max_polys = 32U;
constexpr std::size_t k_shadow_mark_cache_max_vertices = 384U;
constexpr std::size_t k_shadow_mark_cache_max_poly_vertices = 64U;
using ShadowMarkCache =
    ql1k::ShadowMarkCache<k_shadow_mark_cache_slots, k_shadow_mark_cache_max_polys,
                          k_shadow_mark_cache_max_vertices>;
constexpr std::size_t k_history_capacity = 4096U;
constexpr std::size_t k_history_warmup = 64U;
constexpr std::size_t k_usercmd_buttons_offset = 16U;
constexpr std::int32_t k_button_attack = 1;
constexpr std::size_t k_mouse_state_size = 0x11CU;
constexpr std::size_t k_client_info_stride = 0x738U;
constexpr std::size_t k_trace_entity_number_index = 13U;
constexpr std::size_t k_trace_endpoint_index = 3U;
constexpr std::size_t k_max_deferred_font_uploads = 64U;
constexpr std::size_t k_max_font_atlas_dimension = 4096U;
constexpr std::size_t k_max_font_upload_bytes = 16U * 1024U * 1024U;
constexpr GLsizei k_raster_fingerprint_width = 64;
constexpr GLsizei k_raster_fingerprint_height = 64;
constexpr std::size_t k_raster_fingerprint_bytes =
    static_cast<std::size_t>(k_raster_fingerprint_width) *
    static_cast<std::size_t>(k_raster_fingerprint_height) * 3U;
constexpr std::size_t k_raster_fingerprint_capacity = 128U;
constexpr std::size_t k_hud_replay_capacity = 1024U * 1024U;
constexpr std::size_t k_renderer_command_data_offset = 0xB04C8U;
constexpr std::size_t k_renderer_command_used_offset = 0x4B04C8U;
constexpr std::int32_t k_renderer_command_capacity = 0x400000;

constexpr GLenum k_gl_pixel_pack_buffer = 0x88EBU;
constexpr GLenum k_gl_texture0 = 0x84C0U;
constexpr GLenum k_gl_texture1 = 0x84C1U;
constexpr GLenum k_gl_texture_rectangle = 0x84F5U;
constexpr GLenum k_gl_stream_read = 0x88E1U;
constexpr GLenum k_gl_read_only = 0x88B8U;
constexpr GLenum k_gl_framebuffer = 0x8D40U;
constexpr GLenum k_gl_read_framebuffer = 0x8CA8U;
constexpr GLenum k_gl_draw_framebuffer = 0x8CA9U;
constexpr GLenum k_gl_renderbuffer = 0x8D41U;
constexpr GLenum k_gl_color_attachment0 = 0x8CE0U;
constexpr GLenum k_gl_framebuffer_complete = 0x8CD5U;
constexpr GLenum k_gl_rgb8 = 0x8051U;
constexpr GLenum k_gl_sync_gpu_commands_complete = 0x9117U;
constexpr GLenum k_gl_already_signaled = 0x911AU;
constexpr GLenum k_gl_timeout_expired = 0x911BU;
constexpr GLenum k_gl_condition_satisfied = 0x911CU;
constexpr GLenum k_gl_wait_failed = 0x911DU;

enum : LONG {
    k_stock = 0,
    k_waiting_for_modules = 1,
    k_identity_validated = 2,
    k_refused = 3,
    k_runtime_active = 4,
};

struct AuxiliaryRecord {
    std::int32_t command_number{};
    std::int32_t server_time{};
    std::array<std::uint8_t, k_record_size> bytes{};
};

struct HistorySelection {
    std::int32_t oldest_time{};
    std::int32_t q_start{};
    bool coverage{};
    LONG generation{};
};

using DeltaFn = int(__cdecl*)(int);
using ClientFrameFn = void(__cdecl*)(int);
using PublisherFn = void(__cdecl*)();
using GetterFn = int(__cdecl*)(int, void*);
using PresentFn = void(__cdecl*)();
using MouseTransformFn = void(__cdecl*)(void*);
using PacketWriteFn = void(__cdecl*)();
using EndFrameFn = void(__cdecl*)();
using ReEndFrameFn = void(__cdecl*)(int*, int*);
using SmpWakeFn = void(__cdecl*)(void*);
using SmpRendererSleepFn = void*(__cdecl*)();
using SmpFrontEndSleepFn = void(__cdecl*)();
using SmpRendererLoopFn = void(__cdecl*)();
using SmpSpawnThreadFn = int(__cdecl*)(SmpRendererLoopFn);
using BackendAllocationFn = void(__cdecl*)();
using FontAtlasUploadFn = void(__cdecl*)(const int*, const int*, const std::uint8_t*);
using SmpFrameToggleFn = void(__cdecl*)();
using RendererCommandIssueFn = void(__cdecl*)(int);
using RendererFrontEndSyncFn = void(__cdecl*)();
using RendererCommandBufferFn = void*(__cdecl*)(int);
using BloomBackendFn = std::uint8_t*(__cdecl*)(std::uint8_t*);
using PostProcessQuadFn = void(__cdecl*)(int, int, int, int);
using PostProcessVoidFn = void(__cdecl*)();
using RendererBindImageFn = void(__cdecl*)(void*, GLenum);
using GlActiveTextureFn = void(APIENTRY*)(GLenum);
using GlBindTextureFn = void(APIENTRY*)(GLenum, GLuint);
using GlUseProgramFn = void(APIENTRY*)(GLuint);
using RendererBeginRegistrationFn = void(__cdecl*)(void*);
using RendererEndRegistrationFn = void(__cdecl*)();
using RendererShutdownFn = void(__cdecl*)(int);
using SmpCommandBuffersInitFn = void(__cdecl*)();
using SmpCommandBuffersShutdownFn = void(__cdecl*)();
using WglMakeCurrentFn = BOOL(WINAPI*)(HDC, HGLRC);
using DisconnectFn = void(__cdecl*)(int);
using VmLoaderFn = void*(__cdecl*)(const char*, void*, void*, int*);
using CgameEntryFn = void(__cdecl*)();
using ShutdownFn = void(__cdecl*)();
using DrawActiveFrameFn = void(__cdecl*)(int, int, int);
using AddPolyToSceneFn = void(__cdecl*)(int, int, const ql1k::ShadowPolyVert*);
using PlayerRendererFn = void(__cdecl*)(void*);
using EngineAddPolyToSceneFn = void(__cdecl*)(
    int, int, const ql1k::PlayerScenePolyVert*, int);
using EngineAddRefEntityToSceneFn = void(__cdecl*)(const void*);
using EngineAddDlightToSceneFn = void(__cdecl*)(
    const float*, float, float, float, float, int);
using TextPaintFn = void(__cdecl*)(float, float, int, float, const float*, const char*, float,
                                   int, int);
using PointTraceFn = void(__cdecl*)(std::uint32_t*, const float*, const float*, const float*,
                                     const float*, std::int32_t, std::int32_t);
__declspec(noinline) float native_scaled_add(float base, float value,
                                              double scale) noexcept;
bool native_angle_vectors(const float* angles, float* forward) noexcept;
bool native_player_scene_basis(const float* angles,
                               ql1k::PlayerSceneBasis& basis) noexcept;
bool native_player_scene_body_axes(
    void* centity, std::int32_t frame_time,
    std::array<ql1k::PlayerSceneBasis, 3>& body_axes) noexcept;
bool native_position_on_tag(void* destination, const void* parent,
                            std::int32_t parent_model,
                            const char* tag_name) noexcept;
bool native_position_rotated_on_tag(void* destination, const void* parent,
                                    std::int32_t parent_model,
                                    const char* tag_name) noexcept;
bool native_alternate_point_trace(void* function, std::uint32_t* result,
                                  const float* start, const float* end,
                                  std::int32_t skip_entity,
                                  std::int32_t mask) noexcept;
bool native_player_shadow(void* function, const void* centity,
                          float* shadow_plane, std::int32_t* visible) noexcept;
bool native_player_wake(void* function, const void* centity) noexcept;
using UiIsFullscreenFn = int(__cdecl*)();
using GlSync = void*;
using GlGenBuffersFn = void(APIENTRY*)(GLsizei, GLuint*);
using GlDeleteBuffersFn = void(APIENTRY*)(GLsizei, const GLuint*);
using GlBindBufferFn = void(APIENTRY*)(GLenum, GLuint);
using GlBufferDataFn = void(APIENTRY*)(GLenum, std::ptrdiff_t, const void*, GLenum);
using GlMapBufferFn = void*(APIENTRY*)(GLenum, GLenum);
using GlUnmapBufferFn = GLboolean(APIENTRY*)(GLenum);
using GlGenFramebuffersFn = void(APIENTRY*)(GLsizei, GLuint*);
using GlDeleteFramebuffersFn = void(APIENTRY*)(GLsizei, const GLuint*);
using GlBindFramebufferFn = void(APIENTRY*)(GLenum, GLuint);
using GlCheckFramebufferStatusFn = GLenum(APIENTRY*)(GLenum);
using GlGenRenderbuffersFn = void(APIENTRY*)(GLsizei, GLuint*);
using GlDeleteRenderbuffersFn = void(APIENTRY*)(GLsizei, const GLuint*);
using GlBindRenderbufferFn = void(APIENTRY*)(GLenum, GLuint);
using GlRenderbufferStorageFn = void(APIENTRY*)(GLenum, GLenum, GLsizei, GLsizei);
using GlFramebufferRenderbufferFn = void(APIENTRY*)(GLenum, GLenum, GLenum, GLuint);
using GlBlitFramebufferFn = void(APIENTRY*)(GLint, GLint, GLint, GLint, GLint, GLint, GLint,
                                            GLint, GLbitfield, GLenum);
using GlFenceSyncFn = GlSync(APIENTRY*)(GLenum, GLbitfield);
using GlClientWaitSyncFn = GLenum(APIENTRY*)(GlSync, GLbitfield, unsigned long long);
using GlDeleteSyncFn = void(APIENTRY*)(GlSync);

struct ReplayAuthorization {
    unsigned char active{};
    std::int32_t next{};
    std::int32_t end{};
    LONG generation{};
    HMODULE cgame{};
};

struct TransientFrameState {
    unsigned char preview_active{};
    unsigned char draw_active{};
    unsigned char overlay_applied{};
    unsigned char interpolation_active{};
    float pitch_delta{};
    float yaw_delta{};
    float saved_viewangles[3]{};
    float saved_frame_interpolation{};
    double fractional_ms{};
    LONG64 revision{};
};

using PlayerSceneProducts = ql1k::PlayerSceneProducts<
    k_player_scene_max_refs, k_player_scene_max_polys,
    k_player_scene_max_vertices, k_player_scene_max_dlights>;

struct PlayerSceneCacheEntry {
    ql1k::PlayerSceneKey key{};
    std::array<std::byte, ql1k::k_player_scene_nonpose_state_bytes>
        render_input_state{};
    PlayerSceneProducts products{};
    ql1k::PlayerSceneShapeSignature shape{};
    struct DlightBinding {
        enum class Kind : std::uint8_t { none, translation, weapon_flash } kind{};
        enum class OriginMode : std::uint8_t { translated, tagged } origin_mode{};
        std::int16_t gun_ref{-1};
        std::int16_t flash_ref{-1};
    };
    std::array<DlightBinding, k_player_scene_max_dlights> dlight_bindings{};
    LONG64 captured_qpc{};
    bool dlight_bindings_valid{};
    bool shape_valid{};
    bool valid{};
};

using PlayerSceneDlightBinding = PlayerSceneCacheEntry::DlightBinding;

struct PlayerSceneShapeProof;

[[nodiscard]] PlayerSceneShapeProof* player_scene_shape_proof(
    const ql1k::PlayerSceneShapeSignature& shape) noexcept;

struct PlayerSceneShapeProof {
    ql1k::PlayerSceneShapeSignature shape{};
    std::uint32_t successful_comparisons{};
    PlayerSceneCacheEntry::DlightBinding::OriginMode dlight_origin_mode{};
    bool dlight_origin_mode_valid{};
    bool occupied{};
};

struct EngineSceneSnapshot {
    std::int32_t frame_index{};
    std::uint8_t* root{};
    std::uint8_t* poly_descriptors{};
    std::uint8_t* poly_vertices{};
    std::int32_t ref_count{};
    std::int32_t dlight_count{};
    std::int32_t polyvert_count{};
    std::int32_t poly_count{};
    std::int32_t max_polyverts{};
    std::int32_t max_polys{};
    bool valid{};
};

struct PlayerStyleLocalCounters {
    std::uint32_t bypasses{};
    std::uint32_t calls{};
};

struct DeferredFontUpload {
    int texture{};
    ql1k::FontUploadLayout layout{};
    ql1k::FontUploadSizes sizes{};
    std::uint8_t* packed_pixels{};
};

struct DeferredFontUploadQueue {
    std::array<DeferredFontUpload, k_max_deferred_font_uploads> records{};
    std::size_t count{};
};

struct RasterFingerprintSlot {
    GLuint pbo{};
    GlSync fence{};
    std::uint64_t frame_id{};
    bool pending{};
};

struct RasterFingerprintGlState {
    GlGenBuffersFn gen_buffers{};
    GlDeleteBuffersFn delete_buffers{};
    GlBindBufferFn bind_buffer{};
    GlBufferDataFn buffer_data{};
    GlMapBufferFn map_buffer{};
    GlUnmapBufferFn unmap_buffer{};
    GlGenFramebuffersFn gen_framebuffers{};
    GlDeleteFramebuffersFn delete_framebuffers{};
    GlBindFramebufferFn bind_framebuffer{};
    GlCheckFramebufferStatusFn check_framebuffer_status{};
    GlGenRenderbuffersFn gen_renderbuffers{};
    GlDeleteRenderbuffersFn delete_renderbuffers{};
    GlBindRenderbufferFn bind_renderbuffer{};
    GlRenderbufferStorageFn renderbuffer_storage{};
    GlFramebufferRenderbufferFn framebuffer_renderbuffer{};
    GlBlitFramebufferFn blit_framebuffer{};
    GlFenceSyncFn fence_sync{};
    GlClientWaitSyncFn client_wait_sync{};
    GlDeleteSyncFn delete_sync{};
    HGLRC context{};
    GLuint framebuffer{};
    GLuint renderbuffer{};
    std::array<RasterFingerprintSlot, k_raster_fingerprint_capacity> slots{};
    ql1k::RasterFingerprintAccounting<k_raster_fingerprint_capacity> accounting{};
    GLint viewport[4]{};
    std::size_t sample_bytes{};
    std::size_t pbo_bytes{};
    std::size_t issue_index{};
    std::size_t consume_index{};
    std::uint64_t next_frame_id{};
    bool initialized{};
    bool failed{};
};

volatile LONG g_bootstrap_attempted{};
volatile LONG g_worker_started{};
volatile LONG g_installing{};
volatile LONG g_status{k_stock};
volatile LONG g_config_enabled{};
volatile LONG g_config_fresh_view{1};
volatile LONG g_config_highres_entity_interpolation{1};
volatile LONG g_config_raster_fingerprint{};
volatile LONG g_config_hud_replay{1};
volatile LONG g_config_zero_bloom_fast_path{1};
volatile LONG g_config_color_correct_identity_fast_path{};
volatile LONG g_config_shadow_mark_cache{};
volatile LONG g_config_player_scene_replay{};
volatile LONG g_config_player_scene_bypass{};
volatile LONG g_player_scene_validated{};
volatile LONG g_player_scene_failed{};
volatile LONG g_player_scene_validation_module_epoch{};
volatile LONG g_player_scene_validation_renderer_epoch{};
volatile LONG g_player_scene_validation_streak{};
volatile LONG g_player_scene_mismatch_kind{};
volatile LONG g_player_scene_mismatch_index{-1};
volatile LONG g_player_scene_mismatch_offset{-1};
volatile LONG g_player_scene_mismatch_expected{};
volatile LONG g_player_scene_mismatch_actual{};
volatile LONG g_player_scene_mismatch_source{};
volatile LONG g_player_scene_mismatch_delta_x{};
volatile LONG g_player_scene_mismatch_delta_y{};
volatile LONG g_player_scene_mismatch_delta_z{};
volatile LONG g_player_scene_mismatch_ref_type{-1};
volatile LONG g_player_scene_mismatch_beam_length{};
volatile LONG g_player_scene_binding_gun_ref{-1};
volatile LONG g_player_scene_binding_flash_ref{-1};
volatile LONG g_player_scene_binding_gun_model{};
volatile LONG g_player_scene_binding_flash_model{};
volatile LONG g_config_player_style_fast_path{};
volatile LONG g_config_player_style_bypass{};
volatile LONG g_player_style_validated{};
volatile LONG g_player_style_failed{};
volatile LONG g_player_style_validation_module_epoch{};
volatile LONG g_player_style_validation_renderer_epoch{};
volatile LONG g_player_style_call_patch_state{};
volatile LONG g_runtime_armed{};
volatile LONG g_preview_chain_armed{};
volatile LONG g_history_active{};
volatile LONG g_history_fault{};
volatile LONG g_permanent_fault{};
volatile LONG g_transition_closed{};
volatile LONG g_transition_depth{};
volatile LONG g_transition_pending{};
volatile LONG g_history_generation{1};
volatile LONG g_s4_inflight{};
volatile LONG g_cgame_inflight{};
volatile LONG g_module_owner{};
volatile LONG g_module_serial{1};
volatile LONG g_telemetry_started{};
volatile LONG g_force_smp_patch_state{};
volatile LONG g_config_force_smp{};
volatile LONG g_config_smp_synchronous{};
volatile LONG g_config_smp_single_buffer{};
volatile LONG g_config_smp_copy_fpu{};
volatile LONG g_config_smp_main_thread_backend{};
volatile LONG g_config_smp_late_activation{};
volatile LONG g_config_smp_persistent_context{};
volatile LONG g_smp_late_activation_state{};
volatile LONG g_smp_late_failure_code{};
volatile LONG g_smp_persistent_context_state{};
volatile LONG g_smp_context_sync_requests{};
volatile LONG g_smp_persistent_disarm_requested{};
volatile LONG g_smp_persistent_gameplay_eligible{};
volatile LONG g_ui_fullscreen{};
volatile LONG g_renderer_registration_complete{};
volatile LONG g_smp_registration_suspended{};
volatile LONG g_measured_present_fps{};
volatile LONG g_measured_simulation_hz{};
volatile LONG64 g_present_count{};
volatile LONG64 g_simulation_count{};
volatile LONG64 g_outer_loop_count{};
volatile LONG64 g_zero_render_count{};
volatile LONG64 g_preview_attempt_count{};
volatile LONG64 g_preview_accept_count{};
volatile LONG64 g_preview_nonzero_count{};
volatile LONG64 g_preview_skip_count{};
volatile LONG64 g_preview_restore_count{};
volatile LONG64 g_preview_restore_failure_count{};
volatile LONG64 g_prediction_count{};
volatile LONG64 g_overlay_apply_count{};
volatile LONG64 g_overlay_restore_count{};
volatile LONG64 g_scene_submission_count{};
volatile LONG64 g_fresh_view_submission_count{};
volatile LONG64 g_visible_overlay_submission_count{};
volatile LONG64 g_endframe_completion_count{};
volatile LONG64 g_usercmd_publication_count{};
volatile LONG64 g_packet_write_count{};
volatile LONG64 g_render_only_screen_ticks{};
volatile LONG64 g_render_only_screen_samples{};
volatile LONG64 g_positive_client_frame_ticks{};
volatile LONG64 g_positive_client_frame_samples{};
volatile LONG64 g_draw_active_frame_ticks{};
volatile LONG64 g_draw_active_frame_samples{};
volatile LONG64 g_draw_pre_active_ticks{};
volatile LONG64 g_draw_pre_active_samples{};
constexpr std::size_t k_draw_phase_count = 5U;
volatile LONG64 g_draw_phase_ticks[k_draw_phase_count]{};
volatile LONG64 g_draw_phase_samples[k_draw_phase_count]{};
constexpr std::size_t k_cgame_hotpath_count = 5U;
constexpr std::size_t k_hotpath_packet_entities = 0U;
constexpr std::size_t k_hotpath_player_renderer = 1U;
constexpr std::size_t k_hotpath_player_shadow = 2U;
constexpr std::size_t k_hotpath_shadow_impact = 3U;
constexpr std::size_t k_hotpath_lightning = 4U;
volatile LONG64 g_cgame_hotpath_ticks[k_cgame_hotpath_count]{};
volatile LONG64 g_cgame_hotpath_samples[k_cgame_hotpath_count]{};
volatile LONG64 g_renderer_scene_frontend_ticks{};
volatile LONG64 g_renderer_scene_frontend_samples{};
volatile LONG64 g_re_endframe_ticks{};
volatile LONG64 g_re_endframe_samples{};
volatile LONG64 g_glimp_endframe_ticks{};
volatile LONG64 g_glimp_endframe_samples{};
volatile LONG64 g_raster_fingerprint_issued_count{};
volatile LONG64 g_raster_fingerprint_ready_count{};
volatile LONG64 g_raster_fingerprint_changed_count{};
volatile LONG64 g_raster_fingerprint_repeat_count{};
volatile LONG64 g_raster_fingerprint_gap_count{};
volatile LONG64 g_raster_fingerprint_wait_failure_count{};
volatile LONG64 g_raster_fingerprint_init_failure_count{};
volatile LONG64 g_cvar_refresh_count{};
volatile LONG64 g_cvar_reuse_count{};
volatile LONG g_cvar_refresh_warmed{};
volatile LONG64 g_hud_capture_count{};
volatile LONG64 g_hud_replay_count{};
volatile LONG64 g_hud_stock_fallback_count{};
volatile LONG64 g_hud_capture_reject_count{};
volatile LONG64 g_hud_capture_bytes{};
volatile LONG64 g_hud_stock_ticks{};
volatile LONG64 g_hud_stock_samples{};
volatile LONG64 g_hud_replay_ticks{};
volatile LONG64 g_hud_replay_samples{};
volatile LONG64 g_zero_bloom_fast_count{};
volatile LONG64 g_zero_bloom_stock_count{};
volatile LONG64 g_zero_bloom_backend_ticks{};
volatile LONG64 g_zero_bloom_backend_samples{};
volatile LONG64 g_color_correct_backend_ticks{};
volatile LONG64 g_color_correct_backend_samples{};
volatile LONG64 g_color_correct_identity_fast_count{};
volatile LONG64 g_color_correct_identity_stock_count{};
volatile LONG64 g_shadow_mark_cache_hit_count{};
volatile LONG64 g_shadow_mark_cache_miss_count{};
volatile LONG64 g_shadow_mark_cache_capture_count{};
volatile LONG64 g_shadow_mark_cache_replay_poly_count{};
volatile LONG64 g_shadow_mark_cache_overflow_count{};
volatile LONG64 g_player_scene_capture_count{};
volatile LONG64 g_player_scene_capture_reject_count{};
volatile LONG64 g_player_scene_validation_count{};
volatile LONG64 g_player_scene_mismatch_count{};
volatile LONG64 g_player_scene_replay_count{};
volatile LONG64 g_player_scene_stock_fallback_count{};
volatile LONG64 g_player_scene_angle_fallback_count{};
volatile LONG64 g_player_scene_beam_replay_count{};
volatile LONG64 g_player_scene_pose_sample_count{};
volatile LONG64 g_player_scene_pose_change_count{};
volatile LONG64 g_player_scene_pose_repeat_count{};
volatile LONG64 g_player_scene_body_axis_sample_count{};
volatile LONG64 g_player_scene_body_axis_change_count{};
volatile LONG64 g_player_scene_body_axis_repeat_count{};
volatile LONG64 g_player_style_validation_count{};
volatile LONG64 g_player_style_mismatch_count{};
volatile LONG64 g_player_style_mutation_failure_count{};
volatile LONG64 g_player_style_bypass_count{};
volatile LONG g_hud_cache_valid{};
volatile LONG g_renderer_epoch{1};
volatile LONG g_raster_fingerprint_pending{};
volatile LONG g_raster_fingerprint_active{};
volatile LONG g_raster_fingerprint_failure_code{};
volatile LONG64 g_view_revision{};
volatile LONG64 g_entity_pose_sample_count{};
volatile LONG64 g_entity_pose_change_count{};
volatile LONG64 g_entity_pose_repeat_count{};
volatile LONG64 g_entity_pose_track_switch_count{};
volatile LONG64 g_submitted_pose_sample_count{};
volatile LONG64 g_submitted_pose_change_count{};
volatile LONG64 g_submitted_pose_repeat_count{};
volatile LONG64 g_highres_interpolation_count{};
volatile LONG64 g_entity_pose_restore_count{};
volatile LONG64 g_entity_pose_restore_failure_count{};
volatile LONG64 g_smp_synchronous_wait_count{};
volatile LONG64 g_smp_fpu_apply_count{};
volatile LONG64 g_smp_font_upload_capture_count{};
volatile LONG64 g_smp_font_upload_replay_count{};
volatile LONG64 g_smp_font_upload_drop_count{};
volatile LONG64 g_smp_font_upload_byte_count{};
volatile LONG64 g_smp_font_upload_invalid_argument_count{};
volatile LONG64 g_smp_font_upload_invalid_frame_count{};
volatile LONG64 g_smp_font_upload_invalid_layout_count{};
volatile LONG64 g_smp_font_upload_allocation_failure_count{};
volatile LONG64 g_smp_font_upload_queue_full_count{};
volatile LONG64 g_smp_font_upload_zero_texture_skip_count{};
volatile LONG64 g_smp_context_sync_count{};
volatile LONG64 g_smp_context_main_acquire_count{};
volatile LONG64 g_smp_context_main_release_count{};
volatile LONG64 g_smp_context_renderer_acquire_count{};
volatile LONG64 g_smp_context_renderer_release_count{};
volatile LONG64 g_smp_context_transfer_failure_count{};
volatile LONG64 g_smp_ui_suspend_count{};
volatile LONG64 g_smp_ui_resume_count{};
volatile LONG64 g_smp_ui_context_return_count{};
volatile LONG64 g_smp_ui_context_return_failure_count{};
volatile LONG64 g_renderer_begin_registration_count{};
volatile LONG64 g_renderer_end_registration_count{};
volatile LONG64 g_renderer_shutdown_count{};
volatile LONG64 g_smp_restart_rearm_count{};
volatile LONG64 g_smp_registration_suspend_count{};
volatile LONG64 g_smp_registration_resume_count{};
volatile LONG64 g_smp_worker_spawn_count{};
volatile LONG64 g_smp_worker_join_count{};
volatile LONG64 g_smp_worker_join_failure_count{};
volatile LONG64 g_smp_worker_handle_cleanup_count{};
volatile LONG64 g_smp_post_activation_sync_count{};
volatile LONG64 g_smp_font_upload_shutdown_discard_count{};
volatile LONG64 g_smp_lifecycle_event_sequence{};
volatile LONG64 g_renderer_begin_registration_event_sequence{};
volatile LONG64 g_renderer_end_registration_event_sequence{};
volatile LONG64 g_renderer_shutdown_begin_event_sequence{};
volatile LONG64 g_renderer_shutdown_end_event_sequence{};
volatile LONG64 g_smp_activation_event_sequence{};
volatile LONG64 g_smp_persistent_activation_event_sequence{};
volatile LONG64 g_smp_post_activation_sync_event_sequence{};
volatile LONG64 g_smp_font_capture_event_sequence{};
volatile LONG64 g_smp_font_replay_event_sequence{};
volatile LONG g_smp_shutdown_worker_join_result{};
volatile LONG g_smp_fpu_ready{};
volatile LONG g_smp_main_x87_control{};
volatile LONG g_smp_main_mxcsr{};
volatile LONG g_smp_renderer_x87_before{};
volatile LONG g_smp_renderer_x87_after{};
volatile LONG g_smp_renderer_mxcsr_before{};
volatile LONG g_smp_renderer_mxcsr_after{};
volatile LONG g_entity_pose_last_number{-1};
volatile LONG g_entity_pose_x_milli{};
volatile LONG g_entity_pose_y_milli{};
volatile LONG g_entity_pose_z_milli{};
volatile LONG g_render_fractional_us{};
volatile LONG g_frame_interpolation_ppm{};
volatile LONG g_entity_pose_transaction_entities{};
volatile LONG64 g_qpc_frequency{};
std::array<std::uint32_t, 3> g_last_entity_pose_bits{};
bool g_last_entity_pose_valid{};
ql1k::SubmittedPoseSignature<k_max_player_entities> g_last_submitted_pose_signature{};
bool g_last_submitted_pose_signature_valid{};
volatile LONG g_submitted_pose_player_count{};
volatile LONG g_pending_mouse_x_last{};
volatile LONG g_pending_mouse_y_last{};
volatile LONG g_engine_committed_pitch_mdeg{};
volatile LONG g_engine_committed_yaw_mdeg{};
volatile LONG g_preview_pitch_delta_mdeg{};
volatile LONG g_preview_yaw_delta_mdeg{};
volatile LONG g_refdef_angle_pitch_mdeg{};
volatile LONG g_refdef_angle_yaw_mdeg{};
volatile LONG g_submitted_pitch_mdeg{};
volatile LONG g_submitted_yaw_mdeg{};
float g_last_submitted_pitch{};
float g_last_submitted_yaw{};
bool g_last_submitted_view_valid{};
RasterFingerprintGlState g_raster_fingerprint_gl{};
std::atomic<const char*> g_reason{"not_started"};
std::array<char, 65> g_engine_hash{};
std::array<char, 65> g_cgame_hash{};

struct HudReplayCache {
    std::array<std::uint8_t, k_hud_replay_capacity> bytes{};
    std::size_t size{};
    std::int32_t integer_time{};
    LONG renderer_epoch{};
    LONG module_epoch{};
    LONG64 captured_qpc{};
};

HudReplayCache g_hud_replay_cache{};

HMODULE g_engine{};
HMODULE g_cgame{};
HMODULE g_cgame_install_ticket{};
DeltaFn g_stock_delta{};
ClientFrameFn g_stock_client_frame{};
PublisherFn g_stock_publisher{};
GetterFn g_stock_getter{};
PresentFn g_present{};
MouseTransformFn g_mouse_transform{};
PacketWriteFn g_stock_packet_write{};
EndFrameFn g_stock_endframe{};
ReEndFrameFn g_stock_re_endframe{};
DisconnectFn g_stock_disconnect{};
VmLoaderFn g_stock_vm_loader{};
ShutdownFn g_stock_shutdown{};
CgameEntryFn g_stock_warning_entry{};
CgameEntryFn g_stock_predict_entry{};
CgameEntryFn g_stock_cgame_update_cvars{};
CgameEntryFn g_stock_cgame_draw_2d{};
DrawActiveFrameFn g_stock_draw_active_frame{};
PlayerRendererFn g_stock_player_renderer{};
AddPolyToSceneFn g_shadow_add_poly_to_scene{};
std::uint8_t* g_player_style_color_correct_cvar{};
std::uintptr_t g_player_style_stock_target{};
std::array<std::uint8_t, k_player_style_call_signature.size()>
    g_player_style_call_patch{};
SmpWakeFn g_stock_smp_wake{};
SmpFrontEndSleepFn g_stock_smp_frontend_sleep{};
SmpRendererSleepFn g_stock_smp_renderer_sleep{};
SmpFrameToggleFn g_stock_smp_frame_toggle{};
RendererCommandIssueFn g_stock_renderer_command_issue{};
RendererFrontEndSyncFn g_stock_renderer_frontend_sync{};
RendererCommandBufferFn g_renderer_command_buffer{};
BloomBackendFn g_stock_color_correct_backend{};
BloomBackendFn g_stock_bloom_backend{};
PostProcessQuadFn g_postprocess_quad{};
PostProcessVoidFn g_postprocess_state_reset{};
PostProcessVoidFn g_postprocess_bind_destination{};
PostProcessVoidFn g_bloom_uniform_refresh{};
RendererBindImageFn g_renderer_bind_image{};
RendererBeginRegistrationFn g_stock_renderer_begin_registration{};
RendererEndRegistrationFn g_stock_renderer_end_registration{};
RendererShutdownFn g_stock_renderer_shutdown{};
SmpCommandBuffersInitFn g_stock_smp_command_buffers_init{};
SmpCommandBuffersShutdownFn g_stock_smp_command_buffers_shutdown{};
BackendAllocationFn g_stock_backend_allocation{};
FontAtlasUploadFn g_stock_font_atlas_upload{};

safetyhook::MidHook* g_s1{};
safetyhook::InlineHook* g_s2{};
safetyhook::InlineHook* g_s3{};
safetyhook::InlineHook* g_s4{};
safetyhook::InlineHook* g_screen_update{};
safetyhook::InlineHook* g_packet_write{};
safetyhook::InlineHook* g_endframe{};
safetyhook::InlineHook* g_re_endframe{};
safetyhook::InlineHook* g_s8{};
safetyhook::MidHook* g_s9_pre{};
safetyhook::MidHook* g_s9_post{};
safetyhook::InlineHook* g_s10{};
safetyhook::InlineHook* g_s11{};
safetyhook::InlineHook* g_warning_entry{};
safetyhook::InlineHook* g_predict_entry{};
safetyhook::InlineHook* g_cgame_update_cvars{};
safetyhook::InlineHook* g_cgame_draw_2d{};
safetyhook::InlineHook* g_draw_active_frame{};
safetyhook::InlineHook* g_player_scene_renderer{};
safetyhook::InlineHook* g_smp_wake{};
safetyhook::InlineHook* g_smp_frontend_sleep{};
safetyhook::InlineHook* g_smp_renderer_sleep{};
safetyhook::InlineHook* g_smp_frame_toggle{};
safetyhook::InlineHook* g_renderer_command_issue{};
safetyhook::InlineHook* g_color_correct_backend{};
safetyhook::InlineHook* g_bloom_backend{};
safetyhook::InlineHook* g_renderer_frontend_sync{};
safetyhook::InlineHook* g_renderer_begin_registration{};
safetyhook::InlineHook* g_renderer_end_registration{};
safetyhook::InlineHook* g_renderer_shutdown{};
safetyhook::InlineHook* g_smp_command_buffers_init{};
safetyhook::InlineHook* g_smp_command_buffers_shutdown{};
safetyhook::InlineHook* g_backend_allocation{};
safetyhook::InlineHook* g_font_atlas_upload{};
safetyhook::MidHook* g_scene_submission{};
safetyhook::MidHook* g_shadow_impact_return{};
safetyhook::MidHook* g_shadow_mark_entry{};
safetyhook::MidHook* g_shadow_mark_add_poly{};
safetyhook::MidHook* g_frame_interpolation_seam{};
safetyhook::MidHook* g_warning{};
safetyhook::MidHook* g_predict{};
safetyhook::MidHook* g_fps_display{};
safetyhook::MidHook* g_hitreg_fire{};
safetyhook::MidHook* g_hitreg_feedback{};
safetyhook::MidHook* g_hitreg_draw{};

SRWLOCK g_history_lock = SRWLOCK_INIT;
SRWLOCK g_decision_lock = SRWLOCK_INIT;
SRWLOCK g_module_lock = SRWLOCK_INIT;
SRWLOCK g_cgame_gate = SRWLOCK_INIT;
SRWLOCK g_draw_lifecycle_gate = SRWLOCK_INIT;
SRWLOCK g_hitreg_lock = SRWLOCK_INIT;
SRWLOCK g_force_smp_patch_lock = SRWLOCK_INIT;
SRWLOCK g_font_upload_lock = SRWLOCK_INIT;
ShadowMarkCache g_shadow_mark_cache{};
std::array<PlayerSceneCacheEntry, k_player_scene_cache_slots>
    g_player_scene_cache{};
std::array<PlayerSceneShapeProof, k_player_scene_shape_proof_slots>
    g_player_scene_shape_proofs{};
std::size_t g_player_scene_shape_proof_count{};
std::array<std::uint32_t, 3> g_last_player_scene_pose_bits{};
bool g_last_player_scene_pose_valid{};
std::array<std::uint32_t, 9> g_last_player_scene_body_axis_bits{};
bool g_last_player_scene_body_axis_valid{};
volatile LONG g_player_scene_pose_slot{-1};
__declspec(thread) PlayerSceneProducts g_player_scene_actual_scratch{};
__declspec(thread) PlayerSceneProducts g_player_scene_expected_scratch{};
__declspec(thread) PlayerSceneProducts g_player_scene_surface_scratch{};
__declspec(thread) PlayerStyleLocalCounters g_player_style_local_counters{};
__declspec(thread) std::uint32_t g_player_style_validation_streak{};
ql1k::HitregState g_hitreg_state{};
std::array<DeferredFontUploadQueue, 2> g_deferred_font_uploads{};
volatile LONG g_font_upload_pending[2]{};
std::uint8_t* g_font_upload_staging{};
std::size_t g_font_upload_staging_capacity{};
volatile LONG64 g_hitreg_trace_total{};
volatile LONG64 g_hitreg_trace_player{};
volatile LONG64 g_hitreg_trace_world{};
volatile LONG64 g_hitreg_trace_none{};
volatile LONG64 g_hitreg_trace_other{};
volatile LONG g_hitreg_trace_last_entity{-1};
std::array<AuxiliaryRecord, k_history_capacity> g_history{};
std::size_t g_history_head{};
std::size_t g_history_size{};
std::int32_t g_history_newest{};
bool g_history_have_last{};
std::int32_t g_history_last{};
std::int32_t g_history_last_time{};
std::int32_t g_capture_cursor{};
HistorySelection g_decision{};
std::int32_t g_decision_current{};
std::int32_t g_decision_snapshot{};
bool g_decision_valid{};

__declspec(thread) unsigned char g_zero_frame_token{};
__declspec(thread) bool g_zero_bloom_fast_for_color_correct{};
__declspec(thread) ReplayAuthorization g_replay_auth{};
__declspec(thread) LONG g_s9_token_depth{};
__declspec(thread) LONG g_module_ticket_depth{};
__declspec(thread) TransientFrameState g_transient_frame{};
__declspec(thread) ShadowMarkCache::Entry* g_shadow_mark_capture_entry{};
__declspec(thread) bool g_shadow_mark_capture_active{};
__declspec(thread) ql1k::SubmillisecondRenderClock g_render_clock{};
__declspec(thread) ql1k::IndexedFieldTransaction<k_entity_pose_transaction_capacity,
                                                  k_centity_pose_field_count>
    g_entity_pose_transaction{};
__declspec(thread) ql1k::SnapshotEntityIndexCache<k_max_cgame_entities,
                                                   k_max_player_entities>
    g_snapshot_entity_cache{};
__declspec(thread) std::array<std::int32_t, k_max_cgame_entities>
    g_snapshot_entity_source_scratch{};

bool attach_cgame_hooks();
bool detach_cgame_hooks();
bool preview_chain_ready() noexcept;
void clear_transient_frame() noexcept;
bool begin_transient_preview() noexcept;
void* engine_address(std::uintptr_t preferred_va);
void abandon_raster_fingerprint_state() noexcept;

void reset_pose_freshness_baseline() noexcept {
    g_last_entity_pose_bits = {};
    g_last_entity_pose_valid = false;
    g_last_submitted_pose_signature = {};
    g_last_submitted_pose_signature_valid = false;
    g_snapshot_entity_cache.clear();
    InterlockedExchange(&g_entity_pose_last_number, -1);
    InterlockedExchange(&g_submitted_pose_player_count, 0);
}

void reset_hitreg() {
    AcquireSRWLockExclusive(&g_hitreg_lock);
    g_hitreg_state.reset();
    ReleaseSRWLockExclusive(&g_hitreg_lock);
    InterlockedExchange64(&g_hitreg_trace_total, 0);
    InterlockedExchange64(&g_hitreg_trace_player, 0);
    InterlockedExchange64(&g_hitreg_trace_world, 0);
    InterlockedExchange64(&g_hitreg_trace_none, 0);
    InterlockedExchange64(&g_hitreg_trace_other, 0);
    InterlockedExchange(&g_hitreg_trace_last_entity, -1);
}

void record_hitreg_usercmd(const AuxiliaryRecord& record) {
    std::int32_t buttons{};
    std::memcpy(&buttons, record.bytes.data() + k_usercmd_buttons_offset, sizeof(buttons));
    AcquireSRWLockExclusive(&g_hitreg_lock);
    g_hitreg_state.on_usercmd(record.server_time, (buttons & k_button_attack) != 0);
    ReleaseSRWLockExclusive(&g_hitreg_lock);
}

BOOL CALLBACK find_game_window(HWND window, LPARAM result) {
    DWORD process_id{};
    GetWindowThreadProcessId(window, &process_id);
    if (process_id == GetCurrentProcessId() && IsWindowVisible(window) &&
        GetWindow(window, GW_OWNER) == nullptr) {
        *reinterpret_cast<HWND*>(result) = window;
        return FALSE;
    }
    return TRUE;
}

const char* hitreg_kind_name(const ql1k::HitregDisplayKind kind) noexcept {
    switch (kind) {
        case ql1k::HitregDisplayKind::none:
            return "none";
        case ql1k::HitregDisplayKind::percent:
            return "percent";
        case ql1k::HitregDisplayKind::infinity:
            return "infinity";
        case ql1k::HitregDisplayKind::unavailable:
            return "unavailable";
    }
    return "unknown";
}

const char* client_accuracy_kind_name(const ql1k::ClientAccuracyDisplayKind kind) noexcept {
    switch (kind) {
        case ql1k::ClientAccuracyDisplayKind::none:
            return "none";
        case ql1k::ClientAccuracyDisplayKind::percent:
            return "percent";
        case ql1k::ClientAccuracyDisplayKind::unavailable:
            return "unavailable";
    }
    return "unknown";
}

const char* hitreg_reason_name(const ql1k::HitregUnavailableReason reason) noexcept {
    switch (reason) {
        case ql1k::HitregUnavailableReason::none:
            return "none";
        case ql1k::HitregUnavailableReason::inflight_hold_capacity:
            return "inflight_hold_capacity";
        case ql1k::HitregUnavailableReason::pending_tick_capacity:
            return "pending_tick_capacity";
        case ql1k::HitregUnavailableReason::multiple_ticks_before_first_trace:
            return "multiple_ticks_before_first_trace";
        case ql1k::HitregUnavailableReason::cross_hold_feedback:
            return "cross_hold_feedback";
        case ql1k::HitregUnavailableReason::unresolved_trace_on_close:
            return "unresolved_trace_on_close";
        case ql1k::HitregUnavailableReason::client_ray_unavailable:
            return "client_ray_unavailable";
    }
    return "unknown";
}

LONG64 qpc_start() noexcept {
    LARGE_INTEGER value{};
    return QueryPerformanceCounter(&value) ? value.QuadPart : 0;
}

struct RendererCommandSnapshot {
    std::int32_t index{};
    std::uint8_t* root{};
    std::int32_t used{};
    bool valid{};
};

void reset_hud_replay_cache() noexcept {
    InterlockedExchange(&g_hud_cache_valid, 0);
    g_hud_replay_cache.size = 0;
    g_hud_replay_cache.integer_time = 0;
    g_hud_replay_cache.renderer_epoch = 0;
    g_hud_replay_cache.module_epoch = 0;
    g_hud_replay_cache.captured_qpc = 0;
}

void reset_player_scene_replay_epoch() noexcept {
    for (auto& entry : g_player_scene_cache) {
        entry.valid = false;
        entry.dlight_bindings_valid = false;
        entry.shape_valid = false;
        entry.captured_qpc = 0;
        entry.products.valid = false;
    }
    g_player_scene_shape_proofs = {};
    g_player_scene_shape_proof_count = 0U;
    InterlockedExchange(&g_player_scene_validated, 0);
    InterlockedExchange(&g_player_scene_failed, 0);
    InterlockedExchange(&g_player_scene_validation_streak, 0);
    InterlockedExchange(&g_player_scene_mismatch_kind, 0);
    InterlockedExchange(&g_player_scene_mismatch_index, -1);
    InterlockedExchange(&g_player_scene_mismatch_offset, -1);
    InterlockedExchange(&g_player_scene_mismatch_expected, 0);
    InterlockedExchange(&g_player_scene_mismatch_actual, 0);
    InterlockedExchange(&g_player_scene_mismatch_source, 0);
    InterlockedExchange(&g_player_scene_mismatch_delta_x, 0);
    InterlockedExchange(&g_player_scene_mismatch_delta_y, 0);
    InterlockedExchange(&g_player_scene_mismatch_delta_z, 0);
    InterlockedExchange(&g_player_scene_mismatch_ref_type, -1);
    InterlockedExchange(&g_player_scene_mismatch_beam_length, 0);
    InterlockedExchange(&g_player_scene_binding_gun_ref, -1);
    InterlockedExchange(&g_player_scene_binding_flash_ref, -1);
    InterlockedExchange(&g_player_scene_binding_gun_model, 0);
    InterlockedExchange(&g_player_scene_binding_flash_model, 0);
    InterlockedExchange(
        &g_player_scene_validation_module_epoch,
        InterlockedCompareExchange(&g_module_serial, 0, 0));
    InterlockedExchange(
        &g_player_scene_validation_renderer_epoch,
        InterlockedCompareExchange(&g_renderer_epoch, 0, 0));
    g_last_player_scene_pose_bits = {};
    g_last_player_scene_pose_valid = false;
    g_last_player_scene_body_axis_bits = {};
    g_last_player_scene_body_axis_valid = false;
}

void advance_renderer_epoch() noexcept {
    const LONG epoch = InterlockedIncrement(&g_renderer_epoch);
    if (epoch <= 0) {
        InterlockedExchange(&g_renderer_epoch, 1);
    }
    reset_hud_replay_cache();
    reset_player_scene_replay_epoch();
}

[[nodiscard]] RendererCommandSnapshot renderer_command_snapshot() noexcept {
    auto* const frame_index =
        static_cast<const std::int32_t*>(engine_address(k_engine_smp_frame_index));
    auto** const roots =
        static_cast<std::uint8_t**>(engine_address(k_engine_backend_data_roots));
    if (frame_index == nullptr || roots == nullptr ||
        (*frame_index != 0 && *frame_index != 1)) {
        return {};
    }
    std::uint8_t* const root = roots[*frame_index];
    if (root == nullptr) {
        return {};
    }
    const std::int32_t used = *reinterpret_cast<const std::int32_t*>(
        root + k_renderer_command_used_offset);
    if (used < 0 || used > k_renderer_command_capacity - 4) {
        return {};
    }
    return {*frame_index, root, used, true};
}

[[nodiscard]] bool hud_replay_gameplay_eligible() noexcept {
    const auto* const client_state =
        static_cast<const std::int32_t*>(engine_address(k_engine_client_state));
    const auto* const key_catchers =
        static_cast<const std::int32_t*>(engine_address(k_engine_key_catchers));
    return client_state != nullptr && key_catchers != nullptr &&
           *client_state == ql1k::k_client_state_active && *key_catchers == 0 &&
           InterlockedCompareExchange(&g_ui_fullscreen, 0, 0) == 0;
}

[[nodiscard]] bool hud_replay_renderer_ready() noexcept {
    const auto* const registered =
        static_cast<const std::int32_t*>(engine_address(k_engine_renderer_registered));
    return registered != nullptr && *registered != 0 &&
           g_renderer_begin_registration != nullptr &&
           g_renderer_end_registration != nullptr &&
           g_renderer_shutdown != nullptr &&
           InterlockedCompareExchange(&g_renderer_registration_complete, 0, 0) != 0;
}

[[nodiscard]] bool zero_bloom_renderer_ready() noexcept {
    const auto* const registered =
        static_cast<const std::int32_t*>(engine_address(k_engine_renderer_registered));
    return registered != nullptr && *registered != 0;
}

LONG64 average_interval_ns(const LONG64 ticks, const LONG64 samples,
                           const LONG64 frequency) noexcept {
    if (ticks <= 0 || samples <= 0 || frequency <= 0) {
        return 0;
    }
    const long double nanoseconds =
        (static_cast<long double>(ticks) * 1000000000.0L) /
        (static_cast<long double>(frequency) * static_cast<long double>(samples));
    return nanoseconds >= static_cast<long double>(LLONG_MAX)
               ? LLONG_MAX
               : static_cast<LONG64>(nanoseconds + 0.5L);
}

DWORD WINAPI telemetry_worker(void*) noexcept {
    LARGE_INTEGER frequency{};
    LARGE_INTEGER previous_time{};
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0 ||
        !QueryPerformanceCounter(&previous_time)) {
        return 0;
    }
    LONG64 previous_presents = InterlockedCompareExchange64(&g_present_count, 0, 0);
    LONG64 previous_simulation = InterlockedCompareExchange64(&g_simulation_count, 0, 0);
    LONG64 previous_outer = InterlockedCompareExchange64(&g_outer_loop_count, 0, 0);
    LONG64 previous_zero = InterlockedCompareExchange64(&g_zero_render_count, 0, 0);
    LONG64 previous_preview_accept =
        InterlockedCompareExchange64(&g_preview_accept_count, 0, 0);
    LONG64 previous_preview_nonzero =
        InterlockedCompareExchange64(&g_preview_nonzero_count, 0, 0);
    LONG64 previous_prediction = InterlockedCompareExchange64(&g_prediction_count, 0, 0);
    LONG64 previous_scene = InterlockedCompareExchange64(&g_scene_submission_count, 0, 0);
    LONG64 previous_fresh =
        InterlockedCompareExchange64(&g_fresh_view_submission_count, 0, 0);
    LONG64 previous_entity_pose_samples =
        InterlockedCompareExchange64(&g_entity_pose_sample_count, 0, 0);
    LONG64 previous_entity_pose_changes =
        InterlockedCompareExchange64(&g_entity_pose_change_count, 0, 0);
    LONG64 previous_entity_pose_repeats =
        InterlockedCompareExchange64(&g_entity_pose_repeat_count, 0, 0);
    LONG64 previous_submitted_pose_samples =
        InterlockedCompareExchange64(&g_submitted_pose_sample_count, 0, 0);
    LONG64 previous_submitted_pose_changes =
        InterlockedCompareExchange64(&g_submitted_pose_change_count, 0, 0);
    LONG64 previous_submitted_pose_repeats =
        InterlockedCompareExchange64(&g_submitted_pose_repeat_count, 0, 0);
    LONG64 previous_player_scene_body_axis_samples =
        InterlockedCompareExchange64(&g_player_scene_body_axis_sample_count, 0, 0);
    LONG64 previous_player_scene_body_axis_changes =
        InterlockedCompareExchange64(&g_player_scene_body_axis_change_count, 0, 0);
    LONG64 previous_player_scene_body_axis_repeats =
        InterlockedCompareExchange64(&g_player_scene_body_axis_repeat_count, 0, 0);
    LONG64 previous_highres_interpolation =
        InterlockedCompareExchange64(&g_highres_interpolation_count, 0, 0);
    LONG64 previous_visible_overlay =
        InterlockedCompareExchange64(&g_visible_overlay_submission_count, 0, 0);
    LONG64 previous_endframe =
        InterlockedCompareExchange64(&g_endframe_completion_count, 0, 0);
    LONG64 previous_usercmd =
        InterlockedCompareExchange64(&g_usercmd_publication_count, 0, 0);
    LONG64 previous_packet = InterlockedCompareExchange64(&g_packet_write_count, 0, 0);
    LONG64 previous_render_only_ticks =
        InterlockedCompareExchange64(&g_render_only_screen_ticks, 0, 0);
    LONG64 previous_render_only_samples =
        InterlockedCompareExchange64(&g_render_only_screen_samples, 0, 0);
    LONG64 previous_positive_ticks =
        InterlockedCompareExchange64(&g_positive_client_frame_ticks, 0, 0);
    LONG64 previous_positive_samples =
        InterlockedCompareExchange64(&g_positive_client_frame_samples, 0, 0);
    LONG64 previous_draw_active_ticks =
        InterlockedCompareExchange64(&g_draw_active_frame_ticks, 0, 0);
    LONG64 previous_draw_active_samples =
        InterlockedCompareExchange64(&g_draw_active_frame_samples, 0, 0);
    LONG64 previous_draw_pre_active_ticks =
        InterlockedCompareExchange64(&g_draw_pre_active_ticks, 0, 0);
    LONG64 previous_draw_pre_active_samples =
        InterlockedCompareExchange64(&g_draw_pre_active_samples, 0, 0);
    std::array<LONG64, k_draw_phase_count> previous_draw_phase_ticks{};
    std::array<LONG64, k_draw_phase_count> previous_draw_phase_samples{};
    for (std::size_t phase = 0; phase < k_draw_phase_count; ++phase) {
        previous_draw_phase_ticks[phase] =
            InterlockedCompareExchange64(&g_draw_phase_ticks[phase], 0, 0);
        previous_draw_phase_samples[phase] =
            InterlockedCompareExchange64(&g_draw_phase_samples[phase], 0, 0);
    }
    std::array<LONG64, k_cgame_hotpath_count> previous_cgame_hotpath_ticks{};
    std::array<LONG64, k_cgame_hotpath_count> previous_cgame_hotpath_samples{};
    for (std::size_t path = 0; path < k_cgame_hotpath_count; ++path) {
        previous_cgame_hotpath_ticks[path] =
            InterlockedCompareExchange64(&g_cgame_hotpath_ticks[path], 0, 0);
        previous_cgame_hotpath_samples[path] =
            InterlockedCompareExchange64(&g_cgame_hotpath_samples[path], 0, 0);
    }
    LONG64 previous_renderer_scene_frontend_ticks =
        InterlockedCompareExchange64(&g_renderer_scene_frontend_ticks, 0, 0);
    LONG64 previous_renderer_scene_frontend_samples =
        InterlockedCompareExchange64(&g_renderer_scene_frontend_samples, 0, 0);
    LONG64 previous_re_endframe_ticks =
        InterlockedCompareExchange64(&g_re_endframe_ticks, 0, 0);
    LONG64 previous_re_endframe_samples =
        InterlockedCompareExchange64(&g_re_endframe_samples, 0, 0);
    LONG64 previous_glimp_endframe_ticks =
        InterlockedCompareExchange64(&g_glimp_endframe_ticks, 0, 0);
    LONG64 previous_glimp_endframe_samples =
        InterlockedCompareExchange64(&g_glimp_endframe_samples, 0, 0);
    LONG64 previous_raster_issued =
        InterlockedCompareExchange64(&g_raster_fingerprint_issued_count, 0, 0);
    LONG64 previous_raster_ready =
        InterlockedCompareExchange64(&g_raster_fingerprint_ready_count, 0, 0);
    LONG64 previous_raster_changed =
        InterlockedCompareExchange64(&g_raster_fingerprint_changed_count, 0, 0);
    LONG64 previous_raster_repeated =
        InterlockedCompareExchange64(&g_raster_fingerprint_repeat_count, 0, 0);
    LONG64 previous_raster_gaps =
        InterlockedCompareExchange64(&g_raster_fingerprint_gap_count, 0, 0);
    LONG64 previous_cvar_refresh =
        InterlockedCompareExchange64(&g_cvar_refresh_count, 0, 0);
    LONG64 previous_cvar_reuse =
        InterlockedCompareExchange64(&g_cvar_reuse_count, 0, 0);
    LONG64 previous_hud_capture =
        InterlockedCompareExchange64(&g_hud_capture_count, 0, 0);
    LONG64 previous_hud_replay =
        InterlockedCompareExchange64(&g_hud_replay_count, 0, 0);
    LONG64 previous_hud_fallback =
        InterlockedCompareExchange64(&g_hud_stock_fallback_count, 0, 0);
    LONG64 previous_hud_stock_ticks =
        InterlockedCompareExchange64(&g_hud_stock_ticks, 0, 0);
    LONG64 previous_hud_stock_samples =
        InterlockedCompareExchange64(&g_hud_stock_samples, 0, 0);
    LONG64 previous_hud_replay_ticks =
        InterlockedCompareExchange64(&g_hud_replay_ticks, 0, 0);
    LONG64 previous_hud_replay_samples =
        InterlockedCompareExchange64(&g_hud_replay_samples, 0, 0);
    LONG64 previous_zero_bloom_fast =
        InterlockedCompareExchange64(&g_zero_bloom_fast_count, 0, 0);
    LONG64 previous_zero_bloom_stock =
        InterlockedCompareExchange64(&g_zero_bloom_stock_count, 0, 0);
    LONG64 previous_zero_bloom_backend_ticks =
        InterlockedCompareExchange64(&g_zero_bloom_backend_ticks, 0, 0);
    LONG64 previous_zero_bloom_backend_samples =
        InterlockedCompareExchange64(&g_zero_bloom_backend_samples, 0, 0);
    LONG64 previous_color_correct_backend_ticks =
        InterlockedCompareExchange64(&g_color_correct_backend_ticks, 0, 0);
    LONG64 previous_color_correct_backend_samples =
        InterlockedCompareExchange64(&g_color_correct_backend_samples, 0, 0);
    wchar_t log_path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), log_path,
                                            static_cast<DWORD>(std::size(log_path)));
    if (length != 0) {
        wchar_t* slash = wcsrchr(log_path, L'\\');
        if (slash != nullptr) {
            wcscpy_s(slash + 1, std::size(log_path) - static_cast<std::size_t>(slash + 1 - log_path),
                     L"ql_fps_telemetry.log");
        }
    }
    for (;;) {
        Sleep(500);
        LARGE_INTEGER now{};
        if (!QueryPerformanceCounter(&now)) {
            continue;
        }
        const LONG64 elapsed = now.QuadPart - previous_time.QuadPart;
        if (elapsed <= 0) {
            continue;
        }
        const LONG64 presents = InterlockedCompareExchange64(&g_present_count, 0, 0);
        const LONG64 simulation = InterlockedCompareExchange64(&g_simulation_count, 0, 0);
        const LONG64 outer = InterlockedCompareExchange64(&g_outer_loop_count, 0, 0);
        const LONG64 zero = InterlockedCompareExchange64(&g_zero_render_count, 0, 0);
        const LONG64 preview_accept =
            InterlockedCompareExchange64(&g_preview_accept_count, 0, 0);
        const LONG64 preview_nonzero =
            InterlockedCompareExchange64(&g_preview_nonzero_count, 0, 0);
        const LONG64 prediction = InterlockedCompareExchange64(&g_prediction_count, 0, 0);
        const LONG64 scene = InterlockedCompareExchange64(&g_scene_submission_count, 0, 0);
        const LONG64 fresh =
            InterlockedCompareExchange64(&g_fresh_view_submission_count, 0, 0);
        const LONG64 entity_pose_samples =
            InterlockedCompareExchange64(&g_entity_pose_sample_count, 0, 0);
        const LONG64 entity_pose_changes =
            InterlockedCompareExchange64(&g_entity_pose_change_count, 0, 0);
        const LONG64 entity_pose_repeats =
            InterlockedCompareExchange64(&g_entity_pose_repeat_count, 0, 0);
        const LONG64 submitted_pose_samples =
            InterlockedCompareExchange64(&g_submitted_pose_sample_count, 0, 0);
        const LONG64 submitted_pose_changes =
            InterlockedCompareExchange64(&g_submitted_pose_change_count, 0, 0);
        const LONG64 submitted_pose_repeats =
            InterlockedCompareExchange64(&g_submitted_pose_repeat_count, 0, 0);
        const LONG64 highres_interpolation =
            InterlockedCompareExchange64(&g_highres_interpolation_count, 0, 0);
        const LONG64 visible_overlay =
            InterlockedCompareExchange64(&g_visible_overlay_submission_count, 0, 0);
        const LONG64 endframe =
            InterlockedCompareExchange64(&g_endframe_completion_count, 0, 0);
        const LONG64 usercmd =
            InterlockedCompareExchange64(&g_usercmd_publication_count, 0, 0);
        const LONG64 packet = InterlockedCompareExchange64(&g_packet_write_count, 0, 0);
        const LONG64 render_only_ticks =
            InterlockedCompareExchange64(&g_render_only_screen_ticks, 0, 0);
        const LONG64 render_only_samples =
            InterlockedCompareExchange64(&g_render_only_screen_samples, 0, 0);
        const LONG64 positive_ticks =
            InterlockedCompareExchange64(&g_positive_client_frame_ticks, 0, 0);
        const LONG64 positive_samples =
            InterlockedCompareExchange64(&g_positive_client_frame_samples, 0, 0);
        const LONG64 draw_active_ticks =
            InterlockedCompareExchange64(&g_draw_active_frame_ticks, 0, 0);
        const LONG64 draw_active_samples =
            InterlockedCompareExchange64(&g_draw_active_frame_samples, 0, 0);
        const LONG64 draw_pre_active_ticks =
            InterlockedCompareExchange64(&g_draw_pre_active_ticks, 0, 0);
        const LONG64 draw_pre_active_samples =
            InterlockedCompareExchange64(&g_draw_pre_active_samples, 0, 0);
        std::array<LONG64, k_draw_phase_count> draw_phase_ticks{};
        std::array<LONG64, k_draw_phase_count> draw_phase_samples{};
        for (std::size_t phase = 0; phase < k_draw_phase_count; ++phase) {
            draw_phase_ticks[phase] =
                InterlockedCompareExchange64(&g_draw_phase_ticks[phase], 0, 0);
            draw_phase_samples[phase] =
                InterlockedCompareExchange64(&g_draw_phase_samples[phase], 0, 0);
        }
        std::array<LONG64, k_cgame_hotpath_count> cgame_hotpath_ticks{};
        std::array<LONG64, k_cgame_hotpath_count> cgame_hotpath_samples{};
        for (std::size_t path = 0; path < k_cgame_hotpath_count; ++path) {
            cgame_hotpath_ticks[path] =
                InterlockedCompareExchange64(&g_cgame_hotpath_ticks[path], 0, 0);
            cgame_hotpath_samples[path] =
                InterlockedCompareExchange64(&g_cgame_hotpath_samples[path], 0, 0);
        }
        const LONG64 renderer_scene_frontend_ticks =
            InterlockedCompareExchange64(&g_renderer_scene_frontend_ticks, 0, 0);
        const LONG64 renderer_scene_frontend_samples =
            InterlockedCompareExchange64(&g_renderer_scene_frontend_samples, 0, 0);
        const LONG64 re_endframe_ticks =
            InterlockedCompareExchange64(&g_re_endframe_ticks, 0, 0);
        const LONG64 re_endframe_samples =
            InterlockedCompareExchange64(&g_re_endframe_samples, 0, 0);
        const LONG64 glimp_endframe_ticks =
            InterlockedCompareExchange64(&g_glimp_endframe_ticks, 0, 0);
        const LONG64 glimp_endframe_samples =
            InterlockedCompareExchange64(&g_glimp_endframe_samples, 0, 0);
        const LONG64 raster_issued =
            InterlockedCompareExchange64(&g_raster_fingerprint_issued_count, 0, 0);
        const LONG64 raster_ready =
            InterlockedCompareExchange64(&g_raster_fingerprint_ready_count, 0, 0);
        const LONG64 raster_changed =
            InterlockedCompareExchange64(&g_raster_fingerprint_changed_count, 0, 0);
        const LONG64 raster_repeated =
            InterlockedCompareExchange64(&g_raster_fingerprint_repeat_count, 0, 0);
        const LONG64 raster_gaps =
            InterlockedCompareExchange64(&g_raster_fingerprint_gap_count, 0, 0);
        const LONG64 cvar_refresh =
            InterlockedCompareExchange64(&g_cvar_refresh_count, 0, 0);
        const LONG64 cvar_reuse =
            InterlockedCompareExchange64(&g_cvar_reuse_count, 0, 0);
        const LONG64 hud_capture =
            InterlockedCompareExchange64(&g_hud_capture_count, 0, 0);
        const LONG64 hud_replay =
            InterlockedCompareExchange64(&g_hud_replay_count, 0, 0);
        const LONG64 hud_fallback =
            InterlockedCompareExchange64(&g_hud_stock_fallback_count, 0, 0);
        const LONG64 hud_stock_ticks =
            InterlockedCompareExchange64(&g_hud_stock_ticks, 0, 0);
        const LONG64 hud_stock_samples =
            InterlockedCompareExchange64(&g_hud_stock_samples, 0, 0);
        const LONG64 hud_replay_ticks =
            InterlockedCompareExchange64(&g_hud_replay_ticks, 0, 0);
        const LONG64 hud_replay_samples =
            InterlockedCompareExchange64(&g_hud_replay_samples, 0, 0);
        const LONG64 zero_bloom_fast =
            InterlockedCompareExchange64(&g_zero_bloom_fast_count, 0, 0);
        const LONG64 zero_bloom_stock =
            InterlockedCompareExchange64(&g_zero_bloom_stock_count, 0, 0);
        const LONG64 zero_bloom_backend_ticks =
            InterlockedCompareExchange64(&g_zero_bloom_backend_ticks, 0, 0);
        const LONG64 zero_bloom_backend_samples =
            InterlockedCompareExchange64(&g_zero_bloom_backend_samples, 0, 0);
        const LONG64 color_correct_backend_ticks =
            InterlockedCompareExchange64(&g_color_correct_backend_ticks, 0, 0);
        const LONG64 color_correct_backend_samples =
            InterlockedCompareExchange64(&g_color_correct_backend_samples, 0, 0);
        const auto rate = [elapsed, &frequency](const LONG64 current,
                                                const LONG64 previous) noexcept {
            return static_cast<LONG>(((current - previous) * frequency.QuadPart + elapsed / 2) /
                                     elapsed);
        };
        const LONG screen_update_hz = rate(presents, previous_presents);
        const LONG sim_hz = rate(simulation, previous_simulation);
        const LONG outer_hz = rate(outer, previous_outer);
        const LONG zero_hz = rate(zero, previous_zero);
        const LONG preview_hz = rate(preview_accept, previous_preview_accept);
        const LONG preview_nonzero_hz = rate(preview_nonzero, previous_preview_nonzero);
        const LONG prediction_hz = rate(prediction, previous_prediction);
        const LONG scene_hz = rate(scene, previous_scene);
        const LONG fresh_hz = rate(fresh, previous_fresh);
        const LONG entity_pose_sample_hz =
            rate(entity_pose_samples, previous_entity_pose_samples);
        const LONG entity_pose_change_hz =
            rate(entity_pose_changes, previous_entity_pose_changes);
        const LONG entity_pose_repeat_hz =
            rate(entity_pose_repeats, previous_entity_pose_repeats);
        const LONG submitted_pose_sample_hz =
            rate(submitted_pose_samples, previous_submitted_pose_samples);
        const LONG submitted_pose_change_hz =
            rate(submitted_pose_changes, previous_submitted_pose_changes);
        const LONG submitted_pose_repeat_hz =
            rate(submitted_pose_repeats, previous_submitted_pose_repeats);
        const LONG player_scene_body_axis_sample_hz = rate(
            InterlockedCompareExchange64(&g_player_scene_body_axis_sample_count, 0, 0),
            previous_player_scene_body_axis_samples);
        const LONG player_scene_body_axis_change_hz = rate(
            InterlockedCompareExchange64(&g_player_scene_body_axis_change_count, 0, 0),
            previous_player_scene_body_axis_changes);
        const LONG player_scene_body_axis_repeat_hz = rate(
            InterlockedCompareExchange64(&g_player_scene_body_axis_repeat_count, 0, 0),
            previous_player_scene_body_axis_repeats);
        const LONG highres_interpolation_hz =
            rate(highres_interpolation, previous_highres_interpolation);
        const LONG visible_overlay_hz = rate(visible_overlay, previous_visible_overlay);
        const LONG endframe_hz = rate(endframe, previous_endframe);
        const LONG usercmd_hz = rate(usercmd, previous_usercmd);
        const LONG packet_hz = rate(packet, previous_packet);
        const LONG raster_issued_hz = rate(raster_issued, previous_raster_issued);
        const LONG raster_ready_hz = rate(raster_ready, previous_raster_ready);
        const LONG raster_changed_hz = rate(raster_changed, previous_raster_changed);
        const LONG raster_repeat_hz = rate(raster_repeated, previous_raster_repeated);
        const LONG raster_gap_hz = rate(raster_gaps, previous_raster_gaps);
        const LONG cvar_refresh_hz = rate(cvar_refresh, previous_cvar_refresh);
        const LONG cvar_reuse_hz = rate(cvar_reuse, previous_cvar_reuse);
        const LONG hud_capture_hz = rate(hud_capture, previous_hud_capture);
        const LONG hud_replay_hz = rate(hud_replay, previous_hud_replay);
        const LONG hud_fallback_hz = rate(hud_fallback, previous_hud_fallback);
        const LONG zero_bloom_fast_hz =
            rate(zero_bloom_fast, previous_zero_bloom_fast);
        const LONG zero_bloom_stock_hz =
            rate(zero_bloom_stock, previous_zero_bloom_stock);
        const LONG64 render_only_sample_delta =
            render_only_samples - previous_render_only_samples;
        const LONG64 positive_sample_delta = positive_samples - previous_positive_samples;
        const LONG64 draw_active_sample_delta =
            draw_active_samples - previous_draw_active_samples;
        const LONG64 draw_pre_active_sample_delta =
            draw_pre_active_samples - previous_draw_pre_active_samples;
        std::array<LONG64, k_draw_phase_count> draw_phase_sample_deltas{};
        std::array<LONG64, k_draw_phase_count> draw_phase_avg_ns{};
        for (std::size_t phase = 0; phase < k_draw_phase_count; ++phase) {
            draw_phase_sample_deltas[phase] =
                draw_phase_samples[phase] - previous_draw_phase_samples[phase];
            draw_phase_avg_ns[phase] = average_interval_ns(
                draw_phase_ticks[phase] - previous_draw_phase_ticks[phase],
                draw_phase_sample_deltas[phase], frequency.QuadPart);
        }
        std::array<LONG64, k_cgame_hotpath_count> cgame_hotpath_sample_deltas{};
        std::array<LONG64, k_cgame_hotpath_count> cgame_hotpath_avg_ns{};
        for (std::size_t path = 0; path < k_cgame_hotpath_count; ++path) {
            cgame_hotpath_sample_deltas[path] =
                cgame_hotpath_samples[path] - previous_cgame_hotpath_samples[path];
            cgame_hotpath_avg_ns[path] = average_interval_ns(
                cgame_hotpath_ticks[path] - previous_cgame_hotpath_ticks[path],
                cgame_hotpath_sample_deltas[path], frequency.QuadPart);
        }
        const LONG64 renderer_scene_frontend_sample_delta =
            renderer_scene_frontend_samples - previous_renderer_scene_frontend_samples;
        const LONG64 re_endframe_sample_delta =
            re_endframe_samples - previous_re_endframe_samples;
        const LONG64 glimp_endframe_sample_delta =
            glimp_endframe_samples - previous_glimp_endframe_samples;
        const LONG64 hud_stock_sample_delta =
            hud_stock_samples - previous_hud_stock_samples;
        const LONG64 hud_replay_sample_delta =
            hud_replay_samples - previous_hud_replay_samples;
        const LONG64 zero_bloom_backend_sample_delta =
            zero_bloom_backend_samples - previous_zero_bloom_backend_samples;
        const LONG64 color_correct_backend_sample_delta =
            color_correct_backend_samples - previous_color_correct_backend_samples;
        const LONG64 render_only_avg_ns = average_interval_ns(
            render_only_ticks - previous_render_only_ticks, render_only_sample_delta,
            frequency.QuadPart);
        const LONG64 positive_avg_ns = average_interval_ns(
            positive_ticks - previous_positive_ticks, positive_sample_delta,
            frequency.QuadPart);
        const LONG64 draw_active_avg_ns = average_interval_ns(
            draw_active_ticks - previous_draw_active_ticks, draw_active_sample_delta,
            frequency.QuadPart);
        const LONG64 draw_pre_active_avg_ns = average_interval_ns(
            draw_pre_active_ticks - previous_draw_pre_active_ticks,
            draw_pre_active_sample_delta, frequency.QuadPart);
        const LONG64 renderer_scene_frontend_avg_ns = average_interval_ns(
            renderer_scene_frontend_ticks - previous_renderer_scene_frontend_ticks,
            renderer_scene_frontend_sample_delta, frequency.QuadPart);
        const LONG64 re_endframe_avg_ns = average_interval_ns(
            re_endframe_ticks - previous_re_endframe_ticks, re_endframe_sample_delta,
            frequency.QuadPart);
        const LONG64 glimp_endframe_avg_ns = average_interval_ns(
            glimp_endframe_ticks - previous_glimp_endframe_ticks,
            glimp_endframe_sample_delta, frequency.QuadPart);
        const LONG64 hud_stock_avg_ns = average_interval_ns(
            hud_stock_ticks - previous_hud_stock_ticks, hud_stock_sample_delta,
            frequency.QuadPart);
        const LONG64 hud_replay_avg_ns = average_interval_ns(
            hud_replay_ticks - previous_hud_replay_ticks, hud_replay_sample_delta,
            frequency.QuadPart);
        const LONG64 zero_bloom_backend_avg_ns = average_interval_ns(
            zero_bloom_backend_ticks - previous_zero_bloom_backend_ticks,
            zero_bloom_backend_sample_delta, frequency.QuadPart);
        const LONG64 color_correct_backend_avg_ns = average_interval_ns(
            color_correct_backend_ticks - previous_color_correct_backend_ticks,
            color_correct_backend_sample_delta, frequency.QuadPart);
        const LONG fps = endframe_hz;
        InterlockedExchange(&g_measured_present_fps, fps);
        InterlockedExchange(&g_measured_simulation_hz, sim_hz);
        previous_time = now;
        previous_presents = presents;
        previous_simulation = simulation;
        previous_outer = outer;
        previous_zero = zero;
        previous_preview_accept = preview_accept;
        previous_preview_nonzero = preview_nonzero;
        previous_prediction = prediction;
        previous_scene = scene;
        previous_fresh = fresh;
        previous_entity_pose_samples = entity_pose_samples;
        previous_entity_pose_changes = entity_pose_changes;
        previous_entity_pose_repeats = entity_pose_repeats;
        previous_submitted_pose_samples = submitted_pose_samples;
        previous_submitted_pose_changes = submitted_pose_changes;
        previous_submitted_pose_repeats = submitted_pose_repeats;
        previous_player_scene_body_axis_samples =
            InterlockedCompareExchange64(&g_player_scene_body_axis_sample_count, 0, 0);
        previous_player_scene_body_axis_changes =
            InterlockedCompareExchange64(&g_player_scene_body_axis_change_count, 0, 0);
        previous_player_scene_body_axis_repeats =
            InterlockedCompareExchange64(&g_player_scene_body_axis_repeat_count, 0, 0);
        previous_highres_interpolation = highres_interpolation;
        previous_visible_overlay = visible_overlay;
        previous_endframe = endframe;
        previous_usercmd = usercmd;
        previous_packet = packet;
        previous_render_only_ticks = render_only_ticks;
        previous_render_only_samples = render_only_samples;
        previous_positive_ticks = positive_ticks;
        previous_positive_samples = positive_samples;
        previous_draw_active_ticks = draw_active_ticks;
        previous_draw_active_samples = draw_active_samples;
        previous_draw_pre_active_ticks = draw_pre_active_ticks;
        previous_draw_pre_active_samples = draw_pre_active_samples;
        previous_draw_phase_ticks = draw_phase_ticks;
        previous_draw_phase_samples = draw_phase_samples;
        previous_cgame_hotpath_ticks = cgame_hotpath_ticks;
        previous_cgame_hotpath_samples = cgame_hotpath_samples;
        previous_renderer_scene_frontend_ticks = renderer_scene_frontend_ticks;
        previous_renderer_scene_frontend_samples = renderer_scene_frontend_samples;
        previous_re_endframe_ticks = re_endframe_ticks;
        previous_re_endframe_samples = re_endframe_samples;
        previous_glimp_endframe_ticks = glimp_endframe_ticks;
        previous_glimp_endframe_samples = glimp_endframe_samples;
        previous_raster_issued = raster_issued;
        previous_raster_ready = raster_ready;
        previous_raster_changed = raster_changed;
        previous_raster_repeated = raster_repeated;
        previous_raster_gaps = raster_gaps;
        previous_cvar_refresh = cvar_refresh;
        previous_cvar_reuse = cvar_reuse;
        previous_hud_capture = hud_capture;
        previous_hud_replay = hud_replay;
        previous_hud_fallback = hud_fallback;
        previous_hud_stock_ticks = hud_stock_ticks;
        previous_hud_stock_samples = hud_stock_samples;
        previous_hud_replay_ticks = hud_replay_ticks;
        previous_hud_replay_samples = hud_replay_samples;
        previous_zero_bloom_fast = zero_bloom_fast;
        previous_zero_bloom_stock = zero_bloom_stock;
        previous_zero_bloom_backend_ticks = zero_bloom_backend_ticks;
        previous_zero_bloom_backend_samples = zero_bloom_backend_samples;
        previous_color_correct_backend_ticks = color_correct_backend_ticks;
        previous_color_correct_backend_samples = color_correct_backend_samples;

        ql1k::HitregDisplay hitreg{};
        ql1k::HitregDiagnostics hitreg_diagnostics{};
        AcquireSRWLockShared(&g_hitreg_lock);
        hitreg = g_hitreg_state.published();
        hitreg_diagnostics = g_hitreg_state.diagnostics();
        ReleaseSRWLockShared(&g_hitreg_lock);
        const char* const runtime_reason = g_reason.load(std::memory_order_acquire);
        const auto* const smp_active_ptr =
            static_cast<const std::int32_t*>(engine_address(k_engine_smp_active));
        const LONG smp_active = smp_active_ptr == nullptr ? -1 : *smp_active_ptr;
        const auto* const smp_wgl_failures_ptr =
            static_cast<const std::int32_t*>(engine_address(k_engine_smp_wgl_failures));
        const LONG smp_wgl_failures =
            smp_wgl_failures_ptr == nullptr ? -1 : *smp_wgl_failures_ptr;
        const auto* const client_state_ptr =
            static_cast<const std::int32_t*>(engine_address(k_engine_client_state));
        const auto* const key_catchers_ptr =
            static_cast<const std::int32_t*>(engine_address(k_engine_key_catchers));
        const LONG client_state = client_state_ptr == nullptr ? -1 : *client_state_ptr;
        const LONG key_catchers = key_catchers_ptr == nullptr ? -1 : *key_catchers_ptr;
        DWORD_PTR process_affinity{};
        DWORD_PTR system_affinity{};
        const bool affinity_available =
            GetProcessAffinityMask(GetCurrentProcess(), &process_affinity, &system_affinity) != FALSE;

        // This record intentionally carries both immutable hold data and
        // session diagnostics. Never let a future field addition turn
        // telemetry truncation into the CRT invalid-parameter process abort.
        char line[32768]{};
        const int formatted_bytes = _snprintf_s(
            line, sizeof(line), _TRUNCATE,
            "measured_present_fps=%ld screen_update_hz=%ld simulation_hz=%ld outer_loop_hz=%ld "
            "zero_render_hz=%ld preview_accept_hz=%ld preview_nonzero_hz=%ld "
            "prediction_hz=%ld scene_submission_hz=%ld fresh_view_hz=%ld "
            "entity_pose_sample_hz=%ld entity_pose_change_hz=%ld entity_pose_repeat_hz=%ld "
            "submitted_pose_sample_hz=%ld submitted_pose_change_hz=%ld "
            "submitted_pose_repeat_hz=%ld "
            "highres_interpolation_hz=%ld "
            "visible_overlay_hz=%ld endframe_hz=%ld "
            "usercmd_hz=%ld packet_write_hz=%ld preview_chain=%ld "
            "cvar_refresh_hz=%ld cvar_reuse_hz=%ld cvar_refresh_warmed=%ld "
            "cvar_refresh_total=%lld cvar_reuse_total=%lld "
            "hud_replay_configured=%ld hud_capture_hz=%ld hud_replay_hz=%ld "
            "hud_fallback_hz=%ld hud_cache_valid=%ld hud_cache_bytes=%zu "
            "hud_stock_avg_ns=%lld hud_stock_samples=%lld "
            "hud_replay_avg_ns=%lld hud_replay_samples=%lld "
            "hud_capture_total=%lld hud_replay_total=%lld hud_fallback_total=%lld "
            "hud_capture_reject_total=%lld hud_capture_bytes_total=%lld "
            "zero_bloom_fast_configured=%ld zero_bloom_fast_hz=%ld "
            "zero_bloom_stock_hz=%ld zero_bloom_fast_total=%lld "
            "zero_bloom_stock_total=%lld "
            "zero_bloom_backend_avg_ns=%lld zero_bloom_backend_samples=%lld "
            "color_correct_backend_avg_ns=%lld color_correct_backend_samples=%lld "
            "color_correct_identity_configured=%ld "
            "color_correct_identity_fast_total=%lld "
            "color_correct_identity_stock_total=%lld "
            "shadow_mark_cache_configured=%ld shadow_mark_cache_hit_total=%lld "
            "shadow_mark_cache_miss_total=%lld shadow_mark_cache_capture_total=%lld "
            "shadow_mark_cache_replay_poly_total=%lld "
            "shadow_mark_cache_overflow_total=%lld "
            "player_scene_replay_configured=%ld player_scene_bypass_configured=%ld "
            "player_scene_validated=%ld player_scene_failed=%ld "
            "player_scene_validation_streak=%ld "
            "player_scene_mismatch_kind=%ld player_scene_mismatch_index=%ld "
            "player_scene_mismatch_offset=%ld player_scene_mismatch_expected=0x%lX "
            "player_scene_mismatch_actual=0x%lX player_scene_mismatch_source=0x%lX "
            "player_scene_mismatch_delta_x=0x%lX "
            "player_scene_mismatch_delta_y=0x%lX "
            "player_scene_mismatch_delta_z=0x%lX "
            "player_scene_mismatch_ref_type=%ld "
            "player_scene_mismatch_beam_length=0x%lX "
            "player_scene_binding_gun_ref=%ld player_scene_binding_flash_ref=%ld "
            "player_scene_binding_gun_model=%ld player_scene_binding_flash_model=%ld "
            "player_scene_capture_total=%lld player_scene_capture_reject_total=%lld "
            "player_scene_validation_total=%lld player_scene_mismatch_total=%lld "
            "player_scene_replay_total=%lld player_scene_stock_fallback_total=%lld "
            "player_scene_angle_fallback_total=%lld player_scene_beam_replay_total=%lld "
            "player_scene_pose_sample_total=%lld player_scene_pose_change_total=%lld "
            "player_scene_pose_repeat_total=%lld "
            "player_scene_body_axis_sample_hz=%ld "
            "player_scene_body_axis_change_hz=%ld "
            "player_scene_body_axis_repeat_hz=%ld "
            "player_style_fast_configured=%ld player_style_bypass_configured=%ld "
            "player_style_validated=%ld player_style_failed=%ld "
            "player_style_validation_total=%lld player_style_mismatch_total=%lld "
            "player_style_mutation_failure_total=%lld player_style_bypass_total=%lld "
            "render_only_avg_ns=%lld render_only_samples=%lld "
            "positive_frame_avg_ns=%lld positive_frame_samples=%lld "
            "draw_active_avg_ns=%lld draw_active_samples=%lld "
            "draw_pre_active_avg_ns=%lld draw_pre_active_samples=%lld "
            "draw_phase_snapshots_avg_ns=%lld draw_phase_snapshots_samples=%lld "
            "draw_phase_prediction_avg_ns=%lld draw_phase_prediction_samples=%lld "
            "draw_phase_view_avg_ns=%lld draw_phase_view_samples=%lld "
            "draw_phase_entities_avg_ns=%lld draw_phase_entities_samples=%lld "
            "draw_phase_tail_avg_ns=%lld draw_phase_tail_samples=%lld "
            "packet_entities_avg_ns=%lld packet_entities_samples=%lld "
            "player_renderer_avg_ns=%lld player_renderer_samples=%lld "
            "player_shadow_avg_ns=%lld player_shadow_samples=%lld "
            "shadow_impact_avg_ns=%lld shadow_impact_samples=%lld "
            "lightning_avg_ns=%lld lightning_samples=%lld "
            "renderer_scene_frontend_avg_ns=%lld renderer_scene_frontend_samples=%lld "
            "re_endframe_avg_ns=%lld re_endframe_samples=%lld "
            "glimp_endframe_avg_ns=%lld glimp_endframe_samples=%lld "
            "raster_issued_hz=%ld raster_ready_hz=%ld raster_changed_hz=%ld "
            "raster_repeat_hz=%ld raster_gap_hz=%ld raster_active=%ld "
            "raster_pending=%ld raster_failure=%ld "
            "raster_issued_total=%lld raster_ready_total=%lld "
            "raster_changed_total=%lld raster_repeat_total=%lld raster_gap_total=%lld "
            "raster_wait_failure_total=%lld raster_init_failure_total=%lld "
            "smp_active=%ld smp_wgl_failures=%ld force_smp_patch=%ld "
            "smp_persistent_context=%ld smp_persistent_state=%ld "
            "client_state=%ld key_catchers=0x%lX "
            "ui_fullscreen=%ld "
            "smp_persistent_gameplay_eligible=%ld "
            "smp_ui_suspend_total=%lld smp_ui_resume_total=%lld "
            "smp_ui_context_return_total=%lld "
            "smp_ui_context_return_failure_total=%lld "
            "smp_context_sync_pending=%ld smp_context_sync_total=%lld "
            "smp_context_main_acquire_total=%lld smp_context_main_release_total=%lld "
            "smp_context_renderer_acquire_total=%lld "
            "smp_context_renderer_release_total=%lld "
            "smp_context_transfer_failure_total=%lld "
            "renderer_registration_complete=%ld smp_persistent_disarm_pending=%ld "
            "smp_registration_suspended=%ld "
            "renderer_begin_registration_total=%lld renderer_end_registration_total=%lld "
            "renderer_shutdown_total=%lld smp_restart_rearm_total=%lld "
            "smp_worker_spawn_total=%lld smp_worker_join_total=%lld "
            "smp_worker_join_failure_total=%lld smp_worker_cleanup_total=%lld "
            "smp_shutdown_worker_join_result=%ld "
            "smp_post_activation_sync_total=%lld "
            "smp_event_seq=%lld renderer_begin_seq=%lld renderer_end_seq=%lld "
            "renderer_shutdown_begin_seq=%lld renderer_shutdown_end_seq=%lld "
            "smp_activation_seq=%lld smp_persistent_activation_seq=%lld "
            "smp_post_activation_sync_seq=%lld smp_font_capture_seq=%lld "
            "smp_font_replay_seq=%lld "
            "smp_registration_suspend_total=%lld smp_registration_resume_total=%lld "
            "smp_synchronous=%ld smp_single_buffer=%ld smp_synchronous_wait_total=%lld "
            "smp_copy_fpu=%ld smp_fpu_apply_total=%lld "
            "smp_main_thread_backend=%ld smp_late_activation=%ld smp_late_state=%ld "
            "smp_late_failure=%ld "
            "smp_font_upload_capture_total=%lld smp_font_upload_replay_total=%lld "
            "smp_font_upload_drop_total=%lld smp_font_upload_byte_total=%lld "
            "smp_font_upload_invalid_argument_total=%lld "
            "smp_font_upload_invalid_frame_total=%lld "
            "smp_font_upload_invalid_layout_total=%lld "
            "smp_font_upload_allocation_failure_total=%lld "
            "smp_font_upload_queue_full_total=%lld "
            "smp_font_upload_zero_texture_skip_total=%lld "
            "smp_font_upload_shutdown_discard_total=%lld "
            "smp_main_x87=0x%lX smp_renderer_x87_before=0x%lX smp_renderer_x87_after=0x%lX "
            "smp_main_mxcsr=0x%lX smp_renderer_mxcsr_before=0x%lX "
            "smp_renderer_mxcsr_after=0x%lX "
            "process_affinity=0x%llX "
            "preview_attempt_total=%lld preview_accept_total=%lld preview_nonzero_total=%lld "
            "preview_skip_total=%lld "
            "preview_restore_total=%lld preview_restore_failure_total=%lld "
            "overlay_apply_total=%lld overlay_restore_total=%lld "
            "scene_submission_total=%lld fresh_view_total=%lld visible_overlay_total=%lld "
            "submitted_pose_player_count=%ld submitted_pose_sample_total=%lld "
            "submitted_pose_change_total=%lld submitted_pose_repeat_total=%lld "
            "entity_pose_number=%ld entity_pose_x_milli=%ld entity_pose_y_milli=%ld "
            "entity_pose_z_milli=%ld entity_pose_track_switch_total=%lld "
            "highres_interpolation_total=%lld entity_pose_restore_total=%lld "
            "entity_pose_restore_failure_total=%lld render_fractional_us=%ld "
            "frame_interpolation_ppm=%ld entity_pose_transaction_entities=%ld "
            "pending_mouse_x_last=%ld pending_mouse_y_last=%ld "
            "engine_committed_pitch_mdeg=%ld engine_committed_yaw_mdeg=%ld "
            "preview_pitch_delta_mdeg=%ld preview_yaw_delta_mdeg=%ld "
            "refdef_angle_pitch_mdeg=%ld refdef_angle_yaw_mdeg=%ld "
            "submitted_pitch_mdeg=%ld submitted_yaw_mdeg=%ld endframe_total=%lld "
            "usercmd_total=%lld packet_write_total=%lld status=%ld reason=%s "
            "client_accuracy_kind=%s client_accuracy_percent_hundredths=%lu "
            "client_accuracy_hits=%lu client_accuracy_opportunities=%lu "
            "hitreg_generation=%llu hitreg_kind=%s hitreg_percent_hundredths=%lu "
            "hitreg_client=%lu "
            "hitreg_server=%lu hitreg_samples=%lu hitreg_fire_events=%lu "
            "hitreg_hold_start=%ld hitreg_hold_end=%ld "
            "hitreg_hold_trace_total=%lu hitreg_hold_trace_player=%lu "
            "hitreg_hold_trace_world=%lu hitreg_hold_trace_none=%lu "
            "hitreg_hold_trace_other=%lu hitreg_hold_trace_client_player=%lu "
            "hitreg_pending_holds=%zu "
            "hitreg_pending_ticks=%zu hitreg_pending_client=%lu hitreg_hold_open=%u "
            "hitreg_unavailable_reason=%s hitreg_session_feedback_seen=%lu "
            "hitreg_session_feedback_assigned=%lu hitreg_session_feedback_unowned=%lu "
            "hitreg_session_trace_total=%lld hitreg_session_trace_player=%lld "
            "hitreg_session_trace_world=%lld hitreg_session_trace_none=%lld "
            "hitreg_session_trace_other=%lld hitreg_session_trace_last=%ld\r\n",
            fps, screen_update_hz, sim_hz, outer_hz, zero_hz, preview_hz,
            preview_nonzero_hz, prediction_hz, scene_hz, fresh_hz,
            entity_pose_sample_hz, entity_pose_change_hz, entity_pose_repeat_hz,
            submitted_pose_sample_hz, submitted_pose_change_hz,
            submitted_pose_repeat_hz,
            highres_interpolation_hz,
            visible_overlay_hz,
            endframe_hz, usercmd_hz, packet_hz,
            InterlockedCompareExchange(&g_preview_chain_armed, 0, 0),
            cvar_refresh_hz, cvar_reuse_hz,
            InterlockedCompareExchange(&g_cvar_refresh_warmed, 0, 0),
            static_cast<long long>(cvar_refresh),
            static_cast<long long>(cvar_reuse),
            InterlockedCompareExchange(&g_config_hud_replay, 0, 0),
            hud_capture_hz, hud_replay_hz, hud_fallback_hz,
            InterlockedCompareExchange(&g_hud_cache_valid, 0, 0),
            g_hud_replay_cache.size,
            static_cast<long long>(hud_stock_avg_ns),
            static_cast<long long>(hud_stock_sample_delta),
            static_cast<long long>(hud_replay_avg_ns),
            static_cast<long long>(hud_replay_sample_delta),
            static_cast<long long>(hud_capture),
            static_cast<long long>(hud_replay),
            static_cast<long long>(hud_fallback),
            static_cast<long long>(InterlockedCompareExchange64(
                &g_hud_capture_reject_count, 0, 0)),
            static_cast<long long>(InterlockedCompareExchange64(
                &g_hud_capture_bytes, 0, 0)),
            InterlockedCompareExchange(&g_config_zero_bloom_fast_path, 0, 0),
            zero_bloom_fast_hz,
            zero_bloom_stock_hz,
            static_cast<long long>(zero_bloom_fast),
            static_cast<long long>(zero_bloom_stock),
            static_cast<long long>(zero_bloom_backend_avg_ns),
            static_cast<long long>(zero_bloom_backend_sample_delta),
            static_cast<long long>(color_correct_backend_avg_ns),
            static_cast<long long>(color_correct_backend_sample_delta),
            InterlockedCompareExchange(
                &g_config_color_correct_identity_fast_path, 0, 0),
            static_cast<long long>(InterlockedCompareExchange64(
                &g_color_correct_identity_fast_count, 0, 0)),
            static_cast<long long>(InterlockedCompareExchange64(
                &g_color_correct_identity_stock_count, 0, 0)),
            InterlockedCompareExchange(&g_config_shadow_mark_cache, 0, 0),
            static_cast<long long>(InterlockedCompareExchange64(
                &g_shadow_mark_cache_hit_count, 0, 0)),
            static_cast<long long>(InterlockedCompareExchange64(
                &g_shadow_mark_cache_miss_count, 0, 0)),
            static_cast<long long>(InterlockedCompareExchange64(
                &g_shadow_mark_cache_capture_count, 0, 0)),
            static_cast<long long>(InterlockedCompareExchange64(
                &g_shadow_mark_cache_replay_poly_count, 0, 0)),
            static_cast<long long>(InterlockedCompareExchange64(
                &g_shadow_mark_cache_overflow_count, 0, 0)),
            InterlockedCompareExchange(&g_config_player_scene_replay, 0, 0),
            InterlockedCompareExchange(&g_config_player_scene_bypass, 0, 0),
            InterlockedCompareExchange(&g_player_scene_validated, 0, 0),
            InterlockedCompareExchange(&g_player_scene_failed, 0, 0),
            InterlockedCompareExchange(&g_player_scene_validation_streak, 0, 0),
            InterlockedCompareExchange(&g_player_scene_mismatch_kind, 0, 0),
            InterlockedCompareExchange(&g_player_scene_mismatch_index, 0, 0),
            InterlockedCompareExchange(&g_player_scene_mismatch_offset, 0, 0),
            InterlockedCompareExchange(&g_player_scene_mismatch_expected, 0, 0),
            InterlockedCompareExchange(&g_player_scene_mismatch_actual, 0, 0),
            InterlockedCompareExchange(&g_player_scene_mismatch_source, 0, 0),
            InterlockedCompareExchange(&g_player_scene_mismatch_delta_x, 0, 0),
            InterlockedCompareExchange(&g_player_scene_mismatch_delta_y, 0, 0),
            InterlockedCompareExchange(&g_player_scene_mismatch_delta_z, 0, 0),
            InterlockedCompareExchange(&g_player_scene_mismatch_ref_type, 0, 0),
            InterlockedCompareExchange(&g_player_scene_mismatch_beam_length, 0, 0),
            InterlockedCompareExchange(&g_player_scene_binding_gun_ref, 0, 0),
            InterlockedCompareExchange(&g_player_scene_binding_flash_ref, 0, 0),
            InterlockedCompareExchange(&g_player_scene_binding_gun_model, 0, 0),
            InterlockedCompareExchange(&g_player_scene_binding_flash_model, 0, 0),
            static_cast<long long>(InterlockedCompareExchange64(
                &g_player_scene_capture_count, 0, 0)),
            static_cast<long long>(InterlockedCompareExchange64(
                &g_player_scene_capture_reject_count, 0, 0)),
            static_cast<long long>(InterlockedCompareExchange64(
                &g_player_scene_validation_count, 0, 0)),
            static_cast<long long>(InterlockedCompareExchange64(
                &g_player_scene_mismatch_count, 0, 0)),
            static_cast<long long>(InterlockedCompareExchange64(
                &g_player_scene_replay_count, 0, 0)),
            static_cast<long long>(InterlockedCompareExchange64(
                &g_player_scene_stock_fallback_count, 0, 0)),
            static_cast<long long>(InterlockedCompareExchange64(
                &g_player_scene_angle_fallback_count, 0, 0)),
            static_cast<long long>(InterlockedCompareExchange64(
                &g_player_scene_beam_replay_count, 0, 0)),
            static_cast<long long>(InterlockedCompareExchange64(
                &g_player_scene_pose_sample_count, 0, 0)),
            static_cast<long long>(InterlockedCompareExchange64(
                &g_player_scene_pose_change_count, 0, 0)),
            static_cast<long long>(InterlockedCompareExchange64(
                &g_player_scene_pose_repeat_count, 0, 0)),
            player_scene_body_axis_sample_hz,
            player_scene_body_axis_change_hz,
            player_scene_body_axis_repeat_hz,
            InterlockedCompareExchange(&g_config_player_style_fast_path, 0, 0),
            InterlockedCompareExchange(&g_config_player_style_bypass, 0, 0),
            InterlockedCompareExchange(&g_player_style_validated, 0, 0),
            InterlockedCompareExchange(&g_player_style_failed, 0, 0),
            static_cast<long long>(InterlockedCompareExchange64(
                &g_player_style_validation_count, 0, 0)),
            static_cast<long long>(InterlockedCompareExchange64(
                &g_player_style_mismatch_count, 0, 0)),
            static_cast<long long>(InterlockedCompareExchange64(
                &g_player_style_mutation_failure_count, 0, 0)),
            static_cast<long long>(InterlockedCompareExchange64(
                &g_player_style_bypass_count, 0, 0)),
            static_cast<long long>(render_only_avg_ns),
            static_cast<long long>(render_only_sample_delta),
            static_cast<long long>(positive_avg_ns),
            static_cast<long long>(positive_sample_delta),
            static_cast<long long>(draw_active_avg_ns),
            static_cast<long long>(draw_active_sample_delta),
            static_cast<long long>(draw_pre_active_avg_ns),
            static_cast<long long>(draw_pre_active_sample_delta),
            static_cast<long long>(draw_phase_avg_ns[0]),
            static_cast<long long>(draw_phase_sample_deltas[0]),
            static_cast<long long>(draw_phase_avg_ns[1]),
            static_cast<long long>(draw_phase_sample_deltas[1]),
            static_cast<long long>(draw_phase_avg_ns[2]),
            static_cast<long long>(draw_phase_sample_deltas[2]),
            static_cast<long long>(draw_phase_avg_ns[3]),
            static_cast<long long>(draw_phase_sample_deltas[3]),
            static_cast<long long>(draw_phase_avg_ns[4]),
            static_cast<long long>(draw_phase_sample_deltas[4]),
            static_cast<long long>(cgame_hotpath_avg_ns[k_hotpath_packet_entities]),
            static_cast<long long>(
                cgame_hotpath_sample_deltas[k_hotpath_packet_entities]),
            static_cast<long long>(cgame_hotpath_avg_ns[k_hotpath_player_renderer]),
            static_cast<long long>(
                cgame_hotpath_sample_deltas[k_hotpath_player_renderer]),
            static_cast<long long>(cgame_hotpath_avg_ns[k_hotpath_player_shadow]),
            static_cast<long long>(
                cgame_hotpath_sample_deltas[k_hotpath_player_shadow]),
            static_cast<long long>(cgame_hotpath_avg_ns[k_hotpath_shadow_impact]),
            static_cast<long long>(
                cgame_hotpath_sample_deltas[k_hotpath_shadow_impact]),
            static_cast<long long>(cgame_hotpath_avg_ns[k_hotpath_lightning]),
            static_cast<long long>(cgame_hotpath_sample_deltas[k_hotpath_lightning]),
            static_cast<long long>(renderer_scene_frontend_avg_ns),
            static_cast<long long>(renderer_scene_frontend_sample_delta),
            static_cast<long long>(re_endframe_avg_ns),
            static_cast<long long>(re_endframe_sample_delta),
            static_cast<long long>(glimp_endframe_avg_ns),
            static_cast<long long>(glimp_endframe_sample_delta),
            raster_issued_hz, raster_ready_hz, raster_changed_hz,
            raster_repeat_hz, raster_gap_hz,
            InterlockedCompareExchange(&g_raster_fingerprint_active, 0, 0),
            InterlockedCompareExchange(&g_raster_fingerprint_pending, 0, 0),
            InterlockedCompareExchange(&g_raster_fingerprint_failure_code, 0, 0),
            static_cast<long long>(raster_issued),
            static_cast<long long>(raster_ready),
            static_cast<long long>(raster_changed),
            static_cast<long long>(raster_repeated),
            static_cast<long long>(raster_gaps),
            static_cast<long long>(InterlockedCompareExchange64(
                &g_raster_fingerprint_wait_failure_count, 0, 0)),
            static_cast<long long>(InterlockedCompareExchange64(
                &g_raster_fingerprint_init_failure_count, 0, 0)),
            smp_active,
            smp_wgl_failures,
            InterlockedCompareExchange(&g_force_smp_patch_state, 0, 0),
            InterlockedCompareExchange(&g_config_smp_persistent_context, 0, 0),
            InterlockedCompareExchange(&g_smp_persistent_context_state, 0, 0),
            client_state, key_catchers,
            InterlockedCompareExchange(&g_ui_fullscreen, 0, 0),
            InterlockedCompareExchange(&g_smp_persistent_gameplay_eligible, 0, 0),
            static_cast<long long>(
                InterlockedCompareExchange64(&g_smp_ui_suspend_count, 0, 0)),
            static_cast<long long>(
                InterlockedCompareExchange64(&g_smp_ui_resume_count, 0, 0)),
            static_cast<long long>(InterlockedCompareExchange64(
                &g_smp_ui_context_return_count, 0, 0)),
            static_cast<long long>(InterlockedCompareExchange64(
                &g_smp_ui_context_return_failure_count, 0, 0)),
            InterlockedCompareExchange(&g_smp_context_sync_requests, 0, 0),
            static_cast<long long>(
                InterlockedCompareExchange64(&g_smp_context_sync_count, 0, 0)),
            static_cast<long long>(InterlockedCompareExchange64(
                &g_smp_context_main_acquire_count, 0, 0)),
            static_cast<long long>(InterlockedCompareExchange64(
                &g_smp_context_main_release_count, 0, 0)),
            static_cast<long long>(InterlockedCompareExchange64(
                &g_smp_context_renderer_acquire_count, 0, 0)),
            static_cast<long long>(InterlockedCompareExchange64(
                &g_smp_context_renderer_release_count, 0, 0)),
            static_cast<long long>(InterlockedCompareExchange64(
                &g_smp_context_transfer_failure_count, 0, 0)),
            InterlockedCompareExchange(&g_renderer_registration_complete, 0, 0),
            InterlockedCompareExchange(&g_smp_persistent_disarm_requested, 0, 0),
            InterlockedCompareExchange(&g_smp_registration_suspended, 0, 0),
            static_cast<long long>(InterlockedCompareExchange64(
                &g_renderer_begin_registration_count, 0, 0)),
            static_cast<long long>(InterlockedCompareExchange64(
                &g_renderer_end_registration_count, 0, 0)),
            static_cast<long long>(InterlockedCompareExchange64(
                &g_renderer_shutdown_count, 0, 0)),
            static_cast<long long>(InterlockedCompareExchange64(
                &g_smp_restart_rearm_count, 0, 0)),
            static_cast<long long>(InterlockedCompareExchange64(
                &g_smp_worker_spawn_count, 0, 0)),
            static_cast<long long>(InterlockedCompareExchange64(
                &g_smp_worker_join_count, 0, 0)),
            static_cast<long long>(InterlockedCompareExchange64(
                &g_smp_worker_join_failure_count, 0, 0)),
            static_cast<long long>(InterlockedCompareExchange64(
                &g_smp_worker_handle_cleanup_count, 0, 0)),
            InterlockedCompareExchange(&g_smp_shutdown_worker_join_result, 0, 0),
            static_cast<long long>(InterlockedCompareExchange64(
                &g_smp_post_activation_sync_count, 0, 0)),
            static_cast<long long>(InterlockedCompareExchange64(
                &g_smp_lifecycle_event_sequence, 0, 0)),
            static_cast<long long>(InterlockedCompareExchange64(
                &g_renderer_begin_registration_event_sequence, 0, 0)),
            static_cast<long long>(InterlockedCompareExchange64(
                &g_renderer_end_registration_event_sequence, 0, 0)),
            static_cast<long long>(InterlockedCompareExchange64(
                &g_renderer_shutdown_begin_event_sequence, 0, 0)),
            static_cast<long long>(InterlockedCompareExchange64(
                &g_renderer_shutdown_end_event_sequence, 0, 0)),
            static_cast<long long>(InterlockedCompareExchange64(
                &g_smp_activation_event_sequence, 0, 0)),
            static_cast<long long>(InterlockedCompareExchange64(
                &g_smp_persistent_activation_event_sequence, 0, 0)),
            static_cast<long long>(InterlockedCompareExchange64(
                &g_smp_post_activation_sync_event_sequence, 0, 0)),
            static_cast<long long>(InterlockedCompareExchange64(
                &g_smp_font_capture_event_sequence, 0, 0)),
            static_cast<long long>(InterlockedCompareExchange64(
                &g_smp_font_replay_event_sequence, 0, 0)),
            static_cast<long long>(InterlockedCompareExchange64(
                &g_smp_registration_suspend_count, 0, 0)),
            static_cast<long long>(InterlockedCompareExchange64(
                &g_smp_registration_resume_count, 0, 0)),
            InterlockedCompareExchange(&g_config_smp_synchronous, 0, 0),
            InterlockedCompareExchange(&g_config_smp_single_buffer, 0, 0),
            static_cast<long long>(
                InterlockedCompareExchange64(&g_smp_synchronous_wait_count, 0, 0)),
            InterlockedCompareExchange(&g_config_smp_copy_fpu, 0, 0),
            static_cast<long long>(
                InterlockedCompareExchange64(&g_smp_fpu_apply_count, 0, 0)),
            InterlockedCompareExchange(&g_config_smp_main_thread_backend, 0, 0),
            InterlockedCompareExchange(&g_config_smp_late_activation, 0, 0),
            InterlockedCompareExchange(&g_smp_late_activation_state, 0, 0),
            InterlockedCompareExchange(&g_smp_late_failure_code, 0, 0),
            static_cast<long long>(
                InterlockedCompareExchange64(&g_smp_font_upload_capture_count, 0, 0)),
            static_cast<long long>(
                InterlockedCompareExchange64(&g_smp_font_upload_replay_count, 0, 0)),
            static_cast<long long>(
                InterlockedCompareExchange64(&g_smp_font_upload_drop_count, 0, 0)),
            static_cast<long long>(
                InterlockedCompareExchange64(&g_smp_font_upload_byte_count, 0, 0)),
            static_cast<long long>(InterlockedCompareExchange64(
                &g_smp_font_upload_invalid_argument_count, 0, 0)),
            static_cast<long long>(InterlockedCompareExchange64(
                &g_smp_font_upload_invalid_frame_count, 0, 0)),
            static_cast<long long>(InterlockedCompareExchange64(
                &g_smp_font_upload_invalid_layout_count, 0, 0)),
            static_cast<long long>(InterlockedCompareExchange64(
                &g_smp_font_upload_allocation_failure_count, 0, 0)),
            static_cast<long long>(InterlockedCompareExchange64(
                &g_smp_font_upload_queue_full_count, 0, 0)),
            static_cast<long long>(InterlockedCompareExchange64(
                &g_smp_font_upload_zero_texture_skip_count, 0, 0)),
            static_cast<long long>(InterlockedCompareExchange64(
                &g_smp_font_upload_shutdown_discard_count, 0, 0)),
            InterlockedCompareExchange(&g_smp_main_x87_control, 0, 0),
            InterlockedCompareExchange(&g_smp_renderer_x87_before, 0, 0),
            InterlockedCompareExchange(&g_smp_renderer_x87_after, 0, 0),
            InterlockedCompareExchange(&g_smp_main_mxcsr, 0, 0),
            InterlockedCompareExchange(&g_smp_renderer_mxcsr_before, 0, 0),
            InterlockedCompareExchange(&g_smp_renderer_mxcsr_after, 0, 0),
            static_cast<unsigned long long>(affinity_available ? process_affinity : 0),
            static_cast<long long>(InterlockedCompareExchange64(&g_preview_attempt_count, 0, 0)),
            static_cast<long long>(preview_accept),
            static_cast<long long>(preview_nonzero),
            static_cast<long long>(InterlockedCompareExchange64(&g_preview_skip_count, 0, 0)),
            static_cast<long long>(InterlockedCompareExchange64(&g_preview_restore_count, 0, 0)),
            static_cast<long long>(
                InterlockedCompareExchange64(&g_preview_restore_failure_count, 0, 0)),
            static_cast<long long>(InterlockedCompareExchange64(&g_overlay_apply_count, 0, 0)),
            static_cast<long long>(InterlockedCompareExchange64(&g_overlay_restore_count, 0, 0)),
            static_cast<long long>(scene), static_cast<long long>(fresh),
            static_cast<long long>(visible_overlay),
            InterlockedCompareExchange(&g_submitted_pose_player_count, 0, 0),
            static_cast<long long>(submitted_pose_samples),
            static_cast<long long>(submitted_pose_changes),
            static_cast<long long>(submitted_pose_repeats),
            InterlockedCompareExchange(&g_entity_pose_last_number, 0, 0),
            InterlockedCompareExchange(&g_entity_pose_x_milli, 0, 0),
            InterlockedCompareExchange(&g_entity_pose_y_milli, 0, 0),
            InterlockedCompareExchange(&g_entity_pose_z_milli, 0, 0),
            static_cast<long long>(
                InterlockedCompareExchange64(&g_entity_pose_track_switch_count, 0, 0)),
            static_cast<long long>(highres_interpolation),
            static_cast<long long>(
                InterlockedCompareExchange64(&g_entity_pose_restore_count, 0, 0)),
            static_cast<long long>(InterlockedCompareExchange64(
                &g_entity_pose_restore_failure_count, 0, 0)),
            InterlockedCompareExchange(&g_render_fractional_us, 0, 0),
            InterlockedCompareExchange(&g_frame_interpolation_ppm, 0, 0),
            InterlockedCompareExchange(&g_entity_pose_transaction_entities, 0, 0),
            InterlockedCompareExchange(&g_pending_mouse_x_last, 0, 0),
            InterlockedCompareExchange(&g_pending_mouse_y_last, 0, 0),
            InterlockedCompareExchange(&g_engine_committed_pitch_mdeg, 0, 0),
            InterlockedCompareExchange(&g_engine_committed_yaw_mdeg, 0, 0),
            InterlockedCompareExchange(&g_preview_pitch_delta_mdeg, 0, 0),
            InterlockedCompareExchange(&g_preview_yaw_delta_mdeg, 0, 0),
            InterlockedCompareExchange(&g_refdef_angle_pitch_mdeg, 0, 0),
            InterlockedCompareExchange(&g_refdef_angle_yaw_mdeg, 0, 0),
            InterlockedCompareExchange(&g_submitted_pitch_mdeg, 0, 0),
            InterlockedCompareExchange(&g_submitted_yaw_mdeg, 0, 0),
            static_cast<long long>(endframe), static_cast<long long>(usercmd),
            static_cast<long long>(packet), InterlockedCompareExchange(&g_status, 0, 0),
            runtime_reason == nullptr ? "unknown" : runtime_reason,
            client_accuracy_kind_name(hitreg.client_accuracy_kind),
            static_cast<unsigned long>(hitreg.client_accuracy_percent_hundredths),
            static_cast<unsigned long>(hitreg.client_accuracy_hits),
            static_cast<unsigned long>(hitreg.client_accuracy_opportunities),
            static_cast<unsigned long long>(hitreg.generation), hitreg_kind_name(hitreg.kind),
            static_cast<unsigned long>(hitreg.percent_hundredths),
            static_cast<unsigned long>(hitreg.client_hits),
            static_cast<unsigned long>(hitreg.server_hits),
            static_cast<unsigned long>(hitreg.samples),
            static_cast<unsigned long>(hitreg.fire_events), hitreg.start_time, hitreg.end_time,
            static_cast<unsigned long>(hitreg.trace_total),
            static_cast<unsigned long>(hitreg.trace_player),
            static_cast<unsigned long>(hitreg.trace_world),
            static_cast<unsigned long>(hitreg.trace_none),
            static_cast<unsigned long>(hitreg.trace_other),
            static_cast<unsigned long>(hitreg.trace_client_player),
            hitreg_diagnostics.pending_holds,
            hitreg_diagnostics.pending_server_ticks,
            static_cast<unsigned long>(hitreg_diagnostics.pending_client_samples),
            hitreg_diagnostics.hold_open ? 1U : 0U,
            hitreg_reason_name(hitreg.unavailable_reason),
            static_cast<unsigned long>(hitreg_diagnostics.feedback_positive_seen),
            static_cast<unsigned long>(hitreg_diagnostics.feedback_positive_assigned),
            static_cast<unsigned long>(hitreg_diagnostics.feedback_positive_unowned),
            static_cast<long long>(InterlockedCompareExchange64(&g_hitreg_trace_total, 0, 0)),
            static_cast<long long>(InterlockedCompareExchange64(&g_hitreg_trace_player, 0, 0)),
            static_cast<long long>(InterlockedCompareExchange64(&g_hitreg_trace_world, 0, 0)),
            static_cast<long long>(InterlockedCompareExchange64(&g_hitreg_trace_none, 0, 0)),
            static_cast<long long>(InterlockedCompareExchange64(&g_hitreg_trace_other, 0, 0)),
            InterlockedCompareExchange(&g_hitreg_trace_last_entity, 0, 0));
        const std::size_t bytes = formatted_bytes >= 0
                                      ? (std::min)(
                                            static_cast<std::size_t>(formatted_bytes),
                                            sizeof(line) - 1U)
                                      : strnlen_s(line, sizeof(line));
        if (length != 0 && bytes != 0) {
            const HANDLE file = CreateFileW(log_path, GENERIC_WRITE,
                                            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file != INVALID_HANDLE_VALUE) {
                DWORD written{};
                WriteFile(file, line, static_cast<DWORD>(bytes), &written, nullptr);
                CloseHandle(file);
            }
        }
        HWND window{};
        EnumWindows(&find_game_window, reinterpret_cast<LPARAM>(&window));
        if (window != nullptr) {
            char title[128]{};
            sprintf_s(title, "Quake Live | render %ld | fresh %ld | sim %ld Hz", fps,
                      fresh_hz, sim_hz);
            SetWindowTextA(window, title);
        }
    }
}

void ensure_telemetry_worker() {
    if (InterlockedCompareExchange(&g_telemetry_started, 1, 0) != 0) {
        return;
    }
    const HANDLE thread = CreateThread(nullptr, 0, &telemetry_worker, nullptr, 0, nullptr);
    if (thread != nullptr) {
        CloseHandle(thread);
    } else {
        InterlockedExchange(&g_telemetry_started, 0);
    }
}

std::wstring module_path(HMODULE module) {
    wchar_t buffer[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(module, buffer,
                                             static_cast<DWORD>(std::size(buffer)));
    return length == 0 ? std::wstring{} : std::wstring(buffer, buffer + length);
}

bool hash_file(const std::wstring& path, std::array<char, 65>& output) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_HASH_HANDLE hash{};
    PUCHAR object{};
    DWORD object_size{};
    DWORD result_size{};
    bool success = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0;
    if (success) {
        success = BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                                     reinterpret_cast<PUCHAR>(&object_size), sizeof(object_size),
                                     &result_size, 0) == 0;
    }
    if (success) {
        object = new (std::nothrow) UCHAR[object_size];
        success = object != nullptr && BCryptCreateHash(algorithm, &hash, object, object_size,
                                                        nullptr, 0, 0) == 0;
    }
    std::array<UCHAR, 64 * 1024> buffer{};
    while (success) {
        DWORD read{};
        if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr)) {
            success = false;
            break;
        }
        if (read == 0) {
            break;
        }
        success = BCryptHashData(hash, buffer.data(), read, 0) == 0;
    }
    std::array<UCHAR, 32> digest{};
    if (success) {
        success = BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) == 0;
    }
    if (hash != nullptr) {
        BCryptDestroyHash(hash);
    }
    if (algorithm != nullptr) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
    }
    delete[] object;
    CloseHandle(file);
    if (!success) {
        return false;
    }
    static constexpr char hex[] = "0123456789ABCDEF";
    output.fill('\0');
    std::size_t output_index = 0;
    for (const UCHAR byte : digest) {
        output[output_index++] = hex[byte >> 4];
        output[output_index++] = hex[byte & 0x0F];
    }
    return true;
}

bool configured_enabled() {
    wchar_t value[8]{};
    const std::wstring module = module_path(reinterpret_cast<HMODULE>(&__ImageBase));
    const std::size_t separator = module.find_last_of(L"\\/");
    const std::wstring config_path =
        (separator == std::wstring::npos ? std::wstring{} : module.substr(0, separator)) +
        L"\\ql_fps.cfg";
    const DWORD length = GetPrivateProfileStringW(L"ql_fps", L"enabled", L"0", value,
                                                  static_cast<DWORD>(std::size(value)),
                                                  config_path.c_str());
    return length == 1 && value[0] == L'1';
}

bool configured_experimental() {
    wchar_t value[8]{};
    const std::wstring module = module_path(reinterpret_cast<HMODULE>(&__ImageBase));
    const std::size_t separator = module.find_last_of(L"\\/");
    const std::wstring config_path =
        (separator == std::wstring::npos ? std::wstring{} : module.substr(0, separator)) +
        L"\\ql_fps.cfg";
    const DWORD length = GetPrivateProfileStringW(L"ql_fps", L"experimental_runtime", L"0", value,
                                                  static_cast<DWORD>(std::size(value)),
                                                  config_path.c_str());
    return length == 1 && value[0] == L'1';
}

bool configured_force_smp() {
    wchar_t value[8]{};
    const std::wstring module = module_path(reinterpret_cast<HMODULE>(&__ImageBase));
    const std::size_t separator = module.find_last_of(L"\\/");
    const std::wstring config_path =
        (separator == std::wstring::npos ? std::wstring{} : module.substr(0, separator)) +
        L"\\ql_fps.cfg";
    const DWORD length = GetPrivateProfileStringW(L"ql_fps", L"force_smp", L"0", value,
                                                  static_cast<DWORD>(std::size(value)),
                                                  config_path.c_str());
    return length == 1 && value[0] == L'1';
}

bool configured_smp_synchronous() {
    wchar_t value[8]{};
    const std::wstring module = module_path(reinterpret_cast<HMODULE>(&__ImageBase));
    const std::size_t separator = module.find_last_of(L"\\/");
    const std::wstring config_path =
        (separator == std::wstring::npos ? std::wstring{} : module.substr(0, separator)) +
        L"\\ql_fps.cfg";
    const DWORD length = GetPrivateProfileStringW(
        L"ql_fps", L"smp_synchronous", L"0", value,
        static_cast<DWORD>(std::size(value)), config_path.c_str());
    return length == 1 && value[0] == L'1';
}

bool configured_smp_single_buffer() {
    wchar_t value[8]{};
    const std::wstring module = module_path(reinterpret_cast<HMODULE>(&__ImageBase));
    const std::size_t separator = module.find_last_of(L"\\/");
    const std::wstring config_path =
        (separator == std::wstring::npos ? std::wstring{} : module.substr(0, separator)) +
        L"\\ql_fps.cfg";
    const DWORD length = GetPrivateProfileStringW(
        L"ql_fps", L"smp_single_buffer", L"0", value,
        static_cast<DWORD>(std::size(value)), config_path.c_str());
    return length == 1 && value[0] == L'1';
}

bool configured_smp_copy_fpu() {
    wchar_t value[8]{};
    const std::wstring module = module_path(reinterpret_cast<HMODULE>(&__ImageBase));
    const std::size_t separator = module.find_last_of(L"\\/");
    const std::wstring config_path =
        (separator == std::wstring::npos ? std::wstring{} : module.substr(0, separator)) +
        L"\\ql_fps.cfg";
    const DWORD length = GetPrivateProfileStringW(
        L"ql_fps", L"smp_copy_fpu", L"0", value,
        static_cast<DWORD>(std::size(value)), config_path.c_str());
    return length == 1 && value[0] == L'1';
}

bool configured_smp_main_thread_backend() {
    wchar_t value[8]{};
    const std::wstring module = module_path(reinterpret_cast<HMODULE>(&__ImageBase));
    const std::size_t separator = module.find_last_of(L"\\/");
    const std::wstring config_path =
        (separator == std::wstring::npos ? std::wstring{} : module.substr(0, separator)) +
        L"\\ql_fps.cfg";
    const DWORD length = GetPrivateProfileStringW(
        L"ql_fps", L"smp_main_thread_backend", L"0", value,
        static_cast<DWORD>(std::size(value)), config_path.c_str());
    return length == 1 && value[0] == L'1';
}

bool configured_smp_late_activation() {
    wchar_t value[8]{};
    const std::wstring module = module_path(reinterpret_cast<HMODULE>(&__ImageBase));
    const std::size_t separator = module.find_last_of(L"\\/");
    const std::wstring config_path =
        (separator == std::wstring::npos ? std::wstring{} : module.substr(0, separator)) +
        L"\\ql_fps.cfg";
    const DWORD length = GetPrivateProfileStringW(
        L"ql_fps", L"smp_late_activation", L"0", value,
        static_cast<DWORD>(std::size(value)), config_path.c_str());
    return length == 1 && value[0] == L'1';
}

bool configured_smp_persistent_context() {
    wchar_t value[8]{};
    const std::wstring module = module_path(reinterpret_cast<HMODULE>(&__ImageBase));
    const std::size_t separator = module.find_last_of(L"\\/");
    const std::wstring config_path =
        (separator == std::wstring::npos ? std::wstring{} : module.substr(0, separator)) +
        L"\\ql_fps.cfg";
    const DWORD length = GetPrivateProfileStringW(
        L"ql_fps", L"smp_persistent_context", L"0", value,
        static_cast<DWORD>(std::size(value)), config_path.c_str());
    return length == 1 && value[0] == L'1';
}

bool exact_hash(std::string_view value, std::string_view expected) {
    if (value.size() != expected.size()) {
        return false;
    }
    for (std::size_t index = 0; index < value.size(); ++index) {
        const auto upper = [](char character) {
            return character >= 'a' && character <= 'f'
                       ? static_cast<char>(character - 'a' + 'A')
                       : character;
        };
        if (upper(value[index]) != expected[index]) {
            return false;
        }
    }
    return true;
}

template <std::size_t N>
bool signature_matches(const void* address, const std::array<std::uint8_t, N>& expected) {
    if (address == nullptr) {
        return false;
    }
    const auto* actual = static_cast<const std::uint8_t*>(address);
    for (std::size_t index = 0; index < N; ++index) {
        if (actual[index] != expected[index]) {
            return false;
        }
    }
    return true;
}

template <std::size_t N>
bool relocated_absolute_signature_matches(const void* address,
                                          const std::array<std::uint8_t, N>& preferred,
                                          std::size_t operand_offset,
                                          const void* relocated_target) {
    if (operand_offset > N || N - operand_offset < sizeof(std::uint32_t) ||
        relocated_target == nullptr) {
        return false;
    }
    auto expected = preferred;
    const auto encoded = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(relocated_target));
    std::memcpy(expected.data() + operand_offset, &encoded, sizeof(encoded));
    return signature_matches(address, expected);
}

void* engine_address(std::uintptr_t preferred_va) {
    if (g_engine == nullptr || preferred_va < k_engine_preferred_base) {
        return nullptr;
    }
    return reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(g_engine) +
                                   (preferred_va - k_engine_preferred_base));
}

struct SmpHandoffBindings {
    HANDLE renderer_idle{};
    HANDLE command_ready{};
    HANDLE render_completed{};
    void** command_data{};

    [[nodiscard]] bool valid() const noexcept {
        return renderer_idle != nullptr && command_ready != nullptr &&
               render_completed != nullptr && command_data != nullptr;
    }
};

SmpHandoffBindings smp_handoff_bindings() noexcept {
    const auto idle = static_cast<HANDLE*>(
        engine_address(k_engine_smp_renderer_idle_event));
    const auto command = static_cast<HANDLE*>(
        engine_address(k_engine_smp_command_event));
    const auto completed = static_cast<HANDLE*>(
        engine_address(k_engine_smp_render_completed_event));
    return SmpHandoffBindings{
        idle == nullptr ? nullptr : *idle,
        command == nullptr ? nullptr : *command,
        completed == nullptr ? nullptr : *completed,
        static_cast<void**>(engine_address(k_engine_smp_command_data))};
}

struct WglContextBindings {
    WglMakeCurrentFn make_current{};
    HDC device_context{};
    HGLRC rendering_context{};

    [[nodiscard]] bool valid() const noexcept {
        return make_current != nullptr && device_context != nullptr &&
               rendering_context != nullptr;
    }
};

WglContextBindings wgl_context_bindings() noexcept {
    const auto make_current_slot = reinterpret_cast<WglMakeCurrentFn*>(
        engine_address(k_engine_wgl_make_current_slot));
    const auto device_context = static_cast<HDC*>(engine_address(k_engine_wgl_hdc));
    const auto rendering_context = static_cast<HGLRC*>(engine_address(k_engine_wgl_context));
    return WglContextBindings{
        make_current_slot == nullptr ? nullptr : *make_current_slot,
        device_context == nullptr ? nullptr : *device_context,
        rendering_context == nullptr ? nullptr : *rendering_context};
}

LONG persistent_smp_context_state() noexcept {
    return InterlockedCompareExchange(&g_smp_persistent_context_state, 0, 0);
}

bool persistent_smp_context_protocol_active() noexcept {
    return InterlockedCompareExchange(&g_config_smp_persistent_context, 0, 0) != 0 &&
           ql1k::smp_context_protocol_active(persistent_smp_context_state());
}

LONG64 record_smp_lifecycle_event(volatile LONG64* const destination) noexcept {
    const LONG64 sequence =
        InterlockedIncrement64(&g_smp_lifecycle_event_sequence);
    if (destination != nullptr) {
        InterlockedExchange64(destination, sequence);
    }
    return sequence;
}

bool transition_smp_context(const LONG expected_state, const LONG next_state,
                            const bool bind_context, volatile LONG64* const counter,
                            const char* const failure_reason) noexcept {
    if (persistent_smp_context_state() != expected_state) {
        return false;
    }
    const WglContextBindings bindings = wgl_context_bindings();
    if (!bindings.valid() ||
        !bindings.make_current(bindings.device_context,
                               bind_context ? bindings.rendering_context : nullptr)) {
        InterlockedIncrement64(&g_smp_context_transfer_failure_count);
        auto* const engine_failures = static_cast<volatile LONG*>(
            engine_address(k_engine_smp_wgl_failures));
        if (engine_failures != nullptr) {
            InterlockedIncrement(engine_failures);
        }
        g_reason.store(failure_reason, std::memory_order_release);
        InterlockedExchange(&g_smp_persistent_context_state,
                            static_cast<LONG>(ql1k::SmpContextState::fault));
        return false;
    }
    if (InterlockedCompareExchange(&g_smp_persistent_context_state, next_state,
                                   expected_state) != expected_state) {
        InterlockedIncrement64(&g_smp_context_transfer_failure_count);
        g_reason.store("smp_context_state_race", std::memory_order_release);
        InterlockedExchange(&g_smp_persistent_context_state,
                            static_cast<LONG>(ql1k::SmpContextState::fault));
        return false;
    }
    if (counter != nullptr) {
        InterlockedIncrement64(counter);
    }
    return true;
}

void __cdecl smp_frontend_sleep_hook() {
    const auto stock = g_stock_smp_frontend_sleep;
    if (!persistent_smp_context_protocol_active()) {
        if (stock != nullptr) {
            stock();
        }
        return;
    }

    const SmpHandoffBindings handoff = smp_handoff_bindings();
    if (!handoff.valid() ||
        WaitForSingleObject(handoff.renderer_idle, INFINITE) != WAIT_OBJECT_0) {
        g_reason.store("smp_persistent_front_wait_failed", std::memory_order_release);
        InterlockedExchange(&g_smp_persistent_context_state, -1);
        return;
    }
    const LONG state = persistent_smp_context_state();
    if (ql1k::main_should_acquire_after_sync(state)) {
        if (!transition_smp_context(
                state, static_cast<LONG>(ql1k::SmpContextState::main_owned), true,
                &g_smp_context_main_acquire_count,
                "smp_context_main_acquire_failed") &&
            stock != nullptr) {
            stock();
        }
    }
}

void __cdecl smp_wake_hook(void* data) {
    const auto stock = g_stock_smp_wake;
    if (stock == nullptr) {
        return;
    }
    if (data != nullptr &&
        InterlockedCompareExchange(&g_config_smp_copy_fpu, 0, 0) != 0) {
        unsigned int control{};
        if (_controlfp_s(&control, 0U, 0U) == 0) {
            InterlockedExchange(&g_smp_main_x87_control, static_cast<LONG>(control));
            InterlockedExchange(&g_smp_main_mxcsr, static_cast<LONG>(_mm_getcsr()));
            InterlockedExchange(&g_smp_fpu_ready, 1);
        }
    }
    if (persistent_smp_context_protocol_active()) {
        const SmpHandoffBindings handoff = smp_handoff_bindings();
        if (!handoff.valid()) {
            g_reason.store("smp_persistent_wake_bindings_failed", std::memory_order_release);
            InterlockedExchange(&g_smp_persistent_context_state, -1);
            return;
        }
        const LONG state = persistent_smp_context_state();
        if (data != nullptr && ql1k::main_should_release_for_render(state) &&
            !transition_smp_context(
                state, static_cast<LONG>(ql1k::SmpContextState::released_to_renderer),
                false, &g_smp_context_main_release_count,
                "smp_context_main_release_failed")) {
            stock(data);
            return;
        }
        const bool wait_for_ack = ql1k::persistent_wake_requires_ack(
            data == nullptr,
            InterlockedCompareExchange(&g_config_smp_synchronous, 0, 0) != 0);
        const bool wait_for_completion = ql1k::persistent_wake_requires_completion(
            data == nullptr,
            InterlockedCompareExchange(&g_config_smp_synchronous, 0, 0) != 0);
        // Producer ownership of renderer_idle closes the manual-reset-event
        // race: once the prior FrontEndSleep observed idle, the next command
        // makes it non-idle before publication. The worker sets it again only
        // after the backend has consumed that command and re-enters sleep.
        if (!ResetEvent(handoff.renderer_idle)) {
            g_reason.store("smp_persistent_idle_reset_failed", std::memory_order_release);
            InterlockedExchange(&g_smp_persistent_context_state, -1);
            return;
        }
        *handoff.command_data = data;
        // render_completed is the stock worker-pickup acknowledgement. Actual
        // backend completion is renderer_idle becoming signaled when the
        // worker re-enters RendererSleep after executing this command list.
        const bool acknowledgement_ready =
            !wait_for_ack || ResetEvent(handoff.render_completed);
        const bool command_published = acknowledgement_ready &&
                                       SetEvent(handoff.command_ready);
        const bool command_picked_up =
            !wait_for_ack ||
            (command_published &&
             WaitForSingleObject(handoff.render_completed, INFINITE) == WAIT_OBJECT_0);
        const bool command_completed =
            !wait_for_completion ||
            (command_picked_up &&
             WaitForSingleObject(handoff.renderer_idle, INFINITE) == WAIT_OBJECT_0);
        if (!command_published || !command_picked_up || !command_completed) {
            (void)SetEvent(handoff.renderer_idle);
            g_reason.store("smp_persistent_wake_failed", std::memory_order_release);
            InterlockedExchange(&g_smp_persistent_context_state, -1);
        }
        if (wait_for_completion && command_completed) {
            InterlockedIncrement64(&g_smp_synchronous_wait_count);
        }
        if (data == nullptr) {
            const LONG completed_state = persistent_smp_context_state();
            if (ql1k::main_should_acquire_after_sync(completed_state)) {
                (void)transition_smp_context(
                    completed_state,
                    static_cast<LONG>(ql1k::SmpContextState::main_owned), true,
                    &g_smp_context_main_acquire_count,
                    "smp_context_shutdown_main_acquire_failed");
            }
            if (persistent_smp_context_state() ==
                static_cast<LONG>(ql1k::SmpContextState::main_owned)) {
                InterlockedExchange(&g_smp_persistent_context_state,
                                    static_cast<LONG>(ql1k::SmpContextState::stock));
            }
        }
        return;
    }

    stock(data);

    // A null command terminates the render thread. It does not return through
    // RendererSleep and therefore never signals the completed event.
    if (data == nullptr ||
        InterlockedCompareExchange(&g_config_smp_synchronous, 0, 0) == 0) {
        return;
    }
    const auto front_end_sleep = reinterpret_cast<SmpFrontEndSleepFn>(
        engine_address(k_engine_smp_frontend_sleep));
    if (front_end_sleep == nullptr) {
        return;
    }
    front_end_sleep();
    InterlockedIncrement64(&g_smp_synchronous_wait_count);
}

void replay_font_uploads_for_commands(const int* commands) noexcept;

void* __cdecl smp_renderer_sleep_hook() {
    const auto stock = g_stock_smp_renderer_sleep;
    if (stock == nullptr) {
        return nullptr;
    }
    void* data = nullptr;
    if (persistent_smp_context_protocol_active()) {
        const LONG initial_state = persistent_smp_context_state();
        const LONG pending_syncs =
            InterlockedCompareExchange(&g_smp_context_sync_requests, 0, 0);
        if (ql1k::renderer_should_release_for_sync(initial_state, pending_syncs)) {
            (void)transition_smp_context(
                initial_state,
                static_cast<LONG>(ql1k::SmpContextState::released_to_main), false,
                &g_smp_context_renderer_release_count,
                "smp_context_renderer_release_failed");
        }
        const SmpHandoffBindings handoff = smp_handoff_bindings();
        if (!handoff.valid() || !SetEvent(handoff.renderer_idle) ||
            WaitForSingleObject(handoff.command_ready, INFINITE) != WAIT_OBJECT_0 ||
            !ResetEvent(handoff.command_ready)) {
            g_reason.store("smp_persistent_renderer_wait_failed", std::memory_order_release);
            InterlockedExchange(&g_smp_persistent_context_state, -1);
            return nullptr;
        }
        data = *handoff.command_data;
        const LONG command_state = persistent_smp_context_state();
        if (data != nullptr &&
            ql1k::renderer_should_acquire_for_render(command_state)) {
            (void)transition_smp_context(
                command_state,
                static_cast<LONG>(ql1k::SmpContextState::renderer_owned), true,
                &g_smp_context_renderer_acquire_count,
                "smp_context_renderer_acquire_failed");
        }
        if (data == nullptr &&
            persistent_smp_context_state() ==
                static_cast<LONG>(ql1k::SmpContextState::renderer_owned)) {
            (void)transition_smp_context(
                static_cast<LONG>(ql1k::SmpContextState::renderer_owned),
                static_cast<LONG>(ql1k::SmpContextState::released_to_main), false,
                &g_smp_context_renderer_release_count,
                "smp_context_shutdown_renderer_release_failed");
        }
        if (data != nullptr &&
            InterlockedCompareExchange(&g_smp_persistent_disarm_requested, 0, 0) != 0 &&
            persistent_smp_context_state() ==
                static_cast<LONG>(ql1k::SmpContextState::renderer_owned)) {
            InterlockedExchange(&g_smp_persistent_context_state,
                                static_cast<LONG>(ql1k::SmpContextState::stock));
            InterlockedExchange(&g_smp_persistent_disarm_requested, 0);
        }
        if (!SetEvent(handoff.render_completed)) {
            g_reason.store("smp_persistent_renderer_signal_failed", std::memory_order_release);
            InterlockedExchange(&g_smp_persistent_context_state, -1);
            return nullptr;
        }
    } else {
        data = stock();
    }
    if (data != nullptr &&
        InterlockedCompareExchange(&g_smp_late_activation_state, 0, 0) == 1) {
        replay_font_uploads_for_commands(static_cast<const int*>(data));
        if (InterlockedCompareExchange(&g_config_smp_persistent_context, 0, 0) != 0 &&
            InterlockedCompareExchange(
                &g_smp_persistent_gameplay_eligible, 0, 0) != 0 &&
            InterlockedCompareExchange(&g_renderer_registration_complete, 0, 0) != 0 &&
            InterlockedCompareExchange(&g_smp_persistent_disarm_requested, 0, 0) == 0 &&
            InterlockedCompareExchange(
                &g_smp_persistent_context_state,
                static_cast<LONG>(ql1k::SmpContextState::renderer_owned),
                static_cast<LONG>(ql1k::SmpContextState::stock)) ==
                static_cast<LONG>(ql1k::SmpContextState::stock)) {
            // The stock first handoff transferred the WGL context to this
            // render thread. Every subsequent handoff can now retain it.
            (void)record_smp_lifecycle_event(
                &g_smp_persistent_activation_event_sequence);
        }
    }
    if (data == nullptr || InterlockedCompareExchange(&g_config_smp_copy_fpu, 0, 0) == 0 ||
        InterlockedCompareExchange(&g_smp_fpu_ready, 0, 0) == 0) {
        return data;
    }

    unsigned int x87_before{};
    (void)_controlfp_s(&x87_before, 0U, 0U);
    const unsigned int mxcsr_before = _mm_getcsr();
    const unsigned int desired_x87 = static_cast<unsigned int>(
        InterlockedCompareExchange(&g_smp_main_x87_control, 0, 0));
    const unsigned int desired_mxcsr = static_cast<unsigned int>(
        InterlockedCompareExchange(&g_smp_main_mxcsr, 0, 0));
    unsigned int ignored{};
    constexpr unsigned int k_control_mask =
        _MCW_DN | _MCW_EM | _MCW_IC | _MCW_RC | _MCW_PC;
    (void)_controlfp_s(&ignored, desired_x87, k_control_mask);
    _mm_setcsr(desired_mxcsr);
    unsigned int x87_after{};
    (void)_controlfp_s(&x87_after, 0U, 0U);

    InterlockedExchange(&g_smp_renderer_x87_before, static_cast<LONG>(x87_before));
    InterlockedExchange(&g_smp_renderer_x87_after, static_cast<LONG>(x87_after));
    InterlockedExchange(&g_smp_renderer_mxcsr_before, static_cast<LONG>(mxcsr_before));
    InterlockedExchange(&g_smp_renderer_mxcsr_after, static_cast<LONG>(_mm_getcsr()));
    InterlockedIncrement64(&g_smp_fpu_apply_count);
    return data;
}

void __cdecl renderer_frontend_sync_hook() {
    const auto stock = g_stock_renderer_frontend_sync;
    if (stock == nullptr) {
        return;
    }
    if (!persistent_smp_context_protocol_active()) {
        stock();
        return;
    }

    if (InterlockedCompareExchange(&g_smp_late_activation_state, 0, 0) == 1) {
        InterlockedIncrement64(&g_smp_post_activation_sync_count);
        (void)record_smp_lifecycle_event(
            &g_smp_post_activation_sync_event_sequence);
    }
    InterlockedIncrement(&g_smp_context_sync_requests);
    InterlockedIncrement64(&g_smp_context_sync_count);
    stock();
    const LONG remaining = InterlockedDecrement(&g_smp_context_sync_requests);
    if (remaining < 0) {
        InterlockedExchange(&g_smp_context_sync_requests, 0);
        InterlockedIncrement64(&g_smp_context_transfer_failure_count);
        g_reason.store("smp_context_sync_request_underflow", std::memory_order_release);
        InterlockedExchange(&g_smp_persistent_context_state,
                            static_cast<LONG>(ql1k::SmpContextState::fault));
    }
}

bool ui_fullscreen_now() noexcept {
    const auto* const ui_loaded =
        static_cast<const std::int32_t*>(engine_address(k_engine_ui_loaded));
    auto* const ui_interface_slot =
        static_cast<std::uint8_t**>(engine_address(k_engine_ui_interface));
    if (ui_loaded == nullptr || *ui_loaded == 0 || ui_interface_slot == nullptr ||
        *ui_interface_slot == nullptr) {
        return false;
    }
    auto* const is_fullscreen_slot = reinterpret_cast<UiIsFullscreenFn*>(
        *ui_interface_slot + 0x14U);
    const UiIsFullscreenFn is_fullscreen = *is_fullscreen_slot;
    return is_fullscreen != nullptr && is_fullscreen() != 0;
}

bool persistent_context_gameplay_eligible_now() noexcept {
    const auto* const client_state =
        static_cast<const std::int32_t*>(engine_address(k_engine_client_state));
    const auto* const key_catchers =
        static_cast<const std::int32_t*>(engine_address(k_engine_key_catchers));
    const bool ui_fullscreen = ui_fullscreen_now();
    InterlockedExchange(&g_ui_fullscreen, ui_fullscreen ? 1 : 0);
    return client_state != nullptr && key_catchers != nullptr &&
           ql1k::persistent_context_gameplay_eligible(*client_state,
                                                      *key_catchers,
                                                      ui_fullscreen);
}

bool reconcile_persistent_context_for_screen() noexcept {
    if (InterlockedCompareExchange(&g_config_smp_persistent_context, 0, 0) == 0) {
        return true;
    }

    const LONG eligible = persistent_context_gameplay_eligible_now() ? 1 : 0;
    const LONG previous =
        InterlockedExchange(&g_smp_persistent_gameplay_eligible, eligible);
    if (eligible != 0) {
        if (previous == 0) {
            InterlockedIncrement64(&g_smp_ui_resume_count);
        }
        return true;
    }
    if (previous != 0) {
        InterlockedIncrement64(&g_smp_ui_suspend_count);
    }

    const LONG state = persistent_smp_context_state();
    if (!ql1k::smp_context_protocol_active(state)) {
        return state != static_cast<LONG>(ql1k::SmpContextState::fault);
    }

    // Quake Live's UI/Awesomium path can update renderer resources on the
    // main thread. Drain the worker through the exact stock synchronization
    // seam, bind WGL to main, then restore stock per-frame SMP handoff before
    // any menu or settings composition begins.
    renderer_frontend_sync_hook();
    const LONG synchronized_state = persistent_smp_context_state();
    if (synchronized_state == static_cast<LONG>(ql1k::SmpContextState::stock)) {
        return true;
    }
    if (synchronized_state ==
            static_cast<LONG>(ql1k::SmpContextState::main_owned) &&
        InterlockedCompareExchange(
            &g_smp_persistent_context_state,
            static_cast<LONG>(ql1k::SmpContextState::stock),
            synchronized_state) == synchronized_state) {
        InterlockedIncrement64(&g_smp_ui_context_return_count);
        return true;
    }

    InterlockedIncrement64(&g_smp_ui_context_return_failure_count);
    InterlockedIncrement64(&g_smp_context_transfer_failure_count);
    g_reason.store("smp_ui_context_return_failed", std::memory_order_release);
    InterlockedExchange(&g_smp_persistent_context_state,
                        static_cast<LONG>(ql1k::SmpContextState::fault));
    return false;
}

void __cdecl screen_update_hook() {
    const bool runtime_armed =
        InterlockedCompareExchange(&g_runtime_armed, 0, 0) != 0;
    const bool context_reconciled =
        !runtime_armed || reconcile_persistent_context_for_screen();
    if (!ql1k::screen_render_allowed(runtime_armed, context_reconciled)) {
        // Fail closed: a UI frame must never execute GL/Awesomium work until
        // the main thread has proven ownership of the WGL context.  Dropping
        // this screen is recoverable; rendering without a context reproduces
        // the persistent white-menu failure and can corrupt renderer state.
        return;
    }
    const auto stock = g_present;
    if (stock != nullptr) {
        stock();
    }
}

std::size_t discard_deferred_font_uploads_after_shutdown() noexcept {
    const HANDLE heap = GetProcessHeap();
    std::size_t discarded = 0;
    AcquireSRWLockExclusive(&g_font_upload_lock);
    for (std::size_t root = 0; root < g_deferred_font_uploads.size(); ++root) {
        auto& queue = g_deferred_font_uploads[root];
        for (std::size_t index = 0; index < queue.count; ++index) {
            auto& record = queue.records[index];
            if (record.packed_pixels != nullptr) {
                HeapFree(heap, 0, record.packed_pixels);
                ++discarded;
            }
            record = {};
        }
        queue.count = 0;
        InterlockedExchange(&g_font_upload_pending[root], 0);
    }
    ReleaseSRWLockExclusive(&g_font_upload_lock);
    if (discarded != 0U) {
        InterlockedExchangeAdd64(
            &g_smp_font_upload_shutdown_discard_count,
            static_cast<LONG64>(discarded));
    }
    return discarded;
}

void __cdecl renderer_begin_registration_hook(void* const gl_config) {
    advance_renderer_epoch();
    InterlockedExchange(&g_renderer_registration_complete, 0);
    InterlockedIncrement64(&g_renderer_begin_registration_count);
    (void)record_smp_lifecycle_event(
        &g_renderer_begin_registration_event_sequence);

    auto* const smp_active =
        static_cast<std::int32_t*>(engine_address(k_engine_smp_active));
    auto** const cvar_slot =
        static_cast<std::uint8_t**>(engine_address(k_engine_smp_cvar_slot));
    if (smp_active != nullptr && *smp_active != 0 && cvar_slot != nullptr &&
        *cvar_slot != nullptr) {
        // Registration performs synchronous GL uploads and renderer-global
        // resets. Drain the existing worker and keep its context on the main
        // thread, then execute the entire registration interval serially.
        renderer_frontend_sync_hook();
        const bool persistent_ready =
            InterlockedCompareExchange(&g_config_smp_persistent_context, 0, 0) == 0 ||
            persistent_smp_context_state() ==
                static_cast<LONG>(ql1k::SmpContextState::main_owned);
        if (!persistent_ready) {
            InterlockedIncrement64(&g_smp_context_transfer_failure_count);
            g_reason.store("renderer_begin_registration_context_not_main",
                           std::memory_order_release);
        } else {
            constexpr std::size_t k_cvar_integer_offset = 0x30U;
            auto* const cvar_integer = reinterpret_cast<std::int32_t*>(
                *cvar_slot + k_cvar_integer_offset);
            *cvar_integer = 0;
            *smp_active = 0;
            InterlockedExchange(&g_smp_persistent_disarm_requested, 0);
            InterlockedExchange(&g_smp_registration_suspended, 1);
            InterlockedIncrement64(&g_smp_registration_suspend_count);
        }
    }

    const auto stock = g_stock_renderer_begin_registration;
    if (stock != nullptr) {
        stock(gl_config);
    }
}

void __cdecl renderer_end_registration_hook() {
    const auto stock = g_stock_renderer_end_registration;
    if (stock == nullptr) {
        return;
    }
    stock();
    InterlockedIncrement64(&g_renderer_end_registration_count);
    InterlockedExchange(&g_renderer_registration_complete, 1);
    (void)record_smp_lifecycle_event(
        &g_renderer_end_registration_event_sequence);
}

void __cdecl renderer_shutdown_hook(const int destroy_window) {
    advance_renderer_epoch();
    const LONG previous_late_state =
        InterlockedCompareExchange(&g_smp_late_activation_state, 0, 0);
    InterlockedIncrement64(&g_renderer_shutdown_count);
    (void)record_smp_lifecycle_event(
        &g_renderer_shutdown_begin_event_sequence);
    InterlockedExchange(&g_smp_shutdown_worker_join_result, 0);

    // Block any draw-side reactivation while stock drains the final command
    // list and sends the null command that terminates the render thread.
    InterlockedExchange(&g_renderer_registration_complete, 0);
    InterlockedExchange(&g_smp_persistent_gameplay_eligible, 0);
    const auto stock = g_stock_renderer_shutdown;
    if (stock != nullptr) {
        stock(destroy_window);
    }

    // Stock shutdown has drained and joined the renderer worker. The WGL
    // context may already be gone, so abandon names rather than issuing GL
    // deletes against an invalid context; context destruction owns cleanup.
    abandon_raster_fingerprint_state();

    (void)discard_deferred_font_uploads_after_shutdown();
    auto* const smp_active =
        static_cast<std::int32_t*>(engine_address(k_engine_smp_active));
    const LONG persistent_state = persistent_smp_context_state();
    const bool shutdown_clean =
        smp_active != nullptr && *smp_active == 0 &&
        persistent_state == static_cast<LONG>(ql1k::SmpContextState::stock) &&
        (previous_late_state != 1 ||
         InterlockedCompareExchange(
             &g_smp_shutdown_worker_join_result, 0, 0) == 1);

    InterlockedExchange(&g_smp_context_sync_requests, 0);
    InterlockedExchange(&g_smp_persistent_disarm_requested, 0);
    InterlockedExchange(&g_smp_registration_suspended, 0);
    InterlockedExchange(&g_smp_fpu_ready, 0);
    if (shutdown_clean) {
        InterlockedExchange(&g_smp_persistent_context_state,
                            static_cast<LONG>(ql1k::SmpContextState::stock));
        InterlockedExchange(&g_smp_late_activation_state, 0);
        InterlockedExchange(&g_smp_late_failure_code, 0);
        if (previous_late_state == 1) {
            InterlockedIncrement64(&g_smp_restart_rearm_count);
        }
    } else {
        InterlockedIncrement64(&g_smp_context_transfer_failure_count);
        InterlockedExchange(&g_smp_persistent_context_state,
                            static_cast<LONG>(ql1k::SmpContextState::fault));
        InterlockedExchange(&g_smp_late_failure_code, 30);
        InterlockedExchange(&g_smp_late_activation_state, -2);
        g_reason.store("smp_renderer_shutdown_incomplete", std::memory_order_release);
    }
    (void)record_smp_lifecycle_event(
        &g_renderer_shutdown_end_event_sequence);
}

bool install_smp_renderer_sleep_hook() noexcept {
    const bool copy_fpu =
        InterlockedCompareExchange(&g_config_smp_copy_fpu, 0, 0) != 0;
    const bool deferred_font_upload =
        InterlockedCompareExchange(&g_config_smp_late_activation, 0, 0) != 0;
    if (!copy_fpu && !deferred_font_upload) {
        return true;
    }
    if (InterlockedCompareExchange(&g_config_force_smp, 0, 0) == 0) {
        g_reason.store("smp_copy_fpu_requires_force_smp", std::memory_order_release);
        return false;
    }
    if (!relocated_absolute_signature_matches(
            engine_address(k_engine_smp_renderer_sleep), k_smp_renderer_sleep_signature, 1U,
            engine_address(k_engine_wgl_hdc))) {
        g_reason.store("smp_renderer_sleep_signature_mismatch", std::memory_order_release);
        return false;
    }
    auto result = safetyhook::InlineHook::create(
        engine_address(k_engine_smp_renderer_sleep), &smp_renderer_sleep_hook,
        safetyhook::InlineHook::StartDisabled);
    if (!result) {
        g_reason.store("smp_renderer_sleep_hook_create_failed", std::memory_order_release);
        return false;
    }
    auto* const hook = new (std::nothrow) safetyhook::InlineHook(std::move(*result));
    if (hook == nullptr) {
        g_reason.store("smp_renderer_sleep_hook_allocation_failed", std::memory_order_release);
        return false;
    }
    g_smp_renderer_sleep = hook;
    g_stock_smp_renderer_sleep = hook->original<SmpRendererSleepFn>();
    if (g_stock_smp_renderer_sleep == nullptr || !hook->enable().has_value()) {
        g_reason.store("smp_renderer_sleep_hook_enable_failed", std::memory_order_release);
        return false;
    }
    return true;
}

bool install_smp_context_lease_hook() noexcept {
    if (InterlockedCompareExchange(&g_config_smp_persistent_context, 0, 0) == 0) {
        return true;
    }
    if (!relocated_absolute_signature_matches(
            engine_address(k_engine_renderer_frontend_sync),
            k_renderer_frontend_sync_signature, 2U,
            engine_address(k_engine_renderer_registered))) {
        g_reason.store("renderer_frontend_sync_signature_mismatch",
                       std::memory_order_release);
        return false;
    }
    auto result = safetyhook::InlineHook::create(
        engine_address(k_engine_renderer_frontend_sync), &renderer_frontend_sync_hook,
        safetyhook::InlineHook::StartDisabled);
    if (!result) {
        g_reason.store("renderer_frontend_sync_hook_create_failed",
                       std::memory_order_release);
        return false;
    }
    auto* const hook = new (std::nothrow) safetyhook::InlineHook(std::move(*result));
    if (hook == nullptr) {
        g_reason.store("renderer_frontend_sync_hook_allocation_failed",
                       std::memory_order_release);
        return false;
    }
    g_renderer_frontend_sync = hook;
    g_stock_renderer_frontend_sync = hook->original<RendererFrontEndSyncFn>();
    if (g_stock_renderer_frontend_sync == nullptr || !hook->enable().has_value()) {
        g_reason.store("renderer_frontend_sync_hook_enable_failed",
                       std::memory_order_release);
        return false;
    }
    return true;
}

bool install_renderer_registration_hooks() noexcept {
    if (InterlockedCompareExchange(&g_config_smp_late_activation, 0, 0) == 0) {
        return true;
    }
    if (!signature_matches(engine_address(k_engine_renderer_begin_registration),
                           k_renderer_begin_registration_signature) ||
        !signature_matches(engine_address(k_engine_renderer_end_registration),
                           k_renderer_end_registration_signature) ||
        !signature_matches(engine_address(k_engine_renderer_shutdown),
                           k_renderer_shutdown_signature)) {
        g_reason.store("renderer_registration_signature_mismatch",
                       std::memory_order_release);
        return false;
    }

    auto begin_result = safetyhook::InlineHook::create(
        engine_address(k_engine_renderer_begin_registration),
        &renderer_begin_registration_hook, safetyhook::InlineHook::StartDisabled);
    auto end_result = safetyhook::InlineHook::create(
        engine_address(k_engine_renderer_end_registration),
        &renderer_end_registration_hook, safetyhook::InlineHook::StartDisabled);
    auto shutdown_result = safetyhook::InlineHook::create(
        engine_address(k_engine_renderer_shutdown),
        &renderer_shutdown_hook, safetyhook::InlineHook::StartDisabled);
    if (!begin_result || !end_result || !shutdown_result) {
        g_reason.store("renderer_registration_hook_create_failed",
                       std::memory_order_release);
        return false;
    }

    auto* const begin =
        new (std::nothrow) safetyhook::InlineHook(std::move(*begin_result));
    auto* const end = new (std::nothrow) safetyhook::InlineHook(std::move(*end_result));
    auto* const shutdown =
        new (std::nothrow) safetyhook::InlineHook(std::move(*shutdown_result));
    if (begin == nullptr || end == nullptr || shutdown == nullptr) {
        delete begin;
        delete end;
        delete shutdown;
        g_reason.store("renderer_registration_hook_allocation_failed",
                       std::memory_order_release);
        return false;
    }

    g_renderer_begin_registration = begin;
    g_renderer_end_registration = end;
    g_renderer_shutdown = shutdown;
    g_stock_renderer_begin_registration = begin->original<RendererBeginRegistrationFn>();
    g_stock_renderer_end_registration = end->original<RendererEndRegistrationFn>();
    g_stock_renderer_shutdown = shutdown->original<RendererShutdownFn>();
    if (g_stock_renderer_begin_registration == nullptr ||
        g_stock_renderer_end_registration == nullptr ||
        g_stock_renderer_shutdown == nullptr || !begin->enable().has_value() ||
        !end->enable().has_value() || !shutdown->enable().has_value()) {
        g_reason.store("renderer_registration_hook_enable_failed",
                       std::memory_order_release);
        return false;
    }
    return true;
}

bool capture_font_upload(const int* const texture, const int* const rectangle,
                         const std::uint8_t* const pixels) noexcept {
    if (texture == nullptr || rectangle == nullptr || pixels == nullptr) {
        InterlockedIncrement64(&g_smp_font_upload_invalid_argument_count);
        return false;
    }

    const auto* const frame_index =
        static_cast<const std::int32_t*>(engine_address(k_engine_smp_frame_index));
    if (frame_index == nullptr || *frame_index < 0 || *frame_index > 1) {
        InterlockedIncrement64(&g_smp_font_upload_invalid_frame_count);
        return false;
    }

    const ql1k::FontUploadLayout layout{
        rectangle[0], rectangle[1], rectangle[2], rectangle[3], texture[1]};
    ql1k::FontUploadSizes sizes{};
    if (!ql1k::describe_font_upload(layout, k_max_font_atlas_dimension,
                                    k_max_font_upload_bytes, sizes)) {
        InterlockedIncrement64(&g_smp_font_upload_invalid_layout_count);
        return false;
    }

    HANDLE const heap = GetProcessHeap();
    auto* const packed = static_cast<std::uint8_t*>(
        HeapAlloc(heap, 0, static_cast<SIZE_T>(sizes.packed_bytes)));
    if (packed == nullptr) {
        InterlockedIncrement64(&g_smp_font_upload_allocation_failure_count);
        return false;
    }
    ql1k::pack_font_upload(pixels, layout, sizes, packed);

    const std::size_t root = static_cast<std::size_t>(*frame_index);
    AcquireSRWLockExclusive(&g_font_upload_lock);
    auto& queue = g_deferred_font_uploads[root];
    if (queue.count >= queue.records.size()) {
        InterlockedIncrement64(&g_smp_font_upload_queue_full_count);
        ReleaseSRWLockExclusive(&g_font_upload_lock);
        HeapFree(heap, 0, packed);
        return false;
    }
    queue.records[queue.count++] = DeferredFontUpload{texture[0], layout, sizes, packed};
    InterlockedExchange(&g_font_upload_pending[root], 1);
    ReleaseSRWLockExclusive(&g_font_upload_lock);

    InterlockedIncrement64(&g_smp_font_upload_capture_count);
    InterlockedExchangeAdd64(&g_smp_font_upload_byte_count,
                             static_cast<LONG64>(sizes.packed_bytes));
    (void)record_smp_lifecycle_event(&g_smp_font_capture_event_sequence);
    return true;
}

void replay_font_uploads_for_commands(const int* const commands) noexcept {
    if (commands == nullptr) {
        return;
    }

    HANDLE const heap = GetProcessHeap();
    const auto stock = g_stock_font_atlas_upload;
    for (std::size_t root = 0; root < 2U; ++root) {
        if (InterlockedCompareExchange(&g_font_upload_pending[root], 0, 0) == 0) {
            continue;
        }

        std::array<DeferredFontUpload, k_max_deferred_font_uploads> records{};
        std::size_t count = 0;
        AcquireSRWLockExclusive(&g_font_upload_lock);
        auto& queue = g_deferred_font_uploads[root];
        count = queue.count;
        for (std::size_t index = 0; index < count; ++index) {
            records[index] = queue.records[index];
            queue.records[index] = {};
        }
        queue.count = 0;
        InterlockedExchange(&g_font_upload_pending[root], 0);
        ReleaseSRWLockExclusive(&g_font_upload_lock);

        for (std::size_t index = 0; index < count; ++index) {
            auto& record = records[index];
            if (g_font_upload_staging_capacity < record.sizes.staging_bytes) {
                void* const candidate = g_font_upload_staging == nullptr
                                            ? HeapAlloc(heap, 0, record.sizes.staging_bytes)
                                            : HeapReAlloc(heap, 0, g_font_upload_staging,
                                                          record.sizes.staging_bytes);
                if (candidate != nullptr) {
                    g_font_upload_staging = static_cast<std::uint8_t*>(candidate);
                    g_font_upload_staging_capacity = record.sizes.staging_bytes;
                }
            }

            const bool expanded = g_font_upload_staging != nullptr &&
                ql1k::expand_font_upload(record.packed_pixels, record.layout, record.sizes,
                                         g_font_upload_staging,
                                         g_font_upload_staging_capacity);
            if (expanded && stock != nullptr) {
                const int texture[2]{record.texture, record.layout.stride};
                const int rectangle[4]{record.layout.x, record.layout.y,
                                       record.layout.right, record.layout.bottom};
                stock(texture, rectangle, g_font_upload_staging);
                InterlockedIncrement64(&g_smp_font_upload_replay_count);
                (void)record_smp_lifecycle_event(
                    &g_smp_font_replay_event_sequence);
            } else {
                InterlockedIncrement64(&g_smp_font_upload_drop_count);
                g_reason.store("smp_font_upload_replay_failed", std::memory_order_release);
            }
            if (record.packed_pixels != nullptr) {
                HeapFree(heap, 0, record.packed_pixels);
            }
        }
    }
}

void __cdecl font_atlas_upload_hook(const int* const texture, const int* const rectangle,
                                    const std::uint8_t* const pixels) {
    const auto stock = g_stock_font_atlas_upload;
    // The stock callback treats texture name zero as an intentional no-op.
    // Preserve that contract instead of reporting a deferred-upload failure.
    if (texture != nullptr && texture[0] == 0) {
        InterlockedIncrement64(&g_smp_font_upload_zero_texture_skip_count);
        return;
    }
    const auto* const smp_active =
        static_cast<const std::int32_t*>(engine_address(k_engine_smp_active));
    const bool persistent_configured =
        InterlockedCompareExchange(&g_config_smp_persistent_context, 0, 0) != 0;
    const LONG persistent_state = persistent_smp_context_state();
    const bool persistent_main_context_available =
        persistent_state == static_cast<LONG>(ql1k::SmpContextState::stock) ||
        persistent_state == static_cast<LONG>(ql1k::SmpContextState::main_owned);
    const bool defer = ql1k::should_defer_font_upload(
        InterlockedCompareExchange(&g_config_smp_late_activation, 0, 0) != 0,
        InterlockedCompareExchange(&g_smp_late_activation_state, 0, 0) == 1,
        InterlockedCompareExchange(&g_renderer_registration_complete, 0, 0) != 0,
        smp_active != nullptr && *smp_active != 0, persistent_configured,
        persistent_main_context_available);
    if (!defer) {
        if (stock != nullptr) {
            stock(texture, rectangle, pixels);
        }
        return;
    }
    if (!capture_font_upload(texture, rectangle, pixels)) {
        InterlockedIncrement64(&g_smp_font_upload_drop_count);
        g_reason.store("smp_font_upload_capture_failed", std::memory_order_release);
    }
}

bool install_smp_font_upload_hooks() noexcept {
    if (InterlockedCompareExchange(&g_config_smp_late_activation, 0, 0) == 0) {
        return true;
    }
    if (!signature_matches(engine_address(k_engine_font_atlas_upload),
                           k_font_atlas_upload_signature)) {
        g_reason.store("smp_font_upload_signature_mismatch", std::memory_order_release);
        return false;
    }

    auto upload_result = safetyhook::InlineHook::create(
        engine_address(k_engine_font_atlas_upload), &font_atlas_upload_hook,
        safetyhook::InlineHook::StartDisabled);
    if (!upload_result) {
        g_reason.store("smp_font_upload_hook_create_failed", std::memory_order_release);
        return false;
    }

    auto* const upload =
        new (std::nothrow) safetyhook::InlineHook(std::move(*upload_result));
    if (upload == nullptr) {
        delete upload;
        g_reason.store("smp_font_upload_hook_allocation_failed", std::memory_order_release);
        return false;
    }

    g_font_atlas_upload = upload;
    g_stock_font_atlas_upload = upload->original<FontAtlasUploadFn>();
    if (g_stock_font_atlas_upload == nullptr || !upload->enable().has_value()) {
        g_reason.store("smp_font_upload_hook_enable_failed", std::memory_order_release);
        return false;
    }
    return true;
}

bool install_smp_frontend_sleep_hook() noexcept {
    if (InterlockedCompareExchange(&g_config_smp_persistent_context, 0, 0) == 0) {
        return true;
    }
    if (InterlockedCompareExchange(&g_config_force_smp, 0, 0) == 0 ||
        InterlockedCompareExchange(&g_config_smp_late_activation, 0, 0) == 0) {
        g_reason.store("smp_persistent_context_requires_late_smp",
                       std::memory_order_release);
        return false;
    }
    // Synchronous publication is supported by the persistent wake protocol:
    // the worker keeps WGL ownership, acknowledges pickup through
    // render_completed, then signals renderer_idle only after backend work.
    // Waiting for both removes the queued frame without per-frame WGL handoff.
    if (!relocated_absolute_signature_matches(
            engine_address(k_engine_smp_frontend_sleep),
            k_smp_frontend_sleep_signature, 1U,
            engine_address(k_engine_smp_renderer_idle_event))) {
        g_reason.store("smp_frontend_sleep_signature_mismatch", std::memory_order_release);
        return false;
    }
    auto result = safetyhook::InlineHook::create(
        engine_address(k_engine_smp_frontend_sleep), &smp_frontend_sleep_hook,
        safetyhook::InlineHook::StartDisabled);
    if (!result) {
        g_reason.store("smp_frontend_sleep_hook_create_failed", std::memory_order_release);
        return false;
    }
    auto* const hook = new (std::nothrow) safetyhook::InlineHook(std::move(*result));
    if (hook == nullptr) {
        g_reason.store("smp_frontend_sleep_hook_allocation_failed", std::memory_order_release);
        return false;
    }
    g_smp_frontend_sleep = hook;
    g_stock_smp_frontend_sleep = hook->original<SmpFrontEndSleepFn>();
    if (g_stock_smp_frontend_sleep == nullptr || !hook->enable().has_value()) {
        g_reason.store("smp_frontend_sleep_hook_enable_failed", std::memory_order_release);
        return false;
    }
    return true;
}

bool install_smp_synchronous_hook() noexcept {
    const bool synchronous =
        InterlockedCompareExchange(&g_config_smp_synchronous, 0, 0) != 0;
    const bool persistent =
        InterlockedCompareExchange(&g_config_smp_persistent_context, 0, 0) != 0;
    if (!synchronous && !persistent) {
        return true;
    }
    if (InterlockedCompareExchange(&g_config_force_smp, 0, 0) == 0) {
        g_reason.store("smp_wake_hook_requires_force_smp", std::memory_order_release);
        return false;
    }
    if (g_smp_wake != nullptr) {
        return true;
    }
    if (!relocated_absolute_signature_matches(
            engine_address(k_engine_smp_wake), k_smp_wake_signature, 5U,
            engine_address(k_engine_wgl_hdc))) {
        g_reason.store("smp_wake_signature_mismatch", std::memory_order_release);
        return false;
    }

    HMODULE pinned_patch{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                            reinterpret_cast<LPCWSTR>(&__ImageBase), &pinned_patch)) {
        g_reason.store("smp_wake_patch_pin_failed", std::memory_order_release);
        return false;
    }
    (void)pinned_patch;

    auto result = safetyhook::InlineHook::create(
        engine_address(k_engine_smp_wake), &smp_wake_hook,
        safetyhook::InlineHook::StartDisabled);
    if (!result) {
        g_reason.store("smp_wake_hook_create_failed", std::memory_order_release);
        return false;
    }
    auto* const hook = new (std::nothrow) safetyhook::InlineHook(std::move(*result));
    if (hook == nullptr) {
        g_reason.store("smp_wake_hook_allocation_failed", std::memory_order_release);
        return false;
    }
    g_smp_wake = hook;
    g_stock_smp_wake = hook->original<SmpWakeFn>();
    if (g_stock_smp_wake == nullptr || !hook->enable().has_value()) {
        g_reason.store("smp_wake_hook_enable_failed", std::memory_order_release);
        return false;
    }
    return true;
}

void __cdecl smp_frame_toggle_hook() {
    const auto stock = g_stock_smp_frame_toggle;
    if (stock == nullptr) {
        return;
    }
    const bool force_single =
        InterlockedCompareExchange(&g_config_smp_single_buffer, 0, 0) != 0;
    const bool late_pending =
        InterlockedCompareExchange(&g_config_smp_late_activation, 0, 0) != 0 &&
        InterlockedCompareExchange(&g_smp_late_activation_state, 0, 0) != 1;
    if (!force_single && !late_pending) {
        stock();
        return;
    }

    // R_ToggleSmpFrame selects the back-end root and then rewires multiple
    // front-end pointers into that root. Changing only tr.smpFrame afterward
    // creates a split-brain frame. Make the stock transaction select root 0
    // from the start so every dependent pointer is initialized coherently.
    auto** const smp_cvar_slot =
        static_cast<std::uint8_t**>(engine_address(k_engine_smp_cvar_slot));
    if (smp_cvar_slot != nullptr && *smp_cvar_slot != nullptr) {
        constexpr std::size_t k_cvar_integer_offset = 0x30U;
        *reinterpret_cast<std::int32_t*>(*smp_cvar_slot + k_cvar_integer_offset) = 0;
    }
    stock();
}

bool install_smp_single_buffer_hook() noexcept {
    const bool force_single =
        InterlockedCompareExchange(&g_config_smp_single_buffer, 0, 0) != 0;
    const bool late_activation =
        InterlockedCompareExchange(&g_config_smp_late_activation, 0, 0) != 0;
    if (!force_single && !late_activation) {
        return true;
    }
    if (force_single &&
        InterlockedCompareExchange(&g_config_smp_synchronous, 0, 0) == 0 &&
        InterlockedCompareExchange(&g_config_smp_main_thread_backend, 0, 0) == 0) {
        g_reason.store("smp_single_buffer_requires_serial_backend", std::memory_order_release);
        return false;
    }
    if (late_activation &&
        InterlockedCompareExchange(&g_config_force_smp, 0, 0) == 0) {
        g_reason.store("smp_late_activation_requires_force_smp", std::memory_order_release);
        return false;
    }
    if (!relocated_absolute_signature_matches(
            engine_address(k_engine_smp_frame_toggle), k_smp_frame_toggle_signature, 2U,
            engine_address(k_engine_smp_cvar_slot))) {
        g_reason.store("smp_frame_toggle_signature_mismatch", std::memory_order_release);
        return false;
    }
    auto result = safetyhook::InlineHook::create(
        engine_address(k_engine_smp_frame_toggle), &smp_frame_toggle_hook,
        safetyhook::InlineHook::StartDisabled);
    if (!result) {
        g_reason.store("smp_frame_toggle_hook_create_failed", std::memory_order_release);
        return false;
    }
    auto* const hook = new (std::nothrow) safetyhook::InlineHook(std::move(*result));
    if (hook == nullptr) {
        g_reason.store("smp_frame_toggle_hook_allocation_failed", std::memory_order_release);
        return false;
    }
    g_smp_frame_toggle = hook;
    g_stock_smp_frame_toggle = hook->original<SmpFrameToggleFn>();
    if (g_stock_smp_frame_toggle == nullptr || !hook->enable().has_value()) {
        g_reason.store("smp_frame_toggle_hook_enable_failed", std::memory_order_release);
        return false;
    }
    return true;
}

void __cdecl backend_allocation_hook() {
    const auto stock = g_stock_backend_allocation;
    if (stock == nullptr) {
        return;
    }
    if (InterlockedCompareExchange(&g_config_smp_late_activation, 0, 0) != 0) {
        auto** const cvar_slot =
            static_cast<std::uint8_t**>(engine_address(k_engine_smp_cvar_slot));
        if (cvar_slot != nullptr && *cvar_slot != nullptr) {
            constexpr std::size_t k_cvar_integer_offset = 0x30U;
            *reinterpret_cast<std::int32_t*>(*cvar_slot + k_cvar_integer_offset) = 1;
        }
    }
    // The allocation's final R_ToggleSmpFrame call is already hooked. It
    // consumes the second-root allocation, restores r_smp=0, and initializes
    // every front-end pointer coherently against root 0.
    stock();
}

bool install_backend_allocation_hook() noexcept {
    if (InterlockedCompareExchange(&g_config_smp_late_activation, 0, 0) == 0) {
        return true;
    }
    if (!signature_matches(engine_address(k_engine_backend_allocation),
                           k_backend_allocation_signature)) {
        g_reason.store("backend_allocation_signature_mismatch", std::memory_order_release);
        return false;
    }
    auto result = safetyhook::InlineHook::create(
        engine_address(k_engine_backend_allocation), &backend_allocation_hook,
        safetyhook::InlineHook::StartDisabled);
    if (!result) {
        g_reason.store("backend_allocation_hook_create_failed", std::memory_order_release);
        return false;
    }
    auto* const hook = new (std::nothrow) safetyhook::InlineHook(std::move(*result));
    if (hook == nullptr) {
        g_reason.store("backend_allocation_hook_allocation_failed", std::memory_order_release);
        return false;
    }
    g_backend_allocation = hook;
    g_stock_backend_allocation = hook->original<BackendAllocationFn>();
    if (g_stock_backend_allocation == nullptr || !hook->enable().has_value()) {
        g_reason.store("backend_allocation_hook_enable_failed", std::memory_order_release);
        return false;
    }
    return true;
}

[[nodiscard]] bool renderer_cvar_float(const std::uintptr_t slot_address,
                                       float& value) noexcept {
    auto** const slot = static_cast<std::uint8_t**>(engine_address(slot_address));
    if (slot == nullptr || *slot == nullptr) {
        return false;
    }
    value = *reinterpret_cast<const float*>(*slot + 0x2CU);
    return std::isfinite(value);
}

[[nodiscard]] bool renderer_cvar_integer(const std::uintptr_t slot_address,
                                         std::int32_t& value) noexcept {
    auto** const slot = static_cast<std::uint8_t**>(engine_address(slot_address));
    if (slot == nullptr || *slot == nullptr) {
        return false;
    }
    value = *reinterpret_cast<const std::int32_t*>(*slot + 0x30U);
    return true;
}

std::uint8_t* __cdecl color_correct_backend_hook(
    std::uint8_t* const command_bytes) noexcept {
    const auto stock = g_stock_color_correct_backend;
    if (stock == nullptr) {
        return command_bytes;
    }

    const bool zero_bloom_fast_path_ran = g_zero_bloom_fast_for_color_correct;
    g_zero_bloom_fast_for_color_correct = false;
    float gamma{};
    float contrast{};
    std::int32_t overbright_bits{};
    std::int32_t color_correct_enabled{};
    std::int32_t color_correct_active{};
    const bool cvars_valid =
        renderer_cvar_float(k_engine_gamma_cvar, gamma) &&
        renderer_cvar_float(k_engine_contrast_cvar, contrast) &&
        renderer_cvar_integer(k_engine_overbright_bits_cvar, overbright_bits) &&
        renderer_cvar_integer(k_engine_enable_color_correct_cvar,
                              color_correct_enabled) &&
        renderer_cvar_integer(k_engine_color_correct_active_cvar,
                              color_correct_active);
    const ql1k::ColorCorrectIdentityGate gate{
        InterlockedCompareExchange(&g_config_color_correct_identity_fast_path, 0, 0) != 0,
        InterlockedCompareExchange(&g_runtime_armed, 0, 0) != 0,
        hud_replay_gameplay_eligible(),
        zero_bloom_renderer_ready(),
        wglGetCurrentContext() != nullptr,
        cvars_valid,
        color_correct_enabled != 0,
        color_correct_active != 0,
        zero_bloom_fast_path_ran,
        gamma,
        contrast,
        overbright_bits,
    };
    const auto* const command =
        reinterpret_cast<const ql1k::ColorCorrectCommand9*>(command_bytes);
    if (ql1k::should_skip_identity_color_correct(gate, command)) {
        InterlockedIncrement64(&g_color_correct_identity_fast_count);
        return command_bytes + sizeof(ql1k::ColorCorrectCommand9);
    }
    InterlockedIncrement64(&g_color_correct_identity_stock_count);
    return stock(command_bytes);
}

std::uint8_t* __cdecl bloom_backend_hook(std::uint8_t* const command_bytes) noexcept {
    g_zero_bloom_fast_for_color_correct = false;
    const auto stock = g_stock_bloom_backend;
    if (stock == nullptr) {
        return command_bytes;
    }
    const auto* const command =
        reinterpret_cast<const ql1k::BloomCommand10*>(command_bytes);
    float bloom_intensity{};
    float bloom_saturation{};
    float scene_saturation{};
    float scene_intensity{};
    const bool cvars_valid =
        renderer_cvar_float(k_engine_bloom_intensity_cvar, bloom_intensity) &&
        renderer_cvar_float(k_engine_bloom_saturation_cvar, bloom_saturation) &&
        renderer_cvar_float(k_engine_bloom_scene_saturation_cvar, scene_saturation) &&
        renderer_cvar_float(k_engine_bloom_scene_intensity_cvar, scene_intensity);

    const auto* const bloom_mode =
        static_cast<const std::int32_t*>(engine_address(k_engine_bloom_mode));
    auto* const uniform_update_pending = static_cast<volatile LONG*>(
        engine_address(k_engine_bloom_uniform_update_pending));
    const auto* const width =
        static_cast<const std::int32_t*>(engine_address(k_engine_renderer_width));
    const auto* const height =
        static_cast<const std::int32_t*>(engine_address(k_engine_renderer_height));
    const auto* const blend_enabled = static_cast<const std::int32_t*>(
        engine_address(k_engine_renderer_blend_enabled));
    auto** const default_image_slot =
        static_cast<void**>(engine_address(k_engine_renderer_default_image));
    auto* const active_texture_slot =
        static_cast<GlActiveTextureFn*>(engine_address(k_engine_gl_active_texture_slot));
    auto* const bind_texture_slot =
        static_cast<GlBindTextureFn*>(engine_address(k_engine_gl_bind_texture_slot));
    auto* const use_program_slot =
        static_cast<GlUseProgramFn*>(engine_address(k_engine_gl_use_program_slot));

    const bool bindings_valid = bloom_mode != nullptr &&
                                uniform_update_pending != nullptr && width != nullptr &&
                                height != nullptr && blend_enabled != nullptr &&
                                default_image_slot != nullptr && *default_image_slot != nullptr &&
                                active_texture_slot != nullptr && *active_texture_slot != nullptr &&
                                bind_texture_slot != nullptr && *bind_texture_slot != nullptr &&
                                use_program_slot != nullptr && *use_program_slot != nullptr &&
                                g_postprocess_quad != nullptr &&
                                g_postprocess_state_reset != nullptr &&
                                g_postprocess_bind_destination != nullptr &&
                                g_bloom_uniform_refresh != nullptr &&
                                g_renderer_bind_image != nullptr;
    const ql1k::ZeroBloomGate gate{
        InterlockedCompareExchange(&g_config_zero_bloom_fast_path, 0, 0) != 0,
        InterlockedCompareExchange(&g_runtime_armed, 0, 0) != 0,
        hud_replay_gameplay_eligible(),
        zero_bloom_renderer_ready(),
        wglGetCurrentContext() != nullptr,
        uniform_update_pending != nullptr &&
            InterlockedCompareExchange(uniform_update_pending, 0, 0) == 0,
        bindings_valid,
        cvars_valid,
        bloom_mode == nullptr ? 0 : *bloom_mode,
        bloom_intensity,
        bloom_saturation,
        scene_saturation,
        scene_intensity,
        width == nullptr ? 0 : *width,
        height == nullptr ? 0 : *height,
    };
    if (!ql1k::should_use_zero_bloom_fast_path(gate, command)) {
        InterlockedIncrement64(&g_zero_bloom_stock_count);
        return stock(command_bytes);
    }

    const GlActiveTextureFn active_texture = *active_texture_slot;
    const GlBindTextureFn bind_texture = *bind_texture_slot;
    const GlUseProgramFn use_program = *use_program_slot;

    // Match the stock ID-10 prefix exactly. The first reset synchronizes the
    // engine's cached GL state before stock snapshots the blend flag. The raw
    // disable intentionally follows that reset so the destination combine is
    // an overwrite even when the preceding surface used alpha blending.
    active_texture(k_gl_texture0);
    bind_texture(k_gl_texture_rectangle, 0U);
    active_texture(k_gl_texture1);
    bind_texture(k_gl_texture_rectangle, 0U);
    g_postprocess_state_reset();
    const bool restore_blend = *blend_enabled != 0;
    if (restore_blend) {
        glDisable(GL_BLEND);
    }
    g_postprocess_bind_destination();
    g_postprocess_state_reset();
    active_texture(k_gl_texture0);
    bind_texture(k_gl_texture_rectangle, command->scene_texture);
    active_texture(k_gl_texture1);
    // The glow term is exactly zero. Sampling the captured finite scene on the
    // second sampler preserves the stock combine shader and avoids the
    // threshold=1 bright-pass NaN path without allocating a synthetic frame.
    bind_texture(k_gl_texture_rectangle, command->scene_texture);
    use_program(command->combine_program);
    g_postprocess_quad(gate.width, gate.height, gate.width, gate.height);
    use_program(0U);

    active_texture(k_gl_texture1);
    bind_texture(k_gl_texture_rectangle, 0U);
    active_texture(k_gl_texture0);
    bind_texture(k_gl_texture_rectangle, 0U);
    glDisable(k_gl_texture_rectangle);
    if (restore_blend) {
        glEnable(GL_BLEND);
    }
    g_renderer_bind_image(*default_image_slot, GL_TEXTURE_2D);
    if (InterlockedCompareExchange(uniform_update_pending, 0, 0) != 0) {
        g_bloom_uniform_refresh();
        InterlockedExchange(uniform_update_pending, 0);
    }
    InterlockedIncrement64(&g_zero_bloom_fast_count);
    g_zero_bloom_fast_for_color_correct = true;
    return command_bytes + sizeof(ql1k::BloomCommand10);
}

bool install_color_correct_identity_fast_path_hook() noexcept {
    if (InterlockedCompareExchange(&g_config_color_correct_identity_fast_path, 0, 0) == 0) {
        return true;
    }
    if (!signature_matches(engine_address(k_engine_color_correct_backend),
                           k_color_correct_backend_signature)) {
        g_reason.store("color_correct_identity_signature_mismatch",
                       std::memory_order_release);
        return false;
    }
    auto result = safetyhook::InlineHook::create(
        engine_address(k_engine_color_correct_backend), &color_correct_backend_hook,
        safetyhook::InlineHook::StartDisabled);
    if (!result) {
        g_reason.store("color_correct_identity_hook_create_failed",
                       std::memory_order_release);
        return false;
    }
    auto* const hook = new (std::nothrow) safetyhook::InlineHook(std::move(*result));
    if (hook == nullptr) {
        g_reason.store("color_correct_identity_hook_allocation_failed",
                       std::memory_order_release);
        return false;
    }
    g_color_correct_backend = hook;
    g_stock_color_correct_backend = hook->original<BloomBackendFn>();
    if (g_stock_color_correct_backend == nullptr || !hook->enable().has_value()) {
        g_reason.store("color_correct_identity_hook_enable_failed",
                       std::memory_order_release);
        return false;
    }
    return true;
}

bool install_zero_bloom_fast_path_hook() noexcept {
    if (InterlockedCompareExchange(&g_config_zero_bloom_fast_path, 0, 0) == 0) {
        return true;
    }
    if (!signature_matches(engine_address(k_engine_bloom_backend),
                           k_bloom_backend_signature) ||
        !signature_matches(engine_address(k_engine_postprocess_quad),
                           k_postprocess_quad_signature) ||
        !relocated_absolute_signature_matches(
            engine_address(k_engine_postprocess_state_reset),
            k_postprocess_state_reset_signature, 1U,
            engine_address(k_engine_renderer_height)) ||
        !signature_matches(engine_address(k_engine_postprocess_bind_destination),
                           k_postprocess_bind_destination_signature) ||
        !signature_matches(engine_address(k_engine_bloom_uniform_refresh),
                           k_bloom_uniform_refresh_signature) ||
        !signature_matches(engine_address(k_engine_renderer_bind_image),
                           k_renderer_bind_image_signature)) {
        g_reason.store("zero_bloom_helper_signature_mismatch", std::memory_order_release);
        return false;
    }

    g_postprocess_quad = reinterpret_cast<PostProcessQuadFn>(
        engine_address(k_engine_postprocess_quad));
    g_postprocess_state_reset = reinterpret_cast<PostProcessVoidFn>(
        engine_address(k_engine_postprocess_state_reset));
    g_postprocess_bind_destination = reinterpret_cast<PostProcessVoidFn>(
        engine_address(k_engine_postprocess_bind_destination));
    g_bloom_uniform_refresh = reinterpret_cast<PostProcessVoidFn>(
        engine_address(k_engine_bloom_uniform_refresh));
    g_renderer_bind_image = reinterpret_cast<RendererBindImageFn>(
        engine_address(k_engine_renderer_bind_image));
    auto result = safetyhook::InlineHook::create(
        engine_address(k_engine_bloom_backend), &bloom_backend_hook,
        safetyhook::InlineHook::StartDisabled);
    if (!result) {
        g_reason.store("zero_bloom_hook_create_failed", std::memory_order_release);
        return false;
    }
    auto* const hook = new (std::nothrow) safetyhook::InlineHook(std::move(*result));
    if (hook == nullptr) {
        g_reason.store("zero_bloom_hook_allocation_failed", std::memory_order_release);
        return false;
    }
    g_bloom_backend = hook;
    g_stock_bloom_backend = hook->original<BloomBackendFn>();
    if (g_stock_bloom_backend == nullptr || !hook->enable().has_value()) {
        g_reason.store("zero_bloom_hook_enable_failed", std::memory_order_release);
        return false;
    }
    return true;
}

void __cdecl renderer_command_issue_hook(const int run_performance_counters) {
    const auto stock = g_stock_renderer_command_issue;
    if (stock == nullptr) {
        return;
    }
    if (InterlockedCompareExchange(&g_config_smp_main_thread_backend, 0, 0) == 0) {
        stock(run_performance_counters);
        return;
    }
    auto* const smp_active =
        static_cast<std::int32_t*>(engine_address(k_engine_smp_active));
    if (smp_active == nullptr) {
        stock(run_performance_counters);
        return;
    }
    *smp_active = 0;
    stock(run_performance_counters);
}

bool close_smp_handle_slot(const std::uintptr_t preferred_va) noexcept {
    auto* const slot = static_cast<HANDLE*>(engine_address(preferred_va));
    if (slot == nullptr || *slot == nullptr) {
        return true;
    }
    if (!CloseHandle(*slot)) {
        return false;
    }
    *slot = nullptr;
    return true;
}

void __cdecl smp_command_buffers_shutdown_hook() {
    const auto stock = g_stock_smp_command_buffers_shutdown;
    if (stock == nullptr) {
        InterlockedExchange(&g_smp_shutdown_worker_join_result, -1);
        InterlockedIncrement64(&g_smp_worker_join_failure_count);
        g_reason.store("smp_shutdown_trampoline_missing", std::memory_order_release);
        return;
    }

    auto* const smp_active =
        static_cast<std::int32_t*>(engine_address(k_engine_smp_active));
    auto* const thread_slot =
        static_cast<HANDLE*>(engine_address(k_engine_smp_thread_handle));
    const bool active_before = smp_active != nullptr && *smp_active != 0;
    const HANDLE worker = thread_slot == nullptr ? nullptr : *thread_slot;
    stock();

    if (!active_before && worker != nullptr &&
        WaitForSingleObject(worker, 0) == WAIT_TIMEOUT) {
        // Registration can temporarily hold an existing worker in serial
        // mode. If shutdown lands in that interval, stock sees smp_active=0
        // and skips the null command, so terminate it through our lease-aware
        // wake path before joining.
        smp_wake_hook(nullptr);
    }

    if (worker == nullptr) {
        const bool worker_expected =
            InterlockedCompareExchange(&g_smp_late_activation_state, 0, 0) == 1;
        InterlockedExchange(&g_smp_shutdown_worker_join_result,
                            worker_expected ? -1 : 1);
        if (worker_expected) {
            InterlockedIncrement64(&g_smp_worker_join_failure_count);
            g_reason.store("smp_shutdown_worker_handle_missing",
                           std::memory_order_release);
        }
        return;
    }

    constexpr DWORD k_worker_join_timeout_ms = 5000U;
    if (WaitForSingleObject(worker, k_worker_join_timeout_ms) != WAIT_OBJECT_0) {
        InterlockedExchange(&g_smp_shutdown_worker_join_result, -1);
        InterlockedIncrement64(&g_smp_worker_join_failure_count);
        g_reason.store("smp_shutdown_worker_join_failed", std::memory_order_release);
        return;
    }

    const bool handles_closed =
        close_smp_handle_slot(k_engine_smp_thread_handle) &&
        close_smp_handle_slot(k_engine_smp_render_completed_event) &&
        close_smp_handle_slot(k_engine_smp_command_event) &&
        close_smp_handle_slot(k_engine_smp_renderer_idle_event);
    auto* const thread_id =
        static_cast<DWORD*>(engine_address(k_engine_smp_thread_id));
    if (thread_id != nullptr) {
        *thread_id = 0;
    }
    if (!handles_closed) {
        InterlockedExchange(&g_smp_shutdown_worker_join_result, -1);
        InterlockedIncrement64(&g_smp_worker_join_failure_count);
        g_reason.store("smp_shutdown_handle_cleanup_failed",
                       std::memory_order_release);
        return;
    }

    InterlockedExchange(&g_smp_shutdown_worker_join_result, 1);
    InterlockedIncrement64(&g_smp_worker_join_count);
    InterlockedIncrement64(&g_smp_worker_handle_cleanup_count);
}

void __cdecl smp_command_buffers_init_hook() {
    const auto stock = g_stock_smp_command_buffers_init;
    if (stock == nullptr) {
        return;
    }
    if (InterlockedCompareExchange(&g_config_smp_main_thread_backend, 0, 0) == 0) {
        stock();
        return;
    }

    // Diagnostic isolation: the second back-end buffer has already been
    // allocated, but do not let the stock initializer spawn a WGL owner
    // thread. This distinguishes buffer-layout damage from context/thread
    // initialization damage.
    auto** const smp_cvar_slot =
        static_cast<std::uint8_t**>(engine_address(k_engine_smp_cvar_slot));
    if (smp_cvar_slot != nullptr && *smp_cvar_slot != nullptr) {
        constexpr std::size_t k_cvar_integer_offset = 0x30U;
        *reinterpret_cast<std::int32_t*>(*smp_cvar_slot + k_cvar_integer_offset) = 0;
    }
    stock();

    auto* const smp_active =
        static_cast<std::int32_t*>(engine_address(k_engine_smp_active));
    if (smp_active != nullptr) {
        *smp_active = 0;
    }
}

bool install_smp_command_buffers_shutdown_hook() noexcept {
    if (InterlockedCompareExchange(&g_config_smp_late_activation, 0, 0) == 0) {
        return true;
    }
    if (!relocated_absolute_signature_matches(
            engine_address(k_engine_smp_command_buffers_shutdown),
            k_smp_command_buffers_shutdown_signature, 2U,
            engine_address(k_engine_smp_active))) {
        g_reason.store("smp_command_buffers_shutdown_signature_mismatch",
                       std::memory_order_release);
        return false;
    }
    auto result = safetyhook::InlineHook::create(
        engine_address(k_engine_smp_command_buffers_shutdown),
        &smp_command_buffers_shutdown_hook, safetyhook::InlineHook::StartDisabled);
    if (!result) {
        g_reason.store("smp_command_buffers_shutdown_hook_create_failed",
                       std::memory_order_release);
        return false;
    }
    auto* const hook =
        new (std::nothrow) safetyhook::InlineHook(std::move(*result));
    if (hook == nullptr) {
        g_reason.store("smp_command_buffers_shutdown_hook_allocation_failed",
                       std::memory_order_release);
        return false;
    }
    g_smp_command_buffers_shutdown = hook;
    g_stock_smp_command_buffers_shutdown =
        hook->original<SmpCommandBuffersShutdownFn>();
    if (g_stock_smp_command_buffers_shutdown == nullptr ||
        !hook->enable().has_value()) {
        g_reason.store("smp_command_buffers_shutdown_hook_enable_failed",
                       std::memory_order_release);
        return false;
    }
    return true;
}

bool install_smp_command_buffers_init_hook() noexcept {
    if (InterlockedCompareExchange(&g_config_smp_main_thread_backend, 0, 0) == 0) {
        return true;
    }
    if (!relocated_absolute_signature_matches(
            engine_address(k_engine_smp_command_buffers_init),
            k_smp_command_buffers_init_signature, 1U,
            engine_address(k_engine_smp_cvar_slot))) {
        g_reason.store("smp_command_buffers_init_signature_mismatch",
                       std::memory_order_release);
        return false;
    }
    auto result = safetyhook::InlineHook::create(
        engine_address(k_engine_smp_command_buffers_init), &smp_command_buffers_init_hook,
        safetyhook::InlineHook::StartDisabled);
    if (!result) {
        g_reason.store("smp_command_buffers_init_hook_create_failed",
                       std::memory_order_release);
        return false;
    }
    auto* const hook = new (std::nothrow) safetyhook::InlineHook(std::move(*result));
    if (hook == nullptr) {
        g_reason.store("smp_command_buffers_init_hook_allocation_failed",
                       std::memory_order_release);
        return false;
    }
    g_smp_command_buffers_init = hook;
    g_stock_smp_command_buffers_init = hook->original<SmpCommandBuffersInitFn>();
    if (g_stock_smp_command_buffers_init == nullptr || !hook->enable().has_value()) {
        g_reason.store("smp_command_buffers_init_hook_enable_failed",
                       std::memory_order_release);
        return false;
    }
    return true;
}

bool install_smp_main_thread_backend_hook() noexcept {
    if (InterlockedCompareExchange(&g_config_smp_main_thread_backend, 0, 0) == 0) {
        return true;
    }
    if (InterlockedCompareExchange(&g_config_force_smp, 0, 0) == 0) {
        g_reason.store("smp_main_backend_requires_force_smp", std::memory_order_release);
        return false;
    }
    if (!relocated_absolute_signature_matches(
            engine_address(k_engine_renderer_command_issue),
            k_renderer_command_issue_signature, 4U,
            engine_address(k_engine_smp_frame_index))) {
        g_reason.store("renderer_command_issue_signature_mismatch", std::memory_order_release);
        return false;
    }
    auto result = safetyhook::InlineHook::create(
        engine_address(k_engine_renderer_command_issue), &renderer_command_issue_hook,
        safetyhook::InlineHook::StartDisabled);
    if (!result) {
        g_reason.store("renderer_command_issue_hook_create_failed", std::memory_order_release);
        return false;
    }
    auto* const hook = new (std::nothrow) safetyhook::InlineHook(std::move(*result));
    if (hook == nullptr) {
        g_reason.store("renderer_command_issue_hook_allocation_failed", std::memory_order_release);
        return false;
    }
    g_renderer_command_issue = hook;
    g_stock_renderer_command_issue = hook->original<RendererCommandIssueFn>();
    if (g_stock_renderer_command_issue == nullptr || !hook->enable().has_value()) {
        g_reason.store("renderer_command_issue_hook_enable_failed", std::memory_order_release);
        return false;
    }
    return true;
}

bool activate_smp_after_resource_registration() noexcept {
    if (InterlockedCompareExchange(&g_config_smp_late_activation, 0, 0) == 0) {
        return true;
    }
    if (InterlockedCompareExchange(&g_renderer_registration_complete, 0, 0) == 0) {
        return true;
    }
    const LONG current =
        InterlockedCompareExchange(&g_smp_late_activation_state, 0, 0);
    if (current == 1) {
        if (InterlockedCompareExchange(&g_smp_registration_suspended, 0, 0) == 0) {
            return true;
        }
        const auto fail_resume = [](const LONG code, const char* reason) noexcept {
            g_reason.store(reason, std::memory_order_release);
            InterlockedExchange(&g_smp_late_failure_code, code);
            InterlockedExchange(&g_smp_late_activation_state, -2);
            return false;
        };
        auto** const cvar_slot =
            static_cast<std::uint8_t**>(engine_address(k_engine_smp_cvar_slot));
        auto* const smp_active =
            static_cast<std::int32_t*>(engine_address(k_engine_smp_active));
        auto* const frame_index =
            static_cast<std::int32_t*>(engine_address(k_engine_smp_frame_index));
        if (cvar_slot == nullptr || *cvar_slot == nullptr || smp_active == nullptr ||
            frame_index == nullptr) {
            return fail_resume(20, "smp_registration_resume_bindings_missing");
        }
        constexpr std::size_t k_cvar_integer_offset = 0x30U;
        auto* const cvar_integer = reinterpret_cast<std::int32_t*>(
            *cvar_slot + k_cvar_integer_offset);
        if (*smp_active != 0 || *cvar_integer != 0 || *frame_index != 0) {
            return fail_resume(21, "smp_registration_resume_not_serial");
        }
        *cvar_integer = 1;
        *smp_active = 1;
        InterlockedExchange(&g_smp_registration_suspended, 0);
        InterlockedIncrement64(&g_smp_registration_resume_count);
        (void)record_smp_lifecycle_event(&g_smp_activation_event_sequence);
        return true;
    }
    if (current == -2 ||
        InterlockedCompareExchange(&g_smp_late_activation_state, -1, 0) != 0) {
        return false;
    }

    const auto fail = [](const LONG code, const char* reason) noexcept {
        g_reason.store(reason, std::memory_order_release);
        InterlockedExchange(&g_smp_late_failure_code, code);
        InterlockedExchange(&g_smp_late_activation_state, -2);
        return false;
    };
    auto** const cvar_slot =
        static_cast<std::uint8_t**>(engine_address(k_engine_smp_cvar_slot));
    auto* const smp_active =
        static_cast<std::int32_t*>(engine_address(k_engine_smp_active));
    auto* const frame_index =
        static_cast<std::int32_t*>(engine_address(k_engine_smp_frame_index));
    auto* const backend_roots =
        static_cast<std::uintptr_t*>(engine_address(k_engine_backend_data_roots));
    if (cvar_slot == nullptr) return fail(11, "smp_late_cvar_slot_missing");
    if (*cvar_slot == nullptr) return fail(12, "smp_late_cvar_missing");
    if (smp_active == nullptr) return fail(13, "smp_late_active_flag_missing");
    if (frame_index == nullptr) return fail(14, "smp_late_frame_index_missing");
    if (backend_roots == nullptr) return fail(15, "smp_late_roots_missing");
    if (backend_roots[0] == 0U) return fail(16, "smp_late_root0_missing");
    if (backend_roots[1] == 0U) return fail(17, "smp_late_root1_missing");
    constexpr std::size_t k_cvar_integer_offset = 0x30U;
    auto* const cvar_integer =
        reinterpret_cast<std::int32_t*>(*cvar_slot + k_cvar_integer_offset);
    if (*smp_active != 0 || *frame_index != 0 || *cvar_integer != 0) {
        return fail(2, "smp_late_activation_not_serial");
    }
    if (!relocated_absolute_signature_matches(
            engine_address(k_engine_smp_spawn_thread), k_smp_spawn_thread_signature, 6U,
            engine_address(k_engine_create_event_slot)) ||
        !signature_matches(engine_address(k_engine_smp_renderer_loop),
                           k_smp_renderer_loop_signature)) {
        return fail(3, "smp_late_activation_signature_mismatch");
    }

    const auto spawn = reinterpret_cast<SmpSpawnThreadFn>(
        engine_address(k_engine_smp_spawn_thread));
    const auto render_loop = reinterpret_cast<SmpRendererLoopFn>(
        engine_address(k_engine_smp_renderer_loop));
    if (spawn == nullptr || render_loop == nullptr || spawn(render_loop) == 0) {
        return fail(4, "smp_late_activation_spawn_failed");
    }

    // The first asynchronous command list is the already coherent root-0
    // frame. R_IssueRenderCommands waits for the new thread's initial idle
    // signal before handing it over, then the stock toggle prepares root 1.
    *cvar_integer = 1;
    *smp_active = 1;
    InterlockedExchange(&g_smp_late_activation_state, 1);
    InterlockedIncrement64(&g_smp_worker_spawn_count);
    (void)record_smp_lifecycle_event(&g_smp_activation_event_sequence);
    return true;
}

bool apply_force_smp_default() noexcept {
    if (InterlockedCompareExchange(&g_config_force_smp, 0, 0) == 0) {
        return true;
    }

    AcquireSRWLockExclusive(&g_force_smp_patch_lock);
    const LONG existing_state = InterlockedCompareExchange(&g_force_smp_patch_state, 0, 0);
    if (existing_state != 0) {
        ReleaseSRWLockExclusive(&g_force_smp_patch_lock);
        return existing_state > 0;
    }

    g_engine = GetModuleHandleW(nullptr);
    if (g_engine == nullptr || !hash_file(module_path(g_engine), g_engine_hash) ||
        !exact_hash(std::string_view(g_engine_hash.data()), k_engine_hash)) {
        g_reason.store("force_smp_engine_identity_failed", std::memory_order_release);
        InterlockedExchange(&g_force_smp_patch_state, -1);
        ReleaseSRWLockExclusive(&g_force_smp_patch_lock);
        return false;
    }

    auto* const cvar_slot =
        reinterpret_cast<void* volatile*>(engine_address(k_engine_smp_cvar_slot));
    auto* const patch_site = static_cast<std::uint8_t*>(engine_address(k_engine_smp_default_push));
    const void* const zero_string = engine_address(k_engine_zero_string);
    const void* const one_string = engine_address(k_engine_one_string);
    if (cvar_slot == nullptr || patch_site == nullptr || zero_string == nullptr ||
        one_string == nullptr || *cvar_slot != nullptr) {
        g_reason.store("force_smp_requires_suspended_early_launch", std::memory_order_release);
        InterlockedExchange(&g_force_smp_patch_state, -1);
        ReleaseSRWLockExclusive(&g_force_smp_patch_lock);
        return false;
    }
    if (!relocated_absolute_signature_matches(patch_site, k_smp_default_push_signature, 1U,
                                              zero_string)) {
        g_reason.store("force_smp_signature_mismatch", std::memory_order_release);
        InterlockedExchange(&g_force_smp_patch_state, -1);
        ReleaseSRWLockExclusive(&g_force_smp_patch_lock);
        return false;
    }

    const auto one = reinterpret_cast<std::uintptr_t>(one_string);
    if (one > UINT32_MAX) {
        g_reason.store("force_smp_target_out_of_range", std::memory_order_release);
        InterlockedExchange(&g_force_smp_patch_state, -1);
        ReleaseSRWLockExclusive(&g_force_smp_patch_lock);
        return false;
    }
    auto replacement = k_smp_default_push_signature;
    const auto encoded = static_cast<std::uint32_t>(one);
    std::memcpy(replacement.data() + 1U, &encoded, sizeof(encoded));

    DWORD old_protection{};
    if (!VirtualProtect(patch_site, replacement.size(), PAGE_EXECUTE_READWRITE,
                        &old_protection)) {
        g_reason.store("force_smp_virtual_protect_failed", std::memory_order_release);
        InterlockedExchange(&g_force_smp_patch_state, -1);
        ReleaseSRWLockExclusive(&g_force_smp_patch_lock);
        return false;
    }
    std::memcpy(patch_site, replacement.data(), replacement.size());
    const BOOL flushed = FlushInstructionCache(GetCurrentProcess(), patch_site, replacement.size());
    DWORD ignored{};
    const BOOL protection_restored =
        VirtualProtect(patch_site, replacement.size(), old_protection, &ignored);
    if (!flushed || !protection_restored) {
        g_reason.store("force_smp_code_write_finalize_failed", std::memory_order_release);
        InterlockedExchange(&g_force_smp_patch_state, -1);
        ReleaseSRWLockExclusive(&g_force_smp_patch_lock);
        return false;
    }

    InterlockedExchange(&g_force_smp_patch_state, 1);
    ReleaseSRWLockExclusive(&g_force_smp_patch_lock);
    return true;
}

void* cgame_address(std::uintptr_t preferred_va) {
    if (g_cgame == nullptr || preferred_va < k_cgame_preferred_base) {
        return nullptr;
    }
    return reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(g_cgame) +
                                   (preferred_va - k_cgame_preferred_base));
}

void clear_history_locked() {
    g_history_head = 0;
    g_history_size = 0;
    g_history_newest = 0;
    g_history_have_last = false;
    g_history_last = 0;
    g_history_last_time = 0;
    AcquireSRWLockExclusive(&g_decision_lock);
    g_decision = {};
    g_decision_current = 0;
    g_decision_snapshot = 0;
    g_decision_valid = false;
    ReleaseSRWLockExclusive(&g_decision_lock);
}

const std::int32_t* command_number_address() {
    return static_cast<const std::int32_t*>(engine_address(k_engine_cmd_number));
}

void mark_permanent_fault(const char* reason) {
    reset_hud_replay_cache();
    InterlockedExchange(&g_permanent_fault, 1);
    InterlockedExchange(&g_history_fault, 1);
    InterlockedExchange(&g_history_active, 0);
    InterlockedExchange(&g_transition_closed, 1);
    InterlockedExchange(&g_runtime_armed, 0);
    InterlockedExchange(&g_preview_chain_armed, 0);
    InterlockedExchange(&g_status, k_refused);
    if (reason != nullptr) {
        g_reason.store(reason, std::memory_order_release);
    }
}

void maybe_recover_history() {
    if (InterlockedCompareExchange(&g_permanent_fault, 0, 0) != 0) {
        return;
    }
    AcquireSRWLockExclusive(&g_history_lock);
    const bool ready = InterlockedCompareExchange(&g_transition_closed, 0, 0) != 0 &&
                       InterlockedCompareExchange(&g_transition_depth, 0, 0) == 0 &&
                       InterlockedCompareExchange(&g_transition_pending, 0, 0) != 0 &&
                       InterlockedCompareExchange(&g_s4_inflight, 0, 0) == 0 &&
                       InterlockedCompareExchange(&g_cgame_inflight, 0, 0) == 0;
    const auto* command_number_ptr = command_number_address();
    const LONG generation = InterlockedCompareExchange(&g_history_generation, 0, 0);
    const char* recovery_fault = nullptr;
    if (ready) {
        if (command_number_ptr == nullptr) {
            recovery_fault = "recovery_command_number_unresolved";
        } else if (generation == LONG_MAX) {
            recovery_fault = "history_generation_exhausted";
        } else {
            clear_history_locked();
            g_capture_cursor = *command_number_ptr;
            InterlockedIncrement(&g_history_generation);
            InterlockedExchange(&g_history_fault, 0);
            InterlockedExchange(&g_history_active, 0);
            InterlockedExchange(&g_transition_pending, 0);
            InterlockedExchange(&g_transition_closed, 0);
        }
    }
    ReleaseSRWLockExclusive(&g_history_lock);
    if (recovery_fault != nullptr) {
        mark_permanent_fault(recovery_fault);
    }
}

bool begin_transition() {
    reset_hud_replay_cache();
    AcquireSRWLockExclusive(&g_history_lock);
    const LONG depth = InterlockedCompareExchange(&g_transition_depth, 0, 0);
    if (depth == LONG_MAX) {
        ReleaseSRWLockExclusive(&g_history_lock);
        mark_permanent_fault("transition_depth_overflow");
        return false;
    }
    InterlockedIncrement(&g_transition_depth);
    InterlockedExchange(&g_transition_closed, 1);
    InterlockedExchange(&g_transition_pending, 1);
    InterlockedExchange(&g_history_active, 0);
    ReleaseSRWLockExclusive(&g_history_lock);
    InterlockedExchange(&g_cvar_refresh_warmed, 0);
    reset_pose_freshness_baseline();
    reset_hitreg();
    return true;
}

void release_cgame_install_ticket() noexcept {
    const auto ticket = static_cast<HMODULE>(InterlockedExchangePointer(
        reinterpret_cast<void* volatile*>(&g_cgame_install_ticket), nullptr));
    if (ticket != nullptr) {
        FreeLibrary(ticket);
    }
}

bool configured_fresh_view() {
    wchar_t value[8]{};
    const std::wstring module = module_path(reinterpret_cast<HMODULE>(&__ImageBase));
    const std::size_t separator = module.find_last_of(L"\\/");
    const std::wstring config_path =
        (separator == std::wstring::npos ? std::wstring{} : module.substr(0, separator)) +
        L"\\ql_fps.cfg";
    const DWORD length = GetPrivateProfileStringW(L"ql_fps", L"fresh_view", L"1", value,
                                                  static_cast<DWORD>(std::size(value)),
                                                  config_path.c_str());
    return length == 1 && value[0] == L'1';
}

bool configured_highres_entity_interpolation() {
    wchar_t value[8]{};
    const std::wstring module = module_path(reinterpret_cast<HMODULE>(&__ImageBase));
    const std::size_t separator = module.find_last_of(L"\\/");
    const std::wstring config_path =
        (separator == std::wstring::npos ? std::wstring{} : module.substr(0, separator)) +
        L"\\ql_fps.cfg";
    const DWORD length = GetPrivateProfileStringW(
        L"ql_fps", L"highres_entity_interpolation", L"1", value,
        static_cast<DWORD>(std::size(value)), config_path.c_str());
    return length == 1 && value[0] == L'1';
}

bool configured_raster_fingerprint() {
    wchar_t value[8]{};
    const std::wstring module = module_path(reinterpret_cast<HMODULE>(&__ImageBase));
    const std::size_t separator = module.find_last_of(L"\\/");
    const std::wstring config_path =
        (separator == std::wstring::npos ? std::wstring{} : module.substr(0, separator)) +
        L"\\ql_fps.cfg";
    const DWORD length = GetPrivateProfileStringW(
        L"ql_fps", L"raster_fingerprint", L"0", value,
        static_cast<DWORD>(std::size(value)), config_path.c_str());
    return length == 1 && value[0] == L'1';
}

bool configured_hud_replay() {
    wchar_t value[8]{};
    const std::wstring module = module_path(reinterpret_cast<HMODULE>(&__ImageBase));
    const std::size_t separator = module.find_last_of(L"\\/");
    const std::wstring config_path =
        (separator == std::wstring::npos ? std::wstring{} : module.substr(0, separator)) +
        L"\\ql_fps.cfg";
    const DWORD length = GetPrivateProfileStringW(
        L"ql_fps", L"hud_replay", L"1", value,
        static_cast<DWORD>(std::size(value)), config_path.c_str());
    return length == 1 && value[0] == L'1';
}

bool configured_zero_bloom_fast_path() {
    wchar_t value[8]{};
    const std::wstring module = module_path(reinterpret_cast<HMODULE>(&__ImageBase));
    const std::size_t separator = module.find_last_of(L"\\/");
    const std::wstring config_path =
        (separator == std::wstring::npos ? std::wstring{} : module.substr(0, separator)) +
        L"\\ql_fps.cfg";
    const DWORD length = GetPrivateProfileStringW(
        L"ql_fps", L"zero_bloom_fast_path", L"1", value,
        static_cast<DWORD>(std::size(value)), config_path.c_str());
    return length == 1 && value[0] == L'1';
}

bool configured_color_correct_identity_fast_path() {
    wchar_t value[8]{};
    const std::wstring module = module_path(reinterpret_cast<HMODULE>(&__ImageBase));
    const std::size_t separator = module.find_last_of(L"\\/");
    const std::wstring config_path =
        (separator == std::wstring::npos ? std::wstring{} : module.substr(0, separator)) +
        L"\\ql_fps.cfg";
    const DWORD length = GetPrivateProfileStringW(
        L"ql_fps", L"color_correct_identity_fast_path", L"0", value,
        static_cast<DWORD>(std::size(value)), config_path.c_str());
    return length == 1 && value[0] == L'1';
}

bool configured_shadow_mark_cache() {
    wchar_t value[8]{};
    const std::wstring module = module_path(reinterpret_cast<HMODULE>(&__ImageBase));
    const std::size_t separator = module.find_last_of(L"\\/");
    const std::wstring config_path =
        (separator == std::wstring::npos ? std::wstring{} : module.substr(0, separator)) +
        L"\\ql_fps.cfg";
    const DWORD length = GetPrivateProfileStringW(
        L"ql_fps", L"shadow_mark_cache", L"0", value,
        static_cast<DWORD>(std::size(value)), config_path.c_str());
    return length == 1 && value[0] == L'1';
}

bool configured_player_scene_replay() {
    wchar_t value[8]{};
    const std::wstring module = module_path(reinterpret_cast<HMODULE>(&__ImageBase));
    const std::size_t separator = module.find_last_of(L"\\/");
    const std::wstring config_path =
        (separator == std::wstring::npos ? std::wstring{} : module.substr(0, separator)) +
        L"\\ql_fps.cfg";
    const DWORD length = GetPrivateProfileStringW(
        L"ql_fps", L"player_scene_replay", L"0", value,
        static_cast<DWORD>(std::size(value)), config_path.c_str());
    return length == 1 && value[0] == L'1';
}

bool configured_player_scene_bypass() {
    wchar_t value[8]{};
    const std::wstring module = module_path(reinterpret_cast<HMODULE>(&__ImageBase));
    const std::size_t separator = module.find_last_of(L"\\/");
    const std::wstring config_path =
        (separator == std::wstring::npos ? std::wstring{} : module.substr(0, separator)) +
        L"\\ql_fps.cfg";
    const DWORD length = GetPrivateProfileStringW(
        L"ql_fps", L"player_scene_bypass", L"0", value,
        static_cast<DWORD>(std::size(value)), config_path.c_str());
    return length == 1 && value[0] == L'1';
}

bool configured_player_style_fast_path() {
    wchar_t value[8]{};
    const std::wstring module = module_path(reinterpret_cast<HMODULE>(&__ImageBase));
    const std::size_t separator = module.find_last_of(L"\\/");
    const std::wstring config_path =
        (separator == std::wstring::npos ? std::wstring{} : module.substr(0, separator)) +
        L"\\ql_fps.cfg";
    const DWORD length = GetPrivateProfileStringW(
        L"ql_fps", L"player_style_fast_path", L"0", value,
        static_cast<DWORD>(std::size(value)), config_path.c_str());
    return length == 1 && value[0] == L'1';
}

bool configured_player_style_bypass() {
    wchar_t value[8]{};
    const std::wstring module = module_path(reinterpret_cast<HMODULE>(&__ImageBase));
    const std::size_t separator = module.find_last_of(L"\\/");
    const std::wstring config_path =
        (separator == std::wstring::npos ? std::wstring{} : module.substr(0, separator)) +
        L"\\ql_fps.cfg";
    const DWORD length = GetPrivateProfileStringW(
        L"ql_fps", L"player_style_bypass", L"0", value,
        static_cast<DWORD>(std::size(value)), config_path.c_str());
    return length == 1 && value[0] == L'1';
}

void end_transition() {
    AcquireSRWLockExclusive(&g_history_lock);
    const LONG depth = InterlockedCompareExchange(&g_transition_depth, 0, 0);
    if (depth <= 0) {
        ReleaseSRWLockExclusive(&g_history_lock);
        mark_permanent_fault("unmatched_transition_end");
        return;
    }
    InterlockedDecrement(&g_transition_depth);
    if (depth == 1) {
        InterlockedExchange(&g_transition_pending, 1);
    }
    ReleaseSRWLockExclusive(&g_history_lock);
    maybe_recover_history();
}

bool acquire_module_ticket(LONG& serial) {
    LONG owner = static_cast<LONG>(GetCurrentThreadId());
    if (owner <= 0) {
        mark_permanent_fault("module_owner_unresolved");
        return false;
    }
    if (InterlockedCompareExchange(&g_module_owner, 0, 0) == owner) {
        if (g_module_ticket_depth == LONG_MAX) {
            mark_permanent_fault("module_ticket_depth_overflow");
            return false;
        }
        const LONG current_serial = InterlockedCompareExchange(&g_module_serial, 0, 0);
        if (current_serial == LONG_MAX) {
            mark_permanent_fault("module_ticket_serial_overflow");
            return false;
        }
        serial = InterlockedIncrement(&g_module_serial);
        ++g_module_ticket_depth;
        return serial > 0;
    }

    AcquireSRWLockExclusive(&g_module_lock);
    if (InterlockedCompareExchange(&g_module_owner, 0, 0) != 0) {
        ReleaseSRWLockExclusive(&g_module_lock);
        mark_permanent_fault("module_owner_state_fault");
        return false;
    }
    const LONG current_serial = InterlockedCompareExchange(&g_module_serial, 0, 0);
    if (current_serial == LONG_MAX) {
        ReleaseSRWLockExclusive(&g_module_lock);
        mark_permanent_fault("module_ticket_serial_overflow");
        return false;
    }
    serial = InterlockedIncrement(&g_module_serial);
    InterlockedExchange(&g_module_owner, owner);
    g_module_ticket_depth = 1;
    if (serial <= 0) {
        g_module_ticket_depth = 0;
        InterlockedExchange(&g_module_owner, 0);
        ReleaseSRWLockExclusive(&g_module_lock);
        mark_permanent_fault("module_ticket_serial_overflow");
        return false;
    }
    return true;
}

bool module_ticket_current(const LONG serial) {
    const LONG owner = static_cast<LONG>(GetCurrentThreadId());
    return owner > 0 && g_module_ticket_depth > 0 &&
           InterlockedCompareExchange(&g_module_owner, 0, 0) == owner &&
           InterlockedCompareExchange(&g_module_serial, 0, 0) == serial;
}

void release_module_ticket() {
    const LONG owner = static_cast<LONG>(GetCurrentThreadId());
    if (owner <= 0 || g_module_ticket_depth <= 0 ||
        InterlockedCompareExchange(&g_module_owner, 0, 0) != owner) {
        mark_permanent_fault("module_ticket_release_fault");
        return;
    }
    if (g_module_ticket_depth > 1) {
        --g_module_ticket_depth;
        return;
    }
    g_module_ticket_depth = 0;
    InterlockedExchange(&g_module_owner, 0);
    ReleaseSRWLockExclusive(&g_module_lock);
}

void initialize_history(const std::int32_t baseline) {
    reset_hitreg();
    AcquireSRWLockExclusive(&g_history_lock);
    clear_history_locked();
    g_capture_cursor = baseline;
    InterlockedExchange(&g_history_generation, 1);
    InterlockedExchange(&g_history_fault, 0);
    InterlockedExchange(&g_permanent_fault, 0);
    InterlockedExchange(&g_history_active, 0);
    // Transition ownership belongs to the lifecycle hooks. During cgame
    // reload this function runs inside vm_loader_hook's active transition;
    // resetting depth here made its matching end_transition fail closed.
    ReleaseSRWLockExclusive(&g_history_lock);
}

bool append_published_record(const std::int32_t old_command,
                             const std::int32_t new_command,
                             const LONG ticket_generation) {
    const auto* ring = static_cast<const std::uint8_t*>(engine_address(k_engine_cmd_ring));
    if (ring == nullptr || new_command == INT32_MAX) {
        mark_permanent_fault("publisher_record_unresolved");
        return false;
    }
    const auto slot = static_cast<std::uint32_t>(new_command) & 63U;
    const auto* source = ring + (static_cast<std::size_t>(slot) * k_record_size);
    AuxiliaryRecord record{};
    record.command_number = new_command;
    std::memcpy(record.bytes.data(), source, record.bytes.size());
    std::memcpy(&record.server_time, record.bytes.data(), sizeof(record.server_time));

    AcquireSRWLockExclusive(&g_history_lock);
    const bool closed = InterlockedCompareExchange(&g_transition_closed, 0, 0) != 0;
    const bool valid = InterlockedCompareExchange(&g_permanent_fault, 0, 0) == 0 && !closed &&
                       InterlockedCompareExchange(&g_history_generation, 0, 0) == ticket_generation &&
                       old_command < INT32_MAX - 1 && old_command == g_capture_cursor &&
                       new_command == old_command + 1;
    if (!valid) {
        ReleaseSRWLockExclusive(&g_history_lock);
        if (!closed && InterlockedCompareExchange(&g_permanent_fault, 0, 0) == 0) {
            mark_permanent_fault("publisher_sequence_fault");
        }
        return false;
    }
    if (g_history_size != 0 && record.server_time < g_history_last_time) {
        // Legitimate setting/map/renderer changes can restart server-time
        // without a disconnect seam. Close only the auxiliary-history epoch;
        // render/network hooks remain armed and the next publication starts a
        // fresh monotonic generation.
        clear_history_locked();
        g_capture_cursor = new_command;
        if (InterlockedCompareExchange(&g_history_generation, 0, 0) == LONG_MAX) {
            ReleaseSRWLockExclusive(&g_history_lock);
            mark_permanent_fault("history_generation_exhausted");
            return false;
        }
        InterlockedIncrement(&g_history_generation);
        InterlockedExchange(&g_history_fault, 0);
        InterlockedExchange(&g_history_active, 0);
        ReleaseSRWLockExclusive(&g_history_lock);
        reset_hitreg();
        return true;
    }
    if (!g_history_have_last) {
        g_history_have_last = true;
    }
    std::size_t write_index = (g_history_head + g_history_size) % k_history_capacity;
    if (g_history_size == k_history_capacity) {
        g_history_head = (g_history_head + 1) % k_history_capacity;
        write_index = (g_history_head + g_history_size - 1) % k_history_capacity;
    } else {
        ++g_history_size;
    }
    g_history[write_index] = record;
    g_history_last = new_command;
    g_history_last_time = record.server_time;
    g_history_newest = new_command;
    g_capture_cursor = new_command;
    if (g_history_size >= k_history_warmup) {
        InterlockedExchange(&g_history_active, 1);
    }
    ReleaseSRWLockExclusive(&g_history_lock);
    record_hitreg_usercmd(record);
    return true;
}

bool select_history(std::int32_t current, std::int32_t snapshot_time, HistorySelection& selection) {
    AcquireSRWLockShared(&g_history_lock);
    const bool usable = InterlockedCompareExchange(&g_permanent_fault, 0, 0) == 0 &&
                        InterlockedCompareExchange(&g_history_fault, 0, 0) == 0 &&
                        InterlockedCompareExchange(&g_history_active, 0, 0) != 0 &&
                        InterlockedCompareExchange(&g_transition_closed, 0, 0) == 0 &&
                        g_history_size >= k_history_warmup && g_history_newest == current;
    if (!usable) {
        ReleaseSRWLockShared(&g_history_lock);
        return false;
    }
    const LONG generation = InterlockedCompareExchange(&g_history_generation, 0, 0);
    AcquireSRWLockShared(&g_decision_lock);
    if (g_decision_valid && g_decision_current == current &&
        g_decision_snapshot == snapshot_time && g_decision.generation == generation) {
        selection = g_decision;
        ReleaseSRWLockShared(&g_decision_lock);
        ReleaseSRWLockShared(&g_history_lock);
        return true;
    }
    ReleaseSRWLockShared(&g_decision_lock);
    if (current == INT32_MAX) {
        ReleaseSRWLockShared(&g_history_lock);
        mark_permanent_fault("history_command_overflow");
        return false;
    }
    selection.generation = generation;
    const auto& oldest = g_history[g_history_head];
    selection.oldest_time = oldest.server_time;
    selection.q_start = current + 1;
    if (oldest.server_time > snapshot_time) {
        // The finite retained history does not reach the snapshot boundary.
        // Keep the original cgame freeze/warning predicate armed and do not
        // invent a replay start. This is a truthful fail-closed decision.
        selection.coverage = false;
        ReleaseSRWLockShared(&g_history_lock);
        return true;
    }
    selection.coverage = true;
    for (std::size_t index = 0; index < g_history_size; ++index) {
        const auto& candidate = g_history[(g_history_head + index) % k_history_capacity];
        if (candidate.server_time > snapshot_time) {
            selection.q_start = candidate.command_number;
            break;
        }
    }
    AcquireSRWLockExclusive(&g_decision_lock);
    g_decision = selection;
    g_decision_current = current;
    g_decision_snapshot = snapshot_time;
    g_decision_valid = true;
    ReleaseSRWLockExclusive(&g_decision_lock);
    ReleaseSRWLockShared(&g_history_lock);
    return true;
}

int __cdecl get_usercmd_hook(const int requested, void* destination) {
    if (destination != nullptr && g_replay_auth.active != 0) {
        const auto return_address = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
        const auto expected_return = reinterpret_cast<std::uintptr_t>(cgame_address(k_cgame_replay_return));
        const LONG generation = InterlockedCompareExchange(&g_history_generation, 0, 0);
        if (return_address == expected_return && g_replay_auth.cgame == g_cgame &&
            g_replay_auth.generation == generation) {
            if (requested != g_replay_auth.next || requested > g_replay_auth.end) {
                g_replay_auth.active = 0;
                mark_permanent_fault("replay_sequence_fault");
            } else {
                const LONG authorization_generation = g_replay_auth.generation;
                AcquireSRWLockShared(&g_history_lock);
                const bool usable = InterlockedCompareExchange(&g_runtime_armed, 0, 0) != 0 &&
                                     InterlockedCompareExchange(&g_history_fault, 0, 0) == 0 &&
                                     InterlockedCompareExchange(&g_history_active, 0, 0) != 0 &&
                                     InterlockedCompareExchange(&g_transition_closed, 0, 0) == 0 &&
                                     InterlockedCompareExchange(&g_history_generation, 0, 0) ==
                                         authorization_generation &&
                                     g_history_size != 0;
                bool copied = false;
                if (usable) {
                    const auto& oldest = g_history[g_history_head];
                    const std::int64_t offset =
                        static_cast<std::int64_t>(requested) - oldest.command_number;
                    if (offset >= 0 && offset < static_cast<std::int64_t>(g_history_size)) {
                        const auto& record =
                            g_history[(g_history_head + static_cast<std::size_t>(offset)) %
                                      k_history_capacity];
                        std::memcpy(destination, record.bytes.data(), record.bytes.size());
                        copied = true;
                    }
                }
                const bool stable = copied &&
                                    InterlockedCompareExchange(&g_runtime_armed, 0, 0) != 0 &&
                                    InterlockedCompareExchange(&g_history_fault, 0, 0) == 0 &&
                                    InterlockedCompareExchange(&g_history_active, 0, 0) != 0 &&
                                    InterlockedCompareExchange(&g_transition_closed, 0, 0) == 0 &&
                                    InterlockedCompareExchange(&g_history_generation, 0, 0) ==
                                        authorization_generation;
                ReleaseSRWLockShared(&g_history_lock);
                if (stable) {
                    if (g_replay_auth.next == INT32_MAX) {
                        g_replay_auth.active = 0;
                        mark_permanent_fault("replay_command_overflow");
                        return g_stock_getter == nullptr ? 0
                                                           : g_stock_getter(requested, destination);
                    }
                    ++g_replay_auth.next;
                    return 1;
                }
                g_replay_auth.active = 0;
                if (!usable || copied) {
                    mark_permanent_fault("replay_lifecycle_fault");
                } else {
                    mark_permanent_fault("replay_record_missing");
                }
            }
        }
    }
    return g_stock_getter == nullptr ? 0 : g_stock_getter(requested, destination);
}

void __cdecl packet_write_hook() {
    const auto stock = g_stock_packet_write;
    if (g_packet_write == nullptr) {
        if (stock != nullptr) {
            stock();
        }
        return;
    }
    const auto original = g_packet_write->original<PacketWriteFn>();
    if (original != nullptr) {
        original();
    } else if (stock != nullptr) {
        stock();
        mark_permanent_fault("packet_write_trampoline_missing");
    } else {
        mark_permanent_fault("packet_write_trampoline_missing");
    }
    if (InterlockedCompareExchange(&g_runtime_armed, 0, 0) != 0) {
        InterlockedIncrement64(&g_packet_write_count);
    }
}

bool invalid_gl_proc(const PROC proc) noexcept {
    const auto value = reinterpret_cast<std::uintptr_t>(proc);
    return proc == nullptr || value <= 3U || value == UINTPTR_MAX;
}

template <typename T>
T resolve_gl_proc(const char* primary, const char* alternate = nullptr) noexcept {
    const auto resolve_one = [](const char* name) noexcept -> PROC {
        if (name == nullptr) {
            return nullptr;
        }
        PROC proc = wglGetProcAddress(name);
        if (!invalid_gl_proc(proc)) {
            return proc;
        }
        const HMODULE opengl = GetModuleHandleW(L"opengl32.dll");
        if (opengl == nullptr) {
            return nullptr;
        }
        proc = GetProcAddress(opengl, name);
        return invalid_gl_proc(proc) ? nullptr : proc;
    };

    PROC proc = resolve_one(primary);
    if (proc == nullptr) {
        proc = resolve_one(alternate);
    }
    return reinterpret_cast<T>(proc);
}

void set_raster_fingerprint_failure(const LONG code, const bool initialization,
                                    const bool wait_failure) noexcept {
    g_raster_fingerprint_gl.failed = true;
    InterlockedExchange(&g_raster_fingerprint_active, 0);
    (void)InterlockedCompareExchange(&g_raster_fingerprint_failure_code, code, 0);
    if (initialization) {
        InterlockedIncrement64(&g_raster_fingerprint_init_failure_count);
    }
    if (wait_failure) {
        g_raster_fingerprint_gl.accounting.note_wait_failure();
        InterlockedIncrement64(&g_raster_fingerprint_wait_failure_count);
    }
}

void abandon_raster_fingerprint_state() noexcept {
    g_raster_fingerprint_gl = {};
    InterlockedExchange(&g_raster_fingerprint_active, 0);
    InterlockedExchange(&g_raster_fingerprint_pending, 0);
    InterlockedExchange(&g_raster_fingerprint_failure_code, 0);
}

bool initialize_raster_fingerprint() noexcept {
    auto& state = g_raster_fingerprint_gl;
    if (state.initialized) {
        return !state.failed;
    }
    if (state.failed) {
        return false;
    }

    const HGLRC current_context = wglGetCurrentContext();
    const WglContextBindings engine_context = wgl_context_bindings();
    if (current_context == nullptr || !engine_context.valid() ||
        current_context != engine_context.rendering_context) {
        set_raster_fingerprint_failure(1, true, false);
        return false;
    }

    state.gen_buffers = resolve_gl_proc<GlGenBuffersFn>("glGenBuffers", "glGenBuffersARB");
    state.delete_buffers =
        resolve_gl_proc<GlDeleteBuffersFn>("glDeleteBuffers", "glDeleteBuffersARB");
    state.bind_buffer = resolve_gl_proc<GlBindBufferFn>("glBindBuffer", "glBindBufferARB");
    state.buffer_data = resolve_gl_proc<GlBufferDataFn>("glBufferData", "glBufferDataARB");
    state.map_buffer = resolve_gl_proc<GlMapBufferFn>("glMapBuffer", "glMapBufferARB");
    state.unmap_buffer =
        resolve_gl_proc<GlUnmapBufferFn>("glUnmapBuffer", "glUnmapBufferARB");
    state.gen_framebuffers = resolve_gl_proc<GlGenFramebuffersFn>(
        "glGenFramebuffers", "glGenFramebuffersEXT");
    state.delete_framebuffers = resolve_gl_proc<GlDeleteFramebuffersFn>(
        "glDeleteFramebuffers", "glDeleteFramebuffersEXT");
    state.bind_framebuffer = resolve_gl_proc<GlBindFramebufferFn>(
        "glBindFramebuffer", "glBindFramebufferEXT");
    state.check_framebuffer_status = resolve_gl_proc<GlCheckFramebufferStatusFn>(
        "glCheckFramebufferStatus", "glCheckFramebufferStatusEXT");
    state.gen_renderbuffers = resolve_gl_proc<GlGenRenderbuffersFn>(
        "glGenRenderbuffers", "glGenRenderbuffersEXT");
    state.delete_renderbuffers = resolve_gl_proc<GlDeleteRenderbuffersFn>(
        "glDeleteRenderbuffers", "glDeleteRenderbuffersEXT");
    state.bind_renderbuffer = resolve_gl_proc<GlBindRenderbufferFn>(
        "glBindRenderbuffer", "glBindRenderbufferEXT");
    state.renderbuffer_storage = resolve_gl_proc<GlRenderbufferStorageFn>(
        "glRenderbufferStorage", "glRenderbufferStorageEXT");
    state.framebuffer_renderbuffer = resolve_gl_proc<GlFramebufferRenderbufferFn>(
        "glFramebufferRenderbuffer", "glFramebufferRenderbufferEXT");
    state.blit_framebuffer = resolve_gl_proc<GlBlitFramebufferFn>(
        "glBlitFramebuffer", "glBlitFramebufferEXT");
    state.fence_sync = resolve_gl_proc<GlFenceSyncFn>("glFenceSync");
    state.client_wait_sync = resolve_gl_proc<GlClientWaitSyncFn>("glClientWaitSync");
    state.delete_sync = resolve_gl_proc<GlDeleteSyncFn>("glDeleteSync");
    if (state.gen_buffers == nullptr || state.delete_buffers == nullptr ||
        state.bind_buffer == nullptr || state.buffer_data == nullptr ||
        state.map_buffer == nullptr || state.unmap_buffer == nullptr ||
        state.gen_framebuffers == nullptr || state.delete_framebuffers == nullptr ||
        state.bind_framebuffer == nullptr || state.check_framebuffer_status == nullptr ||
        state.gen_renderbuffers == nullptr || state.delete_renderbuffers == nullptr ||
        state.bind_renderbuffer == nullptr || state.renderbuffer_storage == nullptr ||
        state.framebuffer_renderbuffer == nullptr || state.blit_framebuffer == nullptr ||
        state.fence_sync == nullptr || state.client_wait_sync == nullptr ||
        state.delete_sync == nullptr) {
        set_raster_fingerprint_failure(2, true, false);
        return false;
    }

    glGetIntegerv(GL_VIEWPORT, state.viewport);
    if (state.viewport[2] <= 0 || state.viewport[3] <= 0) {
        set_raster_fingerprint_failure(3, true, false);
        return false;
    }
    state.sample_bytes = static_cast<std::size_t>(state.viewport[2]) * 3U;
    state.pbo_bytes = (state.sample_bytes + 3U) & ~std::size_t{3U};
    if (state.sample_bytes == 0U ||
        state.pbo_bytes > static_cast<std::size_t>(PTRDIFF_MAX)) {
        set_raster_fingerprint_failure(3, true, false);
        return false;
    }

    state.gen_framebuffers(1, &state.framebuffer);
    state.gen_renderbuffers(1, &state.renderbuffer);
    if (state.framebuffer == 0U || state.renderbuffer == 0U) {
        set_raster_fingerprint_failure(4, true, false);
        return false;
    }
    state.bind_framebuffer(k_gl_framebuffer, state.framebuffer);
    state.bind_renderbuffer(k_gl_renderbuffer, state.renderbuffer);
    state.renderbuffer_storage(k_gl_renderbuffer, k_gl_rgb8,
                               k_raster_fingerprint_width,
                               k_raster_fingerprint_height);
    state.framebuffer_renderbuffer(k_gl_framebuffer, k_gl_color_attachment0,
                                   k_gl_renderbuffer, state.renderbuffer);
    glDrawBuffer(k_gl_color_attachment0);
    glReadBuffer(k_gl_color_attachment0);
    const GLenum framebuffer_status =
        state.check_framebuffer_status(k_gl_framebuffer);
    state.bind_framebuffer(k_gl_framebuffer, 0U);
    glDrawBuffer(GL_BACK);
    glReadBuffer(GL_BACK);
    if (framebuffer_status != k_gl_framebuffer_complete) {
        set_raster_fingerprint_failure(5, true, false);
        return false;
    }

    std::array<GLuint, k_raster_fingerprint_capacity> pbo_names{};
    state.gen_buffers(static_cast<GLsizei>(pbo_names.size()), pbo_names.data());
    for (std::size_t index = 0; index < pbo_names.size(); ++index) {
        if (pbo_names[index] == 0U) {
            state.bind_buffer(k_gl_pixel_pack_buffer, 0U);
            set_raster_fingerprint_failure(6, true, false);
            return false;
        }
        state.slots[index].pbo = pbo_names[index];
        state.bind_buffer(k_gl_pixel_pack_buffer, pbo_names[index]);
        state.buffer_data(k_gl_pixel_pack_buffer,
                          static_cast<std::ptrdiff_t>(state.pbo_bytes),
                          nullptr, k_gl_stream_read);
    }
    state.bind_buffer(k_gl_pixel_pack_buffer, 0U);
    if (glGetError() != GL_NO_ERROR) {
        set_raster_fingerprint_failure(6, true, false);
        return false;
    }

    state.context = current_context;
    state.initialized = true;
    InterlockedExchange(&g_raster_fingerprint_active, 1);
    return true;
}

bool poll_raster_fingerprint() noexcept {
    auto& state = g_raster_fingerprint_gl;
    if (state.accounting.counters().pending == 0U) {
        return true;
    }

    auto& slot = state.slots[state.consume_index];
    if (!slot.pending || slot.fence == nullptr || slot.frame_id == 0U) {
        state.accounting.note_gap();
        InterlockedIncrement64(&g_raster_fingerprint_gap_count);
        set_raster_fingerprint_failure(12, false, false);
        return false;
    }

    const GLenum wait_result = state.client_wait_sync(slot.fence, 0U, 0U);
    if (wait_result == k_gl_timeout_expired) {
        return true;
    }
    if (wait_result == k_gl_wait_failed ||
        (wait_result != k_gl_already_signaled &&
         wait_result != k_gl_condition_satisfied)) {
        set_raster_fingerprint_failure(8, false, true);
        return false;
    }

    state.bind_buffer(k_gl_pixel_pack_buffer, slot.pbo);
    const auto* const mapped = static_cast<const std::uint8_t*>(
        state.map_buffer(k_gl_pixel_pack_buffer, k_gl_read_only));
    if (mapped == nullptr) {
        state.bind_buffer(k_gl_pixel_pack_buffer, 0U);
        set_raster_fingerprint_failure(9, false, true);
        return false;
    }
    const ql1k::RasterHash128 fingerprint = ql1k::hash_rgb24(
        std::span<const std::uint8_t>(mapped, state.sample_bytes));
    const GLboolean unmap_result = state.unmap_buffer(k_gl_pixel_pack_buffer);
    state.bind_buffer(k_gl_pixel_pack_buffer, 0U);
    if (unmap_result == GL_FALSE) {
        set_raster_fingerprint_failure(10, false, true);
        return false;
    }

    state.delete_sync(slot.fence);
    const ql1k::RasterSampleClass classification =
        state.accounting.note_ready(slot.frame_id, fingerprint);
    if (classification == ql1k::RasterSampleClass::rejected) {
        set_raster_fingerprint_failure(12, false, false);
        return false;
    }
    slot.fence = nullptr;
    slot.frame_id = 0U;
    slot.pending = false;
    state.consume_index =
        (state.consume_index + 1U) % k_raster_fingerprint_capacity;
    InterlockedIncrement64(&g_raster_fingerprint_ready_count);
    if (classification == ql1k::RasterSampleClass::changed) {
        InterlockedIncrement64(&g_raster_fingerprint_changed_count);
    } else if (classification == ql1k::RasterSampleClass::repeated) {
        InterlockedIncrement64(&g_raster_fingerprint_repeat_count);
    }
    InterlockedExchange(
        &g_raster_fingerprint_pending,
        static_cast<LONG>(state.accounting.counters().pending));
    return true;
}

void capture_raster_fingerprint() noexcept {
    if (InterlockedCompareExchange(&g_config_raster_fingerprint, 0, 0) == 0 ||
        InterlockedCompareExchange(&g_runtime_armed, 0, 0) == 0 ||
        InterlockedCompareExchange(
            &g_smp_persistent_gameplay_eligible, 0, 0) == 0) {
        return;
    }
    if (!initialize_raster_fingerprint()) {
        return;
    }

    auto& state = g_raster_fingerprint_gl;
    const HGLRC current_context = wglGetCurrentContext();
    if (current_context == nullptr || current_context != state.context) {
        state.accounting.note_gap();
        InterlockedIncrement64(&g_raster_fingerprint_gap_count);
        set_raster_fingerprint_failure(7, false, false);
        return;
    }
    if (!poll_raster_fingerprint()) {
        return;
    }
    if (!state.accounting.has_capacity()) {
        state.accounting.note_gap();
        InterlockedIncrement64(&g_raster_fingerprint_gap_count);
        return;
    }

    auto& slot = state.slots[state.issue_index];
    if (slot.pending || slot.fence != nullptr || slot.pbo == 0U) {
        state.accounting.note_gap();
        InterlockedIncrement64(&g_raster_fingerprint_gap_count);
        set_raster_fingerprint_failure(12, false, false);
        return;
    }

    glReadBuffer(GL_BACK);
    state.bind_buffer(k_gl_pixel_pack_buffer, slot.pbo);
    glReadPixels(state.viewport[0],
                 state.viewport[1] + state.viewport[3] / 2,
                 state.viewport[2], 1, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    const GlSync fence = state.fence_sync(k_gl_sync_gpu_commands_complete, 0U);
    state.bind_buffer(k_gl_pixel_pack_buffer, 0U);
    if (fence == nullptr) {
        state.accounting.note_gap();
        InterlockedIncrement64(&g_raster_fingerprint_gap_count);
        set_raster_fingerprint_failure(11, false, true);
        return;
    }

    if (state.next_frame_id == UINT64_MAX) {
        state.delete_sync(fence);
        state.accounting.note_gap();
        InterlockedIncrement64(&g_raster_fingerprint_gap_count);
        set_raster_fingerprint_failure(12, false, false);
        return;
    }
    const std::uint64_t frame_id = ++state.next_frame_id;
    if (!state.accounting.note_issued(frame_id)) {
        state.delete_sync(fence);
        InterlockedIncrement64(&g_raster_fingerprint_gap_count);
        set_raster_fingerprint_failure(12, false, false);
        return;
    }
    slot.fence = fence;
    slot.frame_id = frame_id;
    slot.pending = true;
    state.issue_index = (state.issue_index + 1U) % k_raster_fingerprint_capacity;
    InterlockedIncrement64(&g_raster_fingerprint_issued_count);
    InterlockedExchange(
        &g_raster_fingerprint_pending,
        static_cast<LONG>(state.accounting.counters().pending));
}

void __cdecl endframe_hook() {
    const auto stock = g_stock_endframe;
    if (g_endframe == nullptr) {
        if (stock != nullptr) {
            stock();
        }
        return;
    }
    const auto original = g_endframe->original<EndFrameFn>();
    capture_raster_fingerprint();
    if (original != nullptr) {
        original();
    } else if (stock != nullptr) {
        stock();
        mark_permanent_fault("endframe_trampoline_missing");
    } else {
        mark_permanent_fault("endframe_trampoline_missing");
    }
    if (InterlockedCompareExchange(&g_runtime_armed, 0, 0) != 0) {
        InterlockedIncrement64(&g_endframe_completion_count);
    }
}

void __cdecl re_endframe_hook(int* front_end_msec, int* back_end_msec) {
    const auto stock = g_stock_re_endframe;
    if (g_re_endframe == nullptr) {
        if (stock != nullptr) {
            stock(front_end_msec, back_end_msec);
        }
    } else {
        const auto original = g_re_endframe->original<ReEndFrameFn>();
        if (original != nullptr) {
            original(front_end_msec, back_end_msec);
        } else if (stock != nullptr) {
            stock(front_end_msec, back_end_msec);
            mark_permanent_fault("re_endframe_trampoline_missing");
        } else {
            mark_permanent_fault("re_endframe_trampoline_missing");
        }
    }
}

void __cdecl publisher_hook() {
    const auto stock = g_stock_publisher;
    if (InterlockedCompareExchange(&g_runtime_armed, 0, 0) == 0 || g_s4 == nullptr) {
        if (stock != nullptr) {
            stock();
        }
        return;
    }

    const LONG in_flight = InterlockedIncrement(&g_s4_inflight);
    if (in_flight <= 0) {
        InterlockedExchange(&g_s4_inflight, 1);
        mark_permanent_fault("publisher_inflight_overflow");
    } else if (in_flight != 1) {
        mark_permanent_fault("publisher_overlap");
    }

    const auto* command_number_ptr = command_number_address();
    const std::int32_t old_command =
        command_number_ptr == nullptr ? INT32_MAX : *command_number_ptr;
    const LONG ticket_generation = InterlockedCompareExchange(&g_history_generation, 0, 0);
    const auto original = g_s4->original<PublisherFn>();
    if (original != nullptr) {
        original();
    } else if (stock != nullptr) {
        stock();
    }

    if (in_flight == 1 && InterlockedCompareExchange(&g_runtime_armed, 0, 0) != 0) {
        const auto* after_command_number_ptr = command_number_address();
        const std::int32_t new_command = after_command_number_ptr == nullptr
                                              ? INT32_MAX
                                              : *after_command_number_ptr;
        if (append_published_record(old_command, new_command, ticket_generation)) {
            InterlockedIncrement64(&g_usercmd_publication_count);
        }
    }
    const LONG retired = InterlockedDecrement(&g_s4_inflight);
    if (retired < 0) {
        InterlockedExchange(&g_s4_inflight, 0);
        mark_permanent_fault("publisher_inflight_underflow");
    }
    maybe_recover_history();
}

bool ensure_getter_slot() {
    auto* slot = reinterpret_cast<void* volatile*>(engine_address(k_engine_getter_slot));
    const void* stock = engine_address(k_engine_stock_getter);
    if (slot == nullptr || stock == nullptr) {
        return false;
    }
    g_stock_getter = reinterpret_cast<GetterFn>(const_cast<void*>(stock));
    void* existing = *slot;
    const void* ours = reinterpret_cast<const void*>(&get_usercmd_hook);
    if (existing == ours) {
        return true;
    }
    if (existing != stock) {
        return false;
    }
    const auto previous = InterlockedCompareExchangePointer(
        slot, const_cast<void*>(ours), const_cast<void*>(stock));
    return previous == const_cast<void*>(stock) || *slot == ours;
}

void __cdecl disconnect_hook(const int reason) {
    const auto stock = g_stock_disconnect;
    if (InterlockedCompareExchange(&g_runtime_armed, 0, 0) == 0 || g_s8 == nullptr) {
        if (stock != nullptr) {
            stock(reason);
        }
        return;
    }
    const bool token = begin_transition();
    const auto original = g_s8->original<DisconnectFn>();
    if (original != nullptr) {
        original(reason);
    } else if (stock != nullptr) {
        stock(reason);
        mark_permanent_fault("disconnect_trampoline_missing");
    } else {
        mark_permanent_fault("disconnect_trampoline_missing");
    }
    if (token) {
        end_transition();
    }
}

void s9_pre_hook(safetyhook::Context&) {
    if (InterlockedCompareExchange(&g_runtime_armed, 0, 0) == 0) {
        return;
    }
    if (g_s9_token_depth == LONG_MAX) {
        mark_permanent_fault("map_restart_token_overflow");
        return;
    }
    if (begin_transition()) {
        ++g_s9_token_depth;
    }
}

void s9_post_hook(safetyhook::Context&) {
    if (g_s9_token_depth <= 0) {
        if (InterlockedCompareExchange(&g_permanent_fault, 0, 0) == 0) {
            mark_permanent_fault("map_restart_token_missing");
        }
        return;
    }
    --g_s9_token_depth;
    end_transition();
}

void* __cdecl vm_loader_hook(const char* name, void* interface_table, void* callback_table,
                             int* version) {
    const auto stock = g_stock_vm_loader;
    if (g_s10 == nullptr) {
        return stock == nullptr ? nullptr : stock(name, interface_table, callback_table, version);
    }

    const bool is_cgame = name != nullptr && std::strcmp(name, "cgame") == 0;
    LONG module_ticket = 0;
    const bool owns_module = !is_cgame || acquire_module_ticket(module_ticket);
    if (!owns_module) {
        return stock == nullptr ? nullptr : stock(name, interface_table, callback_table, version);
    }
    const bool token = is_cgame && begin_transition();
    const auto original = g_s10->original<VmLoaderFn>();
    void* result = nullptr;
    if (original != nullptr) {
        result = original(name, interface_table, callback_table, version);
    } else if (stock != nullptr) {
        result = stock(name, interface_table, callback_table, version);
        mark_permanent_fault("vm_loader_trampoline_missing");
    } else {
        mark_permanent_fault("vm_loader_trampoline_missing");
    }

    if (is_cgame && module_ticket_current(module_ticket) &&
        InterlockedCompareExchange(&g_permanent_fault, 0, 0) == 0) {
        const HMODULE current_cgame = GetModuleHandleW(L"cgamex86.dll");
        std::array<char, 65> current_hash{};
        if (current_cgame == nullptr ||
            !hash_file(module_path(current_cgame), current_hash) ||
            !exact_hash(std::string_view(current_hash.data()), k_cgame_hash)) {
            mark_permanent_fault("cgame_reload_identity_mismatch");
        } else {
            g_cgame = current_cgame;
            g_cgame_hash = current_hash;
            if (!attach_cgame_hooks() || !ensure_getter_slot()) {
                mark_permanent_fault("cgame_reload_hook_failed");
            } else {
                const auto* command_number_ptr =
                    static_cast<const std::int32_t*>(engine_address(k_engine_cmd_number));
                if (command_number_ptr == nullptr) {
                    mark_permanent_fault("cgame_reload_command_number_missing");
                } else {
                    initialize_history(*command_number_ptr);
                    InterlockedExchange(&g_runtime_armed, 1);
                    g_reason.store(g_config_fresh_view != 0
                                       ? "runtime_candidate_active_unverified"
                                       : "runtime_control_floor1",
                                   std::memory_order_release);
                }
            }
        }
    }
    if (token) {
        end_transition();
    }
    if (is_cgame) {
        release_module_ticket();
    }
    return result;
}

void __cdecl shutdown_hook() {
    const auto stock = g_stock_shutdown;
    if (InterlockedCompareExchange(&g_runtime_armed, 0, 0) == 0 || g_s11 == nullptr) {
        if (stock != nullptr) {
            stock();
        }
        return;
    }
    LONG module_ticket = 0;
    if (!acquire_module_ticket(module_ticket)) {
        if (stock != nullptr) {
            stock();
        }
        return;
    }
    const bool token = begin_transition();
    InterlockedExchange(&g_runtime_armed, 0);
    InterlockedExchange(&g_preview_chain_armed, 0);
    AcquireSRWLockExclusive(&g_draw_lifecycle_gate);
    AcquireSRWLockExclusive(&g_cgame_gate);
    if (!detach_cgame_hooks()) {
        mark_permanent_fault("cgame_restart_detach_failed");
        if (token) {
            end_transition();
        }
        ReleaseSRWLockExclusive(&g_cgame_gate);
        ReleaseSRWLockExclusive(&g_draw_lifecycle_gate);
        release_module_ticket();
        return;
    }
    const auto original = g_s11->original<ShutdownFn>();
    if (original != nullptr) {
        original();
    } else if (stock != nullptr) {
        stock();
        mark_permanent_fault("shutdown_trampoline_missing");
    } else {
        mark_permanent_fault("shutdown_trampoline_missing");
    }

    if (module_ticket_current(module_ticket) &&
        InterlockedCompareExchange(&g_permanent_fault, 0, 0) == 0) {
        auto* interface_slot = static_cast<void* volatile*>(engine_address(k_engine_interface));
        auto* vm_slot = static_cast<void* volatile*>(engine_address(k_engine_vm_pointer));
        if (interface_slot == nullptr || vm_slot == nullptr || *interface_slot != nullptr ||
            *vm_slot != nullptr) {
            mark_permanent_fault("shutdown_vm_clear_missing");
        }
    } else if (InterlockedCompareExchange(&g_permanent_fault, 0, 0) == 0) {
        mark_permanent_fault("shutdown_nested_transition");
    }
    if (token) {
        end_transition();
    }
    g_cgame = nullptr;
    ReleaseSRWLockExclusive(&g_cgame_gate);
    ReleaseSRWLockExclusive(&g_draw_lifecycle_gate);
    release_module_ticket();
}

void call_stock_draw_2d() noexcept {
    auto* const hook = g_cgame_draw_2d;
    const auto original = hook == nullptr ? nullptr : hook->original<CgameEntryFn>();
    const auto stock = g_stock_cgame_draw_2d;
    if (original != nullptr) {
        original();
    } else if (stock != nullptr) {
        stock();
        mark_permanent_fault("cgame_draw_2d_trampoline_missing");
    } else {
        mark_permanent_fault("cgame_draw_2d_trampoline_missing");
        return;
    }
}

void __cdecl cgame_draw_2d_hook() {
    const bool configured =
        InterlockedCompareExchange(&g_config_hud_replay, 0, 0) != 0;
    const bool runtime_armed =
        InterlockedCompareExchange(&g_runtime_armed, 0, 0) != 0;
    const bool preview_frame = g_transient_frame.preview_active != 0 &&
                               g_transient_frame.draw_active != 0;

    if (preview_frame) {
        const auto* const integer_time =
            static_cast<const std::int32_t*>(cgame_address(k_cgame_integer_time));
        const LONG64 now = qpc_start();
        const LONG64 frequency = InterlockedCompareExchange64(&g_qpc_frequency, 0, 0);
        const LONG renderer_epoch = InterlockedCompareExchange(&g_renderer_epoch, 0, 0);
        const LONG module_epoch = InterlockedCompareExchange(&g_module_serial, 0, 0);
        const ql1k::HudReplayGate gate{
            configured,
            runtime_armed,
            true,
            hud_replay_gameplay_eligible(),
            hud_replay_renderer_ready(),
            InterlockedCompareExchange(&g_hud_cache_valid, 0, 0) != 0,
            integer_time != nullptr &&
                *integer_time == g_hud_replay_cache.integer_time,
            renderer_epoch == g_hud_replay_cache.renderer_epoch,
            module_epoch == g_hud_replay_cache.module_epoch,
            ql1k::hud_replay_age_valid(now, g_hud_replay_cache.captured_qpc,
                                       frequency)};
        if (ql1k::should_replay_hud(gate) && g_renderer_command_buffer != nullptr &&
            g_hud_replay_cache.size != 0U) {
            void* const destination = g_renderer_command_buffer(
                static_cast<int>(g_hud_replay_cache.size));
            if (destination != nullptr) {
                std::memcpy(destination, g_hud_replay_cache.bytes.data(),
                            g_hud_replay_cache.size);
                InterlockedIncrement64(&g_hud_replay_count);
                return;
            }
            reset_hud_replay_cache();
        }

        InterlockedIncrement64(&g_hud_stock_fallback_count);
        call_stock_draw_2d();
        return;
    }

    const bool capture_candidate = configured && runtime_armed &&
                                   hud_replay_gameplay_eligible() &&
                                   hud_replay_renderer_ready();
    const RendererCommandSnapshot before =
        capture_candidate ? renderer_command_snapshot() : RendererCommandSnapshot{};
    call_stock_draw_2d();
    if (!capture_candidate || !before.valid) {
        reset_hud_replay_cache();
        return;
    }

    const RendererCommandSnapshot after = renderer_command_snapshot();
    const auto* const integer_time =
        static_cast<const std::int32_t*>(cgame_address(k_cgame_integer_time));
    const bool stable = after.valid && before.index == after.index &&
                        before.root == after.root && after.used >= before.used;
    const std::size_t segment_size = stable
        ? static_cast<std::size_t>(after.used - before.used)
        : 0U;
    if (!stable || integer_time == nullptr || segment_size == 0U ||
        segment_size > g_hud_replay_cache.bytes.size()) {
        reset_hud_replay_cache();
        InterlockedIncrement64(&g_hud_capture_reject_count);
        return;
    }

    const auto* const segment = before.root + k_renderer_command_data_offset +
                                static_cast<std::size_t>(before.used);
    const auto validation = ql1k::validate_hud_command_segment(
        std::span<const std::uint8_t>(segment, segment_size));
    if (!validation.valid) {
        reset_hud_replay_cache();
        InterlockedIncrement64(&g_hud_capture_reject_count);
        return;
    }

    InterlockedExchange(&g_hud_cache_valid, 0);
    std::memcpy(g_hud_replay_cache.bytes.data(), segment, segment_size);
    g_hud_replay_cache.size = segment_size;
    g_hud_replay_cache.integer_time = *integer_time;
    g_hud_replay_cache.renderer_epoch =
        InterlockedCompareExchange(&g_renderer_epoch, 0, 0);
    g_hud_replay_cache.module_epoch =
        InterlockedCompareExchange(&g_module_serial, 0, 0);
    g_hud_replay_cache.captured_qpc = qpc_start();
    MemoryBarrier();
    InterlockedExchange(&g_hud_cache_valid, 1);
    InterlockedIncrement64(&g_hud_capture_count);
    InterlockedAdd64(&g_hud_capture_bytes, static_cast<LONG64>(segment_size));
}

void __cdecl cgame_update_cvars_hook() {
    const bool reuse = ql1k::should_reuse_cgame_cvars(
        InterlockedCompareExchange(&g_runtime_armed, 0, 0) != 0,
        g_transient_frame.preview_active != 0,
        InterlockedCompareExchange(&g_cvar_refresh_warmed, 0, 0) != 0);
    if (reuse) {
        InterlockedIncrement64(&g_cvar_reuse_count);
        return;
    }

    auto* const hook = g_cgame_update_cvars;
    const auto original =
        hook == nullptr ? nullptr : hook->original<CgameEntryFn>();
    const auto stock = g_stock_cgame_update_cvars;
    if (original != nullptr) {
        original();
    } else if (stock != nullptr) {
        stock();
        mark_permanent_fault("cgame_update_cvars_trampoline_missing");
    } else {
        mark_permanent_fault("cgame_update_cvars_trampoline_missing");
        return;
    }

    InterlockedExchange(&g_cvar_refresh_warmed, 1);
    InterlockedIncrement64(&g_cvar_refresh_count);
}

void __cdecl warning_entry_hook() {
    AcquireSRWLockShared(&g_cgame_gate);
    const auto stock = g_stock_warning_entry;
    if (InterlockedCompareExchange(&g_runtime_armed, 0, 0) == 0 || g_warning_entry == nullptr) {
        if (stock != nullptr) {
            stock();
        }
        ReleaseSRWLockShared(&g_cgame_gate);
        return;
    }
    const LONG leases = InterlockedIncrement(&g_cgame_inflight);
    if (leases <= 0) {
        InterlockedExchange(&g_cgame_inflight, 1);
        mark_permanent_fault("cgame_lease_overflow");
    }
    const auto original = g_warning_entry->original<CgameEntryFn>();
    if (original != nullptr) {
        original();
    } else if (stock != nullptr) {
        stock();
        mark_permanent_fault("warning_entry_trampoline_missing");
    } else {
        mark_permanent_fault("warning_entry_trampoline_missing");
    }
    const LONG retired = InterlockedDecrement(&g_cgame_inflight);
    if (retired < 0) {
        InterlockedExchange(&g_cgame_inflight, 0);
        mark_permanent_fault("cgame_lease_underflow");
    }
    ReleaseSRWLockShared(&g_cgame_gate);
    maybe_recover_history();
}

bool ensure_snapshot_entity_cache(const std::uint8_t* const snapshot) noexcept {
    if (snapshot == nullptr) {
        g_snapshot_entity_cache.clear();
        return false;
    }
    std::int32_t snapshot_time{};
    std::int32_t local_player_number{};
    std::int32_t source_count{};
    std::memcpy(&snapshot_time, snapshot + 8U, sizeof(snapshot_time));
    std::memcpy(&local_player_number,
                snapshot + k_snapshot_player_number_offset,
                sizeof(local_player_number));
    std::memcpy(&source_count, snapshot + k_snapshot_entity_count_offset,
                sizeof(source_count));
    if (local_player_number < 0 ||
        local_player_number >= static_cast<std::int32_t>(k_max_cgame_entities) ||
        source_count < 0 ||
        source_count > static_cast<std::int32_t>(k_max_cgame_entities)) {
        g_snapshot_entity_cache.clear();
        return false;
    }
    const std::size_t count = static_cast<std::size_t>(source_count);
    if (g_snapshot_entity_cache.matches(snapshot, snapshot_time,
                                        local_player_number, count)) {
        return true;
    }

    for (std::size_t index = 0; index < count; ++index) {
        std::memcpy(&g_snapshot_entity_source_scratch[index],
                    snapshot + k_snapshot_entities_offset +
                        index * k_snapshot_entity_stride,
                    sizeof(g_snapshot_entity_source_scratch[index]));
    }
    return g_snapshot_entity_cache.rebuild(
        snapshot, snapshot_time, local_player_number,
        std::span<const std::int32_t>(g_snapshot_entity_source_scratch.data(), count));
}

void record_entity_pose_freshness() noexcept {
    const auto* const current_slot =
        static_cast<std::uint8_t* const*>(cgame_address(k_cgame_current_snapshot));
    const auto* const centity_base =
        static_cast<const std::uint8_t*>(cgame_address(k_cgame_centities));
    if (current_slot == nullptr || *current_slot == nullptr || centity_base == nullptr) {
        return;
    }

    if (!ensure_snapshot_entity_cache(*current_slot)) {
        return;
    }

    ql1k::SubmittedPoseSignature<k_max_player_entities> submitted_signature{};
    const auto* const vieworg =
        static_cast<const float*>(cgame_address(k_cgame_refdef_vieworg));
    const auto* const viewaxis =
        static_cast<const float*>(cgame_address(k_cgame_refdef_viewaxis));
    if (vieworg != nullptr && viewaxis != nullptr) {
        std::array<float, 12> camera{};
        std::memcpy(camera.data(), vieworg, 3U * sizeof(float));
        std::memcpy(camera.data() + 3U, viewaxis, 9U * sizeof(float));
        bool valid_camera = true;
        for (const float component : camera) {
            if (!std::isfinite(component) || std::fabs(component) > 2147483.0F) {
                valid_camera = false;
                break;
            }
        }
        if (valid_camera) {
            std::memcpy(submitted_signature.camera_bits.data(), camera.data(),
                        camera.size() * sizeof(float));
            submitted_signature.camera_valid = true;
        }
    }

    std::size_t selected_number = k_max_player_entities;
    std::array<float, 3> selected_position{};
    std::array<std::uint32_t, 3> selected_bits{};
    LONG submitted_player_count = 0;
    for (const std::int32_t entity_number :
         g_snapshot_entity_cache.player_numbers()) {
        const auto* const entity =
            centity_base + static_cast<std::size_t>(entity_number) * k_centity_stride;
        std::array<float, 3> position{};
        std::memcpy(position.data(), entity + k_centity_position_offset,
                    sizeof(position));
        if (!std::isfinite(position[0]) || !std::isfinite(position[1]) ||
            !std::isfinite(position[2]) || std::fabs(position[0]) > 2147483.0F ||
            std::fabs(position[1]) > 2147483.0F ||
            std::fabs(position[2]) > 2147483.0F) {
            continue;
        }

        const auto player_index = static_cast<std::size_t>(entity_number);
        const std::uint64_t presence_bit = 1ULL << player_index;
        if ((submitted_signature.player_presence & presence_bit) == 0U) {
            ++submitted_player_count;
        }
        submitted_signature.player_presence |= presence_bit;
        std::memcpy(submitted_signature.player_position_bits[player_index].data(),
                    position.data(), sizeof(position));
        if (player_index < selected_number) {
            selected_number = player_index;
            selected_position = position;
            std::memcpy(selected_bits.data(), position.data(), sizeof(selected_bits));
        }
    }

    InterlockedExchange(&g_submitted_pose_player_count, submitted_player_count);
    if (submitted_signature.valid()) {
        InterlockedIncrement64(&g_submitted_pose_sample_count);
        if (g_last_submitted_pose_signature_valid) {
            if (submitted_signature == g_last_submitted_pose_signature) {
                InterlockedIncrement64(&g_submitted_pose_repeat_count);
            } else {
                InterlockedIncrement64(&g_submitted_pose_change_count);
            }
        }
        g_last_submitted_pose_signature = submitted_signature;
        g_last_submitted_pose_signature_valid = true;
    }
    if (selected_number >= k_max_player_entities) {
        return;
    }
    InterlockedIncrement64(&g_entity_pose_sample_count);
    if (g_last_entity_pose_valid &&
        InterlockedCompareExchange(&g_entity_pose_last_number, 0, 0) ==
            static_cast<LONG>(selected_number)) {
        const bool repeated = selected_bits == g_last_entity_pose_bits;
        if (repeated) {
            InterlockedIncrement64(&g_entity_pose_repeat_count);
        } else {
            InterlockedIncrement64(&g_entity_pose_change_count);
        }
    } else if (g_last_entity_pose_valid) {
        InterlockedIncrement64(&g_entity_pose_track_switch_count);
    }
    g_last_entity_pose_bits = selected_bits;
    g_last_entity_pose_valid = true;
    InterlockedExchange(&g_entity_pose_last_number, static_cast<LONG>(selected_number));
    InterlockedExchange(&g_entity_pose_x_milli,
                        static_cast<LONG>(std::lround(selected_position[0] * 1000.0F)));
    InterlockedExchange(&g_entity_pose_y_milli,
                        static_cast<LONG>(std::lround(selected_position[1] * 1000.0F)));
    InterlockedExchange(&g_entity_pose_z_milli,
                        static_cast<LONG>(std::lround(selected_position[2] * 1000.0F)));
}

bool capture_rendered_entity_poses(std::uint8_t* current_snapshot,
                                   std::uint8_t* centity_base) noexcept {
    if (current_snapshot == nullptr || centity_base == nullptr) {
        return false;
    }
    g_entity_pose_transaction.begin();
    const auto capture_unique_fields = [](std::uint8_t* centity) noexcept {
        return centity != nullptr && g_entity_pose_transaction.capture_unique(
                                        centity + k_centity_position_offset);
    };
    if (!capture_unique_fields(static_cast<std::uint8_t*>(
            cgame_address(k_cgame_predicted_player_centity)))) {
        g_entity_pose_transaction.clear();
        return false;
    }
    if (!ensure_snapshot_entity_cache(current_snapshot)) {
        g_entity_pose_transaction.clear();
        return false;
    }
    for (const std::int32_t entity_number :
         g_snapshot_entity_cache.entity_numbers()) {
        if (!capture_unique_fields(
                centity_base + static_cast<std::size_t>(entity_number) *
                                   k_centity_stride)) {
            g_entity_pose_transaction.clear();
            return false;
        }
    }
    InterlockedExchange(&g_entity_pose_transaction_entities,
                        static_cast<LONG>(g_entity_pose_transaction.count()));
    return true;
}

bool restore_transient_entity_interpolation() noexcept {
    if (g_transient_frame.interpolation_active == 0) {
        return !g_entity_pose_transaction.valid();
    }
    auto* const centity_base =
        static_cast<std::uint8_t*>(cgame_address(k_cgame_centities));
    auto* const frame_interpolation =
        static_cast<float*>(cgame_address(k_cgame_frame_interpolation));
    const bool poses_restored = centity_base != nullptr &&
                                g_entity_pose_transaction.restore();
    bool fraction_restored = frame_interpolation != nullptr;
    if (frame_interpolation != nullptr) {
        *frame_interpolation = g_transient_frame.saved_frame_interpolation;
        fraction_restored = std::memcmp(frame_interpolation,
                                        &g_transient_frame.saved_frame_interpolation,
                                        sizeof(*frame_interpolation)) == 0;
    }
    g_entity_pose_transaction.clear();
    g_transient_frame.interpolation_active = 0;
    if (!poses_restored || !fraction_restored) {
        InterlockedIncrement64(&g_entity_pose_restore_failure_count);
        return false;
    }
    InterlockedIncrement64(&g_entity_pose_restore_count);
    return true;
}

void __cdecl frame_interpolation_seam_hook(safetyhook::Context&) {
    if (!preview_chain_ready() ||
        InterlockedCompareExchange(&g_config_highres_entity_interpolation, 0, 0) == 0 ||
        g_transient_frame.preview_active == 0 ||
        g_transient_frame.draw_active == 0 ||
        g_transient_frame.interpolation_active != 0 ||
        g_transient_frame.fractional_ms <= 0.0) {
        return;
    }

    auto* const frame_interpolation =
        static_cast<float*>(cgame_address(k_cgame_frame_interpolation));
    const auto* const render_time =
        static_cast<const std::int32_t*>(cgame_address(k_cgame_integer_time));
    const auto* const current_slot =
        static_cast<std::uint8_t* const*>(cgame_address(k_cgame_current_snapshot));
    const auto* const next_slot =
        static_cast<std::uint8_t* const*>(cgame_address(k_cgame_next_snapshot));
    auto* const centity_base =
        static_cast<std::uint8_t*>(cgame_address(k_cgame_centities));
    if (frame_interpolation == nullptr || render_time == nullptr || current_slot == nullptr ||
        next_slot == nullptr || *current_slot == nullptr || *next_slot == nullptr ||
        centity_base == nullptr) {
        return;
    }

    std::int32_t current_snapshot_time{};
    std::int32_t next_snapshot_time{};
    std::memcpy(&current_snapshot_time, *current_slot + 8U,
                sizeof(current_snapshot_time));
    std::memcpy(&next_snapshot_time, *next_slot + 8U, sizeof(next_snapshot_time));
    const float stock_fraction = *frame_interpolation;
    const float refined_fraction = ql1k::refine_snapshot_fraction(
        stock_fraction, g_transient_frame.fractional_ms, *render_time,
        current_snapshot_time, next_snapshot_time);
    if (std::memcmp(&refined_fraction, &stock_fraction, sizeof(refined_fraction)) == 0 ||
        !capture_rendered_entity_poses(*current_slot, centity_base)) {
        return;
    }

    g_transient_frame.saved_frame_interpolation = stock_fraction;
    g_transient_frame.interpolation_active = 1;
    *frame_interpolation = refined_fraction;
    InterlockedExchange(&g_frame_interpolation_ppm,
                        static_cast<LONG>(std::lround(refined_fraction * 1000000.0F)));
    InterlockedIncrement64(&g_highres_interpolation_count);
}

[[nodiscard]] std::size_t player_scene_slot(const void* const centity) noexcept {
    const auto address = reinterpret_cast<std::uintptr_t>(centity);
    const auto predicted = reinterpret_cast<std::uintptr_t>(
        cgame_address(k_cgame_predicted_player_centity));
    if (address == predicted) {
        return k_max_player_entities;
    }
    const auto base = reinterpret_cast<std::uintptr_t>(
        cgame_address(k_cgame_centities));
    if (address < base) {
        return k_player_scene_cache_slots;
    }
    const std::uintptr_t offset = address - base;
    if ((offset % k_centity_stride) != 0U) {
        return k_player_scene_cache_slots;
    }
    const std::size_t slot = static_cast<std::size_t>(offset / k_centity_stride);
    return slot < k_max_player_entities ? slot : k_player_scene_cache_slots;
}

[[nodiscard]] bool capture_player_scene_pose(
    const void* const centity, ql1k::PlayerScenePose& pose) noexcept {
    if (centity == nullptr) {
        return false;
    }
    const auto* const bytes = static_cast<const std::uint8_t*>(centity);
    std::memcpy(pose.origin.data(), bytes + k_centity_position_offset,
                sizeof(pose.origin));
    std::memcpy(pose.angles.data(),
                bytes + k_centity_position_offset + sizeof(pose.origin),
                sizeof(pose.angles));
    return ql1k::player_scene_pose_valid(pose);
}

[[nodiscard]] bool capture_player_body_model_handles(
    const void* const centity, std::array<std::int32_t, 3>& handles) noexcept {
    if (centity == nullptr) {
        return false;
    }
    std::uint32_t client{};
    std::memcpy(&client,
                static_cast<const std::uint8_t*>(centity) +
                    k_centity_style_client_offset,
                sizeof(client));
    if (client >= k_max_player_entities) {
        return false;
    }
    const std::array<std::uintptr_t, 3> preferred{
        k_cgame_client_legs_model, k_cgame_client_torso_model,
        k_cgame_client_head_model};
    for (std::size_t index = 0; index < handles.size(); ++index) {
        const auto* const handle = static_cast<const std::int32_t*>(
            cgame_address(preferred[index] +
                          client * k_client_info_stride));
        if (handle == nullptr || *handle == 0) {
            return false;
        }
        handles[index] = *handle;
    }
    return true;
}

[[nodiscard]] bool capture_player_scene_key(
    const void* const centity, ql1k::PlayerSceneKey& key) noexcept {
    if (centity == nullptr) {
        return false;
    }
    const auto* const integer_time =
        static_cast<const std::int32_t*>(cgame_address(k_cgame_integer_time));
    const auto* const current_slot =
        static_cast<std::uint8_t* const*>(cgame_address(k_cgame_current_snapshot));
    const auto* const next_slot =
        static_cast<std::uint8_t* const*>(cgame_address(k_cgame_next_snapshot));
    if (integer_time == nullptr || current_slot == nullptr || next_slot == nullptr ||
        *current_slot == nullptr || *next_slot == nullptr) {
        return false;
    }
    ql1k::PlayerSceneKey candidate{};
    // Capture the complete non-pose centity prefix at the caller-selected
    // point (both before and after stock rendering are used by scene replay).
    // CG_PlayerAngles updates persistent torso/legs smoothing fields beyond
    // currentState (0xEC); omitting them can make unchanged lerpAngles produce
    // different model axes on a later preview.
    std::memcpy(candidate.nonpose_state.data(), centity,
                candidate.nonpose_state.size());
    candidate.entity_identity = reinterpret_cast<std::uintptr_t>(centity);
    candidate.current_snapshot = reinterpret_cast<std::uintptr_t>(*current_slot);
    candidate.next_snapshot = reinterpret_cast<std::uintptr_t>(*next_slot);
    candidate.integer_time = *integer_time;
    std::memcpy(&candidate.current_snapshot_time, *current_slot + 8U,
                sizeof(candidate.current_snapshot_time));
    std::memcpy(&candidate.next_snapshot_time, *next_slot + 8U,
                sizeof(candidate.next_snapshot_time));
    candidate.module_epoch = InterlockedCompareExchange(&g_module_serial, 0, 0);
    candidate.renderer_epoch = InterlockedCompareExchange(&g_renderer_epoch, 0, 0);
    if (candidate.current_snapshot_time > candidate.integer_time ||
        candidate.next_snapshot_time <= candidate.current_snapshot_time) {
        return false;
    }
    key = candidate;
    return true;
}

[[nodiscard]] EngineSceneSnapshot capture_engine_scene_snapshot() noexcept {
    const auto* const registered =
        static_cast<const std::int32_t*>(engine_address(k_engine_renderer_registered));
    const auto* const frame_index =
        static_cast<const std::int32_t*>(engine_address(k_engine_smp_frame_index));
    auto** const roots =
        static_cast<std::uint8_t**>(engine_address(k_engine_backend_data_roots));
    const auto* const ref_count =
        static_cast<const std::int32_t*>(engine_address(k_engine_scene_ref_count));
    const auto* const dlight_count =
        static_cast<const std::int32_t*>(engine_address(k_engine_scene_dlight_count));
    const auto* const polyvert_count =
        static_cast<const std::int32_t*>(engine_address(k_engine_scene_polyvert_count));
    const auto* const poly_count =
        static_cast<const std::int32_t*>(engine_address(k_engine_scene_poly_count));
    const auto* const max_polyverts =
        static_cast<const std::int32_t*>(engine_address(k_engine_max_polyverts));
    const auto* const max_polys =
        static_cast<const std::int32_t*>(engine_address(k_engine_max_polys));
    if (registered == nullptr || *registered == 0 || frame_index == nullptr ||
        roots == nullptr || ref_count == nullptr || dlight_count == nullptr ||
        polyvert_count == nullptr || poly_count == nullptr ||
        max_polyverts == nullptr || max_polys == nullptr ||
        (*frame_index != 0 && *frame_index != 1) || *ref_count < 0 ||
        *ref_count >= k_engine_scene_ref_limit || *dlight_count < 0 ||
        *dlight_count >= k_engine_scene_dlight_limit || *polyvert_count < 0 ||
        *poly_count < 0 || *max_polyverts <= 0 || *max_polys <= 0 ||
        *max_polyverts > (1 << 20) || *max_polys > (1 << 20) ||
        *polyvert_count > *max_polyverts || *poly_count > *max_polys) {
        return {};
    }
    std::uint8_t* const root = roots[*frame_index];
    if (root == nullptr) {
        return {};
    }
    std::uint8_t* poly_descriptors{};
    std::uint8_t* poly_vertices{};
    std::memcpy(&poly_descriptors,
                root + k_engine_scene_poly_descriptor_pointer_offset,
                sizeof(poly_descriptors));
    std::memcpy(&poly_vertices,
                root + k_engine_scene_polyvert_pointer_offset,
                sizeof(poly_vertices));
    if (poly_descriptors == nullptr || poly_vertices == nullptr) {
        return {};
    }
    return {*frame_index, root, poly_descriptors, poly_vertices,
            *ref_count, *dlight_count, *polyvert_count, *poly_count,
            *max_polyverts, *max_polys, true};
}

[[nodiscard]] bool restore_engine_scene_counts(
    const EngineSceneSnapshot& snapshot) noexcept {
    if (!snapshot.valid) {
        return false;
    }
    const auto* const frame_index =
        static_cast<const std::int32_t*>(engine_address(k_engine_smp_frame_index));
    auto** const roots =
        static_cast<std::uint8_t**>(engine_address(k_engine_backend_data_roots));
    auto* const ref_count =
        static_cast<std::int32_t*>(engine_address(k_engine_scene_ref_count));
    auto* const dlight_count =
        static_cast<std::int32_t*>(engine_address(k_engine_scene_dlight_count));
    auto* const polyvert_count =
        static_cast<std::int32_t*>(engine_address(k_engine_scene_polyvert_count));
    auto* const poly_count =
        static_cast<std::int32_t*>(engine_address(k_engine_scene_poly_count));
    if (frame_index == nullptr || roots == nullptr || ref_count == nullptr ||
        dlight_count == nullptr ||
        polyvert_count == nullptr || poly_count == nullptr) {
        return false;
    }
    MemoryBarrier();
    if (*frame_index != snapshot.frame_index ||
        (*frame_index != 0 && *frame_index != 1) ||
        roots[*frame_index] != snapshot.root) {
        return false;
    }
    *ref_count = snapshot.ref_count;
    *dlight_count = snapshot.dlight_count;
    *polyvert_count = snapshot.polyvert_count;
    *poly_count = snapshot.poly_count;
    MemoryBarrier();
    return *frame_index == snapshot.frame_index &&
           roots[*frame_index] == snapshot.root &&
           *ref_count == snapshot.ref_count &&
           *dlight_count == snapshot.dlight_count &&
           *polyvert_count == snapshot.polyvert_count &&
           *poly_count == snapshot.poly_count;
}

[[nodiscard]] bool capture_player_scene_products(
    const EngineSceneSnapshot& before, const EngineSceneSnapshot& after,
    const ql1k::PlayerScenePose& pose, PlayerSceneProducts& products) noexcept {
    products.begin(pose);
    if (!before.valid || !after.valid || before.frame_index != after.frame_index ||
        before.root != after.root ||
        before.poly_descriptors != after.poly_descriptors ||
        before.poly_vertices != after.poly_vertices ||
        after.ref_count < before.ref_count ||
        after.dlight_count < before.dlight_count ||
        after.polyvert_count < before.polyvert_count ||
        after.poly_count < before.poly_count) {
        return false;
    }
    const std::size_t ref_delta =
        static_cast<std::size_t>(after.ref_count - before.ref_count);
    const std::size_t dlight_delta =
        static_cast<std::size_t>(after.dlight_count - before.dlight_count);
    const std::size_t polyvert_delta =
        static_cast<std::size_t>(after.polyvert_count - before.polyvert_count);
    const std::size_t poly_delta =
        static_cast<std::size_t>(after.poly_count - before.poly_count);
    if (ref_delta > k_player_scene_max_refs ||
        dlight_delta > k_player_scene_max_dlights ||
        polyvert_delta > k_player_scene_max_vertices ||
        poly_delta > k_player_scene_max_polys) {
        products.overflow = true;
        return false;
    }

    for (std::int32_t index = before.ref_count; index < after.ref_count; ++index) {
        ql1k::PlayerSceneRefEntity ref{};
        std::memcpy(ref.data(),
                    after.root + k_engine_scene_ref_offset +
                        static_cast<std::size_t>(index) * k_engine_scene_ref_stride,
                    ref.size());
        ql1k::PlayerSceneRefEntity checked{};
        if (!ql1k::translate_player_scene_ref_entity(ref, pose, pose, checked) ||
            !products.append_ref(ref)) {
            return false;
        }
    }

    const auto polyvert_begin =
        reinterpret_cast<std::uintptr_t>(after.poly_vertices);
    const auto polyvert_end =
        polyvert_begin + static_cast<std::size_t>(after.max_polyverts) *
                             sizeof(ql1k::PlayerScenePolyVert);
    if (polyvert_end < polyvert_begin) {
        return false;
    }
    std::size_t captured_poly_vertices = 0U;
    for (std::int32_t index = before.poly_count; index < after.poly_count; ++index) {
        const auto* const descriptor =
            after.poly_descriptors +
            static_cast<std::size_t>(index) *
                k_engine_scene_poly_descriptor_stride;
        std::int32_t command{};
        std::int32_t shader{};
        std::int32_t vertex_count{};
        std::uint8_t* vertices{};
        std::memcpy(&command, descriptor, sizeof(command));
        std::memcpy(&shader, descriptor + 4U, sizeof(shader));
        std::memcpy(&vertex_count, descriptor + 0xCU, sizeof(vertex_count));
        std::memcpy(&vertices, descriptor + 0x10U, sizeof(vertices));
        if (command != 5 || shader == 0 || vertex_count <= 0 ||
            vertices == nullptr) {
            return false;
        }
        const std::size_t vertex_bytes =
            static_cast<std::size_t>(vertex_count) *
            sizeof(ql1k::PlayerScenePolyVert);
        const auto vertex_address = reinterpret_cast<std::uintptr_t>(vertices);
        if (vertex_address < polyvert_begin || vertex_address > polyvert_end ||
            vertex_bytes > polyvert_end - vertex_address ||
            captured_poly_vertices + static_cast<std::size_t>(vertex_count) >
                polyvert_delta) {
            return false;
        }
        const auto source = std::span<const ql1k::PlayerScenePolyVert>(
            reinterpret_cast<const ql1k::PlayerScenePolyVert*>(vertices),
            static_cast<std::size_t>(vertex_count));
        for (const auto& vertex : source) {
            if (!ql1k::player_scene_finite(vertex.xyz[0]) ||
                !ql1k::player_scene_finite(vertex.xyz[1]) ||
                !ql1k::player_scene_finite(vertex.xyz[2]) ||
                !std::isfinite(vertex.st[0]) || !std::isfinite(vertex.st[1])) {
                return false;
            }
        }
        if (!products.append_poly(shader, source)) {
            return false;
        }
        captured_poly_vertices += static_cast<std::size_t>(vertex_count);
    }
    if (captured_poly_vertices != polyvert_delta) {
        return false;
    }

    for (std::int32_t index = before.dlight_count;
         index < after.dlight_count; ++index) {
        const auto* const record =
            after.root + k_engine_scene_dlight_offset +
            static_cast<std::size_t>(index) * k_engine_scene_dlight_stride;
        ql1k::PlayerSceneDlight dlight{};
        std::memcpy(dlight.origin.data(), record, sizeof(dlight.origin));
        std::memcpy(dlight.color.data(), record + 0xCU, sizeof(dlight.color));
        std::memcpy(&dlight.radius, record + 0x18U, sizeof(dlight.radius));
        std::memcpy(&dlight.additive, record + 0x28U,
                    sizeof(dlight.additive));
        ql1k::PlayerSceneDlight checked{};
        if (dlight.radius <= 0.0F ||
            !ql1k::translate_player_scene_dlight(dlight, pose, pose, checked) ||
            !products.append_dlight(dlight)) {
            return false;
        }
    }
    return products.finish();
}

[[nodiscard]] bool build_translated_player_scene_products(
    const PlayerSceneProducts& source, const ql1k::PlayerScenePose& current,
    PlayerSceneProducts& destination) noexcept {
    destination.begin(current);
    ql1k::PlayerSceneBasis captured_basis{};
    ql1k::PlayerSceneBasis current_basis{};
    if (!source.valid ||
        !native_player_scene_basis(source.pose.angles.data(), captured_basis) ||
        !native_player_scene_basis(current.angles.data(), current_basis)) {
        return false;
    }
    for (std::size_t index = 0; index < source.ref_count; ++index) {
        ql1k::PlayerSceneRefEntity translated{};
        if (!ql1k::transform_player_scene_ref_entity(
                source.refs[index], source.pose, current, captured_basis,
                current_basis, translated) ||
            !destination.append_ref(translated)) {
            return false;
        }
    }
    // Shadow marks and water wakes are collision-clipped world products. Their
    // topology can change after sub-unit movement, so production replay invokes
    // the exact stock surface helpers instead of translating cached polygons.
    for (std::size_t index = 0; index < source.dlight_count; ++index) {
        ql1k::PlayerSceneDlight translated{};
        if (!ql1k::transform_player_scene_dlight(
                source.dlights[index], source.pose, current, captured_basis,
                current_basis, translated) ||
            !destination.append_dlight(translated)) {
            return false;
        }
    }
    return destination.finish();
}

[[nodiscard]] bool player_scene_body_indices(
    const PlayerSceneProducts& products,
    const std::span<const std::int32_t> body_models,
    std::array<std::size_t, 3>& body_indices) noexcept {
    body_indices.fill(products.ref_count);
    if (body_models.size() != body_indices.size() || products.ref_count == 0U) {
        return false;
    }
    for (std::size_t ref_index = 0; ref_index < products.ref_count; ++ref_index) {
        const auto& ref = products.refs[ref_index];
        if (ql1k::player_scene_ref_type(ref) !=
            static_cast<std::int32_t>(ql1k::PlayerSceneRefType::model)) {
            continue;
        }
        const std::int32_t handle = ql1k::player_scene_ref_hmodel(ref);
        for (std::size_t body = 0; body < body_models.size(); ++body) {
            if (handle != body_models[body]) {
                continue;
            }
            if (body_indices[body] != products.ref_count) {
                return false;
            }
            body_indices[body] = ref_index;
        }
    }
    for (const std::size_t index : body_indices) {
        if (index >= products.ref_count) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool player_scene_ref_axis_scale(
    const ql1k::PlayerSceneRefEntity& ref, float& scale) noexcept {
    std::array<float, 3> row_lengths{};
    for (std::size_t row = 0; row < row_lengths.size(); ++row) {
        float squared_length{};
        for (std::size_t column = 0; column < 3U; ++column) {
            float component{};
            std::memcpy(&component,
                        ref.data() + ql1k::k_player_scene_axis_offset +
                            (row * 3U + column) * sizeof(float),
                        sizeof(component));
            if (!ql1k::player_scene_finite(component)) {
                return false;
            }
            squared_length += component * component;
        }
        if (!ql1k::player_scene_finite(squared_length) ||
            squared_length < 0.0625F || squared_length > 16.0F) {
            return false;
        }
        row_lengths[row] = std::sqrt(squared_length);
    }
    scale = (row_lengths[0] + row_lengths[1] + row_lengths[2]) / 3.0F;
    if (!ql1k::player_scene_finite(scale) || scale < 0.25F || scale > 4.0F) {
        return false;
    }
    const float maximum_row_delta = std::fmax(0.002F, scale * 0.01F);
    for (const float length : row_lengths) {
        if (std::fabs(length - scale) > maximum_row_delta) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool player_scene_scale_ref_axes(
    ql1k::PlayerSceneRefEntity& ref, const float factor) noexcept {
    if (!ql1k::player_scene_finite(factor) || factor < 0.0625F ||
        factor > 16.0F) {
        return false;
    }
    for (std::size_t axis = 0; axis < 9U; ++axis) {
        float component{};
        const std::size_t offset = ql1k::k_player_scene_axis_offset +
                                   axis * sizeof(float);
        std::memcpy(&component, ref.data() + offset, sizeof(component));
        component *= factor;
        if (!ql1k::player_scene_finite(component) ||
            std::fabs(component) > 16.0F) {
            return false;
        }
        std::memcpy(ref.data() + offset, &component, sizeof(component));
    }
    return true;
}

[[nodiscard]] bool player_scene_read_ref_vec3(
    const ql1k::PlayerSceneRefEntity& ref, const std::size_t offset,
    std::array<float, 3>& value) noexcept {
    if (offset + sizeof(value) > ref.size()) {
        return false;
    }
    std::memcpy(value.data(), ref.data() + offset, sizeof(value));
    return std::ranges::all_of(value, [](const float component) noexcept {
        return ql1k::player_scene_finite(component);
    });
}

[[nodiscard]] bool player_scene_write_ref_vec3(
    ql1k::PlayerSceneRefEntity& ref, const std::size_t offset,
    const std::array<float, 3>& value) noexcept {
    if (offset + sizeof(value) > ref.size() ||
        !std::ranges::all_of(value, [](const float component) noexcept {
            return ql1k::player_scene_finite(component);
        })) {
        return false;
    }
    std::memcpy(ref.data() + offset, value.data(), sizeof(value));
    return true;
}

[[nodiscard]] bool refresh_player_scene_body_pose(
    void* const centity, const PlayerSceneProducts& source,
    const std::span<const std::int32_t> body_models,
    PlayerSceneProducts& products, LONG* const failure_stage = nullptr) noexcept {
    if (failure_stage != nullptr) {
        *failure_stage = 0;
    }
    const auto fail = [failure_stage](const LONG stage) noexcept {
        if (failure_stage != nullptr) {
            *failure_stage = stage;
        }
        return false;
    };
    if (centity == nullptr || body_models.size() != 3U || !source.valid ||
        !products.valid || !ql1k::player_scene_pose_valid(source.pose) ||
        !ql1k::player_scene_pose_valid(products.pose)) {
        return fail(1);
    }
    std::array<std::size_t, 3> source_body_indices{};
    std::array<std::size_t, 3> body_indices{};
    if (!player_scene_body_indices(source, body_models, source_body_indices) ||
        !player_scene_body_indices(products, body_models, body_indices)) {
        return fail(2);
    }

    std::array<float, 3> stock_body_scales{};
    for (std::size_t body = 0; body < stock_body_scales.size(); ++body) {
        if (!player_scene_ref_axis_scale(
                source.refs[source_body_indices[body]],
                stock_body_scales[body])) {
            return fail(static_cast<LONG>(3U + body));
        }
    }

    std::array<ql1k::PlayerSceneBasis, 3> body_axes{};
    if (!native_player_scene_body_axes(centity, 0, body_axes)) {
        return fail(6);
    }
    const float render_fraction = static_cast<float>(std::clamp(
        g_transient_frame.fractional_ms, 0.0, 1.0));
    if (render_fraction > 0.0F) {
        std::array<ql1k::PlayerSceneBasis, 3> next_body_axes{};
        if (!native_player_scene_body_axes(centity, 1, next_body_axes)) {
            return fail(7);
        }
        for (std::size_t body = 0; body < body_axes.size(); ++body) {
            ql1k::PlayerSceneBasis interpolated{};
            if (!ql1k::player_scene_interpolate_rotation_basis(
                    body_axes[body], next_body_axes[body], render_fraction,
                    interpolated)) {
                return fail(static_cast<LONG>(8U + body));
            }
            body_axes[body] = interpolated;
        }
    }
    for (std::size_t body = 0; body < body_indices.size(); ++body) {
        std::memcpy(products.refs[body_indices[body]].data() +
                        ql1k::k_player_scene_axis_offset,
                    body_axes[body].axis.data(), 9U * sizeof(float));
    }

    auto& legs = products.refs[body_indices[0]];
    auto& torso = products.refs[body_indices[1]];
    auto& head = products.refs[body_indices[2]];
    const auto& source_legs = source.refs[source_body_indices[0]];
    const auto& source_torso = source.refs[source_body_indices[1]];
    const auto& source_head = source.refs[source_body_indices[2]];

    // Stock CG_Player applies its per-client model scale to the legs before
    // tag_torso. It also anchors the scaled root in world space. Preserve both
    // products from the authoritative sample instead of rotating that root
    // offset with the player's view/body angles.
    if (!player_scene_scale_ref_axes(legs, stock_body_scales[0]) ||
        !player_scene_scale_ref_axes(
            torso, stock_body_scales[1] / stock_body_scales[0])) {
        return fail(11);
    }
    std::array<float, 3> source_legs_origin{};
    std::array<float, 3> current_legs_origin{};
    if (!player_scene_read_ref_vec3(source_legs,
                                    ql1k::k_player_scene_origin_offset,
                                    source_legs_origin)) {
        return fail(12);
    }
    for (std::size_t axis = 0; axis < current_legs_origin.size(); ++axis) {
        current_legs_origin[axis] =
            products.pose.origin[axis] +
            (source_legs_origin[axis] - source.pose.origin[axis]);
    }
    if (!player_scene_write_ref_vec3(legs, ql1k::k_player_scene_origin_offset,
                                     current_legs_origin)) {
        return fail(13);
    }

    static constexpr char k_tag_torso[] = "tag_torso";
    static constexpr char k_tag_head[] = "tag_head";
    if (!native_position_rotated_on_tag(
            torso.data(), legs.data(), body_models[0], k_tag_torso)) {
        return fail(14);
    }
    float recomposed_torso_scale{};
    if (!player_scene_ref_axis_scale(torso, recomposed_torso_scale) ||
        !player_scene_scale_ref_axes(
            torso, stock_body_scales[1] / recomposed_torso_scale) ||
        !native_position_rotated_on_tag(
            head.data(), torso.data(), body_models[1], k_tag_head)) {
        return fail(15);
    }
    float recomposed_head_scale{};
    if (!player_scene_ref_axis_scale(head, recomposed_head_scale) ||
        !player_scene_scale_ref_axes(
            head, stock_body_scales[2] / recomposed_head_scale)) {
        return fail(16);
    }

    // Forced-model head scaling shifts only the final head origin after
    // tag_head. Recover that exact authoritative residual by replaying the tag
    // against the captured torso, then retain it in world space (the installed
    // CG_Player changes origin.z, not the articulated tag basis).
    ql1k::PlayerSceneRefEntity unscaled_source_head = source_head;
    if (!native_position_rotated_on_tag(
            unscaled_source_head.data(), source_torso.data(), body_models[1],
            k_tag_head)) {
        return fail(17);
    }
    std::array<float, 3> source_head_origin{};
    std::array<float, 3> unscaled_source_head_origin{};
    std::array<float, 3> current_head_origin{};
    if (!player_scene_read_ref_vec3(source_head,
                                    ql1k::k_player_scene_origin_offset,
                                    source_head_origin) ||
        !player_scene_read_ref_vec3(unscaled_source_head,
                                    ql1k::k_player_scene_origin_offset,
                                    unscaled_source_head_origin) ||
        !player_scene_read_ref_vec3(head, ql1k::k_player_scene_origin_offset,
                                    current_head_origin)) {
        return fail(18);
    }
    for (std::size_t axis = 0; axis < current_head_origin.size(); ++axis) {
        const float residual =
            source_head_origin[axis] - unscaled_source_head_origin[axis];
        if (!ql1k::player_scene_finite(residual) ||
            std::fabs(residual) > 64.0F) {
            return fail(19);
        }
        current_head_origin[axis] += residual;
    }
    if (!player_scene_write_ref_vec3(head, ql1k::k_player_scene_origin_offset,
                                     current_head_origin)) {
        return fail(20);
    }
    for (const std::size_t index : body_indices) {
        const auto& ref = products.refs[index];
        for (std::size_t axis = 0; axis < 9U; ++axis) {
            float component{};
            std::memcpy(&component,
                        ref.data() + ql1k::k_player_scene_axis_offset +
                            axis * sizeof(float),
                        sizeof(component));
            if (!ql1k::player_scene_finite(component)) {
                return fail(21);
            }
        }
        std::array<float, 3> origin{};
        std::memcpy(origin.data(),
                    ref.data() + ql1k::k_player_scene_origin_offset,
                    sizeof(origin));
        for (const float component : origin) {
            if (!ql1k::player_scene_finite(component)) {
                return fail(22);
            }
        }
    }
    return true;
}

[[nodiscard]] float player_scene_vec3_distance_squared(
    const std::array<float, 3>& left,
    const std::array<float, 3>& right) noexcept {
    float result{};
    for (std::size_t axis = 0; axis < left.size(); ++axis) {
        const float delta = left[axis] - right[axis];
        result += delta * delta;
    }
    return result;
}

[[nodiscard]] bool player_scene_ref_unit_basis(
    const ql1k::PlayerSceneRefEntity& ref,
    ql1k::PlayerSceneBasis& basis) noexcept {
    float scale{};
    if (!player_scene_ref_axis_scale(ref, scale)) {
        return false;
    }
    for (std::size_t row = 0; row < 3U; ++row) {
        for (std::size_t axis = 0; axis < 3U; ++axis) {
            std::memcpy(&basis.axis[row][axis],
                        ref.data() + ql1k::k_player_scene_axis_offset +
                            (row * 3U + axis) * sizeof(float),
                        sizeof(float));
            basis.axis[row][axis] /= scale;
        }
    }
    return ql1k::player_scene_basis_valid(basis);
}

[[nodiscard]] bool player_scene_transform_child_axes(
    const ql1k::PlayerSceneRefEntity& source_child,
    const ql1k::PlayerSceneRefEntity& source_parent,
    const ql1k::PlayerSceneRefEntity& current_parent,
    ql1k::PlayerSceneRefEntity& current_child) noexcept {
    ql1k::PlayerSceneBasis source_parent_basis{};
    ql1k::PlayerSceneBasis current_parent_basis{};
    if (!player_scene_ref_unit_basis(source_parent, source_parent_basis) ||
        !player_scene_ref_unit_basis(current_parent, current_parent_basis)) {
        return false;
    }
    for (std::size_t row = 0; row < 3U; ++row) {
        std::array<float, 3> source_axis{};
        std::array<float, 3> current_axis{};
        std::memcpy(source_axis.data(),
                    source_child.data() + ql1k::k_player_scene_axis_offset +
                        row * 3U * sizeof(float),
                    sizeof(source_axis));
        if (!ql1k::player_scene_rotate_world_vector(
                source_axis, source_parent_basis, current_parent_basis,
                current_axis)) {
            return false;
        }
        std::memcpy(current_child.data() + ql1k::k_player_scene_axis_offset +
                        row * 3U * sizeof(float),
                    current_axis.data(), sizeof(current_axis));
    }
    return true;
}

[[nodiscard]] bool resolve_player_scene_dlight_bindings(
    const PlayerSceneProducts& source,
    const std::span<const std::int32_t> body_models,
    std::array<PlayerSceneDlightBinding, k_player_scene_max_dlights>&
        bindings) noexcept {
    bindings = {};
    if (!source.valid || source.dlight_count > bindings.size()) {
        return false;
    }
    if (source.dlight_count == 0U) {
        return true;
    }
    std::array<std::size_t, 3> body_indices{};
    if (!player_scene_body_indices(source, body_models, body_indices)) {
        return false;
    }
    static constexpr char k_tag_weapon[] = "tag_weapon";
    static constexpr char k_tag_flash[] = "tag_flash";
    for (std::size_t light_index = 0; light_index < source.dlight_count;
         ++light_index) {
        const auto& light = source.dlights[light_index];
        std::size_t flash_index = source.ref_count;
        float best_flash_distance = 0.0625F;
        for (std::size_t ref_index = 0; ref_index < source.ref_count;
             ++ref_index) {
            const auto& ref = source.refs[ref_index];
            if (ql1k::player_scene_ref_type(ref) !=
                static_cast<std::int32_t>(ql1k::PlayerSceneRefType::model)) {
                continue;
            }
            std::array<float, 3> origin{};
            if (!player_scene_read_ref_vec3(
                    ref, ql1k::k_player_scene_origin_offset, origin)) {
                return false;
            }
            const float distance =
                player_scene_vec3_distance_squared(origin, light.origin);
            if (distance <= best_flash_distance) {
                best_flash_distance = distance;
                flash_index = ref_index;
            }
        }
        if (flash_index < source.ref_count) {
            std::array<float, 3> flash_origin{};
            if (!player_scene_read_ref_vec3(
                    source.refs[flash_index],
                    ql1k::k_player_scene_origin_offset, flash_origin)) {
                return false;
            }
            std::size_t gun_index = source.ref_count;
            float best_gun_distance = 0.25F;
            for (std::size_t ref_index = 0; ref_index < source.ref_count;
                 ++ref_index) {
                if (ref_index == flash_index ||
                    std::ranges::find(body_indices, ref_index) !=
                        body_indices.end()) {
                    continue;
                }
                const auto& candidate = source.refs[ref_index];
                if (ql1k::player_scene_ref_type(candidate) !=
                    static_cast<std::int32_t>(
                        ql1k::PlayerSceneRefType::model)) {
                    continue;
                }
                const std::int32_t model =
                    ql1k::player_scene_ref_hmodel(candidate);
                if (model == 0) {
                    continue;
                }
                auto attached = source.refs[flash_index];
                std::array<float, 3> attached_origin{};
                if (!native_position_rotated_on_tag(
                        attached.data(), candidate.data(), model, k_tag_flash) ||
                    !player_scene_read_ref_vec3(
                        attached, ql1k::k_player_scene_origin_offset,
                        attached_origin)) {
                    return false;
                }
                const float distance = player_scene_vec3_distance_squared(
                    attached_origin, flash_origin);
                if (distance <= best_gun_distance) {
                    best_gun_distance = distance;
                    gun_index = ref_index;
                }
            }
            if (gun_index < source.ref_count) {
                auto attached_gun = source.refs[gun_index];
                std::array<float, 3> attached_gun_origin{};
                std::array<float, 3> source_gun_origin{};
                if (!native_position_on_tag(
                        attached_gun.data(), source.refs[body_indices[1]].data(),
                        body_models[1], k_tag_weapon) ||
                    !player_scene_read_ref_vec3(
                        attached_gun, ql1k::k_player_scene_origin_offset,
                        attached_gun_origin) ||
                    !player_scene_read_ref_vec3(
                        source.refs[gun_index],
                        ql1k::k_player_scene_origin_offset,
                        source_gun_origin) ||
                    player_scene_vec3_distance_squared(
                        attached_gun_origin, source_gun_origin) > 0.25F) {
                    return false;
                }
                bindings[light_index].kind =
                    PlayerSceneDlightBinding::Kind::weapon_flash;
                bindings[light_index].gun_ref =
                    static_cast<std::int16_t>(gun_index);
                bindings[light_index].flash_ref =
                    static_cast<std::int16_t>(flash_index);
                if (light_index == 0U) {
                    InterlockedExchange(&g_player_scene_binding_gun_ref,
                                        static_cast<LONG>(gun_index));
                    InterlockedExchange(&g_player_scene_binding_flash_ref,
                                        static_cast<LONG>(flash_index));
                    InterlockedExchange(
                        &g_player_scene_binding_gun_model,
                        ql1k::player_scene_ref_hmodel(source.refs[gun_index]));
                    InterlockedExchange(
                        &g_player_scene_binding_flash_model,
                        ql1k::player_scene_ref_hmodel(source.refs[flash_index]));
                }
                continue;
            }
        }
        if (player_scene_vec3_distance_squared(light.origin,
                                               source.pose.origin) > 1.0F) {
            return false;
        }
        bindings[light_index].kind =
            PlayerSceneDlightBinding::Kind::translation;
    }
    return true;
}

[[nodiscard]] bool refresh_player_scene_dlight_bindings(
    const PlayerSceneProducts& source,
    const std::span<const std::int32_t> body_models,
    const std::span<const PlayerSceneDlightBinding> bindings,
    PlayerSceneProducts& products) noexcept {
    if (!source.valid || !products.valid ||
        source.dlight_count != products.dlight_count ||
        bindings.size() < source.dlight_count) {
        return false;
    }
    std::array<std::size_t, 3> body_indices{};
    if (!player_scene_body_indices(products, body_models, body_indices)) {
        return false;
    }
    static constexpr char k_tag_weapon[] = "tag_weapon";
    static constexpr char k_tag_flash[] = "tag_flash";
    for (std::size_t light_index = 0; light_index < source.dlight_count;
         ++light_index) {
        const auto& binding = bindings[light_index];
        if (binding.kind == PlayerSceneDlightBinding::Kind::translation) {
            continue;
        }
        if (binding.kind !=
                PlayerSceneDlightBinding::Kind::weapon_flash ||
            binding.gun_ref < 0 || binding.flash_ref < 0) {
            return false;
        }
        const std::size_t gun_index =
            static_cast<std::size_t>(binding.gun_ref);
        const std::size_t flash_index =
            static_cast<std::size_t>(binding.flash_ref);
        if (gun_index >= source.ref_count || flash_index >= source.ref_count ||
            gun_index >= products.ref_count ||
            flash_index >= products.ref_count) {
            return false;
        }
        const auto& source_gun = source.refs[gun_index];
        const auto& source_flash = source.refs[flash_index];
        auto& current_gun = products.refs[gun_index];
        auto& current_flash = products.refs[flash_index];
        if (ql1k::player_scene_ref_hmodel(source_gun) == 0 ||
            ql1k::player_scene_ref_hmodel(source_gun) !=
                ql1k::player_scene_ref_hmodel(current_gun) ||
            ql1k::player_scene_ref_hmodel(source_flash) !=
                ql1k::player_scene_ref_hmodel(current_flash) ||
            !native_position_on_tag(
                current_gun.data(), products.refs[body_indices[1]].data(),
                body_models[1], k_tag_weapon) ||
            !player_scene_transform_child_axes(
                source_flash, source_gun, current_gun, current_flash)) {
            return false;
        }
        auto attached_flash = current_flash;
        if (!native_position_rotated_on_tag(
                attached_flash.data(), current_gun.data(),
                ql1k::player_scene_ref_hmodel(current_gun), k_tag_flash)) {
            return false;
        }
        std::array<float, 3> source_flash_origin{};
        std::array<float, 3> current_flash_origin{};
        if (!player_scene_read_ref_vec3(
                source_flash, ql1k::k_player_scene_origin_offset,
                source_flash_origin) ||
            !player_scene_read_ref_vec3(
                attached_flash, ql1k::k_player_scene_origin_offset,
                current_flash_origin) ||
            !player_scene_write_ref_vec3(
                current_flash, ql1k::k_player_scene_origin_offset,
                current_flash_origin)) {
            return false;
        }
        // The exact CG_Player dlight helper receives centity plus a temporary
        // flash orientation, but the installed binary's origin for this
        // weapon-flash shape remains the authoritative dlight origin after
        // the high-resolution player translation.  build_translated_* has
        // already produced that value.  Do not replace it with tag_flash's
        // model origin: that is a different scene product and produced a
        // measured 0.105-unit validation error in v62.
        if (binding.origin_mode ==
            PlayerSceneDlightBinding::OriginMode::tagged) {
            products.dlights[light_index].origin = current_flash_origin;
        }

        ql1k::PlayerSceneBasis source_gun_basis{};
        ql1k::PlayerSceneBasis current_gun_basis{};
        if (!player_scene_ref_unit_basis(source_gun, source_gun_basis) ||
            !player_scene_ref_unit_basis(current_gun, current_gun_basis)) {
            return false;
        }
        for (std::size_t ref_index = 0; ref_index < products.ref_count;
             ++ref_index) {
            const auto type = static_cast<ql1k::PlayerSceneRefType>(
                ql1k::player_scene_ref_type(source.refs[ref_index]));
            if (type != ql1k::PlayerSceneRefType::beam &&
                type != ql1k::PlayerSceneRefType::lightning) {
                continue;
            }
            std::array<float, 3> source_start{};
            if (!player_scene_read_ref_vec3(
                    source.refs[ref_index],
                    ql1k::k_player_scene_origin_offset, source_start)) {
                return false;
            }
            std::array<float, 3> relative{};
            for (std::size_t axis = 0; axis < relative.size(); ++axis) {
                relative[axis] = source_start[axis] - source_flash_origin[axis];
            }
            if (player_scene_vec3_distance_squared(relative, {}) >
                64.0F * 64.0F) {
                continue;
            }
            std::array<float, 3> rotated{};
            if (!ql1k::player_scene_rotate_world_vector(
                    relative, source_gun_basis, current_gun_basis, rotated)) {
                return false;
            }
            for (std::size_t axis = 0; axis < rotated.size(); ++axis) {
                rotated[axis] += current_flash_origin[axis];
            }
            if (!player_scene_write_ref_vec3(
                    products.refs[ref_index],
                    ql1k::k_player_scene_origin_offset, rotated)) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] bool player_scene_has_lightning(
    const PlayerSceneProducts& products) noexcept {
    for (std::size_t index = 0; index < products.ref_count; ++index) {
        if (ql1k::player_scene_ref_type(products.refs[index]) ==
            static_cast<std::int32_t>(ql1k::PlayerSceneRefType::lightning)) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool player_scene_is_local_entity(
    const void* const centity) noexcept {
    const auto* const local_client_source = static_cast<const std::int32_t*>(
        cgame_address(k_cgame_style_local_client));
    if (centity == nullptr || local_client_source == nullptr) {
        return false;
    }
    std::int32_t entity_number{};
    std::int32_t local_client{};
    std::memcpy(&entity_number, centity, sizeof(entity_number));
    std::memcpy(&local_client, local_client_source, sizeof(local_client));
    return entity_number >= 0 && entity_number < 64 &&
           local_client >= 0 && local_client < 64 &&
           entity_number == local_client;
}

[[nodiscard]] bool refresh_player_scene_lightning_endpoints(
    const void* const centity, const ql1k::PlayerScenePose& current,
    PlayerSceneProducts& products) noexcept {
    if (!player_scene_has_lightning(products)) {
        return true;
    }
    const auto trace = reinterpret_cast<PointTraceFn>(
        cgame_address(k_cgame_point_trace));
    void* const alternate_trace = cgame_address(k_cgame_alternate_point_trace);
    const auto* const serverinfo_flags = static_cast<const std::int32_t*>(
        cgame_address(k_cgame_serverinfo_flags));
    const auto* const local_client_source = static_cast<const std::int32_t*>(
        cgame_address(k_cgame_style_local_client));
    const auto* const trace_mode_source = static_cast<const std::int32_t*>(
        cgame_address(k_cgame_trace_mode));
    if (centity == nullptr || trace == nullptr || alternate_trace == nullptr ||
        serverinfo_flags == nullptr || local_client_source == nullptr ||
        trace_mode_source == nullptr || !ql1k::player_scene_pose_valid(current)) {
        return false;
    }
    std::int32_t entity_number{};
    std::int32_t local_client{};
    std::int32_t legs_animation{};
    std::int32_t trace_mode{};
    std::memcpy(&entity_number, centity, sizeof(entity_number));
    std::memcpy(&local_client, local_client_source, sizeof(local_client));
    std::memcpy(&trace_mode, trace_mode_source, sizeof(trace_mode));
    std::memcpy(&legs_animation,
                static_cast<const std::byte*>(centity) + 0xD8U,
                sizeof(legs_animation));
    if (entity_number < 0 || entity_number >= 64 || local_client < 0 ||
        local_client >= 64) {
        return false;
    }
    // The exact installed CG_LightningBolt binary traces the local beam from
    // camera/predicted globals rather than centity lerpOrigin/lerpAngles. Until
    // that separate branch is reproduced exactly, fail closed so the caller
    // renders this player through stock and preserves frame-cadence mouse aim.
    if (entity_number == local_client) {
        return false;
    }
    legs_animation &= ~0x80;
    const float viewheight =
        legs_animation == 13 || legs_animation == 23 ? 12.0F : 26.0F;
    std::array<float, 3> forward{};
    if (!native_angle_vectors(current.angles.data(), forward.data())) {
        return false;
    }
    std::array<float, 3> start = current.origin;
    start[2] += viewheight;
    std::array<float, 3> end{};
    for (std::size_t axis = 0; axis < start.size(); ++axis) {
        // Exact cgame 0x10073BA0 double constant (SHA-256-gated build): 5.0.
        start[axis] = native_scaled_add(start[axis], forward[axis], 5.0);
        end[axis] = native_scaled_add(start[axis], forward[axis], 768.0);
    }
    std::array<std::uint32_t, 14> trace_result{};
    const std::int32_t mask =
        (*serverinfo_flags & 0x02000000) != 0 ? 0x00000001 : 0x06000001;
    if (trace_mode == 0) {
        trace(trace_result.data(), start.data(), nullptr, nullptr, end.data(),
              entity_number, mask);
    } else if (!native_alternate_point_trace(
                   alternate_trace, trace_result.data(), start.data(), end.data(),
                   entity_number, mask)) {
        return false;
    }
    std::array<float, 3> endpoint{};
    std::memcpy(endpoint.data(),
                trace_result.data() + k_trace_endpoint_index,
                sizeof(endpoint));
    for (const float component : endpoint) {
        if (!ql1k::player_scene_finite(component)) {
            return false;
        }
    }
    for (std::size_t index = 0; index < products.ref_count; ++index) {
        if (ql1k::player_scene_ref_type(products.refs[index]) !=
            static_cast<std::int32_t>(ql1k::PlayerSceneRefType::lightning)) {
            continue;
        }
        std::memcpy(products.refs[index].data() +
                        ql1k::k_player_scene_old_origin_offset,
                    endpoint.data(), sizeof(endpoint));
    }
    return true;
}

[[nodiscard]] bool refresh_player_scene_world_surfaces(
    const void* const centity, PlayerSceneProducts& products,
    const EngineSceneSnapshot& before,
    EngineSceneSnapshot& after_surfaces) noexcept {
    const auto* const shadow_mode_source = static_cast<const std::int32_t*>(
        cgame_address(k_cgame_shadow_mode));
    void* const shadow_function = cgame_address(k_cgame_player_shadow);
    void* const wake_function = cgame_address(k_cgame_player_wake);
    if (centity == nullptr || shadow_mode_source == nullptr ||
        shadow_function == nullptr || wake_function == nullptr || !before.valid) {
        return false;
    }
    if (before.poly_count >
            before.max_polys - static_cast<std::int32_t>(k_player_scene_max_polys) ||
        before.polyvert_count >
            before.max_polyverts -
                static_cast<std::int32_t>(k_player_scene_max_vertices)) {
        return false;
    }
    std::int32_t shadow_mode{};
    std::memcpy(&shadow_mode, shadow_mode_source, sizeof(shadow_mode));
    // Mode 1 is the real blob/mark path used by the locked visual config.
    // Other modes change model render flags based on the trace result and stay
    // on stock until that separate contract is implemented.
    if (shadow_mode != 1) {
        return false;
    }
    float shadow_plane{};
    std::int32_t shadow_visible{};
    if (!native_player_shadow(shadow_function, centity, &shadow_plane,
                              &shadow_visible) ||
        !native_player_wake(wake_function, centity) ||
        !ql1k::player_scene_finite(shadow_plane) ||
        (shadow_visible != 0 && shadow_visible != 1)) {
        if (!restore_engine_scene_counts(before)) {
            mark_permanent_fault("player_surface_rollback_failed");
        }
        return false;
    }
    after_surfaces = capture_engine_scene_snapshot();
    if (!after_surfaces.valid || before.frame_index != after_surfaces.frame_index ||
        before.root != after_surfaces.root ||
        before.ref_count != after_surfaces.ref_count ||
        before.dlight_count != after_surfaces.dlight_count ||
        after_surfaces.poly_count < before.poly_count ||
        after_surfaces.polyvert_count < before.polyvert_count ||
        after_surfaces.poly_count - before.poly_count >
            static_cast<std::int32_t>(k_player_scene_max_polys) ||
        after_surfaces.polyvert_count - before.polyvert_count >
            static_cast<std::int32_t>(k_player_scene_max_vertices)) {
        if (!restore_engine_scene_counts(before)) {
            mark_permanent_fault("player_surface_rollback_failed");
        }
        return false;
    }
    for (std::size_t index = 0; index < products.ref_count; ++index) {
        auto& ref = products.refs[index];
        if (ql1k::player_scene_ref_type(ref) !=
            static_cast<std::int32_t>(ql1k::PlayerSceneRefType::model)) {
            continue;
        }
        std::memcpy(ref.data() + ql1k::k_player_scene_shadow_plane_offset,
                    &shadow_plane, sizeof(shadow_plane));
    }
    return true;
}

void record_player_scene_mismatch(LONG kind, LONG index, LONG offset,
                                  LONG expected, LONG actual) noexcept;
void diagnose_player_scene_world_surface_mismatch(
    const PlayerSceneProducts& expected,
    const PlayerSceneProducts& actual) noexcept;

[[nodiscard]] bool validate_player_scene_world_surfaces(
    const void* const centity, const ql1k::PlayerScenePose& current,
    PlayerSceneProducts& expected,
    const PlayerSceneProducts& stock_actual) noexcept {
    const EngineSceneSnapshot before = capture_engine_scene_snapshot();
    EngineSceneSnapshot after{};
    if (!before.valid) {
        record_player_scene_mismatch(11, -1, -1, 1, 0);
        return false;
    }
    if (!refresh_player_scene_world_surfaces(centity, expected, before, after)) {
        record_player_scene_mismatch(11, -2, -1, 1, 0);
        return false;
    }

    auto& native_actual = g_player_scene_surface_scratch;
    const bool empty = after.poly_count == before.poly_count &&
                       after.polyvert_count == before.polyvert_count;
    bool captured = false;
    if (empty) {
        native_actual.begin(current);
        captured = true;
    } else {
        captured = capture_player_scene_products(before, after, current,
                                                  native_actual);
    }
    if (!restore_engine_scene_counts(before)) {
        mark_permanent_fault("player_surface_validation_rollback_failed");
        return false;
    }
    if (!captured) {
        record_player_scene_mismatch(
            11, -3, -1,
            static_cast<LONG>(after.poly_count - before.poly_count),
            static_cast<LONG>(after.polyvert_count - before.polyvert_count));
        return false;
    }
    if (!ql1k::player_scene_world_surfaces_near(
            stock_actual, native_actual, 0.01F)) {
        diagnose_player_scene_world_surface_mismatch(stock_actual,
                                                     native_actual);
        return false;
    }
    return true;
}

[[nodiscard]] bool player_scene_cache_age_valid(
    const PlayerSceneCacheEntry& entry, const LONG64 now) noexcept {
    const LONG64 frequency = InterlockedCompareExchange64(&g_qpc_frequency, 0, 0);
    if (frequency <= 0 || entry.captured_qpc <= 0 || now < entry.captured_qpc) {
        return false;
    }
    const LONG64 maximum = frequency / 500LL;
    return maximum > 0 && now - entry.captured_qpc <= maximum;
}

[[nodiscard]] PlayerSceneShapeProof* player_scene_shape_proof(
    const ql1k::PlayerSceneShapeSignature& shape) noexcept {
    for (std::size_t index = 0; index < g_player_scene_shape_proof_count;
         ++index) {
        auto& proof = g_player_scene_shape_proofs[index];
        if (proof.occupied && proof.shape == shape) {
            return &proof;
        }
    }
    return nullptr;
}

[[nodiscard]] bool player_scene_shape_proven(
    const ql1k::PlayerSceneShapeSignature& shape) noexcept {
    const auto* const proof = player_scene_shape_proof(shape);
    return proof != nullptr &&
           proof->successful_comparisons >=
               k_player_scene_shape_validation_threshold;
}

void note_player_scene_shape_validation(
    const ql1k::PlayerSceneShapeSignature& shape) noexcept {
    auto* proof = player_scene_shape_proof(shape);
    if (proof == nullptr) {
        if (g_player_scene_shape_proof_count >=
            g_player_scene_shape_proofs.size()) {
            return;
        }
        proof = &g_player_scene_shape_proofs[g_player_scene_shape_proof_count++];
        proof->shape = shape;
        proof->successful_comparisons = 0U;
        proof->occupied = true;
    }
    if (proof->successful_comparisons <
        k_player_scene_shape_validation_threshold) {
        ++proof->successful_comparisons;
    }
}

void record_player_scene_mismatch(const LONG kind, const LONG index,
                                  const LONG offset, const LONG expected,
                                  const LONG actual) noexcept {
    if (InterlockedCompareExchange(&g_player_scene_mismatch_kind, 0, 0) != 0) {
        return;
    }
    InterlockedExchange(&g_player_scene_mismatch_index, index);
    InterlockedExchange(&g_player_scene_mismatch_offset, offset);
    InterlockedExchange(&g_player_scene_mismatch_expected, expected);
    InterlockedExchange(&g_player_scene_mismatch_actual, actual);
    MemoryBarrier();
    InterlockedCompareExchange(&g_player_scene_mismatch_kind, kind, 0);
}

void diagnose_player_scene_world_surface_mismatch(
    const PlayerSceneProducts& expected,
    const PlayerSceneProducts& actual) noexcept {
    const auto bits = [](const float value) noexcept {
        LONG result{};
        std::memcpy(&result, &value, sizeof(result));
        return result;
    };
    if (expected.poly_count != actual.poly_count) {
        record_player_scene_mismatch(
            11, -10, -1, static_cast<LONG>(expected.poly_count),
            static_cast<LONG>(actual.poly_count));
        return;
    }
    if (expected.vertex_count != actual.vertex_count) {
        record_player_scene_mismatch(
            11, -11, -1, static_cast<LONG>(expected.vertex_count),
            static_cast<LONG>(actual.vertex_count));
        return;
    }
    for (std::size_t index = 0; index < expected.poly_count; ++index) {
        const auto& left = expected.polys[index];
        const auto& right = actual.polys[index];
        if (left.shader != right.shader) {
            record_player_scene_mismatch(11, static_cast<LONG>(index), 0,
                                         left.shader, right.shader);
            return;
        }
        if (left.first_vertex != right.first_vertex) {
            record_player_scene_mismatch(
                11, static_cast<LONG>(index), 1,
                static_cast<LONG>(left.first_vertex),
                static_cast<LONG>(right.first_vertex));
            return;
        }
        if (left.vertex_count != right.vertex_count) {
            record_player_scene_mismatch(
                11, static_cast<LONG>(index), 2,
                static_cast<LONG>(left.vertex_count),
                static_cast<LONG>(right.vertex_count));
            return;
        }
    }
    for (std::size_t index = 0; index < expected.vertex_count; ++index) {
        for (std::size_t axis = 0; axis < 3U; ++axis) {
            const float left = expected.vertices[index].xyz[axis];
            const float right = actual.vertices[index].xyz[axis];
            if (!ql1k::player_scene_float_near(left, right, 0.01F)) {
                record_player_scene_mismatch(
                    11, static_cast<LONG>(index),
                    static_cast<LONG>(10U + axis), bits(left), bits(right));
                return;
            }
        }
        for (std::size_t axis = 0; axis < 2U; ++axis) {
            const float left = expected.vertices[index].st[axis];
            const float right = actual.vertices[index].st[axis];
            if (!ql1k::player_scene_float_near(left, right, 0.01F)) {
                record_player_scene_mismatch(
                    11, static_cast<LONG>(index),
                    static_cast<LONG>(20U + axis), bits(left), bits(right));
                return;
            }
        }
        for (std::size_t channel = 0; channel < 4U; ++channel) {
            const LONG left = expected.vertices[index].modulate[channel];
            const LONG right = actual.vertices[index].modulate[channel];
            if (left - right < -1 || left - right > 1) {
                record_player_scene_mismatch(
                    11, static_cast<LONG>(index),
                    static_cast<LONG>(30U + channel), left, right);
                return;
            }
        }
    }
    record_player_scene_mismatch(11, -12, -1, 0, 0);
}

void diagnose_player_scene_mismatch(
    const PlayerSceneProducts& expected,
    const PlayerSceneProducts& actual) noexcept {
    const auto count_mismatch = [](const std::uint16_t left,
                                   const std::uint16_t right,
                                   const LONG field) noexcept {
        if (left == right) {
            return false;
        }
        record_player_scene_mismatch(1, field, -1, static_cast<LONG>(left),
                                     static_cast<LONG>(right));
        return true;
    };
    if (!expected.valid || !actual.valid) {
        record_player_scene_mismatch(1, -1, -1, expected.valid ? 1 : 0,
                                     actual.valid ? 1 : 0);
        return;
    }
    if (count_mismatch(expected.ref_count, actual.ref_count, 0) ||
        count_mismatch(expected.poly_count, actual.poly_count, 1) ||
        count_mismatch(expected.vertex_count, actual.vertex_count, 2) ||
        count_mismatch(expected.dlight_count, actual.dlight_count, 3)) {
        return;
    }
    // Repeated stock tag composition (legs -> torso -> weapon -> barrel/flash)
    // amplifies sub-ULP x87 differences. Positions stay below one hundredth of
    // a world unit; player_scene_ref_float_tolerance gives normalized axes a
    // two-degree component ceiling and beam attachment fields a bounded margin.
    // All structural fields remain byte-exact.
    constexpr float tolerance = 0.01F;
    const auto bits = [](const float value) noexcept {
        LONG result{};
        std::memcpy(&result, &value, sizeof(result));
        return result;
    };
    for (std::size_t index = 0; index < expected.ref_count; ++index) {
        const auto& left = expected.refs[index];
        const auto& right = actual.refs[index];
        const std::int32_t left_type = ql1k::player_scene_ref_type(left);
        const std::int32_t right_type = ql1k::player_scene_ref_type(right);
        if (left_type != right_type) {
            record_player_scene_mismatch(2, static_cast<LONG>(index), 0,
                                         left_type, right_type);
            return;
        }
        std::array<bool, ql1k::k_player_scene_ref_entity_bytes> near_float{};
        std::array<bool, ql1k::k_player_scene_ref_entity_bytes> ignored{};
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
        mark_vec3(ql1k::k_player_scene_lighting_origin_offset);
        mark_float(ql1k::k_player_scene_shadow_plane_offset);
        for (std::size_t axis = 0; axis < 9U; ++axis) {
            mark_float(ql1k::k_player_scene_axis_offset +
                       axis * sizeof(float));
        }
        mark_vec3(ql1k::k_player_scene_origin_offset);
        mark_vec3(ql1k::k_player_scene_old_origin_offset);
        mark_float(ql1k::k_player_scene_backlerp_offset);
        mark_float(ql1k::k_player_scene_shader_texcoord_offset);
        mark_float(ql1k::k_player_scene_shader_texcoord_offset + sizeof(float));
        mark_float(ql1k::k_player_scene_shader_time_offset);
        mark_float(ql1k::k_player_scene_radius_offset);
        mark_float(ql1k::k_player_scene_rotation_offset);
        switch (static_cast<ql1k::PlayerSceneRefType>(left_type)) {
        case ql1k::PlayerSceneRefType::model:
            ignore_vec3(ql1k::k_player_scene_old_origin_offset);
            break;
        case ql1k::PlayerSceneRefType::sprite:
        case ql1k::PlayerSceneRefType::beam:
        case ql1k::PlayerSceneRefType::lightning:
        case ql1k::PlayerSceneRefType::rail_core:
        case ql1k::PlayerSceneRefType::rail_rings:
            break;
        default:
            record_player_scene_mismatch(2, static_cast<LONG>(index), 0,
                                         left_type, right_type);
            return;
        }
        for (std::size_t offset = 0; offset < left.size(); offset += sizeof(float)) {
            if (!near_float[offset] || ignored[offset]) {
                continue;
            }
            float left_float{};
            float right_float{};
            std::memcpy(&left_float, left.data() + offset, sizeof(left_float));
            std::memcpy(&right_float, right.data() + offset, sizeof(right_float));
            const float field_tolerance = ql1k::player_scene_ref_float_tolerance(
                static_cast<ql1k::PlayerSceneRefType>(left_type), offset,
                tolerance);
            if (!ql1k::player_scene_float_near(left_float, right_float,
                                               field_tolerance)) {
                record_player_scene_mismatch(
                    4, static_cast<LONG>(index), static_cast<LONG>(offset),
                    bits(left_float), bits(right_float));
                return;
            }
        }
        for (std::size_t offset = 0; offset < left.size(); ++offset) {
            if (!near_float[offset] && !ignored[offset] &&
                left[offset] != right[offset]) {
                record_player_scene_mismatch(
                    3, static_cast<LONG>(index), static_cast<LONG>(offset),
                    static_cast<LONG>(std::to_integer<std::uint8_t>(left[offset])),
                    static_cast<LONG>(std::to_integer<std::uint8_t>(right[offset])));
                return;
            }
        }
    }
    for (std::size_t index = 0; index < expected.poly_count; ++index) {
        if (expected.polys[index] != actual.polys[index]) {
            record_player_scene_mismatch(5, static_cast<LONG>(index), -1,
                                         expected.polys[index].shader,
                                         actual.polys[index].shader);
            return;
        }
    }
    for (std::size_t index = 0; index < expected.vertex_count; ++index) {
        for (std::size_t axis = 0; axis < 3U; ++axis) {
            const float left = expected.vertices[index].xyz[axis];
            const float right = actual.vertices[index].xyz[axis];
            if (!ql1k::player_scene_float_near(left, right, tolerance)) {
                record_player_scene_mismatch(
                    6, static_cast<LONG>(index), static_cast<LONG>(axis),
                    bits(left), bits(right));
                return;
            }
        }
        for (std::size_t axis = 0; axis < 2U; ++axis) {
            const float left = expected.vertices[index].st[axis];
            const float right = actual.vertices[index].st[axis];
            if (!ql1k::player_scene_float_near(left, right, tolerance)) {
                record_player_scene_mismatch(
                    7, static_cast<LONG>(index), static_cast<LONG>(axis),
                    bits(left), bits(right));
                return;
            }
        }
        for (std::size_t channel = 0; channel < 4U; ++channel) {
            const LONG left = expected.vertices[index].modulate[channel];
            const LONG right = actual.vertices[index].modulate[channel];
            if (left - right < -1 || left - right > 1) {
                record_player_scene_mismatch(
                    10, static_cast<LONG>(index), static_cast<LONG>(channel),
                    left, right);
                return;
            }
        }
    }
    for (std::size_t index = 0; index < expected.dlight_count; ++index) {
        const auto& left = expected.dlights[index];
        const auto& right = actual.dlights[index];
        if (left.additive != right.additive) {
            record_player_scene_mismatch(8, static_cast<LONG>(index), 7,
                                         left.additive, right.additive);
            return;
        }
        for (std::size_t axis = 0; axis < 3U; ++axis) {
            if (!ql1k::player_scene_float_near(left.origin[axis],
                                               right.origin[axis], tolerance)) {
                record_player_scene_mismatch(
                    8, static_cast<LONG>(index), static_cast<LONG>(axis),
                    bits(left.origin[axis]), bits(right.origin[axis]));
                return;
            }
            if (!ql1k::player_scene_float_near(left.color[axis],
                                               right.color[axis], tolerance)) {
                record_player_scene_mismatch(
                    8, static_cast<LONG>(index), static_cast<LONG>(4U + axis),
                    bits(left.color[axis]), bits(right.color[axis]));
                return;
            }
        }
        if (!ql1k::player_scene_float_near(left.radius, right.radius,
                                           tolerance)) {
            record_player_scene_mismatch(
                8, static_cast<LONG>(index), 3, bits(left.radius),
                bits(right.radius));
            return;
        }
    }
    record_player_scene_mismatch(9, -1, -1, 0, 0);
}

void record_player_scene_mismatch_context(
    const PlayerSceneProducts& source,
    const ql1k::PlayerScenePose& current_pose,
    const std::span<const std::int32_t> body_models) noexcept {
    const LONG index =
        InterlockedCompareExchange(&g_player_scene_mismatch_index, 0, 0);
    const LONG offset =
        InterlockedCompareExchange(&g_player_scene_mismatch_offset, 0, 0);
    const LONG kind =
        InterlockedCompareExchange(&g_player_scene_mismatch_kind, 0, 0);
    LONG source_bits{};
    LONG ref_type{-1};
    LONG beam_length_bits{};
    const bool dlight_context =
        kind == 8 && index >= 0 &&
        static_cast<std::size_t>(index) < source.dlight_count;
    if (dlight_context) {
        const auto& dlight = source.dlights[static_cast<std::size_t>(index)];
        ref_type = -2;
        std::memcpy(&beam_length_bits, &dlight.radius,
                    sizeof(beam_length_bits));
        const float* field{};
        if (offset >= 0 && offset < 3) {
            field = &dlight.origin[static_cast<std::size_t>(offset)];
        } else if (offset == 3) {
            field = &dlight.radius;
        } else if (offset >= 4 && offset < 7) {
            field = &dlight.color[static_cast<std::size_t>(offset - 4)];
        }
        if (field != nullptr) {
            std::memcpy(&source_bits, field, sizeof(source_bits));
        } else if (offset == 7) {
            source_bits = dlight.additive;
        }
    } else if (index >= 0 &&
               static_cast<std::size_t>(index) < source.ref_count) {
        const auto& source_ref = source.refs[static_cast<std::size_t>(index)];
        ref_type = ql1k::player_scene_ref_type(source_ref);
        const auto typed = static_cast<ql1k::PlayerSceneRefType>(ref_type);
        if (typed == ql1k::PlayerSceneRefType::model) {
            const std::int32_t hmodel =
                ql1k::player_scene_ref_hmodel(source_ref);
            beam_length_bits = hmodel;
            for (std::size_t body = 0; body < body_models.size(); ++body) {
                if (hmodel == body_models[body]) {
                    ref_type = static_cast<LONG>(100U + body);
                    break;
                }
            }
        }
        if (typed == ql1k::PlayerSceneRefType::beam ||
            typed == ql1k::PlayerSceneRefType::lightning) {
            std::array<float, 3> origin{};
            std::array<float, 3> endpoint{};
            std::memcpy(origin.data(),
                        source_ref.data() + ql1k::k_player_scene_origin_offset,
                        sizeof(origin));
            std::memcpy(endpoint.data(),
                        source_ref.data() + ql1k::k_player_scene_old_origin_offset,
                        sizeof(endpoint));
            float squared{};
            for (std::size_t axis = 0; axis < origin.size(); ++axis) {
                const float difference = endpoint[axis] - origin[axis];
                squared += difference * difference;
            }
            const float length = std::sqrt(squared);
            std::memcpy(&beam_length_bits, &length, sizeof(beam_length_bits));
        }
    }
    if (!dlight_context && index >= 0 &&
        static_cast<std::size_t>(index) < source.ref_count &&
        offset >= 0 &&
        static_cast<std::size_t>(offset) + sizeof(float) <=
            ql1k::k_player_scene_ref_entity_bytes) {
        std::memcpy(&source_bits,
                    source.refs[static_cast<std::size_t>(index)].data() + offset,
                    sizeof(source_bits));
    }
    const auto delta =
        ql1k::player_scene_origin_delta(source.pose, current_pose);
    std::array<LONG, 3> delta_bits{};
    for (std::size_t axis = 0; axis < delta.size(); ++axis) {
        std::memcpy(&delta_bits[axis], &delta[axis], sizeof(delta_bits[axis]));
    }
    InterlockedExchange(&g_player_scene_mismatch_source, source_bits);
    InterlockedExchange(&g_player_scene_mismatch_delta_x, delta_bits[0]);
    InterlockedExchange(&g_player_scene_mismatch_delta_y, delta_bits[1]);
    InterlockedExchange(&g_player_scene_mismatch_delta_z, delta_bits[2]);
    InterlockedExchange(&g_player_scene_mismatch_ref_type, ref_type);
    InterlockedExchange(&g_player_scene_mismatch_beam_length,
                        beam_length_bits);
}

void fail_player_scene_validation() noexcept {
    InterlockedIncrement64(&g_player_scene_mismatch_count);
    InterlockedExchange(&g_player_scene_failed, 1);
    InterlockedExchange(&g_player_scene_validated, 0);
    InterlockedExchange(&g_player_scene_validation_streak, 0);
}

void note_player_scene_pose(const std::size_t slot,
                            const PlayerSceneProducts& products,
                            const std::span<const std::int32_t> body_models) noexcept {
    LONG tracked = InterlockedCompareExchange(&g_player_scene_pose_slot, 0, 0);
    if (tracked < 0) {
        tracked = InterlockedCompareExchange(
            &g_player_scene_pose_slot, static_cast<LONG>(slot), -1);
        if (tracked < 0) {
            tracked = static_cast<LONG>(slot);
        }
    }
    if (tracked != static_cast<LONG>(slot)) {
        return;
    }
    for (std::size_t index = 0; index < products.ref_count; ++index) {
        const auto type = static_cast<ql1k::PlayerSceneRefType>(
            ql1k::player_scene_ref_type(products.refs[index]));
        if (type == ql1k::PlayerSceneRefType::rail_core ||
            type == ql1k::PlayerSceneRefType::rail_rings) {
            continue;
        }
        std::array<std::uint32_t, 3> bits{};
        std::memcpy(bits.data(), products.refs[index].data() +
                                      ql1k::k_player_scene_origin_offset,
                    sizeof(bits));
        InterlockedIncrement64(&g_player_scene_pose_sample_count);
        if (g_last_player_scene_pose_valid) {
            if (bits == g_last_player_scene_pose_bits) {
                InterlockedIncrement64(&g_player_scene_pose_repeat_count);
            } else {
                InterlockedIncrement64(&g_player_scene_pose_change_count);
            }
        }
        g_last_player_scene_pose_bits = bits;
        g_last_player_scene_pose_valid = true;
        break;
    }

    if (body_models.empty()) {
        return;
    }
    const std::int32_t legs_model = body_models.front();
    for (std::size_t index = 0; index < products.ref_count; ++index) {
        const auto& ref = products.refs[index];
        if (ql1k::player_scene_ref_type(ref) !=
                static_cast<std::int32_t>(ql1k::PlayerSceneRefType::model) ||
            ql1k::player_scene_ref_hmodel(ref) != legs_model) {
            continue;
        }
        std::array<std::uint32_t, 9> axis_bits{};
        if (!ql1k::player_scene_ref_axis_bits(ref, axis_bits)) {
            return;
        }
        InterlockedIncrement64(&g_player_scene_body_axis_sample_count);
        if (g_last_player_scene_body_axis_valid) {
            if (axis_bits == g_last_player_scene_body_axis_bits) {
                InterlockedIncrement64(&g_player_scene_body_axis_repeat_count);
            } else {
                InterlockedIncrement64(&g_player_scene_body_axis_change_count);
            }
        }
        g_last_player_scene_body_axis_bits = axis_bits;
        g_last_player_scene_body_axis_valid = true;
        return;
    }
}

[[nodiscard]] bool replay_player_scene(
    const std::size_t slot, void* const centity,
    const PlayerSceneProducts& source,
    const std::span<const PlayerSceneDlightBinding> dlight_bindings,
    const ql1k::PlayerScenePose& current, bool& hard_failure) noexcept {
    hard_failure = false;
    std::array<std::int32_t, 3> body_models{};
    if (!build_translated_player_scene_products(
            source, current, g_player_scene_expected_scratch) ||
        !capture_player_body_model_handles(centity, body_models) ||
        !refresh_player_scene_body_pose(
            centity, source, body_models, g_player_scene_expected_scratch) ||
        !refresh_player_scene_dlight_bindings(
            source, body_models, dlight_bindings,
            g_player_scene_expected_scratch) ||
        !refresh_player_scene_lightning_endpoints(
            centity, current, g_player_scene_expected_scratch)) {
        return false;
    }
    const EngineSceneSnapshot before = capture_engine_scene_snapshot();
    auto& translated = g_player_scene_expected_scratch;
    if (!before.valid ||
        before.ref_count + static_cast<std::int32_t>(translated.ref_count) >=
            k_engine_scene_ref_limit ||
        before.dlight_count + static_cast<std::int32_t>(translated.dlight_count) >=
            k_engine_scene_dlight_limit) {
        return false;
    }
    const auto add_ref = reinterpret_cast<EngineAddRefEntityToSceneFn>(
        engine_address(k_engine_add_ref_entity_to_scene));
    const auto add_dlight = reinterpret_cast<EngineAddDlightToSceneFn>(
        engine_address(k_engine_add_dlight_to_scene));
    if (add_ref == nullptr || add_dlight == nullptr) {
        return false;
    }
    EngineSceneSnapshot after_surfaces{};
    if (!refresh_player_scene_world_surfaces(centity, translated, before,
                                             after_surfaces)) {
        return false;
    }
    LONG64 beam_count{};
    for (std::size_t index = 0; index < translated.ref_count; ++index) {
        add_ref(translated.refs[index].data());
        const auto type = static_cast<ql1k::PlayerSceneRefType>(
            ql1k::player_scene_ref_type(translated.refs[index]));
        if (type == ql1k::PlayerSceneRefType::beam ||
            type == ql1k::PlayerSceneRefType::lightning) {
            ++beam_count;
        }
    }
    for (std::size_t index = 0; index < translated.dlight_count; ++index) {
        const auto& dlight = translated.dlights[index];
        add_dlight(dlight.origin.data(), dlight.radius, dlight.color[0],
                   dlight.color[1], dlight.color[2], dlight.additive);
    }
    const EngineSceneSnapshot after = capture_engine_scene_snapshot();
    const bool committed =
        after.valid && before.frame_index == after.frame_index &&
        before.root == after.root &&
        after.ref_count ==
            before.ref_count + static_cast<std::int32_t>(translated.ref_count) &&
        after.dlight_count ==
            before.dlight_count +
                static_cast<std::int32_t>(translated.dlight_count) &&
        after.poly_count == after_surfaces.poly_count &&
        after.polyvert_count == after_surfaces.polyvert_count;
    if (!committed) {
        if (!restore_engine_scene_counts(before)) {
            mark_permanent_fault("player_scene_rollback_failed");
        }
        hard_failure = true;
        return false;
    }
    if (beam_count != 0) {
        InterlockedExchangeAdd64(&g_player_scene_beam_replay_count, beam_count);
    }
    InterlockedIncrement64(&g_player_scene_replay_count);
    note_player_scene_pose(slot, translated, body_models);
    return true;
}

void call_stock_player_renderer(void* const centity) noexcept {
    auto* const hook = g_player_scene_renderer;
    const auto original =
        hook == nullptr ? nullptr : hook->original<PlayerRendererFn>();
    const auto stock = g_stock_player_renderer;
    if (original != nullptr) {
        original(centity);
    } else if (stock != nullptr) {
        stock(centity);
        mark_permanent_fault("player_scene_trampoline_missing");
    } else {
        mark_permanent_fault("player_scene_trampoline_missing");
    }
}

// Duplicate-server-time preview draws inherit the last positive cg.frametime;
// cgame does not rewrite it when the server-time argument is unchanged. Running
// stock CG_Player with that stale value would advance torso/legs swing once per
// presentation. Force the semantically correct zero delta for the duration of
// a preview-only stock fallback, then restore the authoritative global.
void call_stock_player_renderer_zero_time(void* const centity) noexcept {
    auto* const frame_time = static_cast<std::int32_t*>(
        cgame_address(k_cgame_frame_time));
    if (frame_time == nullptr) {
        call_stock_player_renderer(centity);
        return;
    }
    const std::int32_t saved_frame_time = *frame_time;
    *frame_time = 0;
    call_stock_player_renderer(centity);
    *frame_time = saved_frame_time;
}

void __cdecl player_scene_renderer_hook(void* const centity) noexcept {
    if (InterlockedCompareExchange(&g_config_player_scene_replay, 0, 0) == 0 ||
        InterlockedCompareExchange(&g_runtime_armed, 0, 0) == 0 ||
        centity == nullptr || !hud_replay_gameplay_eligible() ||
        !hud_replay_renderer_ready()) {
        call_stock_player_renderer(centity);
        return;
    }
    const LONG module_epoch = InterlockedCompareExchange(&g_module_serial, 0, 0);
    const LONG renderer_epoch = InterlockedCompareExchange(&g_renderer_epoch, 0, 0);
    if (InterlockedCompareExchange(
            &g_player_scene_validation_module_epoch, 0, 0) != module_epoch ||
        InterlockedCompareExchange(
            &g_player_scene_validation_renderer_epoch, 0, 0) != renderer_epoch) {
        reset_player_scene_replay_epoch();
    }
    const bool preview = g_transient_frame.preview_active != 0 &&
                         g_transient_frame.draw_active != 0 &&
                         g_transient_frame.interpolation_active != 0;
    const std::size_t slot = player_scene_slot(centity);
    ql1k::PlayerScenePose current_pose{};
    if (slot >= k_player_scene_cache_slots ||
        !capture_player_scene_pose(centity, current_pose)) {
        InterlockedIncrement64(&g_player_scene_stock_fallback_count);
        if (preview) {
            call_stock_player_renderer_zero_time(centity);
        } else {
            call_stock_player_renderer(centity);
        }
        return;
    }

    if (!preview) {
        ql1k::PlayerSceneKey render_input_key{};
        const bool captured_render_input =
            capture_player_scene_key(centity, render_input_key);
        const EngineSceneSnapshot before = capture_engine_scene_snapshot();
        call_stock_player_renderer(centity);
        const EngineSceneSnapshot after = capture_engine_scene_snapshot();
        auto& entry = g_player_scene_cache[slot];
        entry.valid = false;
        entry.shape_valid = false;
        entry.dlight_bindings_valid = false;
        ql1k::PlayerSceneKey key{};
        std::array<std::int32_t, 3> body_models{};
        if (captured_render_input &&
            InterlockedCompareExchange(&g_player_scene_failed, 0, 0) == 0 &&
            capture_player_scene_key(centity, key) &&
            capture_player_scene_products(before, after, current_pose,
                                           entry.products) &&
            capture_player_body_model_handles(centity, body_models) &&
            resolve_player_scene_dlight_bindings(
                entry.products, body_models, entry.dlight_bindings) &&
            ql1k::player_scene_shape_signature(entry.products, entry.shape)) {
            if (const auto* const proof =
                    player_scene_shape_proof(entry.shape);
                proof != nullptr && proof->dlight_origin_mode_valid) {
                for (auto& binding : entry.dlight_bindings) {
                    binding.origin_mode = proof->dlight_origin_mode;
                }
            }
            entry.key = key;
            entry.render_input_state = render_input_key.nonpose_state;
            entry.captured_qpc = qpc_start();
            entry.dlight_bindings_valid = true;
            entry.shape_valid = true;
            MemoryBarrier();
            entry.valid = true;
            InterlockedIncrement64(&g_player_scene_capture_count);
        } else {
            InterlockedIncrement64(&g_player_scene_capture_reject_count);
        }
        return;
    }

    ql1k::PlayerSceneKey current_key{};
    auto& entry = g_player_scene_cache[slot];
    const LONG64 now = qpc_start();
    if (InterlockedCompareExchange(&g_player_scene_failed, 0, 0) != 0 ||
        !entry.valid || !entry.shape_valid || !entry.dlight_bindings_valid ||
        !capture_player_scene_key(centity, current_key) ||
        !(entry.key == current_key) || !player_scene_cache_age_valid(entry, now)) {
        InterlockedIncrement64(&g_player_scene_stock_fallback_count);
        call_stock_player_renderer_zero_time(centity);
        return;
    }
    const bool globally_validated =
        InterlockedCompareExchange(&g_player_scene_validated, 0, 0) != 0;
    if (!globally_validated || !player_scene_shape_proven(entry.shape)) {
        // Validate against the state stock would actually receive on this
        // preview. CG_PlayerAngles advances its persistent torso/legs swing by
        // cg.frametime (0x10A9C1E8). The authoritative render already committed
        // that positive-time advance; preview frametime is zero. Restoring the
        // pre-render state here therefore manufactures an older orientation
        // that neither the live stock preview nor production replay would use.
        // current_key is restored after the reversible stock call below.
        const EngineSceneSnapshot before = capture_engine_scene_snapshot();
        call_stock_player_renderer_zero_time(centity);
        const EngineSceneSnapshot after = capture_engine_scene_snapshot();
        std::memcpy(centity, current_key.nonpose_state.data(),
                    current_key.nonpose_state.size());
        std::array<std::int32_t, 3> body_models{};
        bool matched = capture_player_scene_products(
            before, after, current_pose, g_player_scene_actual_scratch);
        if (!matched) {
            record_player_scene_mismatch(12, 0, -1, 1, 0);
        }
        if (matched) {
            matched = build_translated_player_scene_products(
                entry.products, current_pose, g_player_scene_expected_scratch);
            if (!matched) {
                record_player_scene_mismatch(12, 1, -1, 1, 0);
            }
        }
        if (matched) {
            matched = capture_player_body_model_handles(centity, body_models);
            if (!matched) {
                record_player_scene_mismatch(12, 2, -1, 1, 0);
            }
        }
        if (matched) {
            LONG body_pose_failure_stage{};
            matched = refresh_player_scene_body_pose(
                centity, entry.products, body_models,
                g_player_scene_expected_scratch,
                &body_pose_failure_stage);
            if (!matched) {
                record_player_scene_mismatch(12, 4,
                                             body_pose_failure_stage, 1, 0);
            }
        }
        if (matched) {
            matched = refresh_player_scene_dlight_bindings(
                entry.products, body_models, entry.dlight_bindings,
                g_player_scene_expected_scratch);
            if (!matched) {
                record_player_scene_mismatch(12, 5, -1, 1, 0);
            }
        }
        if (matched &&
            player_scene_has_lightning(g_player_scene_expected_scratch) &&
            player_scene_is_local_entity(centity)) {
            // Stock has already rendered this entity in the validation call.
            // Production deliberately does the same because the local beam is
            // camera/prediction-owned rather than centity-pose-owned.
            InterlockedIncrement64(&g_player_scene_stock_fallback_count);
            return;
        }
        if (matched) {
            matched = refresh_player_scene_lightning_endpoints(
                centity, current_pose, g_player_scene_expected_scratch);
            if (!matched) {
                record_player_scene_mismatch(12, 3, -1, 1, 0);
            }
        }
        std::array<std::int16_t, k_player_scene_max_dlights * 2U>
            trusted_tag_refs{};
        std::array<std::int16_t, k_player_scene_max_dlights>
            trusted_dlight_indices{};
        std::size_t trusted_tag_ref_count{};
        std::size_t trusted_dlight_count{};
        for (std::size_t light_index = 0;
             light_index < entry.dlight_bindings.size(); ++light_index) {
            const auto& binding = entry.dlight_bindings[light_index];
            if (binding.kind !=
                PlayerSceneDlightBinding::Kind::weapon_flash) {
                continue;
            }
            trusted_dlight_indices[trusted_dlight_count++] =
                static_cast<std::int16_t>(light_index);
            if (binding.gun_ref >= 0) {
                trusted_tag_refs[trusted_tag_ref_count++] = binding.gun_ref;
            }
            if (binding.flash_ref >= 0) {
                trusted_tag_refs[trusted_tag_ref_count++] = binding.flash_ref;
            }
        }
        bool world_surfaces_validated = false;
        if (matched) {
            // Invoke the exact production wrappers once behind a reversible
            // scene-count transaction. This proves their ABI and every current
            // shadow/wake product against the stock CG_Player output before the
            // cache can ever bypass CG_Player.
            matched = validate_player_scene_world_surfaces(
                centity, current_pose, g_player_scene_expected_scratch,
                g_player_scene_actual_scratch);
            world_surfaces_validated = matched;
        }
        if (matched) {
            ql1k::PlayerSceneNormalizationFailure normalization_failure{};
            matched = ql1k::player_scene_normalize_model_tag_rounding(
                g_player_scene_expected_scratch,
                g_player_scene_actual_scratch, body_models,
                &normalization_failure,
                std::span<const std::int16_t>(trusted_tag_refs.data(),
                                              trusted_tag_ref_count));
            if (!matched) {
                record_player_scene_mismatch(
                    13, normalization_failure.index,
                    normalization_failure.offset,
                    static_cast<LONG>(normalization_failure.expected),
                    static_cast<LONG>(normalization_failure.actual));
            }
        }
        if (matched) {
            ql1k::PlayerSceneNormalizationFailure normalization_failure{};
            matched = ql1k::player_scene_normalize_dlight_cosmetics(
                g_player_scene_expected_scratch,
                g_player_scene_actual_scratch, &normalization_failure,
                std::span<const std::int16_t>(
                    trusted_dlight_indices.data(), trusted_dlight_count));
            if (!matched) {
                record_player_scene_mismatch(
                    14, normalization_failure.index,
                    normalization_failure.offset,
                    static_cast<LONG>(normalization_failure.expected),
                    static_cast<LONG>(normalization_failure.actual));
            }
        }
        if (matched) {
            // Surfaces were compared independently above. The translated cache
            // intentionally contains none because production regenerates them.
            ql1k::player_scene_discard_world_surfaces(
                g_player_scene_actual_scratch);
            matched = ql1k::player_scene_products_near(
                g_player_scene_expected_scratch,
                g_player_scene_actual_scratch, 0.01F);
        }
        if (!matched && world_surfaces_validated &&
            InterlockedCompareExchange(&g_player_scene_mismatch_kind, 0, 0) ==
                0) {
            diagnose_player_scene_mismatch(g_player_scene_expected_scratch,
                                           g_player_scene_actual_scratch);
        }
        if (!matched && world_surfaces_validated &&
            InterlockedCompareExchange(&g_player_scene_mismatch_kind, 0, 0) ==
                8) {
            // Weapon-flash dlights have two binary-observable products: the
            // translated light origin and the tag_flash model origin. The
            // installed client uses one consistently per render shape, so
            // retry only this exact alternate when the generic comparison
            // proves that the current shape needs it.
            auto tagged_bindings = entry.dlight_bindings;
            for (auto& binding : tagged_bindings) {
                if (binding.kind ==
                    PlayerSceneDlightBinding::Kind::weapon_flash) {
                    binding.origin_mode =
                        PlayerSceneDlightBinding::OriginMode::tagged;
                }
            }
            bool tagged_matched =
                build_translated_player_scene_products(
                    entry.products, current_pose,
                    g_player_scene_expected_scratch) &&
                refresh_player_scene_body_pose(
                    centity, entry.products, body_models,
                    g_player_scene_expected_scratch) &&
                refresh_player_scene_dlight_bindings(
                    entry.products, body_models, tagged_bindings,
                    g_player_scene_expected_scratch);
            if (tagged_matched &&
                player_scene_has_lightning(g_player_scene_expected_scratch) &&
                player_scene_is_local_entity(centity)) {
                tagged_matched = false;
            }
            if (tagged_matched) {
                tagged_matched = refresh_player_scene_lightning_endpoints(
                    centity, current_pose, g_player_scene_expected_scratch);
            }
            if (tagged_matched) {
                tagged_matched =
                    ql1k::player_scene_normalize_model_tag_rounding(
                        g_player_scene_expected_scratch,
                        g_player_scene_actual_scratch, body_models) &&
                    ql1k::player_scene_normalize_dlight_cosmetics(
                        g_player_scene_expected_scratch,
                        g_player_scene_actual_scratch);
            }
            if (tagged_matched) {
                ql1k::player_scene_discard_world_surfaces(
                    g_player_scene_actual_scratch);
                tagged_matched = ql1k::player_scene_products_near(
                    g_player_scene_expected_scratch,
                    g_player_scene_actual_scratch, 0.01F);
            }
            if (tagged_matched) {
                entry.dlight_bindings = tagged_bindings;
                if (auto* const proof = player_scene_shape_proof(entry.shape);
                    proof != nullptr) {
                    proof->dlight_origin_mode =
                        PlayerSceneDlightBinding::OriginMode::tagged;
                    proof->dlight_origin_mode_valid = true;
                }
                InterlockedExchange(&g_player_scene_mismatch_kind, 0);
                InterlockedExchange(&g_player_scene_mismatch_index, 0);
                InterlockedExchange(&g_player_scene_mismatch_offset, 0);
                InterlockedExchange(&g_player_scene_mismatch_expected, 0);
                InterlockedExchange(&g_player_scene_mismatch_actual, 0);
                matched = true;
            }
        }
        if (!matched) {
            // Once the native surface products have passed their independent
            // comparator, remove stock's captured shadow/wake polys before the
            // generic diagnostic. Otherwise a later model/dlight normalization
            // rejection is misleadingly reported as only a polygon count.
            if (world_surfaces_validated) {
                ql1k::player_scene_discard_world_surfaces(
                    g_player_scene_actual_scratch);
                if (InterlockedCompareExchange(
                        &g_player_scene_mismatch_kind, 0, 0) == 0) {
                    diagnose_player_scene_mismatch(
                        g_player_scene_expected_scratch,
                        g_player_scene_actual_scratch);
                }
            }
            record_player_scene_mismatch_context(entry.products, current_pose,
                                                 body_models);
            fail_player_scene_validation();
            return;
        }
        note_player_scene_pose(slot, g_player_scene_actual_scratch, body_models);
        note_player_scene_shape_validation(entry.shape);
        if (auto* const proof = player_scene_shape_proof(entry.shape);
            proof != nullptr && !proof->dlight_origin_mode_valid) {
            proof->dlight_origin_mode =
                PlayerSceneDlightBinding::OriginMode::translated;
            proof->dlight_origin_mode_valid = true;
        }
        InterlockedIncrement64(&g_player_scene_validation_count);
        if (!globally_validated) {
            const LONG streak =
                InterlockedIncrement(&g_player_scene_validation_streak);
            if (streak >= k_player_scene_validation_threshold &&
                InterlockedCompareExchange(&g_player_scene_failed, 0, 0) == 0) {
                InterlockedCompareExchange(&g_player_scene_validated, 1, 0);
            }
        }
        return;
    }

    if (InterlockedCompareExchange(&g_config_player_scene_bypass, 0, 0) == 0) {
        call_stock_player_renderer_zero_time(centity);
        return;
    }
    bool hard_failure = false;
    if (replay_player_scene(slot, centity, entry.products,
                            entry.dlight_bindings, current_pose,
                            hard_failure)) {
        return;
    }
    if (hard_failure) {
        fail_player_scene_validation();
    }
    InterlockedIncrement64(&g_player_scene_stock_fallback_count);
    call_stock_player_renderer_zero_time(centity);
}

void flush_player_style_local_counters() noexcept {
    if (g_player_style_local_counters.bypasses != 0U) {
        InterlockedAdd64(
            &g_player_style_bypass_count,
            static_cast<LONG64>(g_player_style_local_counters.bypasses));
    }
    g_player_style_local_counters = {};
}

void note_player_style_bypass() noexcept {
    ++g_player_style_local_counters.bypasses;
    if (++g_player_style_local_counters.calls == 256U) {
        flush_player_style_local_counters();
    }
}

void fail_player_style_validation(volatile LONG64* const counter) noexcept {
    if (counter != nullptr) {
        InterlockedIncrement64(counter);
    }
    InterlockedExchange(&g_player_style_failed, 1);
    InterlockedExchange(&g_player_style_validated, 0);
    g_player_style_validation_streak = 0U;
}

[[nodiscard]] bool prepare_player_style_epoch() noexcept {
    const LONG module_epoch = g_module_serial;
    const LONG renderer_epoch = g_renderer_epoch;
    if (module_epoch <= 0 || renderer_epoch <= 0) {
        return false;
    }
    if (g_player_style_validation_module_epoch != module_epoch ||
        g_player_style_validation_renderer_epoch != renderer_epoch) {
        InterlockedExchange(&g_player_style_validated, 0);
        InterlockedExchange(&g_player_style_validation_module_epoch, module_epoch);
        InterlockedExchange(&g_player_style_validation_renderer_epoch, renderer_epoch);
        g_player_style_color_correct_cvar = nullptr;
        g_player_style_validation_streak = 0U;
    }
    return true;
}

[[nodiscard]] bool valid_player_style_client(const std::int32_t client) noexcept {
    return client >= 0 && client < static_cast<std::int32_t>(k_max_player_entities);
}

[[nodiscard]] bool player_style_team(const std::int32_t client,
                                     std::int32_t& team) noexcept {
    if (!valid_player_style_client(client)) {
        return false;
    }
    const auto* const teams =
        static_cast<const std::uint8_t*>(cgame_address(k_cgame_client_info_teams));
    if (teams == nullptr) {
        return false;
    }
    std::memcpy(&team, teams + static_cast<std::size_t>(client) * k_client_info_stride,
                sizeof(team));
    return team >= 0 && team <= 3;
}

[[nodiscard]] bool player_style_source(const std::uintptr_t centity,
                                       ql1k::PlayerStyleSource& source) noexcept {
    if (!prepare_player_style_epoch()) {
        return false;
    }
    const auto centity_base =
        reinterpret_cast<std::uintptr_t>(cgame_address(k_cgame_centities));
    if (centity_base == 0U || centity < centity_base) {
        return false;
    }
    const std::uintptr_t centity_offset = centity - centity_base;
    if ((centity_offset % k_centity_stride) != 0U ||
        centity_offset / k_centity_stride >= k_max_cgame_entities) {
        return false;
    }

    auto** const color_cvar_slot =
        static_cast<std::uint8_t**>(engine_address(k_engine_color_correct_active_cvar));
    if (color_cvar_slot == nullptr || *color_cvar_slot == nullptr) {
        return false;
    }
    if (g_player_style_color_correct_cvar == nullptr) {
        g_player_style_color_correct_cvar = *color_cvar_slot;
    } else if (g_player_style_color_correct_cvar != *color_cvar_slot) {
        fail_player_style_validation(nullptr);
        return false;
    }
    float color_correct_active{};
    std::memcpy(&color_correct_active, *color_cvar_slot + 0x2CU,
                sizeof(color_correct_active));
    if (!std::isfinite(color_correct_active)) {
        return false;
    }
    source.multiplier = color_correct_active == 0.0F ? 2U : 1U;

    const auto* const centity_bytes = reinterpret_cast<const std::uint8_t*>(centity);
    const auto read_cgame_i32 = [](const std::uintptr_t address,
                                   std::int32_t& value) noexcept {
        const auto* const source_bytes =
            static_cast<const std::uint8_t*>(cgame_address(address));
        if (source_bytes == nullptr) {
            return false;
        }
        std::memcpy(&value, source_bytes, sizeof(value));
        return true;
    };
    const auto read_cgame_u32 = [](const std::uintptr_t address,
                                   std::uint32_t& value) noexcept {
        const auto* const source_bytes =
            static_cast<const std::uint8_t*>(cgame_address(address));
        if (source_bytes == nullptr) {
            return false;
        }
        std::memcpy(&value, source_bytes, sizeof(value));
        return true;
    };

    std::int32_t target_client{};
    std::int32_t local_client{};
    std::int32_t dead_darken{};
    std::memcpy(&target_client, centity_bytes + k_centity_style_client_offset,
                sizeof(target_client));
    if (!read_cgame_i32(k_cgame_style_local_client, local_client) ||
        !read_cgame_i32(k_cgame_style_dead_darken, dead_darken) ||
        !valid_player_style_client(target_client) ||
        !valid_player_style_client(local_client)) {
        return false;
    }

    if (dead_darken != 0 && (centity_bytes[8] & 1U) != 0U) {
        std::uint32_t dead_color{};
        if (!read_cgame_u32(k_cgame_style_dead_color, dead_color)) {
            return false;
        }
        source.packed_colors.fill(dead_color);
        return true;
    }

    std::int32_t local_team{};
    std::int32_t target_team{};
    std::int32_t gametype{};
    std::int32_t state_flags{};
    if (!player_style_team(local_client, local_team) ||
        !player_style_team(target_client, target_team) ||
        !read_cgame_i32(k_cgame_style_gametype, gametype) ||
        !read_cgame_i32(k_cgame_style_player_state_flags, state_flags)) {
        return false;
    }
    const auto* const player_flag =
        static_cast<const std::uint8_t*>(cgame_address(k_cgame_style_player_flag));
    const auto* const force_red =
        static_cast<const std::uint8_t*>(cgame_address(k_cgame_style_force_red));
    const auto* const force_blue =
        static_cast<const std::uint8_t*>(cgame_address(k_cgame_style_force_blue));
    if (player_flag == nullptr || force_red == nullptr || force_blue == nullptr) {
        return false;
    }

    const bool special_mode = gametype >= 3 && (*player_flag & 2U) != 0U &&
                              (state_flags & 0x1000) == 0 && local_team != 1 &&
                              local_team != 2;
    bool enemy_colors = special_mode ? target_team == 2
                                     : local_team != target_team || local_team == 0;
    if (target_team == 1 && *force_red != 0U) {
        enemy_colors = false;
    } else if (target_team == 2 && *force_blue != 0U) {
        enemy_colors = true;
    }

    const std::array<std::uintptr_t, ql1k::k_player_style_entity_count> addresses =
        enemy_colors
            ? std::array<std::uintptr_t, ql1k::k_player_style_entity_count>{
                  k_cgame_style_enemy_lower, k_cgame_style_enemy_upper,
                  k_cgame_style_enemy_head}
            : std::array<std::uintptr_t, ql1k::k_player_style_entity_count>{
                  k_cgame_style_team_lower, k_cgame_style_team_upper,
                  k_cgame_style_team_head};
    for (std::size_t index = 0; index < addresses.size(); ++index) {
        if (!read_cgame_u32(addresses[index], source.packed_colors[index])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool valid_player_style_entities(
    const std::array<void*, ql1k::k_player_style_entity_count>& entities) noexcept {
    for (std::size_t index = 0; index < entities.size(); ++index) {
        const auto pointer = reinterpret_cast<std::uintptr_t>(entities[index]);
        if (pointer == 0U || (pointer & (alignof(std::uint32_t) - 1U)) != 0U) {
            return false;
        }
        for (std::size_t previous = 0; previous < index; ++previous) {
            if (entities[index] == entities[previous]) {
                return false;
            }
        }
    }
    return true;
}

extern "C" void __declspec(naked) __cdecl player_style_stock_thunk(
    std::uintptr_t, void*, void*, void*, std::uintptr_t) noexcept {
    __asm {
        mov eax, dword ptr [esp + 4]
        mov ecx, dword ptr [esp + 20]
        push dword ptr [esp + 16]
        push dword ptr [esp + 16]
        push dword ptr [esp + 16]
        call ecx
        add esp, 12
        ret
    }
}

[[nodiscard]] __declspec(noinline) int validate_player_style_direct_call(
    const std::uintptr_t centity,
    const std::array<void*, ql1k::k_player_style_entity_count>& entities,
    const ql1k::PlayerStyleOutputs& expected) noexcept {
    ql1k::PlayerStyleImages before{};
    std::array<const void*, ql1k::k_player_style_entity_count> sources{};
    for (std::size_t index = 0; index < sources.size(); ++index) {
        sources[index] = entities[index];
    }
    if (!ql1k::capture_player_style_images(sources, before)) {
        return 0;
    }

    player_style_stock_thunk(centity, entities[0], entities[1], entities[2],
                             g_player_style_stock_target);

    ql1k::PlayerStyleImages after{};
    if (!ql1k::capture_player_style_images(sources, after) ||
        !ql1k::player_style_mutation_is_local(before, after)) {
        fail_player_style_validation(&g_player_style_mutation_failure_count);
        return 1;
    }
    if (ql1k::player_style_outputs(after) != expected) {
        fail_player_style_validation(&g_player_style_mismatch_count);
        return 1;
    }

    InterlockedIncrement64(&g_player_style_validation_count);
    if (g_player_style_validation_streak <
        static_cast<std::uint32_t>(k_player_style_validation_threshold)) {
        ++g_player_style_validation_streak;
    }
    if (g_player_style_validation_streak >=
            static_cast<std::uint32_t>(k_player_style_validation_threshold) &&
        g_player_style_failed == 0) {
        InterlockedCompareExchange(&g_player_style_validated, 1, 0);
    }
    return 1;
}

extern "C" int __cdecl player_style_direct_dispatch(
    const std::uintptr_t centity, void* const lower, void* const upper,
    void* const head) noexcept {
    if (g_config_player_style_fast_path == 0 || g_runtime_armed == 0 ||
        g_player_style_failed != 0 || centity == 0U ||
        g_player_style_stock_target == 0U) {
        return 0;
    }

    const std::array<void*, ql1k::k_player_style_entity_count> entities{
        lower, upper, head};
    if (!valid_player_style_entities(entities)) {
        return 0;
    }

    ql1k::PlayerStyleSource source{};
    ql1k::PlayerStyleOutputs expected{};
    if (!player_style_source(centity, source) ||
        !ql1k::resolve_player_style(source, expected)) {
        return 0;
    }

    if (g_player_style_validated == 0) {
        return validate_player_style_direct_call(centity, entities, expected);
    }

    if (g_config_player_style_bypass == 0) {
        return 0;
    }
    if (!ql1k::apply_player_style_outputs(entities, expected)) {
        fail_player_style_validation(&g_player_style_mismatch_count);
        return 0;
    }
    note_player_style_bypass();
    return 1;
}

extern "C" std::uintptr_t __cdecl player_style_stock_target_value() noexcept {
    return g_player_style_stock_target;
}

extern "C" void __declspec(naked) player_style_direct_bridge() noexcept {
    __asm {
        push eax
        mov edx, dword ptr [esp + 16]
        push edx
        mov edx, dword ptr [esp + 16]
        push edx
        mov edx, dword ptr [esp + 16]
        push edx
        push eax
        call player_style_direct_dispatch
        add esp, 16
        test eax, eax
        pop eax
        jnz handled
        push eax
        call player_style_stock_target_value
        mov edx, eax
        pop eax
        jmp edx
    handled:
        ret
    }
}

[[nodiscard]] std::size_t shadow_mark_slot(const std::uintptr_t centity) noexcept {
    const auto predicted = reinterpret_cast<std::uintptr_t>(
        cgame_address(k_cgame_predicted_player_centity));
    if (centity == predicted) {
        return k_max_player_entities;
    }
    const auto base = reinterpret_cast<std::uintptr_t>(cgame_address(k_cgame_centities));
    if (centity < base) {
        return k_shadow_mark_cache_slots;
    }
    const std::uintptr_t offset = centity - base;
    if ((offset % k_centity_stride) != 0U) {
        return k_shadow_mark_cache_slots;
    }
    const std::size_t slot = static_cast<std::size_t>(offset / k_centity_stride);
    return slot < k_max_player_entities ? slot : k_shadow_mark_cache_slots;
}

[[nodiscard]] bool replay_shadow_mark(const ShadowMarkCache::Entry& entry,
                                      const float* const origin, const float red,
                                      const float green, const float blue,
                                      const float alpha) noexcept {
    const auto add_poly = g_shadow_add_poly_to_scene;
    if (entry.poly_count != 0U && add_poly == nullptr) {
        return false;
    }
    std::array<ql1k::ShadowPolyVert, k_shadow_mark_cache_max_poly_vertices> vertices{};
    for (std::size_t poly_index = 0; poly_index < entry.poly_count; ++poly_index) {
        const auto& poly = entry.polys[poly_index];
        if (!ShadowMarkCache::translated_poly(entry, poly_index, origin, vertices)) {
            return false;
        }
        const auto channel = [](const float value) noexcept {
            if (value <= 0.0F) {
                return static_cast<std::uint8_t>(0U);
            }
            if (value >= 1.0F) {
                return static_cast<std::uint8_t>(255U);
            }
            return static_cast<std::uint8_t>(value * 255.0F);
        };
        const std::uint8_t modulate[4]{channel(red), channel(green), channel(blue),
                                       channel(alpha)};
        for (std::size_t vertex = 0; vertex < poly.vertex_count; ++vertex) {
            std::memcpy(vertices[vertex].modulate, modulate, sizeof(modulate));
        }
        add_poly(poly.shader, poly.vertex_count, vertices.data());
        InterlockedIncrement64(&g_shadow_mark_cache_replay_poly_count);
    }
    return true;
}

void __cdecl shadow_mark_entry_hook(safetyhook::Context& context) noexcept {
    g_shadow_mark_capture_active = false;
    g_shadow_mark_capture_entry = nullptr;
    if (InterlockedCompareExchange(&g_config_shadow_mark_cache, 0, 0) == 0 ||
        context.esp == 0U) {
        return;
    }
    const auto* const arguments = reinterpret_cast<const std::uint32_t*>(context.esp);
    const auto expected_return = reinterpret_cast<std::uintptr_t>(
        cgame_address(k_cgame_shadow_impact_return));
    if (arguments[0] != expected_return) {
        return;
    }
    if (arguments[11] == 0U) {
        return;
    }
    const auto* const origin = reinterpret_cast<const float*>(arguments[2]);
    const auto* const direction = reinterpret_cast<const float*>(arguments[3]);
    const auto* const integer_time =
        static_cast<const std::int32_t*>(cgame_address(k_cgame_integer_time));
    const std::size_t slot = shadow_mark_slot(context.esi);
    if (origin == nullptr || direction == nullptr || integer_time == nullptr ||
        slot >= k_shadow_mark_cache_slots) {
        return;
    }
    const ql1k::ShadowMarkKey key = ql1k::make_shadow_mark_key(
        *integer_time, static_cast<std::int32_t>(arguments[1]), direction,
        static_cast<std::int32_t>(arguments[9]), std::bit_cast<float>(arguments[10]),
        static_cast<std::int32_t>(arguments[11]));
    if (ShadowMarkCache::Entry* const entry = g_shadow_mark_cache.find(slot, key);
        entry != nullptr &&
        replay_shadow_mark(*entry, origin, std::bit_cast<float>(arguments[5]),
                           std::bit_cast<float>(arguments[6]),
                           std::bit_cast<float>(arguments[7]),
                           std::bit_cast<float>(arguments[8]))) {
        InterlockedIncrement64(&g_shadow_mark_cache_hit_count);
        // At a function-entry midhook the original stack already has the real
        // return address on top. Point SafetyHook's restore RET at that stack
        // to skip only CG_ImpactMark; the caller still removes its 11 args.
        context.trampoline_esp = context.esp;
        return;
    }
    InterlockedIncrement64(&g_shadow_mark_cache_miss_count);
    g_shadow_mark_capture_entry = g_shadow_mark_cache.begin_capture(slot, key, origin);
    g_shadow_mark_capture_active = g_shadow_mark_capture_entry != nullptr;
}

void __cdecl shadow_mark_add_poly_hook(safetyhook::Context& context) noexcept {
    if (!g_shadow_mark_capture_active || g_shadow_mark_capture_entry == nullptr ||
        context.esp == 0U) {
        return;
    }
    const auto* const arguments = reinterpret_cast<const std::uint32_t*>(context.esp);
    if (context.ecx != 0U) {
        g_shadow_add_poly_to_scene = reinterpret_cast<AddPolyToSceneFn>(context.ecx);
    }
    const auto* const vertices =
        reinterpret_cast<const ql1k::ShadowPolyVert*>(arguments[2]);
    if (!g_shadow_mark_cache.append(g_shadow_mark_capture_entry,
                                    static_cast<std::int32_t>(arguments[0]),
                                    static_cast<std::int32_t>(arguments[1]), vertices)) {
        InterlockedIncrement64(&g_shadow_mark_cache_overflow_count);
    }
}

void __cdecl shadow_impact_return_hook(safetyhook::Context&) noexcept {
    if (g_shadow_mark_capture_active) {
        if (g_shadow_mark_cache.finish_capture(g_shadow_mark_capture_entry)) {
            InterlockedIncrement64(&g_shadow_mark_cache_capture_count);
        }
        g_shadow_mark_capture_active = false;
        g_shadow_mark_capture_entry = nullptr;
    }
}

void __cdecl scene_submission_hook(safetyhook::Context&) {
    if (InterlockedCompareExchange(&g_runtime_armed, 0, 0) == 0) {
        return;
    }
    InterlockedIncrement64(&g_scene_submission_count);
    record_entity_pose_freshness();
    const auto* const refdef_angles =
        static_cast<const float*>(cgame_address(k_cgame_refdef_viewangles));
    if (refdef_angles != nullptr && std::isfinite(refdef_angles[0]) &&
        std::isfinite(refdef_angles[1])) {
        InterlockedExchange(&g_refdef_angle_pitch_mdeg,
                            static_cast<LONG>(std::lround(refdef_angles[0] * 1000.0F)));
        InterlockedExchange(&g_refdef_angle_yaw_mdeg,
                            static_cast<LONG>(std::lround(refdef_angles[1] * 1000.0F)));
    }
    const auto* const submitted_axis =
        static_cast<const float*>(cgame_address(k_cgame_refdef_viewaxis));
    const ql1k::AxisViewangles submitted =
        ql1k::viewangles_from_forward_axis(submitted_axis);
    if (!submitted.valid) {
        return;
    }
    const float pitch = submitted.pitch;
    const float yaw = submitted.yaw;
    InterlockedExchange(&g_submitted_pitch_mdeg,
                        static_cast<LONG>(std::lround(pitch * 1000.0F)));
    InterlockedExchange(&g_submitted_yaw_mdeg,
                        static_cast<LONG>(std::lround(yaw * 1000.0F)));
    const bool changed = g_last_submitted_view_valid &&
                         ql1k::materially_changed_view(
                             pitch, yaw, g_last_submitted_pitch, g_last_submitted_yaw);
    g_last_submitted_pitch = pitch;
    g_last_submitted_yaw = yaw;
    g_last_submitted_view_valid = true;
    if (changed) {
        InterlockedIncrement64(&g_fresh_view_submission_count);
        if (g_transient_frame.overlay_applied != 0 && g_transient_frame.revision != 0) {
            InterlockedIncrement64(&g_visible_overlay_submission_count);
        }
    }
}

void __cdecl draw_active_frame_hook(const int server_time, const int stereo_view,
                                    const int demo_playback) {
    AcquireSRWLockShared(&g_draw_lifecycle_gate);
    AcquireSRWLockShared(&g_cgame_gate);
    const auto stock = g_stock_draw_active_frame;
    auto* const hook = g_draw_active_frame;
    if (hook == nullptr) {
        if (stock != nullptr) {
            stock(server_time, stereo_view, demo_playback);
        }
        ReleaseSRWLockShared(&g_cgame_gate);
        ReleaseSRWLockShared(&g_draw_lifecycle_gate);
        return;
    }

    const LONG leases = InterlockedIncrement(&g_cgame_inflight);
    if (leases <= 0) {
        InterlockedExchange(&g_cgame_inflight, 1);
        mark_permanent_fault("cgame_lease_overflow");
    }
    const auto original = hook->original<DrawActiveFrameFn>();
    ReleaseSRWLockShared(&g_cgame_gate);

    const bool owns_transaction = preview_chain_ready() &&
                                  g_transient_frame.preview_active != 0 &&
                                  g_transient_frame.draw_active == 0;
    if (owns_transaction) {
        g_transient_frame.draw_active = 1;
        g_transient_frame.overlay_applied = 0;
    }

    const LONG64 frame_now = qpc_start();
    g_transient_frame.fractional_ms = g_render_clock.sample(
        server_time, frame_now,
        InterlockedCompareExchange64(&g_qpc_frequency, 0, 0), owns_transaction);
    InterlockedExchange(
        &g_render_fractional_us,
        static_cast<LONG>(std::lround(g_transient_frame.fractional_ms * 1000.0)));
    if (original != nullptr) {
        original(server_time, stereo_view, demo_playback);
    } else if (stock != nullptr) {
        stock(server_time, stereo_view, demo_playback);
        mark_permanent_fault("draw_active_frame_trampoline_missing");
    } else {
        mark_permanent_fault("draw_active_frame_trampoline_missing");
    }
    if (owns_transaction) {
        if (!restore_transient_entity_interpolation()) {
            mark_permanent_fault("transient_entity_interpolation_restore_failed");
        }
        if (g_transient_frame.overlay_applied != 0) {
            auto* viewangles =
                static_cast<float*>(cgame_address(k_cgame_predicted_viewangles));
            if (viewangles == nullptr) {
                InterlockedIncrement64(&g_preview_restore_failure_count);
                mark_permanent_fault("transient_view_restore_address_missing");
            } else {
                std::memcpy(viewangles, g_transient_frame.saved_viewangles,
                            sizeof(g_transient_frame.saved_viewangles));
                if (std::memcmp(viewangles, g_transient_frame.saved_viewangles,
                                sizeof(g_transient_frame.saved_viewangles)) != 0) {
                    InterlockedIncrement64(&g_preview_restore_failure_count);
                    mark_permanent_fault("transient_view_restore_failed");
                } else {
                    InterlockedIncrement64(&g_overlay_restore_count);
                }
            }
        }
        g_transient_frame.overlay_applied = 0;
        g_transient_frame.draw_active = 0;
    }

    if (!activate_smp_after_resource_registration()) {
        mark_permanent_fault(g_reason.load(std::memory_order_acquire));
    }

    // Initial installation can race the engine's first cgame transition. The
    // temporary loader reference is released only after a real cgame frame
    // returned while the lifecycle lease is still held.
    release_cgame_install_ticket();

    const LONG retired = InterlockedDecrement(&g_cgame_inflight);
    if (retired < 0) {
        InterlockedExchange(&g_cgame_inflight, 0);
        mark_permanent_fault("cgame_lease_underflow");
    }
    maybe_recover_history();
    ReleaseSRWLockShared(&g_draw_lifecycle_gate);
}

void __cdecl predictor_entry_hook() {
    g_replay_auth.active = 0;
    AcquireSRWLockShared(&g_cgame_gate);
    const auto stock = g_stock_predict_entry;
    if (InterlockedCompareExchange(&g_runtime_armed, 0, 0) == 0 || g_predict_entry == nullptr) {
        if (stock != nullptr) {
            stock();
        }
        ReleaseSRWLockShared(&g_cgame_gate);
        return;
    }
    const LONG leases = InterlockedIncrement(&g_cgame_inflight);
    if (leases <= 0) {
        InterlockedExchange(&g_cgame_inflight, 1);
        mark_permanent_fault("cgame_lease_overflow");
    }
    const auto original = g_predict_entry->original<CgameEntryFn>();
    if (original != nullptr) {
        original();
    } else if (stock != nullptr) {
        stock();
        mark_permanent_fault("predictor_entry_trampoline_missing");
    } else {
        mark_permanent_fault("predictor_entry_trampoline_missing");
    }
    g_replay_auth.active = 0;
    InterlockedIncrement64(&g_prediction_count);
    if (preview_chain_ready() && g_transient_frame.preview_active != 0 &&
        g_transient_frame.draw_active != 0 && g_transient_frame.overlay_applied == 0) {
        auto* viewangles = static_cast<float*>(cgame_address(k_cgame_predicted_viewangles));
        if (viewangles != nullptr) {
            std::memcpy(g_transient_frame.saved_viewangles, viewangles,
                        sizeof(g_transient_frame.saved_viewangles));
            viewangles[0] += g_transient_frame.pitch_delta;
            viewangles[1] += g_transient_frame.yaw_delta;
            g_transient_frame.overlay_applied = 1;
            InterlockedIncrement64(&g_overlay_apply_count);
        }
    }
    const LONG retired = InterlockedDecrement(&g_cgame_inflight);
    if (retired < 0) {
        InterlockedExchange(&g_cgame_inflight, 0);
        mark_permanent_fault("cgame_lease_underflow");
    }
    ReleaseSRWLockShared(&g_cgame_gate);
    maybe_recover_history();
}

void frame_floor_hook(safetyhook::Context& context) {
    if (InterlockedCompareExchange(&g_runtime_armed, 0, 0) == 0 || context.ebp == 0) {
        return;
    }
    InterlockedIncrement64(&g_outer_loop_count);
    const bool preview_armed =
        InterlockedCompareExchange(&g_preview_chain_armed, 0, 0) != 0;
    *reinterpret_cast<std::int32_t*>(context.ebp - 4U) =
        ql1k::scheduler_floor(true, preview_armed);
}

int __cdecl delta_hook(const int raw_delta) {
    if (InterlockedCompareExchange(&g_runtime_armed, 0, 0) == 0 || g_s2 == nullptr) {
        return g_stock_delta == nullptr ? raw_delta : g_stock_delta(raw_delta);
    }
    const auto original = g_s2->original<DeltaFn>();
    if (raw_delta == 0) {
        g_zero_frame_token = 1;
        return 0;
    }
    g_zero_frame_token = 0;
    if (original != nullptr) {
        return original(raw_delta);
    }
    return raw_delta;
}

void __cdecl client_frame_hook(const int delta) {
    if (InterlockedCompareExchange(&g_runtime_armed, 0, 0) == 0 || g_s3 == nullptr) {
        if (g_stock_client_frame != nullptr) {
            g_stock_client_frame(delta);
        }
        return;
    }
    const auto original = g_s3->original<ClientFrameFn>();
    const bool zero_token = g_zero_frame_token != 0;
    const auto ownership = ql1k::classify_frame(delta, zero_token, preview_chain_ready());
    if (ownership == ql1k::FrameOwnership::render_only) {
        g_zero_frame_token = 0;
        if (!begin_transient_preview()) {
            InterlockedExchange(&g_preview_chain_armed, 0);
            return;
        }
        const auto present = g_present;
        if (present == nullptr) {
            clear_transient_frame();
            return;
        }

        screen_update_hook();
        InterlockedIncrement64(&g_present_count);
        InterlockedIncrement64(&g_zero_render_count);

        if (g_transient_frame.overlay_applied != 0 ||
            g_transient_frame.draw_active != 0) {
            auto* viewangles =
                static_cast<float*>(cgame_address(k_cgame_predicted_viewangles));
            if (viewangles != nullptr && g_transient_frame.overlay_applied != 0) {
                std::memcpy(viewangles, g_transient_frame.saved_viewangles,
                            sizeof(g_transient_frame.saved_viewangles));
            }
            InterlockedIncrement64(&g_preview_restore_failure_count);
            mark_permanent_fault("transient_view_scope_leaked");
        }
        if (g_transient_frame.interpolation_active != 0 ||
            g_entity_pose_transaction.valid()) {
            if (!restore_transient_entity_interpolation()) {
                mark_permanent_fault("transient_entity_interpolation_restore_failed");
            } else {
                mark_permanent_fault("transient_entity_interpolation_scope_leaked");
            }
        }
        clear_transient_frame();
        return;
    }
    if (delta == 0 && zero_token) {
        g_zero_frame_token = 0;
        return;
    }
    g_zero_frame_token = 0;
    if (original != nullptr) {
        original(delta);
        InterlockedIncrement64(&g_simulation_count);
        InterlockedIncrement64(&g_present_count);
    }
}

void __cdecl warning_compare_hook(safetyhook::Context& context) {
    if (InterlockedCompareExchange(&g_runtime_armed, 0, 0) == 0) {
        return;
    }
    const auto* command_number_ptr = static_cast<const std::int32_t*>(engine_address(k_engine_cmd_number));
    const auto* snapshot = reinterpret_cast<const std::uint8_t*>(context.edx);
    if (command_number_ptr == nullptr || snapshot == nullptr) {
        return;
    }
    HistorySelection selection{};
    if (select_history(*command_number_ptr,
                       *reinterpret_cast<const std::int32_t*>(snapshot + 0x2C), selection)) {
        context.ecx = static_cast<std::uintptr_t>(static_cast<std::uint32_t>(selection.oldest_time));
    }
}

void __cdecl predictor_compare_hook(safetyhook::Context& context) {
    g_replay_auth.active = 0;
    if (InterlockedCompareExchange(&g_runtime_armed, 0, 0) == 0) {
        return;
    }
    const auto* command_number_ptr = static_cast<const std::int32_t*>(engine_address(k_engine_cmd_number));
    const auto* snapshot = reinterpret_cast<const std::uint8_t*>(context.ecx);
    if (command_number_ptr == nullptr || snapshot == nullptr) {
        return;
    }
    const std::int32_t current = *command_number_ptr;
    HistorySelection selection{};
    if (select_history(current,
                       *reinterpret_cast<const std::int32_t*>(snapshot + 0x2C), selection)) {
        context.eax = static_cast<std::uintptr_t>(static_cast<std::uint32_t>(selection.oldest_time));
        // On a coverage miss q_start is N+1, so the unchanged cgame body
        // reaches its normal no-replay path only if the freeze predicate does
        // not fire; the oldest-time witness above keeps the loss truthful.
        context.ebx = static_cast<std::uintptr_t>(static_cast<std::uint32_t>(selection.q_start));
        if (selection.coverage && selection.q_start <= current && g_cgame != nullptr) {
            g_replay_auth.active = 1;
            g_replay_auth.next = selection.q_start;
            g_replay_auth.end = current;
            g_replay_auth.generation = selection.generation;
            g_replay_auth.cgame = g_cgame;
        }
    }
}

void fps_display_hook(safetyhook::Context& context) {
    const LONG measured = InterlockedCompareExchange(&g_measured_present_fps, 0, 0);
    if (InterlockedCompareExchange(&g_runtime_armed, 0, 0) != 0 && measured > 0) {
        context.eax = static_cast<std::uintptr_t>(static_cast<std::uint32_t>(measured));
    }
}

bool preview_chain_ready() noexcept {
    return InterlockedCompareExchange(&g_runtime_armed, 0, 0) != 0 &&
           InterlockedCompareExchange(&g_preview_chain_armed, 0, 0) != 0;
}

void clear_transient_frame() noexcept {
    g_transient_frame.preview_active = 0;
    g_transient_frame.draw_active = 0;
    g_transient_frame.overlay_applied = 0;
    g_transient_frame.interpolation_active = 0;
    g_transient_frame.pitch_delta = 0.0F;
    g_transient_frame.yaw_delta = 0.0F;
    g_transient_frame.saved_frame_interpolation = 0.0F;
    g_transient_frame.fractional_ms = 0.0;
    g_transient_frame.revision = 0;
    g_entity_pose_transaction.clear();
}

bool begin_transient_preview() noexcept {
    InterlockedIncrement64(&g_preview_attempt_count);
    clear_transient_frame();
    if (!preview_chain_ready() || g_mouse_transform == nullptr ||
        g_draw_active_frame == nullptr || g_scene_submission == nullptr) {
        InterlockedIncrement64(&g_preview_skip_count);
        return false;
    }

    auto* pending_x = static_cast<std::int32_t*>(engine_address(k_engine_pending_mouse_x));
    auto* pending_y = static_cast<std::int32_t*>(engine_address(k_engine_pending_mouse_y));
    auto* mouse_state = static_cast<std::uint8_t*>(engine_address(k_engine_mouse_state));
    auto* filter_slot = static_cast<std::uint8_t**>(engine_address(k_engine_m_filter));
    auto* debug_slot = static_cast<std::uint8_t**>(engine_address(k_engine_mouse_accel_debug));
    auto* debug_file = static_cast<std::int32_t*>(engine_address(k_engine_mouse_debug_file));
    if (pending_x == nullptr || pending_y == nullptr || mouse_state == nullptr ||
        filter_slot == nullptr || debug_slot == nullptr || debug_file == nullptr ||
        *filter_slot == nullptr || *debug_slot == nullptr) {
        InterlockedIncrement64(&g_preview_skip_count);
        return false;
    }

    auto* const filter = *filter_slot;
    const auto* const debug = *debug_slot;
    const std::int32_t filter_value =
        *reinterpret_cast<const std::int32_t*>(filter + 0x30U);
    if (filter_value < 0 || filter_value > 31 ||
        *reinterpret_cast<const std::int32_t*>(debug + 0x30U) != 0 || *debug_file != 0) {
        InterlockedIncrement64(&g_preview_skip_count);
        return false;
    }

    const std::int32_t saved_pending_x = *pending_x;
    const std::int32_t saved_pending_y = *pending_y;
    InterlockedExchange(&g_pending_mouse_x_last, saved_pending_x);
    InterlockedExchange(&g_pending_mouse_y_last, saved_pending_y);
    std::int32_t saved_filter_modified{};
    std::memcpy(&saved_filter_modified, filter + 0x20U, sizeof(saved_filter_modified));
    ql1k::ByteSnapshot<k_mouse_state_size> state_snapshot;
    state_snapshot.capture(mouse_state);

    float committed_pitch{};
    float committed_yaw{};
    std::memcpy(&committed_pitch, mouse_state, sizeof(committed_pitch));
    std::memcpy(&committed_yaw, mouse_state + sizeof(float), sizeof(committed_yaw));
    if (std::isfinite(committed_pitch) && std::isfinite(committed_yaw)) {
        InterlockedExchange(&g_engine_committed_pitch_mdeg,
                            static_cast<LONG>(std::lround(committed_pitch * 1000.0F)));
        InterlockedExchange(&g_engine_committed_yaw_mdeg,
                            static_cast<LONG>(std::lround(committed_yaw * 1000.0F)));
    }
    std::array<std::uint8_t, k_record_size> dummy_command{};
    g_mouse_transform(dummy_command.data());

    float preview_pitch{};
    float preview_yaw{};
    std::memcpy(&preview_pitch, mouse_state, sizeof(preview_pitch));
    std::memcpy(&preview_yaw, mouse_state + sizeof(float), sizeof(preview_yaw));
    (void)state_snapshot.restore(mouse_state);
    *pending_x = saved_pending_x;
    *pending_y = saved_pending_y;
    std::memcpy(filter + 0x20U, &saved_filter_modified, sizeof(saved_filter_modified));

    const bool restored = state_snapshot.matches(mouse_state) &&
                          *pending_x == saved_pending_x && *pending_y == saved_pending_y &&
                          std::memcmp(filter + 0x20U, &saved_filter_modified,
                                      sizeof(saved_filter_modified)) == 0;
    if (!restored) {
        InterlockedIncrement64(&g_preview_restore_failure_count);
        mark_permanent_fault("transient_mouse_restore_failed");
        return false;
    }
    InterlockedIncrement64(&g_preview_restore_count);

    g_transient_frame.pitch_delta =
        ql1k::wrapped_angle_delta(preview_pitch, committed_pitch);
    g_transient_frame.yaw_delta = ql1k::wrapped_angle_delta(preview_yaw, committed_yaw);
    InterlockedExchange(&g_preview_pitch_delta_mdeg,
                        static_cast<LONG>(std::lround(g_transient_frame.pitch_delta * 1000.0F)));
    InterlockedExchange(&g_preview_yaw_delta_mdeg,
                        static_cast<LONG>(std::lround(g_transient_frame.yaw_delta * 1000.0F)));
    if (g_transient_frame.pitch_delta != 0.0F || g_transient_frame.yaw_delta != 0.0F) {
        InterlockedIncrement64(&g_preview_nonzero_count);
        g_transient_frame.revision = InterlockedIncrement64(&g_view_revision);
    }
    g_transient_frame.preview_active = 1;
    InterlockedIncrement64(&g_preview_accept_count);
    return true;
}

__declspec(noinline) float native_scaled_add(const float base, const float value,
                                             const double scale) noexcept {
    float result{};
    __asm {
        fld dword ptr [value]
        fmul qword ptr [scale]
        fadd dword ptr [base]
        fstp dword ptr [result]
    }
    return result;
}

bool native_angle_vectors(const float* angles, float* forward) noexcept {
    const auto function = cgame_address(k_cgame_angle_vectors);
    if (function == nullptr || angles == nullptr || forward == nullptr) {
        return false;
    }
    __asm {
        push ebx
        push esi
        push edi
        mov esi, angles
        mov edi, forward
        xor ebx, ebx
        push 0
        mov eax, function
        call eax
        add esp, 4
        pop edi
        pop esi
        pop ebx
    }
    return true;
}

bool native_player_scene_basis(
    const float* const angles, ql1k::PlayerSceneBasis& basis) noexcept {
    const auto function = cgame_address(k_cgame_angle_vectors);
    if (function == nullptr || angles == nullptr) {
        return false;
    }
    std::array<float, 3> right{};
    std::array<float, 3> up{};
    float* const forward_output = basis.axis[0].data();
    float* const right_output = right.data();
    float* const up_output = up.data();
    // Exact 0x10056E40 usercall: ESI=angles, EDI=forward, EBX=right,
    // stack[0]=up. Quake refEntity axis[1] is left, so negate right.
    __asm {
        push ebx
        push esi
        push edi
        mov esi, angles
        mov edi, forward_output
        mov ebx, right_output
        push up_output
        mov eax, function
        call eax
        add esp, 4
        pop edi
        pop esi
        pop ebx
    }
    for (std::size_t axis = 0; axis < 3U; ++axis) {
        basis.axis[1][axis] = -right[axis];
        basis.axis[2][axis] = up[axis];
    }
    return ql1k::player_scene_basis_valid(basis);
}

bool native_player_scene_body_axes(
    void* const centity, const std::int32_t requested_frame_time,
    std::array<ql1k::PlayerSceneBasis, 3>& body_axes) noexcept {
    using PlayerAnglesFn = void(__cdecl*)(void*, float*, float*, float*);
    const auto function = reinterpret_cast<PlayerAnglesFn>(
        cgame_address(k_cgame_player_angles));
    auto* const frame_time = static_cast<std::int32_t*>(
        cgame_address(k_cgame_frame_time));
    if (function == nullptr || frame_time == nullptr || centity == nullptr) {
        return false;
    }
    std::array<std::byte, ql1k::k_player_scene_nonpose_state_bytes> saved_state{};
    std::memcpy(saved_state.data(), centity, saved_state.size());
    const std::int32_t saved_frame_time = *frame_time;
    if (requested_frame_time < 0 || requested_frame_time > 1) {
        return false;
    }
    *frame_time = requested_frame_time;
    function(centity, body_axes[0].axis[0].data(),
             body_axes[1].axis[0].data(), body_axes[2].axis[0].data());
    *frame_time = saved_frame_time;
    std::memcpy(centity, saved_state.data(), saved_state.size());
    return ql1k::player_scene_basis_valid(body_axes[0]) &&
           ql1k::player_scene_basis_valid(body_axes[1]) &&
           ql1k::player_scene_basis_valid(body_axes[2]);
}

bool native_position_on_tag(
    void* const destination, const void* const parent,
    const std::int32_t parent_model, const char* const tag_name) noexcept {
    const auto function = cgame_address(k_cgame_position_on_tag);
    if (function == nullptr || destination == nullptr || parent == nullptr ||
        parent_model == 0 || tag_name == nullptr) {
        return false;
    }
    // Exact 0x10015240 usercall: ESI=destination, EDI=parent,
    // EDX=parent hModel, and one caller-cleaned tag-name stack argument.
    __asm {
        push ebx
        push esi
        push edi
        mov esi, destination
        mov edi, parent
        mov edx, parent_model
        push tag_name
        mov eax, function
        call eax
        add esp, 4
        pop edi
        pop esi
        pop ebx
    }
    return true;
}

bool native_position_rotated_on_tag(
    void* const destination, const void* const parent,
    const std::int32_t parent_model, const char* const tag_name) noexcept {
    const auto function = cgame_address(k_cgame_position_rotated_on_tag);
    if (function == nullptr || destination == nullptr || parent == nullptr ||
        parent_model == 0 || tag_name == nullptr) {
        return false;
    }
    // Exact 0x10015340 usercall from installed CG_Player:
    // ESI=destination refEntity, EDI=parent refEntity, EDX=parent hModel,
    // stack[0]=tag name. Caller pops the sole stack argument.
    __asm {
        push ebx
        push esi
        push edi
        mov esi, destination
        mov edi, parent
        mov edx, parent_model
        push tag_name
        mov eax, function
        call eax
        add esp, 4
        pop edi
        pop esi
        pop ebx
    }
    return true;
}

bool native_alternate_point_trace(
    void* const function, std::uint32_t* const result, const float* const start,
    const float* const end, const std::int32_t skip_entity,
    const std::int32_t mask) noexcept {
    if (function == nullptr || result == nullptr || start == nullptr || end == nullptr) {
        return false;
    }
    // Exact cgame 0x10051e51..0x10051e6f usercall for 0x10044100:
    // ECX=result, EDX=start, EBX=end, then stack skip-entity and mask.
    // The wrapper performs the alternate engine trace (+0x84) and native
    // active-centity clipping before copying all fourteen trace words.
    __asm {
        push ebx
        mov eax, function
        mov ecx, result
        mov edx, start
        mov ebx, end
        push mask
        push skip_entity
        call eax
        add esp, 8
        pop ebx
    }
    return true;
}

bool native_player_shadow(void* const function, const void* const centity,
                          float* const shadow_plane,
                          std::int32_t* const visible) noexcept {
    if (function == nullptr || centity == nullptr || shadow_plane == nullptr ||
        visible == nullptr) {
        return false;
    }
    // Exact 0x10041ce7 usercall: ESI=centity and EDI=&shadowPlane.
    std::int32_t result{};
    __asm {
        push esi
        push edi
        mov esi, centity
        mov edi, shadow_plane
        mov eax, function
        call eax
        mov result, eax
        pop edi
        pop esi
    }
    *visible = result;
    return true;
}

bool native_player_wake(void* const function,
                        const void* const centity) noexcept {
    if (function == nullptr || centity == nullptr) {
        return false;
    }
    // Exact 0x10041cee usercall: the wake helper consumes ESI=centity.
    __asm {
        push esi
        mov esi, centity
        mov eax, function
        call eax
        pop esi
    }
    return true;
}

struct ClientRayResult {
    ql1k::HitregTraceKind kind{ql1k::HitregTraceKind::other};
    std::int32_t entity_number{-1};
    bool opponent_contact{};
    bool available{};
};

ClientRayResult trace_native_client_lg_ray(const std::uint8_t* player_state) noexcept {
    ClientRayResult result{};
    const auto trace = reinterpret_cast<PointTraceFn>(cgame_address(k_cgame_point_trace));
    const auto* const serverinfo_flags =
        static_cast<const std::int32_t*>(cgame_address(k_cgame_serverinfo_flags));
    if (player_state == nullptr || trace == nullptr || serverinfo_flags == nullptr) {
        return result;
    }
    const std::int32_t shooter =
        *reinterpret_cast<const std::int32_t*>(player_state + 0x88U);
    if (shooter < 0 || shooter >= 64) {
        return result;
    }

    std::array<float, 3> angles{};
    std::array<float, 3> forward{};
    std::array<float, 3> origin{};
    std::memcpy(angles.data(), player_state + 0xA0U, sizeof(angles));
    std::memcpy(origin.data(), player_state + 0x14U, sizeof(origin));
    if (!native_angle_vectors(angles.data(), forward.data())) {
        return result;
    }

    const auto pm_flags = *reinterpret_cast<const std::int32_t*>(player_state + 0x0CU);
    const auto viewheight = *reinterpret_cast<const std::int32_t*>(player_state + 0xACU);
    const double muzzle_offset = (pm_flags & 1) != 0 ? 3.0 : 5.0;
    std::array<float, 3> start{};
    for (std::size_t axis = 0; axis < start.size(); ++axis) {
        const float snapped = static_cast<float>(static_cast<std::int32_t>(origin[axis]));
        const float base = axis == 2U ? snapped + static_cast<float>(viewheight) : snapped;
        start[axis] = native_scaled_add(base, forward[axis], muzzle_offset);
    }
    std::array<float, 3> end{};
    for (std::size_t axis = 0; axis < end.size(); ++axis) {
        end[axis] = native_scaled_add(start[axis], forward[axis], 768.0);
    }

    std::array<std::uint32_t, 14> trace_result{};
    const std::int32_t mask = (*serverinfo_flags & 0x02000000) != 0 ? 0x00000001 : 0x06000001;
    trace(trace_result.data(), start.data(), nullptr, nullptr, end.data(), shooter, mask);
    std::memcpy(&result.entity_number,
                trace_result.data() + k_trace_entity_number_index,
                sizeof(result.entity_number));

    bool opponent = result.entity_number >= 0 && result.entity_number < 64 &&
                    result.entity_number != shooter;
    if (opponent) {
        const auto* const teams =
            static_cast<const std::uint8_t*>(cgame_address(k_cgame_client_info_teams));
        if (teams == nullptr) {
            return result;
        } else {
            std::int32_t local_team{};
            std::int32_t candidate_team{};
            std::memcpy(&local_team,
                        teams + static_cast<std::size_t>(shooter) * k_client_info_stride,
                        sizeof(local_team));
            std::memcpy(&candidate_team,
                        teams + static_cast<std::size_t>(result.entity_number) *
                                    k_client_info_stride,
                        sizeof(candidate_team));
            opponent = ql1k::hitreg_is_opponent(local_team, candidate_team);
        }
    }
    result.opponent_contact = opponent;
    result.kind = opponent                    ? ql1k::HitregTraceKind::player
                  : result.entity_number == 1022 ? ql1k::HitregTraceKind::world
                  : result.entity_number == 1023 ? ql1k::HitregTraceKind::none
                                                 : ql1k::HitregTraceKind::other;
    result.available = true;
    return result;
}

void hitreg_fire_hook(safetyhook::Context& context) {
    if (InterlockedCompareExchange(&g_runtime_armed, 0, 0) == 0 || context.esi == 0 ||
        context.edx != 0x14U) {
        return;
    }
    const auto* const player_state =
        *reinterpret_cast<const std::uint8_t* const*>(context.esi);
    if (player_state == nullptr) {
        return;
    }

    const std::int32_t command_time = *reinterpret_cast<const std::int32_t*>(player_state);
    const std::int32_t weapon = *reinterpret_cast<const std::int32_t*>(player_state + 0x90U);
    AcquireSRWLockExclusive(&g_hitreg_lock);
    if (!g_hitreg_state.wants_client_ray(weapon, command_time)) {
        g_hitreg_state.on_weapon_fire(
            weapon, command_time, ql1k::HitregTraceKind::other, false, true);
        ReleaseSRWLockExclusive(&g_hitreg_lock);
        return;
    }
    const ClientRayResult ray = trace_native_client_lg_ray(player_state);
    if (ray.available) {
        InterlockedIncrement64(&g_hitreg_trace_total);
        InterlockedExchange(&g_hitreg_trace_last_entity, ray.entity_number);
        if (ray.kind == ql1k::HitregTraceKind::player) {
            InterlockedIncrement64(&g_hitreg_trace_player);
        } else if (ray.kind == ql1k::HitregTraceKind::world) {
            InterlockedIncrement64(&g_hitreg_trace_world);
        } else if (ray.kind == ql1k::HitregTraceKind::none) {
            InterlockedIncrement64(&g_hitreg_trace_none);
        } else {
            InterlockedIncrement64(&g_hitreg_trace_other);
        }
    }
    g_hitreg_state.on_weapon_fire(weapon, command_time, ray.kind, ray.opponent_contact,
                                  ray.available);
    ReleaseSRWLockExclusive(&g_hitreg_lock);
}

void hitreg_feedback_hook(safetyhook::Context& context) {
    if (InterlockedCompareExchange(&g_runtime_armed, 0, 0) == 0 || context.esi == 0 ||
        context.ebp == 0) {
        return;
    }
    const auto* const new_player_state = reinterpret_cast<const std::uint8_t*>(context.esi);
    const auto* const old_player_state = reinterpret_cast<const std::uint8_t*>(context.ebp);
    const std::int32_t new_command_time =
        *reinterpret_cast<const std::int32_t*>(new_player_state);
    const std::int32_t old_command_time =
        *reinterpret_cast<const std::int32_t*>(old_player_state);
    const std::int32_t new_hits =
        *reinterpret_cast<const std::int32_t*>(new_player_state + 0x104U);
    const std::int32_t old_hits =
        *reinterpret_cast<const std::int32_t*>(old_player_state + 0x104U);
    AcquireSRWLockExclusive(&g_hitreg_lock);
    // PERS_HITS is accumulated damage. Its magnitude changes with server weapon
    // settings, so HitregState consumes only its positive/negative hit-beep signal.
    g_hitreg_state.on_server_feedback_transition(
        old_command_time, new_command_time, old_hits, new_hits);
    ReleaseSRWLockExclusive(&g_hitreg_lock);
}

bool measure_hitreg_text(const char* const text, const float scale, const int font,
                         int& width, int& height) {
    const auto measure = cgame_address(k_cgame_text_measure);
    if (measure == nullptr || text == nullptr) {
        return false;
    }
    width = 0;
    height = 0;
    int* const width_output = &width;
    int* const height_output = &height;
    __asm {
        mov ecx, text
        xor edx, edx
        mov esi, width_output
        xor edi, edi
        push scale
        push font
        call measure
        add esp, 8
        mov ecx, text
        xor edx, edx
        xor esi, esi
        mov edi, height_output
        push scale
        push font
        call measure
        add esp, 8
    }
    return width > 0 && height > 0;
}

void hitreg_draw_hook(safetyhook::Context& context) {
    if (InterlockedCompareExchange(&g_runtime_armed, 0, 0) == 0 || context.esp == 0) {
        return;
    }

    ql1k::HitregDisplay display{};
    std::size_t pending_holds = 0;
    AcquireSRWLockShared(&g_hitreg_lock);
    display = g_hitreg_state.published();
    pending_holds = g_hitreg_state.pending_holds();
    ReleaseSRWLockShared(&g_hitreg_lock);
    if (display.client_accuracy_kind == ql1k::ClientAccuracyDisplayKind::none ||
        pending_holds != 0) {
        return;
    }

    char text[32]{};
    ql1k::format_hitreg_text(display, text, sizeof(text));

    const auto* const arguments = reinterpret_cast<const std::uint32_t*>(context.esp);
    float source_y{};
    float scale{};
    float adjust{};
    std::memcpy(&source_y, &arguments[1], sizeof(source_y));
    std::memcpy(&scale, &arguments[3], sizeof(scale));
    std::memcpy(&adjust, &arguments[6], sizeof(adjust));
    const int font = static_cast<int>(arguments[2]);
    int width{};
    int height{};
    if (!measure_hitreg_text(text, scale, font, width, height)) {
        return;
    }

    const auto paint = reinterpret_cast<TextPaintFn>(cgame_address(k_cgame_text_paint));
    const auto* const color = reinterpret_cast<const float*>(arguments[4]);
    if (paint == nullptr || color == nullptr) {
        return;
    }
    constexpr float k_stock_fps_right_edge = 635.0F;
    paint(k_stock_fps_right_edge - static_cast<float>(width),
          source_y + static_cast<float>(height), font, scale, color, text, adjust,
          static_cast<int>(arguments[7]), static_cast<int>(arguments[8]));
}

template <typename T>
T* move_to_heap(std::expected<T, typename T::Error>& result) {
    if (!result) {
        return nullptr;
    }
    return new (std::nothrow) T(std::move(*result));
}

void destroy_candidate(safetyhook::MidHook* hook) {
    if (hook != nullptr) {
        hook->reset();
        delete hook;
    }
}

void destroy_candidate(safetyhook::InlineHook* hook) {
    if (hook != nullptr) {
        hook->reset();
        delete hook;
    }
}

void detach_player_scene_replay_hook() noexcept {
    destroy_candidate(g_player_scene_renderer);
    g_player_scene_renderer = nullptr;
    g_stock_player_renderer = nullptr;
    reset_player_scene_replay_epoch();
}

bool attach_player_scene_replay_hook() noexcept {
    if (InterlockedCompareExchange(&g_config_player_scene_replay, 0, 0) == 0) {
        return true;
    }
    if (g_player_scene_renderer != nullptr ||
        !signature_matches(cgame_address(k_cgame_player_renderer),
                           k_player_renderer_signature) ||
        !relocated_absolute_signature_matches(
            cgame_address(k_cgame_player_shadow), k_player_shadow_signature,
            4U, cgame_address(k_cgame_warning_absolute)) ||
        !relocated_absolute_signature_matches(
            cgame_address(k_cgame_player_wake), k_player_wake_signature,
            7U, cgame_address(k_cgame_warning_absolute)) ||
        !signature_matches(cgame_address(k_cgame_player_angles),
                           k_player_angles_signature) ||
        !relocated_absolute_signature_matches(
            cgame_address(k_cgame_position_on_tag),
            k_position_on_tag_signature, 4U,
            cgame_address(k_cgame_warning_absolute)) ||
        !relocated_absolute_signature_matches(
            cgame_address(k_cgame_position_rotated_on_tag),
            k_position_rotated_on_tag_signature, 4U,
            cgame_address(k_cgame_warning_absolute))) {
        g_reason.store("player_scene_renderer_signature_mismatch",
                       std::memory_order_release);
        return false;
    }
    auto result = safetyhook::InlineHook::create(
        cgame_address(k_cgame_player_renderer), &player_scene_renderer_hook,
        safetyhook::InlineHook::StartDisabled);
    auto* const hook = move_to_heap(result);
    if (hook == nullptr) {
        g_reason.store("player_scene_renderer_hook_create_failed",
                       std::memory_order_release);
        return false;
    }
    g_player_scene_renderer = hook;
    g_stock_player_renderer = hook->original<PlayerRendererFn>();
    reset_player_scene_replay_epoch();
    if (g_stock_player_renderer == nullptr || !hook->enable().has_value()) {
        detach_player_scene_replay_hook();
        g_reason.store("player_scene_renderer_hook_enable_failed",
                       std::memory_order_release);
        return false;
    }
    return true;
}

void detach_shadow_mark_cache_hooks() noexcept {
    destroy_candidate(g_shadow_impact_return);
    destroy_candidate(g_shadow_mark_add_poly);
    destroy_candidate(g_shadow_mark_entry);
    g_shadow_impact_return = nullptr;
    g_shadow_mark_add_poly = nullptr;
    g_shadow_mark_entry = nullptr;
    g_shadow_add_poly_to_scene = nullptr;
    g_shadow_mark_capture_active = false;
    g_shadow_mark_capture_entry = nullptr;
    g_shadow_mark_cache.clear();
}

bool attach_shadow_mark_cache_hooks() noexcept {
    if (InterlockedCompareExchange(&g_config_shadow_mark_cache, 0, 0) == 0) {
        return true;
    }
    if (g_shadow_mark_entry != nullptr || g_shadow_mark_add_poly != nullptr ||
        g_shadow_impact_return != nullptr ||
        !signature_matches(cgame_address(k_cgame_impact_mark), k_impact_mark_signature) ||
        !signature_matches(cgame_address(k_cgame_impact_add_poly_call),
                           k_impact_add_poly_call_signature) ||
        !signature_matches(cgame_address(k_cgame_shadow_impact_return),
                           k_shadow_impact_return_signature)) {
        g_reason.store("shadow_mark_cache_signature_mismatch", std::memory_order_release);
        return false;
    }
    auto entry_result = safetyhook::MidHook::create(
        cgame_address(k_cgame_impact_mark), &shadow_mark_entry_hook,
        safetyhook::MidHook::StartDisabled);
    auto add_poly_result = safetyhook::MidHook::create(
        cgame_address(k_cgame_impact_add_poly_call), &shadow_mark_add_poly_hook,
        safetyhook::MidHook::StartDisabled);
    auto return_result = safetyhook::MidHook::create(
        cgame_address(k_cgame_shadow_impact_return), &shadow_impact_return_hook,
        safetyhook::MidHook::StartDisabled);
    auto* entry = move_to_heap(entry_result);
    auto* add_poly = move_to_heap(add_poly_result);
    auto* return_hook = move_to_heap(return_result);
    if (entry == nullptr || add_poly == nullptr || return_hook == nullptr) {
        destroy_candidate(return_hook);
        destroy_candidate(add_poly);
        destroy_candidate(entry);
        g_reason.store("shadow_mark_cache_hook_create_failed", std::memory_order_release);
        return false;
    }
    g_shadow_mark_entry = entry;
    g_shadow_mark_add_poly = add_poly;
    g_shadow_impact_return = return_hook;
    g_shadow_mark_cache.clear();
    if (!entry->enable().has_value() || !add_poly->enable().has_value() ||
        !return_hook->enable().has_value()) {
        detach_shadow_mark_cache_hooks();
        g_reason.store("shadow_mark_cache_hook_enable_failed", std::memory_order_release);
        return false;
    }
    return true;
}

void detach_player_style_fast_path_hooks() noexcept {
    flush_player_style_local_counters();
    if (InterlockedCompareExchange(&g_player_style_call_patch_state, 0, 0) == 1) {
        auto* const patch_site = static_cast<std::uint8_t*>(
            cgame_address(k_cgame_player_style_call));
        if (patch_site != nullptr &&
            signature_matches(patch_site, g_player_style_call_patch)) {
            DWORD old_protection{};
            if (VirtualProtect(patch_site, k_player_style_call_signature.size(),
                               PAGE_EXECUTE_READWRITE, &old_protection)) {
                std::memcpy(patch_site, k_player_style_call_signature.data(),
                            k_player_style_call_signature.size());
                const BOOL flushed = FlushInstructionCache(
                    GetCurrentProcess(), patch_site,
                    k_player_style_call_signature.size());
                DWORD ignored{};
                const BOOL protection_restored =
                    VirtualProtect(patch_site, k_player_style_call_signature.size(),
                                   old_protection, &ignored);
                if (flushed && protection_restored) {
                    InterlockedExchange(&g_player_style_call_patch_state, 0);
                } else {
                    InterlockedExchange(&g_player_style_call_patch_state, -1);
                    g_reason.store("player_style_direct_restore_finalize_failed",
                                   std::memory_order_release);
                }
            } else {
                InterlockedExchange(&g_player_style_call_patch_state, -1);
                g_reason.store("player_style_direct_restore_protect_failed",
                               std::memory_order_release);
            }
        } else {
            InterlockedExchange(&g_player_style_call_patch_state, -1);
            g_reason.store("player_style_direct_restore_signature_mismatch",
                           std::memory_order_release);
        }
    }
    if (InterlockedCompareExchange(&g_player_style_call_patch_state, 0, 0) == 0) {
        g_player_style_stock_target = 0U;
        g_player_style_call_patch = {};
    }
    g_player_style_color_correct_cvar = nullptr;
    InterlockedExchange(&g_player_style_validated, 0);
    InterlockedExchange(&g_player_style_validation_module_epoch, 0);
    InterlockedExchange(&g_player_style_validation_renderer_epoch, 0);
    g_player_style_validation_streak = 0U;
}

bool attach_player_style_fast_path_hooks() noexcept {
    if (g_config_player_style_fast_path == 0) {
        return true;
    }
    if (InterlockedCompareExchange(&g_player_style_call_patch_state, 0, 0) != 0 ||
        !signature_matches(cgame_address(k_cgame_player_style_call),
                           k_player_style_call_signature)) {
        g_reason.store("player_style_fast_path_signature_mismatch",
                       std::memory_order_release);
        return false;
    }

    auto* const patch_site = static_cast<std::uint8_t*>(
        cgame_address(k_cgame_player_style_call));
    auto* const stock_target = cgame_address(k_cgame_player_style_stock);
    if (patch_site == nullptr || stock_target == nullptr ||
        !ql1k::make_player_style_rel32_call(
            reinterpret_cast<std::uintptr_t>(patch_site),
            reinterpret_cast<std::uintptr_t>(&player_style_direct_bridge),
            g_player_style_call_patch)) {
        g_reason.store("player_style_direct_target_out_of_range",
                       std::memory_order_release);
        return false;
    }

    g_player_style_stock_target = reinterpret_cast<std::uintptr_t>(stock_target);
    MemoryBarrier();
    DWORD old_protection{};
    if (!VirtualProtect(patch_site, g_player_style_call_patch.size(),
                        PAGE_EXECUTE_READWRITE, &old_protection)) {
        g_player_style_stock_target = 0U;
        g_player_style_call_patch = {};
        g_reason.store("player_style_direct_patch_protect_failed",
                       std::memory_order_release);
        return false;
    }
    std::memcpy(patch_site, g_player_style_call_patch.data(),
                g_player_style_call_patch.size());
    InterlockedExchange(&g_player_style_call_patch_state, 1);
    const BOOL flushed = FlushInstructionCache(
        GetCurrentProcess(), patch_site, g_player_style_call_patch.size());
    DWORD ignored{};
    const BOOL protection_restored =
        VirtualProtect(patch_site, g_player_style_call_patch.size(), old_protection,
                       &ignored);
    if (!flushed || !protection_restored) {
        g_reason.store("player_style_direct_patch_finalize_failed",
                       std::memory_order_release);
        return false;
    }

    g_player_style_color_correct_cvar = nullptr;
    InterlockedExchange(&g_player_style_validated, 0);
    InterlockedExchange(&g_player_style_validation_module_epoch, 0);
    InterlockedExchange(&g_player_style_validation_renderer_epoch, 0);
    g_player_style_validation_streak = 0U;
    return true;
}

bool detach_cgame_hooks() {
    InterlockedExchange(&g_preview_chain_armed, 0);
    InterlockedExchange(&g_cvar_refresh_warmed, 0);
    reset_hud_replay_cache();
    // VM teardown must not cross the unload boundary while any callback can
    // still return through cgame code. A timeout followed by stock shutdown
    // would turn a slow callback into a stale-trampoline crash.
    while (InterlockedCompareExchange(&g_cgame_inflight, 0, 0) != 0) {
        Sleep(1);
    }

    auto* slot = reinterpret_cast<void* volatile*>(engine_address(k_engine_getter_slot));
    const auto stock = engine_address(k_engine_stock_getter);
    const auto ours = reinterpret_cast<void*>(&get_usercmd_hook);
    if (slot != nullptr && stock != nullptr && *slot == ours) {
        (void)InterlockedCompareExchangePointer(slot, stock, ours);
    }

    detach_player_style_fast_path_hooks();
    detach_player_scene_replay_hook();
    detach_shadow_mark_cache_hooks();
    destroy_candidate(g_scene_submission);
    destroy_candidate(g_frame_interpolation_seam);
    destroy_candidate(g_draw_active_frame);
    destroy_candidate(g_cgame_draw_2d);
    destroy_candidate(g_cgame_update_cvars);
    destroy_candidate(g_predict);
    destroy_candidate(g_fps_display);
    destroy_candidate(g_hitreg_draw);
    destroy_candidate(g_hitreg_feedback);
    destroy_candidate(g_hitreg_fire);
    destroy_candidate(g_warning);
    destroy_candidate(g_predict_entry);
    destroy_candidate(g_warning_entry);
    g_scene_submission = nullptr;
    g_frame_interpolation_seam = nullptr;
    g_draw_active_frame = nullptr;
    g_cgame_draw_2d = nullptr;
    g_cgame_update_cvars = nullptr;
    g_predict = nullptr;
    g_fps_display = nullptr;
    g_hitreg_draw = nullptr;
    g_hitreg_feedback = nullptr;
    g_hitreg_fire = nullptr;
    g_warning = nullptr;
    g_predict_entry = nullptr;
    g_warning_entry = nullptr;
    g_stock_predict_entry = nullptr;
    g_stock_cgame_update_cvars = nullptr;
    g_stock_cgame_draw_2d = nullptr;
    g_stock_draw_active_frame = nullptr;
    g_stock_warning_entry = nullptr;
    g_replay_auth.active = 0;
    clear_transient_frame();
    g_render_clock.reset();
    g_last_submitted_pitch = 0.0F;
    g_last_submitted_yaw = 0.0F;
    g_last_submitted_view_valid = false;
    reset_pose_freshness_baseline();
    InterlockedExchange(&g_refdef_angle_pitch_mdeg, 0);
    InterlockedExchange(&g_refdef_angle_yaw_mdeg, 0);
    InterlockedExchange(&g_submitted_pitch_mdeg, 0);
    InterlockedExchange(&g_submitted_yaw_mdeg, 0);
    reset_hitreg();
    return true;
}

bool attach_cgame_hooks() {
    AcquireSRWLockExclusive(&g_cgame_gate);
    if (g_cgame == nullptr || g_warning_entry != nullptr || g_predict_entry != nullptr ||
        g_cgame_update_cvars != nullptr || g_cgame_draw_2d != nullptr ||
        g_warning != nullptr || g_predict != nullptr || g_fps_display != nullptr ||
        g_hitreg_fire != nullptr || g_hitreg_feedback != nullptr ||
        g_hitreg_draw != nullptr || g_draw_active_frame != nullptr ||
        g_scene_submission != nullptr || g_frame_interpolation_seam != nullptr ||
        g_shadow_impact_return != nullptr || g_shadow_mark_entry != nullptr ||
        g_shadow_mark_add_poly != nullptr || g_player_scene_renderer != nullptr ||
        InterlockedCompareExchange(&g_player_style_call_patch_state, 0, 0) != 0) {
        ReleaseSRWLockExclusive(&g_cgame_gate);
        return false;
    }
    if (!relocated_absolute_signature_matches(cgame_address(k_cgame_warning_entry),
                                               k_warning_entry_signature, 4U,
                                               cgame_address(k_cgame_warning_absolute)) ||
        !signature_matches(cgame_address(k_cgame_predict_entry), k_predict_entry_signature) ||
        !signature_matches(cgame_address(k_cgame_warning_compare), k_warning_signature) ||
        !signature_matches(cgame_address(k_cgame_predict_compare), k_predict_signature) ||
        !relocated_absolute_signature_matches(cgame_address(k_cgame_fps_value),
                                               k_fps_value_signature, 4U,
                                               cgame_address(k_cgame_fps_absolute)) ||
        !signature_matches(cgame_address(k_cgame_hitreg_fire), k_hitreg_fire_signature) ||
        !signature_matches(cgame_address(k_cgame_angle_vectors), k_angle_vectors_signature) ||
        !signature_matches(cgame_address(k_cgame_hitreg_feedback), k_hitreg_feedback_signature) ||
        !signature_matches(cgame_address(k_cgame_hitreg_draw), k_hitreg_draw_signature) ||
        !signature_matches(cgame_address(k_cgame_text_measure), k_text_measure_signature) ||
        !signature_matches(cgame_address(k_cgame_text_paint), k_text_paint_signature) ||
        !signature_matches(cgame_address(k_cgame_draw_active_frame),
                           k_draw_active_frame_signature) ||
        !relocated_absolute_signature_matches(
            cgame_address(k_cgame_draw_2d), k_draw_2d_signature, 4U,
            cgame_address(k_cgame_warning_absolute)) ||
        !relocated_absolute_signature_matches(
            cgame_address(k_cgame_update_cvars), k_update_cvars_signature, 4U,
            cgame_address(k_cgame_cvar_table)) ||
        !signature_matches(cgame_address(k_cgame_scene_submission_call),
                           k_scene_submission_call_signature) ||
        !relocated_absolute_signature_matches(
            cgame_address(k_cgame_frame_interpolation_seam),
            k_frame_interpolation_seam_signature, 1U,
            cgame_address(k_cgame_frame_interpolation_followup))) {
        ReleaseSRWLockExclusive(&g_cgame_gate);
        return false;
    }

    auto entry1_result = safetyhook::InlineHook::create(
        cgame_address(k_cgame_warning_entry), &warning_entry_hook,
        safetyhook::InlineHook::StartDisabled);
    auto entry2_result = safetyhook::InlineHook::create(
        cgame_address(k_cgame_predict_entry), &predictor_entry_hook,
        safetyhook::InlineHook::StartDisabled);
    auto warning_result = safetyhook::MidHook::create(
        cgame_address(k_cgame_warning_compare), &warning_compare_hook,
        safetyhook::MidHook::StartDisabled);
    auto predict_result = safetyhook::MidHook::create(
        cgame_address(k_cgame_predict_compare), &predictor_compare_hook,
        safetyhook::MidHook::StartDisabled);
    auto fps_result = safetyhook::MidHook::create(
        cgame_address(k_cgame_fps_value), &fps_display_hook,
        safetyhook::MidHook::StartDisabled);
    auto hitreg_fire_result = safetyhook::MidHook::create(
        cgame_address(k_cgame_hitreg_fire), &hitreg_fire_hook,
        safetyhook::MidHook::StartDisabled);
    auto hitreg_feedback_result = safetyhook::MidHook::create(
        cgame_address(k_cgame_hitreg_feedback), &hitreg_feedback_hook,
        safetyhook::MidHook::StartDisabled);
    auto hitreg_draw_result = safetyhook::MidHook::create(
        cgame_address(k_cgame_hitreg_draw), &hitreg_draw_hook,
        safetyhook::MidHook::StartDisabled);
    auto draw_active_frame_result = safetyhook::InlineHook::create(
        cgame_address(k_cgame_draw_active_frame), &draw_active_frame_hook,
        safetyhook::InlineHook::StartDisabled);
    auto draw_2d_result = safetyhook::InlineHook::create(
        cgame_address(k_cgame_draw_2d), &cgame_draw_2d_hook,
        safetyhook::InlineHook::StartDisabled);
    auto update_cvars_result = safetyhook::InlineHook::create(
        cgame_address(k_cgame_update_cvars), &cgame_update_cvars_hook,
        safetyhook::InlineHook::StartDisabled);
    auto scene_submission_result = safetyhook::MidHook::create(
        cgame_address(k_cgame_scene_submission_call), &scene_submission_hook,
        safetyhook::MidHook::StartDisabled);
    auto frame_interpolation_result = safetyhook::MidHook::create(
        cgame_address(k_cgame_frame_interpolation_seam),
        &frame_interpolation_seam_hook, safetyhook::MidHook::StartDisabled);
    auto* entry1 = move_to_heap(entry1_result);
    auto* entry2 = move_to_heap(entry2_result);
    auto* warning = move_to_heap(warning_result);
    auto* predict = move_to_heap(predict_result);
    auto* fps = move_to_heap(fps_result);
    auto* hitreg_fire = move_to_heap(hitreg_fire_result);
    auto* hitreg_feedback = move_to_heap(hitreg_feedback_result);
    auto* hitreg_draw = move_to_heap(hitreg_draw_result);
    auto* draw_active_frame = move_to_heap(draw_active_frame_result);
    auto* draw_2d = move_to_heap(draw_2d_result);
    auto* update_cvars = move_to_heap(update_cvars_result);
    auto* scene_submission = move_to_heap(scene_submission_result);
    auto* frame_interpolation = move_to_heap(frame_interpolation_result);
    if (entry1 == nullptr || entry2 == nullptr || warning == nullptr || predict == nullptr ||
        fps == nullptr || hitreg_fire == nullptr || hitreg_feedback == nullptr ||
        hitreg_draw == nullptr || draw_active_frame == nullptr || draw_2d == nullptr ||
        update_cvars == nullptr || scene_submission == nullptr ||
        frame_interpolation == nullptr) {
        destroy_candidate(entry1);
        destroy_candidate(entry2);
        destroy_candidate(warning);
        destroy_candidate(predict);
        destroy_candidate(fps);
        destroy_candidate(hitreg_fire);
        destroy_candidate(hitreg_feedback);
        destroy_candidate(hitreg_draw);
        destroy_candidate(draw_active_frame);
        destroy_candidate(draw_2d);
        destroy_candidate(update_cvars);
        destroy_candidate(scene_submission);
        destroy_candidate(frame_interpolation);
        ReleaseSRWLockExclusive(&g_cgame_gate);
        return false;
    }
    g_warning_entry = entry1;
    g_predict_entry = entry2;
    g_warning = warning;
    g_predict = predict;
    g_fps_display = fps;
    g_hitreg_fire = hitreg_fire;
    g_hitreg_feedback = hitreg_feedback;
    g_hitreg_draw = hitreg_draw;
    g_draw_active_frame = draw_active_frame;
    g_cgame_draw_2d = draw_2d;
    g_cgame_update_cvars = update_cvars;
    g_scene_submission = scene_submission;
    g_frame_interpolation_seam = frame_interpolation;
    g_stock_warning_entry = entry1->original<CgameEntryFn>();
    g_stock_predict_entry = entry2->original<CgameEntryFn>();
    g_stock_cgame_update_cvars = update_cvars->original<CgameEntryFn>();
    g_stock_cgame_draw_2d = draw_2d->original<CgameEntryFn>();
    g_stock_draw_active_frame = draw_active_frame->original<DrawActiveFrameFn>();
    InterlockedExchange(&g_cvar_refresh_warmed, 0);
    reset_hud_replay_cache();
    g_last_submitted_pitch = 0.0F;
    g_last_submitted_yaw = 0.0F;
    g_last_submitted_view_valid = false;
    reset_pose_freshness_baseline();
    g_render_clock.reset();
    if (!entry1->enable().has_value() || !entry2->enable().has_value() ||
        !warning->enable().has_value() || !predict->enable().has_value() ||
        !fps->enable().has_value() || !hitreg_fire->enable().has_value() ||
        !hitreg_feedback->enable().has_value() || !hitreg_draw->enable().has_value() ||
        !draw_active_frame->enable().has_value() ||
        !draw_2d->enable().has_value() ||
        !update_cvars->enable().has_value() ||
        !scene_submission->enable().has_value() ||
        !frame_interpolation->enable().has_value()) {
        (void)detach_cgame_hooks();
        ReleaseSRWLockExclusive(&g_cgame_gate);
        return false;
    }
    if (!attach_shadow_mark_cache_hooks() ||
        !attach_player_scene_replay_hook() ||
        !attach_player_style_fast_path_hooks()) {
        (void)detach_cgame_hooks();
        ReleaseSRWLockExclusive(&g_cgame_gate);
        return false;
    }
    reset_hitreg();
    InterlockedExchange(&g_preview_chain_armed, g_config_fresh_view);
    ReleaseSRWLockExclusive(&g_cgame_gate);
    return true;
}

bool install_runtime_hooks() {
    const auto check_signature = [](const void* address, const auto& expected,
                                    const char* reason) {
        if (signature_matches(address, expected)) {
            return true;
        }
        g_reason.store(reason, std::memory_order_release);
        return false;
    };
    const auto check_relocated_signature = [](const void* address, const auto& expected,
                                              std::size_t operand_offset,
                                              const void* relocated_target,
                                              const char* reason) {
        if (relocated_absolute_signature_matches(address, expected, operand_offset,
                                                 relocated_target)) {
            return true;
        }
        g_reason.store(reason, std::memory_order_release);
        return false;
    };
    if (!check_signature(engine_address(k_engine_s1), k_s1_signature,
                          "hook_signature_mismatch_s1") ||
        !check_relocated_signature(engine_address(k_engine_s2), k_s2_signature, 4U,
                                   engine_address(k_engine_s2_absolute),
                                   "hook_signature_mismatch_s2") ||
        !check_relocated_signature(engine_address(k_engine_s3), k_s3_signature, 5U,
                                   engine_address(k_engine_time_absolute),
                                   "hook_signature_mismatch_s3") ||
        !check_signature(engine_address(k_engine_s4), k_s4_signature,
                         "hook_signature_mismatch_s4") ||
        !check_relocated_signature(engine_address(k_engine_s8), k_s8_signature, 4U,
                                   engine_address(k_engine_time_absolute),
                                   "hook_signature_mismatch_s8") ||
        !check_signature(engine_address(k_engine_s9_pre), k_s9_pre_signature,
                         "hook_signature_mismatch_s9_pre") ||
        !check_signature(engine_address(k_engine_s9_post), k_s9_post_signature,
                         "hook_signature_mismatch_s9_post") ||
        !check_signature(engine_address(k_engine_s10), k_s10_signature,
                         "hook_signature_mismatch_s10") ||
        !check_relocated_signature(engine_address(k_engine_s11), k_s11_signature, 2U,
                                   engine_address(k_engine_s11_absolute),
                                   "hook_signature_mismatch_s11") ||
        !check_relocated_signature(engine_address(k_engine_present), k_present_signature, 2U,
                                   engine_address(k_engine_present_absolute),
                                   "hook_signature_mismatch_present") ||
        !check_signature(engine_address(k_engine_mouse_transform), k_mouse_transform_signature,
                         "hook_signature_mismatch_mouse_transform") ||
        !check_signature(engine_address(k_engine_packet_write), k_packet_write_signature,
                         "hook_signature_mismatch_packet_write") ||
        !check_relocated_signature(engine_address(k_engine_endframe), k_endframe_signature, 1U,
                                   engine_address(k_engine_endframe_absolute),
                                   "hook_signature_mismatch_endframe") ||
        !check_signature(engine_address(k_engine_re_endframe), k_re_endframe_signature,
                         "hook_signature_mismatch_re_endframe") ||
        !check_relocated_signature(
            engine_address(k_engine_renderer_command_buffer),
            k_renderer_command_buffer_signature, 4U,
            engine_address(k_engine_smp_frame_index),
            "hook_signature_mismatch_renderer_command_buffer") ||
        !check_relocated_signature(cgame_address(k_cgame_warning_entry),
                                   k_warning_entry_signature, 4U,
                                   cgame_address(k_cgame_warning_absolute),
                                   "hook_signature_mismatch_warning_entry") ||
        !check_signature(cgame_address(k_cgame_predict_entry), k_predict_entry_signature,
                         "hook_signature_mismatch_predict_entry") ||
        !check_signature(cgame_address(k_cgame_warning_compare), k_warning_signature,
                         "hook_signature_mismatch_warning_compare") ||
        !check_signature(cgame_address(k_cgame_predict_compare), k_predict_signature,
                         "hook_signature_mismatch_predict_compare") ||
        !check_relocated_signature(cgame_address(k_cgame_fps_value), k_fps_value_signature, 4U,
                                   cgame_address(k_cgame_fps_absolute),
                                   "hook_signature_mismatch_fps_value") ||
        !check_signature(cgame_address(k_cgame_hitreg_fire), k_hitreg_fire_signature,
                         "hook_signature_mismatch_hitreg_fire") ||
        !check_signature(cgame_address(k_cgame_angle_vectors), k_angle_vectors_signature,
                         "hook_signature_mismatch_angle_vectors") ||
        !check_signature(cgame_address(k_cgame_hitreg_feedback), k_hitreg_feedback_signature,
                         "hook_signature_mismatch_hitreg_feedback") ||
        !check_signature(cgame_address(k_cgame_hitreg_draw), k_hitreg_draw_signature,
                         "hook_signature_mismatch_hitreg_draw") ||
        !check_signature(cgame_address(k_cgame_text_measure), k_text_measure_signature,
                         "hook_signature_mismatch_text_measure") ||
        !check_signature(cgame_address(k_cgame_text_paint), k_text_paint_signature,
                         "hook_signature_mismatch_text_paint") ||
        !check_signature(cgame_address(k_cgame_draw_active_frame),
                         k_draw_active_frame_signature,
                         "hook_signature_mismatch_draw_active_frame") ||
        !check_relocated_signature(cgame_address(k_cgame_draw_2d),
                                   k_draw_2d_signature, 4U,
                                   cgame_address(k_cgame_warning_absolute),
                                   "hook_signature_mismatch_draw_2d") ||
        !check_relocated_signature(cgame_address(k_cgame_update_cvars),
                                   k_update_cvars_signature, 4U,
                                   cgame_address(k_cgame_cvar_table),
                                   "hook_signature_mismatch_update_cvars") ||
        !check_signature(cgame_address(k_cgame_scene_submission_call),
                         k_scene_submission_call_signature,
                         "hook_signature_mismatch_scene_submission") ||
        !check_relocated_signature(cgame_address(k_cgame_frame_interpolation_seam),
                                   k_frame_interpolation_seam_signature, 1U,
                                   cgame_address(k_cgame_frame_interpolation_followup),
                                   "hook_signature_mismatch_frame_interpolation")) {
        return false;
    }

    HMODULE pinned_patch{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                            reinterpret_cast<LPCWSTR>(&__ImageBase), &pinned_patch)) {
        g_reason.store("patch_pin_failed", std::memory_order_release);
        return false;
    }
    (void)pinned_patch;

    auto* slot = reinterpret_cast<void* volatile*>(engine_address(k_engine_getter_slot));
    const auto stock_getter = engine_address(k_engine_stock_getter);
    if (slot == nullptr || stock_getter == nullptr) {
        g_reason.store("getter_slot_unresolved", std::memory_order_release);
        return false;
    }

    void* existing = *slot;
    if (existing != stock_getter && existing != reinterpret_cast<void*>(&get_usercmd_hook)) {
        g_reason.store("getter_slot_owner_changed", std::memory_order_release);
        return false;
    }
    g_stock_getter = reinterpret_cast<GetterFn>(stock_getter);

    const auto* command_number_ptr =
        static_cast<const std::int32_t*>(engine_address(k_engine_cmd_number));
    if (command_number_ptr == nullptr) {
        g_reason.store("command_number_unresolved", std::memory_order_release);
        return false;
    }
    initialize_history(*command_number_ptr);

    auto s1_result = safetyhook::MidHook::create(engine_address(k_engine_s1), &frame_floor_hook,
                                                  safetyhook::MidHook::StartDisabled);
    auto s2_result = safetyhook::InlineHook::create(engine_address(k_engine_s2), &delta_hook,
                                                    safetyhook::InlineHook::StartDisabled);
    auto s3_result = safetyhook::InlineHook::create(engine_address(k_engine_s3), &client_frame_hook,
                                                    safetyhook::InlineHook::StartDisabled);
    auto s4_result = safetyhook::InlineHook::create(engine_address(k_engine_s4), &publisher_hook,
                                                    safetyhook::InlineHook::StartDisabled);
    auto screen_update_result = safetyhook::InlineHook::create(
        engine_address(k_engine_present), &screen_update_hook,
        safetyhook::InlineHook::StartDisabled);
    auto packet_write_result = safetyhook::InlineHook::create(
        engine_address(k_engine_packet_write), &packet_write_hook,
        safetyhook::InlineHook::StartDisabled);
    auto endframe_result = safetyhook::InlineHook::create(
        engine_address(k_engine_endframe), &endframe_hook,
        safetyhook::InlineHook::StartDisabled);
    auto re_endframe_result = safetyhook::InlineHook::create(
        engine_address(k_engine_re_endframe), &re_endframe_hook,
        safetyhook::InlineHook::StartDisabled);
    auto s8_result = safetyhook::InlineHook::create(engine_address(k_engine_s8), &disconnect_hook,
                                                    safetyhook::InlineHook::StartDisabled);
    auto s9_pre_result = safetyhook::MidHook::create(engine_address(k_engine_s9_pre), &s9_pre_hook,
                                                     safetyhook::MidHook::StartDisabled);
    auto s9_post_result = safetyhook::MidHook::create(engine_address(k_engine_s9_post), &s9_post_hook,
                                                      safetyhook::MidHook::StartDisabled);
    auto s10_result = safetyhook::InlineHook::create(engine_address(k_engine_s10), &vm_loader_hook,
                                                     safetyhook::InlineHook::StartDisabled);
    auto s11_result = safetyhook::InlineHook::create(engine_address(k_engine_s11), &shutdown_hook,
                                                     safetyhook::InlineHook::StartDisabled);
    auto warning_entry_result =
        safetyhook::InlineHook::create(cgame_address(k_cgame_warning_entry), &warning_entry_hook,
                                       safetyhook::InlineHook::StartDisabled);
    auto predict_entry_result =
        safetyhook::InlineHook::create(cgame_address(k_cgame_predict_entry), &predictor_entry_hook,
                                       safetyhook::InlineHook::StartDisabled);
    auto warning_result = safetyhook::MidHook::create(cgame_address(k_cgame_warning_compare),
                                                      &warning_compare_hook,
                                                      safetyhook::MidHook::StartDisabled);
    auto predict_result = safetyhook::MidHook::create(cgame_address(k_cgame_predict_compare),
                                                      &predictor_compare_hook,
                                                      safetyhook::MidHook::StartDisabled);
    auto fps_result = safetyhook::MidHook::create(cgame_address(k_cgame_fps_value),
                                                  &fps_display_hook,
                                                  safetyhook::MidHook::StartDisabled);
    auto hitreg_fire_result = safetyhook::MidHook::create(cgame_address(k_cgame_hitreg_fire),
                                                          &hitreg_fire_hook,
                                                          safetyhook::MidHook::StartDisabled);
    auto hitreg_feedback_result = safetyhook::MidHook::create(
        cgame_address(k_cgame_hitreg_feedback), &hitreg_feedback_hook,
        safetyhook::MidHook::StartDisabled);
    auto hitreg_draw_result = safetyhook::MidHook::create(cgame_address(k_cgame_hitreg_draw),
                                                          &hitreg_draw_hook,
                                                          safetyhook::MidHook::StartDisabled);
    auto draw_active_frame_result = safetyhook::InlineHook::create(
        cgame_address(k_cgame_draw_active_frame), &draw_active_frame_hook,
        safetyhook::InlineHook::StartDisabled);
    auto draw_2d_result = safetyhook::InlineHook::create(
        cgame_address(k_cgame_draw_2d), &cgame_draw_2d_hook,
        safetyhook::InlineHook::StartDisabled);
    auto update_cvars_result = safetyhook::InlineHook::create(
        cgame_address(k_cgame_update_cvars), &cgame_update_cvars_hook,
        safetyhook::InlineHook::StartDisabled);
    auto scene_submission_result = safetyhook::MidHook::create(
        cgame_address(k_cgame_scene_submission_call), &scene_submission_hook,
        safetyhook::MidHook::StartDisabled);
    auto frame_interpolation_result = safetyhook::MidHook::create(
        cgame_address(k_cgame_frame_interpolation_seam),
        &frame_interpolation_seam_hook, safetyhook::MidHook::StartDisabled);

    auto* s1 = move_to_heap(s1_result);
    auto* s2 = move_to_heap(s2_result);
    auto* s3 = move_to_heap(s3_result);
    auto* s4 = move_to_heap(s4_result);
    auto* screen_update = move_to_heap(screen_update_result);
    auto* packet_write = move_to_heap(packet_write_result);
    auto* endframe = move_to_heap(endframe_result);
    auto* re_endframe = move_to_heap(re_endframe_result);
    auto* s8 = move_to_heap(s8_result);
    auto* s9_pre = move_to_heap(s9_pre_result);
    auto* s9_post = move_to_heap(s9_post_result);
    auto* s10 = move_to_heap(s10_result);
    auto* s11 = move_to_heap(s11_result);
    auto* warning_entry = move_to_heap(warning_entry_result);
    auto* predict_entry = move_to_heap(predict_entry_result);
    auto* warning = move_to_heap(warning_result);
    auto* predict = move_to_heap(predict_result);
    auto* fps = move_to_heap(fps_result);
    auto* hitreg_fire = move_to_heap(hitreg_fire_result);
    auto* hitreg_feedback = move_to_heap(hitreg_feedback_result);
    auto* hitreg_draw = move_to_heap(hitreg_draw_result);
    auto* draw_active_frame = move_to_heap(draw_active_frame_result);
    auto* draw_2d = move_to_heap(draw_2d_result);
    auto* update_cvars = move_to_heap(update_cvars_result);
    auto* scene_submission = move_to_heap(scene_submission_result);
    auto* frame_interpolation = move_to_heap(frame_interpolation_result);
    if (s1 == nullptr || s2 == nullptr || s3 == nullptr || s4 == nullptr ||
        screen_update == nullptr ||
        packet_write == nullptr || endframe == nullptr || re_endframe == nullptr ||
        s8 == nullptr ||
        s9_pre == nullptr || s9_post == nullptr || s10 == nullptr || s11 == nullptr ||
        warning_entry == nullptr || predict_entry == nullptr || warning == nullptr ||
        predict == nullptr || fps == nullptr || hitreg_fire == nullptr ||
        hitreg_feedback == nullptr || hitreg_draw == nullptr ||
        draw_active_frame == nullptr || draw_2d == nullptr ||
        update_cvars == nullptr || scene_submission == nullptr ||
        frame_interpolation == nullptr) {
        destroy_candidate(s1);
        destroy_candidate(s2);
        destroy_candidate(s3);
        destroy_candidate(s4);
        destroy_candidate(screen_update);
        destroy_candidate(packet_write);
        destroy_candidate(endframe);
        destroy_candidate(re_endframe);
        destroy_candidate(s8);
        destroy_candidate(s9_pre);
        destroy_candidate(s9_post);
        destroy_candidate(s10);
        destroy_candidate(s11);
        destroy_candidate(warning_entry);
        destroy_candidate(predict_entry);
        destroy_candidate(warning);
        destroy_candidate(predict);
        destroy_candidate(fps);
        destroy_candidate(hitreg_fire);
        destroy_candidate(hitreg_feedback);
        destroy_candidate(hitreg_draw);
        destroy_candidate(draw_active_frame);
        destroy_candidate(draw_2d);
        destroy_candidate(update_cvars);
        destroy_candidate(scene_submission);
        destroy_candidate(frame_interpolation);
        g_reason.store("hook_create_failed", std::memory_order_release);
        return false;
    }

    g_s1 = s1;
    g_s2 = s2;
    g_s3 = s3;
    g_s4 = s4;
    g_screen_update = screen_update;
    g_packet_write = packet_write;
    g_endframe = endframe;
    g_re_endframe = re_endframe;
    g_s8 = s8;
    g_s9_pre = s9_pre;
    g_s9_post = s9_post;
    g_s10 = s10;
    g_s11 = s11;
    g_warning_entry = warning_entry;
    g_predict_entry = predict_entry;
    g_warning = warning;
    g_predict = predict;
    g_fps_display = fps;
    g_hitreg_fire = hitreg_fire;
    g_hitreg_feedback = hitreg_feedback;
    g_hitreg_draw = hitreg_draw;
    g_draw_active_frame = draw_active_frame;
    g_cgame_draw_2d = draw_2d;
    g_cgame_update_cvars = update_cvars;
    g_scene_submission = scene_submission;
    g_frame_interpolation_seam = frame_interpolation;
    g_stock_delta = g_s2->original<DeltaFn>();
    g_stock_client_frame = g_s3->original<ClientFrameFn>();
    g_stock_publisher = g_s4->original<PublisherFn>();
    g_present = g_screen_update->original<PresentFn>();
    g_stock_packet_write = g_packet_write->original<PacketWriteFn>();
    g_stock_endframe = g_endframe->original<EndFrameFn>();
    g_stock_re_endframe = g_re_endframe->original<ReEndFrameFn>();
    g_stock_disconnect = g_s8->original<DisconnectFn>();
    g_stock_vm_loader = g_s10->original<VmLoaderFn>();
    g_stock_shutdown = g_s11->original<ShutdownFn>();
    g_stock_warning_entry = g_warning_entry->original<CgameEntryFn>();
    g_stock_predict_entry = g_predict_entry->original<CgameEntryFn>();
    g_stock_cgame_update_cvars = g_cgame_update_cvars->original<CgameEntryFn>();
    g_stock_cgame_draw_2d = g_cgame_draw_2d->original<CgameEntryFn>();
    g_stock_draw_active_frame = g_draw_active_frame->original<DrawActiveFrameFn>();
    InterlockedExchange(&g_cvar_refresh_warmed, 0);
    reset_hud_replay_cache();
    g_mouse_transform =
        reinterpret_cast<MouseTransformFn>(engine_address(k_engine_mouse_transform));
    g_renderer_command_buffer = reinterpret_cast<RendererCommandBufferFn>(
        engine_address(k_engine_renderer_command_buffer));

    const bool e1 = s1->enable().has_value();
    const bool e2 = s2->enable().has_value();
    const bool e3 = s3->enable().has_value();
    const bool e4 = s4->enable().has_value();
    const bool e_screen_update = screen_update->enable().has_value();
    const bool e_packet_write = packet_write->enable().has_value();
    const bool e_endframe = endframe->enable().has_value();
    const bool e_re_endframe = re_endframe->enable().has_value();
    const bool e8 = s8->enable().has_value();
    const bool e9_pre = s9_pre->enable().has_value();
    const bool e9_post = s9_post->enable().has_value();
    const bool e10 = s10->enable().has_value();
    const bool e11 = s11->enable().has_value();
    const bool e_warning_entry = warning_entry->enable().has_value();
    const bool e_predict_entry = predict_entry->enable().has_value();
    const bool e_warning = warning->enable().has_value();
    const bool e_predict = predict->enable().has_value();
    const bool e_fps = fps->enable().has_value();
    const bool e_hitreg_fire = hitreg_fire->enable().has_value();
    const bool e_hitreg_feedback = hitreg_feedback->enable().has_value();
    const bool e_hitreg_draw = hitreg_draw->enable().has_value();
    const bool e_draw_active_frame = draw_active_frame->enable().has_value();
    const bool e_draw_2d = draw_2d->enable().has_value();
    const bool e_update_cvars = update_cvars->enable().has_value();
    const bool e_scene_submission = scene_submission->enable().has_value();
    const bool e_frame_interpolation = frame_interpolation->enable().has_value();
    const bool enabled = e1 && e2 && e3 && e4 &&
                         e_screen_update &&
                         e_packet_write && e_endframe &&
                         e_re_endframe && e8 &&
                         e9_pre && e9_post && e10 && e11 &&
                         e_warning_entry && e_predict_entry && e_warning && e_predict && e_fps &&
                         e_hitreg_fire && e_hitreg_feedback && e_hitreg_draw &&
                         e_draw_active_frame && e_draw_2d && e_update_cvars &&
                         e_scene_submission && e_frame_interpolation;
    if (!enabled) {
        InterlockedExchange(&g_runtime_armed, 0);
        InterlockedExchange(&g_preview_chain_armed, 0);
        // Keep any partially enabled hooks resident for process life. All
        // callbacks are explicitly stock-only while runtime_armed is zero;
        // removing a live hook here would race a game thread and violate the
        // no-live-unhook lifecycle contract.
        g_reason.store("hook_enable_failed_pass_through", std::memory_order_release);
        return false;
    }

    AcquireSRWLockExclusive(&g_cgame_gate);
    const bool attached_cgame_candidates =
        attach_shadow_mark_cache_hooks() &&
        attach_player_scene_replay_hook() &&
        attach_player_style_fast_path_hooks();
    ReleaseSRWLockExclusive(&g_cgame_gate);
    if (!attached_cgame_candidates) {
        InterlockedExchange(&g_runtime_armed, 0);
        InterlockedExchange(&g_preview_chain_armed, 0);
        return false;
    }

    if (!ensure_getter_slot()) {
        InterlockedExchange(&g_runtime_armed, 0);
        InterlockedExchange(&g_preview_chain_armed, 0);
        g_reason.store("getter_slot_install_failed_pass_through", std::memory_order_release);
        return false;
    }

    InterlockedExchange(&g_preview_chain_armed, g_config_fresh_view);
    InterlockedExchange(&g_runtime_armed, 1);
    ensure_telemetry_worker();
    g_reason.store(g_config_fresh_view != 0 ? "runtime_candidate_active_unverified"
                                            : "runtime_control_floor1",
                   std::memory_order_release);
    return true;
}

bool try_install() {
    if (InterlockedCompareExchange(&g_runtime_armed, 0, 0) != 0) {
        return true;
    }
    if (InterlockedCompareExchange(&g_installing, 1, 0) != 0) {
        return false;
    }

    bool installed = false;
    g_config_enabled = configured_enabled() && configured_experimental() ? 1 : 0;
    g_config_fresh_view = configured_fresh_view() ? 1 : 0;
    g_config_highres_entity_interpolation =
        configured_highres_entity_interpolation() ? 1 : 0;
    g_config_raster_fingerprint = configured_raster_fingerprint() ? 1 : 0;
    g_config_hud_replay = configured_hud_replay() ? 1 : 0;
    g_config_zero_bloom_fast_path = configured_zero_bloom_fast_path() ? 1 : 0;
    g_config_color_correct_identity_fast_path =
        configured_color_correct_identity_fast_path() ? 1 : 0;
    g_config_shadow_mark_cache = configured_shadow_mark_cache() ? 1 : 0;
    g_config_player_scene_replay = configured_player_scene_replay() ? 1 : 0;
    g_config_player_scene_bypass =
        g_config_player_scene_replay != 0 && configured_player_scene_bypass() ? 1 : 0;
    g_config_player_style_fast_path = configured_player_style_fast_path() ? 1 : 0;
    g_config_player_style_bypass =
        g_config_player_style_fast_path != 0 && configured_player_style_bypass() ? 1 : 0;
    if (g_config_enabled == 0) {
        g_reason.store("disabled_stock", std::memory_order_release);
        InterlockedExchange(&g_status, k_stock);
    } else {
        g_engine = GetModuleHandleW(nullptr);
        HMODULE cgame_ticket{};
        const bool acquired_cgame =
            GetModuleHandleExW(0, L"cgamex86.dll", &cgame_ticket) != FALSE;
        if (g_engine == nullptr || !acquired_cgame || cgame_ticket == nullptr) {
            g_reason.store("waiting_for_engine_or_cgame", std::memory_order_release);
            InterlockedExchange(&g_status, k_waiting_for_modules);
        } else if (InterlockedCompareExchangePointer(
                       reinterpret_cast<void* volatile*>(&g_cgame_install_ticket),
                       cgame_ticket, nullptr) != nullptr) {
            FreeLibrary(cgame_ticket);
            g_reason.store("cgame_install_ticket_state_fault", std::memory_order_release);
            InterlockedExchange(&g_status, k_refused);
        } else {
            g_cgame = cgame_ticket;
            if (!hash_file(module_path(g_engine), g_engine_hash) ||
                   !hash_file(module_path(g_cgame), g_cgame_hash)) {
                g_reason.store("module_hash_read_failed", std::memory_order_release);
                InterlockedExchange(&g_status, k_refused);
            } else if (!exact_hash(std::string_view(g_engine_hash.data()), k_engine_hash)) {
                g_reason.store("engine_identity_mismatch", std::memory_order_release);
                InterlockedExchange(&g_status, k_refused);
            } else if (!exact_hash(std::string_view(g_cgame_hash.data()), k_cgame_hash)) {
                g_reason.store("cgame_identity_mismatch", std::memory_order_release);
                InterlockedExchange(&g_status, k_refused);
            } else {
                InterlockedExchange(&g_status, k_identity_validated);
                installed = install_runtime_hooks();
                if (installed) {
                    InterlockedExchange(&g_status, k_runtime_active);
                } else {
                    InterlockedExchange(&g_status, k_refused);
                }
            }
        }
    }
    if (!installed) {
        release_cgame_install_ticket();
    }
    InterlockedExchange(&g_installing, 0);
    return installed;
}

DWORD WINAPI bootstrap_worker(void*) noexcept {
    bool first_attempt = true;
    for (;;) {
        if (!first_attempt && InterlockedCompareExchange(&g_config_enabled, 0, 0) == 0) {
            return 0;
        }
        first_attempt = false;
        if (try_install()) {
            return 0;
        }
        if (InterlockedCompareExchange(&g_status, 0, 0) == k_refused) {
            return 0;
        }
        Sleep(100);
    }
}

void ensure_worker() {
    if (InterlockedCompareExchange(&g_worker_started, 1, 0) != 0) {
        return;
    }
    const HANDLE thread = CreateThread(nullptr, 0, &bootstrap_worker, nullptr, 0, nullptr);
    if (thread != nullptr) {
        CloseHandle(thread);
    } else {
        InterlockedExchange(&g_worker_started, 0);
    }
}

} // namespace

extern "C" BOOL WINAPI ql_patch_bootstrap() noexcept {
    const LONG current_status = InterlockedCompareExchange(&g_status, 0, 0);
    if (current_status != k_waiting_for_modules &&
        InterlockedCompareExchange(&g_bootstrap_attempted, 1, 0) != 0) {
        return InterlockedCompareExchange(&g_status, 0, 0) == k_runtime_active;
    }
    if (current_status == k_waiting_for_modules &&
        InterlockedCompareExchange(&g_bootstrap_attempted, 1, 0) != 0) {
        ensure_worker();
        return FALSE;
    }
    g_config_enabled = configured_enabled() && configured_experimental() ? 1 : 0;
    g_config_force_smp = configured_force_smp() ? 1 : 0;
    g_config_smp_synchronous = configured_smp_synchronous() ? 1 : 0;
    g_config_smp_single_buffer = configured_smp_single_buffer() ? 1 : 0;
    g_config_smp_copy_fpu = configured_smp_copy_fpu() ? 1 : 0;
    g_config_smp_main_thread_backend = configured_smp_main_thread_backend() ? 1 : 0;
    g_config_smp_late_activation = configured_smp_late_activation() ? 1 : 0;
    g_config_smp_persistent_context = configured_smp_persistent_context() ? 1 : 0;
    g_config_raster_fingerprint = configured_raster_fingerprint() ? 1 : 0;
    g_config_hud_replay = configured_hud_replay() ? 1 : 0;
    g_config_zero_bloom_fast_path = configured_zero_bloom_fast_path() ? 1 : 0;
    g_config_color_correct_identity_fast_path =
        configured_color_correct_identity_fast_path() ? 1 : 0;
    g_config_shadow_mark_cache = configured_shadow_mark_cache() ? 1 : 0;
    g_config_player_scene_replay = configured_player_scene_replay() ? 1 : 0;
    g_config_player_scene_bypass =
        g_config_player_scene_replay != 0 && configured_player_scene_bypass() ? 1 : 0;
    g_config_player_style_fast_path = configured_player_style_fast_path() ? 1 : 0;
    g_config_player_style_bypass =
        g_config_player_style_fast_path != 0 && configured_player_style_bypass() ? 1 : 0;
    LARGE_INTEGER qpc_frequency{};
    if (QueryPerformanceFrequency(&qpc_frequency) && qpc_frequency.QuadPart > 0) {
        InterlockedExchange64(&g_qpc_frequency, qpc_frequency.QuadPart);
    }
    if (g_config_enabled == 0) {
        g_reason.store("disabled_stock", std::memory_order_release);
        InterlockedExchange(&g_status, k_stock);
        return FALSE;
    }
    g_engine = GetModuleHandleW(nullptr);
    if (g_engine == nullptr || !hash_file(module_path(g_engine), g_engine_hash) ||
        !exact_hash(std::string_view(g_engine_hash.data()), k_engine_hash)) {
        g_reason.store("bootstrap_engine_identity_failed", std::memory_order_release);
        InterlockedExchange(&g_status, k_refused);
        return FALSE;
    }
    if (!apply_force_smp_default()) {
        InterlockedExchange(&g_status, k_refused);
        return FALSE;
    }
    if (!install_smp_renderer_sleep_hook()) {
        InterlockedExchange(&g_status, k_refused);
        return FALSE;
    }
    if (!install_smp_frontend_sleep_hook()) {
        InterlockedExchange(&g_status, k_refused);
        return FALSE;
    }
    if (!install_smp_synchronous_hook()) {
        InterlockedExchange(&g_status, k_refused);
        return FALSE;
    }
    if (!install_smp_context_lease_hook()) {
        InterlockedExchange(&g_status, k_refused);
        return FALSE;
    }
    if (!install_renderer_registration_hooks()) {
        InterlockedExchange(&g_status, k_refused);
        return FALSE;
    }
    if (!install_zero_bloom_fast_path_hook()) {
        InterlockedExchange(&g_status, k_refused);
        return FALSE;
    }
    if (!install_color_correct_identity_fast_path_hook()) {
        InterlockedExchange(&g_status, k_refused);
        return FALSE;
    }
    if (!install_smp_command_buffers_shutdown_hook()) {
        InterlockedExchange(&g_status, k_refused);
        return FALSE;
    }
    if (!install_smp_single_buffer_hook()) {
        InterlockedExchange(&g_status, k_refused);
        return FALSE;
    }
    if (!install_backend_allocation_hook()) {
        InterlockedExchange(&g_status, k_refused);
        return FALSE;
    }
    if (!install_smp_font_upload_hooks()) {
        InterlockedExchange(&g_status, k_refused);
        return FALSE;
    }
    if (!install_smp_command_buffers_init_hook()) {
        InterlockedExchange(&g_status, k_refused);
        return FALSE;
    }
    if (!install_smp_main_thread_backend_hook()) {
        InterlockedExchange(&g_status, k_refused);
        return FALSE;
    }
    ensure_worker();
    const bool installed = try_install();
    return installed ? TRUE : FALSE;
}

extern "C" LONG WINAPI ql_patch_status() noexcept {
    return InterlockedCompareExchange(&g_status, 0, 0);
}

extern "C" LONG WINAPI ql_patch_measured_present_fps() noexcept {
    return InterlockedCompareExchange(&g_measured_present_fps, 0, 0);
}

extern "C" LONG WINAPI ql_patch_measured_simulation_hz() noexcept {
    return InterlockedCompareExchange(&g_measured_simulation_hz, 0, 0);
}

extern "C" const char* WINAPI ql_patch_reason() noexcept {
    return g_reason.load(std::memory_order_acquire);
}

extern "C" const char* WINAPI ql_patch_engine_hash() noexcept { return g_engine_hash.data(); }

extern "C" const char* WINAPI ql_patch_cgame_hash() noexcept { return g_cgame_hash.data(); }

extern "C" LONG WINAPI ql_patch_config_enabled() noexcept {
    return InterlockedCompareExchange(&g_config_enabled, 0, 0);
}

extern "C" BOOL WINAPI ql_patch_live_unload_refused() noexcept { return TRUE; }

extern "C" LONG WINAPI ql_patch_safetyhook_bound() noexcept {
    // Constructing an empty hook is side-effect free, but its out-of-line
    // destructor forces the release DLL to bind the pinned SafetyHook library
    // rather than merely carrying an unused link line.
    safetyhook::InlineHook probe{};
    return probe ? 0 : 1;
}

DWORD WINAPI auto_bootstrap_thread(void*) noexcept {
    // Let loader lock clear before any config, module, or hook work.
    Sleep(100);
    (void)ql_patch_bootstrap();
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) noexcept {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        const HANDLE thread = CreateThread(nullptr, 0, &auto_bootstrap_thread, nullptr, 0, nullptr);
        if (thread != nullptr) {
            CloseHandle(thread);
        }
    }
    return TRUE;
}
