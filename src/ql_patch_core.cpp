#include <Windows.h>
#include <bcrypt.h>
#include <intrin.h>

#include <safetyhook.hpp>

#include "hitreg_state.hpp"

#include <array>
#include <atomic>
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
constexpr std::uintptr_t k_engine_cmd_number = 0x014725D0U;
constexpr std::uintptr_t k_engine_cmd_ring = 0x01471ED0U;
constexpr std::uintptr_t k_engine_getter_slot = 0x00565AC0U;
constexpr std::uintptr_t k_engine_stock_getter = 0x004B0180U;
constexpr std::uintptr_t k_engine_interface = 0x0146CC38U;
constexpr std::uintptr_t k_engine_vm_pointer = 0x01647F0CU;
constexpr std::uintptr_t k_engine_s2_absolute = 0x0145B950U;
constexpr std::uintptr_t k_engine_time_absolute = 0x01205E30U;
constexpr std::uintptr_t k_engine_s11_absolute = 0x01528BA4U;
constexpr std::uintptr_t k_engine_present_absolute = 0x0146CC34U;
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
constexpr std::uintptr_t k_cgame_text_measure = 0x100082B0U;
constexpr std::uintptr_t k_cgame_text_paint = 0x10008440U;
constexpr std::uintptr_t k_cgame_client_info_teams = 0x10A41DF8U;
constexpr std::uintptr_t k_cgame_serverinfo_flags = 0x10A3FF28U;
constexpr std::uintptr_t k_cgame_replay_return = 0x10044953U;
constexpr std::uintptr_t k_cgame_warning_absolute = 0x10075000U;
constexpr std::uintptr_t k_cgame_fps_absolute = 0x10ABAF8CU;

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
constexpr std::array<std::uint8_t, 7> k_s10_signature{
    0x55, 0x8B, 0xEC, 0x53, 0x8B, 0x5D, 0x08};
constexpr std::array<std::uint8_t, 7> k_s11_signature{
    0x83, 0x25, 0xA4, 0x8B, 0x52, 0x01, 0xF7};
constexpr std::array<std::uint8_t, 7> k_present_signature{
    0x83, 0x3D, 0x34, 0xCC, 0x46, 0x01, 0x00};
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

constexpr std::size_t k_record_size = 28U;
constexpr std::size_t k_history_capacity = 4096U;
constexpr std::size_t k_history_warmup = 64U;
constexpr std::size_t k_usercmd_buttons_offset = 16U;
constexpr std::int32_t k_button_attack = 1;
constexpr std::size_t k_client_info_stride = 0x738U;
constexpr std::size_t k_trace_entity_number_index = 13U;

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
using DisconnectFn = void(__cdecl*)(int);
using VmLoaderFn = void*(__cdecl*)(const char*, void*, void*, int*);
using CgameEntryFn = void(__cdecl*)();
using ShutdownFn = void(__cdecl*)();
using TextPaintFn = void(__cdecl*)(float, float, int, float, const float*, const char*, float,
                                   int, int);
using PointTraceFn = void(__cdecl*)(std::uint32_t*, const float*, const float*, const float*,
                                    const float*, std::int32_t, std::int32_t);

struct ReplayAuthorization {
    unsigned char active{};
    std::int32_t next{};
    std::int32_t end{};
    LONG generation{};
    HMODULE cgame{};
};

volatile LONG g_bootstrap_attempted{};
volatile LONG g_worker_started{};
volatile LONG g_installing{};
volatile LONG g_status{k_stock};
volatile LONG g_config_enabled{};
volatile LONG g_runtime_armed{};
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
volatile LONG g_measured_present_fps{};
volatile LONG g_measured_simulation_hz{};
volatile LONG64 g_present_count{};
volatile LONG64 g_simulation_count{};
std::atomic<const char*> g_reason{"not_started"};
std::array<char, 65> g_engine_hash{};
std::array<char, 65> g_cgame_hash{};

HMODULE g_engine{};
HMODULE g_cgame{};
DeltaFn g_stock_delta{};
ClientFrameFn g_stock_client_frame{};
PublisherFn g_stock_publisher{};
GetterFn g_stock_getter{};
PresentFn g_present{};
DisconnectFn g_stock_disconnect{};
VmLoaderFn g_stock_vm_loader{};
ShutdownFn g_stock_shutdown{};
CgameEntryFn g_stock_warning_entry{};
CgameEntryFn g_stock_predict_entry{};

safetyhook::MidHook* g_s1{};
safetyhook::InlineHook* g_s2{};
safetyhook::InlineHook* g_s3{};
safetyhook::InlineHook* g_s4{};
safetyhook::InlineHook* g_s8{};
safetyhook::MidHook* g_s9_pre{};
safetyhook::MidHook* g_s9_post{};
safetyhook::InlineHook* g_s10{};
safetyhook::InlineHook* g_s11{};
safetyhook::InlineHook* g_warning_entry{};
safetyhook::InlineHook* g_predict_entry{};
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
SRWLOCK g_hitreg_lock = SRWLOCK_INIT;
ql1k::HitregState g_hitreg_state{};
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
__declspec(thread) ReplayAuthorization g_replay_auth{};
__declspec(thread) LONG g_s9_token_depth{};
__declspec(thread) LONG g_module_ticket_depth{};

bool attach_cgame_hooks();
bool detach_cgame_hooks();

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

DWORD WINAPI telemetry_worker(void*) noexcept {
    LARGE_INTEGER frequency{};
    LARGE_INTEGER previous_time{};
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0 ||
        !QueryPerformanceCounter(&previous_time)) {
        return 0;
    }
    LONG64 previous_presents = InterlockedCompareExchange64(&g_present_count, 0, 0);
    LONG64 previous_simulation = InterlockedCompareExchange64(&g_simulation_count, 0, 0);
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
        const LONG fps = static_cast<LONG>(((presents - previous_presents) * frequency.QuadPart +
                                            elapsed / 2) / elapsed);
        const LONG sim_hz = static_cast<LONG>(((simulation - previous_simulation) *
                                               frequency.QuadPart + elapsed / 2) / elapsed);
        InterlockedExchange(&g_measured_present_fps, fps);
        InterlockedExchange(&g_measured_simulation_hz, sim_hz);
        previous_time = now;
        previous_presents = presents;
        previous_simulation = simulation;

        ql1k::HitregDisplay hitreg{};
        ql1k::HitregDiagnostics hitreg_diagnostics{};
        AcquireSRWLockShared(&g_hitreg_lock);
        hitreg = g_hitreg_state.published();
        hitreg_diagnostics = g_hitreg_state.diagnostics();
        ReleaseSRWLockShared(&g_hitreg_lock);
        const char* const runtime_reason = g_reason.load(std::memory_order_acquire);

        // This record intentionally carries both immutable hold data and
        // session diagnostics. Never let a future field addition turn
        // telemetry truncation into the CRT invalid-parameter process abort.
        char line[1536]{};
        const int formatted_bytes = _snprintf_s(
            line, sizeof(line), _TRUNCATE,
            "measured_present_fps=%ld simulation_hz=%ld status=%ld reason=%s "
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
            fps, sim_hz, InterlockedCompareExchange(&g_status, 0, 0),
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
                                      ? static_cast<std::size_t>(formatted_bytes)
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
            sprintf_s(title, "Quake Live | measured %ld FPS | simulation %ld Hz", fps, sim_hz);
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
    InterlockedExchange(&g_permanent_fault, 1);
    InterlockedExchange(&g_history_fault, 1);
    InterlockedExchange(&g_history_active, 0);
    InterlockedExchange(&g_transition_closed, 1);
    InterlockedExchange(&g_runtime_armed, 0);
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
    reset_hitreg();
    return true;
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
        (void)append_published_record(old_command, new_command, ticket_generation);
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
                    g_reason.store("runtime_candidate_active_unverified",
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
    AcquireSRWLockExclusive(&g_cgame_gate);
    if (!detach_cgame_hooks()) {
        mark_permanent_fault("cgame_restart_detach_failed");
        if (token) {
            end_transition();
        }
        ReleaseSRWLockExclusive(&g_cgame_gate);
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
    release_module_ticket();
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
    // Preserve the engine's 1 ms input/usercmd boundary. Sub-millisecond
    // presentation without a transient cgame camera repeats committed view and
    // can starve/lose perceived mouse motion under sustained busy rendering.
    *reinterpret_cast<std::int32_t*>(context.ebp - 4U) = 1;
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
    if (delta == 0 && g_zero_frame_token != 0 &&
        InterlockedCompareExchange(&g_runtime_armed, 0, 0) != 0) {
        g_zero_frame_token = 0;
        const auto present = g_present;
        if (present != nullptr) {
            present();
            InterlockedIncrement64(&g_present_count);
        }
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

bool detach_cgame_hooks() {
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

    destroy_candidate(g_predict);
    destroy_candidate(g_fps_display);
    destroy_candidate(g_hitreg_draw);
    destroy_candidate(g_hitreg_feedback);
    destroy_candidate(g_hitreg_fire);
    destroy_candidate(g_warning);
    destroy_candidate(g_predict_entry);
    destroy_candidate(g_warning_entry);
    g_predict = nullptr;
    g_fps_display = nullptr;
    g_hitreg_draw = nullptr;
    g_hitreg_feedback = nullptr;
    g_hitreg_fire = nullptr;
    g_warning = nullptr;
    g_predict_entry = nullptr;
    g_warning_entry = nullptr;
    g_stock_predict_entry = nullptr;
    g_stock_warning_entry = nullptr;
    g_replay_auth.active = 0;
    reset_hitreg();
    return true;
}

bool attach_cgame_hooks() {
    AcquireSRWLockExclusive(&g_cgame_gate);
    if (g_cgame == nullptr || g_warning_entry != nullptr || g_predict_entry != nullptr ||
        g_warning != nullptr || g_predict != nullptr || g_fps_display != nullptr ||
        g_hitreg_fire != nullptr || g_hitreg_feedback != nullptr ||
        g_hitreg_draw != nullptr) {
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
        !signature_matches(cgame_address(k_cgame_text_paint), k_text_paint_signature)) {
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
    auto* entry1 = move_to_heap(entry1_result);
    auto* entry2 = move_to_heap(entry2_result);
    auto* warning = move_to_heap(warning_result);
    auto* predict = move_to_heap(predict_result);
    auto* fps = move_to_heap(fps_result);
    auto* hitreg_fire = move_to_heap(hitreg_fire_result);
    auto* hitreg_feedback = move_to_heap(hitreg_feedback_result);
    auto* hitreg_draw = move_to_heap(hitreg_draw_result);
    if (entry1 == nullptr || entry2 == nullptr || warning == nullptr || predict == nullptr ||
        fps == nullptr || hitreg_fire == nullptr || hitreg_feedback == nullptr ||
        hitreg_draw == nullptr) {
        destroy_candidate(entry1);
        destroy_candidate(entry2);
        destroy_candidate(warning);
        destroy_candidate(predict);
        destroy_candidate(fps);
        destroy_candidate(hitreg_fire);
        destroy_candidate(hitreg_feedback);
        destroy_candidate(hitreg_draw);
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
    g_stock_warning_entry = entry1->original<CgameEntryFn>();
    g_stock_predict_entry = entry2->original<CgameEntryFn>();
    if (!entry1->enable().has_value() || !entry2->enable().has_value() ||
        !warning->enable().has_value() || !predict->enable().has_value() ||
        !fps->enable().has_value() || !hitreg_fire->enable().has_value() ||
        !hitreg_feedback->enable().has_value() || !hitreg_draw->enable().has_value()) {
        (void)detach_cgame_hooks();
        ReleaseSRWLockExclusive(&g_cgame_gate);
        return false;
    }
    reset_hitreg();
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
                         "hook_signature_mismatch_text_paint")) {
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

    auto* s1 = move_to_heap(s1_result);
    auto* s2 = move_to_heap(s2_result);
    auto* s3 = move_to_heap(s3_result);
    auto* s4 = move_to_heap(s4_result);
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
    if (s1 == nullptr || s2 == nullptr || s3 == nullptr || s4 == nullptr || s8 == nullptr ||
        s9_pre == nullptr || s9_post == nullptr || s10 == nullptr || s11 == nullptr ||
        warning_entry == nullptr || predict_entry == nullptr || warning == nullptr ||
        predict == nullptr || fps == nullptr || hitreg_fire == nullptr ||
        hitreg_feedback == nullptr || hitreg_draw == nullptr) {
        destroy_candidate(s1);
        destroy_candidate(s2);
        destroy_candidate(s3);
        destroy_candidate(s4);
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
        g_reason.store("hook_create_failed", std::memory_order_release);
        return false;
    }

    g_s1 = s1;
    g_s2 = s2;
    g_s3 = s3;
    g_s4 = s4;
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
    g_stock_delta = g_s2->original<DeltaFn>();
    g_stock_client_frame = g_s3->original<ClientFrameFn>();
    g_stock_publisher = g_s4->original<PublisherFn>();
    g_stock_disconnect = g_s8->original<DisconnectFn>();
    g_stock_vm_loader = g_s10->original<VmLoaderFn>();
    g_stock_shutdown = g_s11->original<ShutdownFn>();
    g_stock_warning_entry = g_warning_entry->original<CgameEntryFn>();
    g_stock_predict_entry = g_predict_entry->original<CgameEntryFn>();
    g_present = reinterpret_cast<PresentFn>(engine_address(k_engine_present));

    const bool e1 = s1->enable().has_value();
    const bool e2 = s2->enable().has_value();
    const bool e3 = s3->enable().has_value();
    const bool e4 = s4->enable().has_value();
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
    const bool enabled = e1 && e2 && e3 && e4 && e8 && e9_pre && e9_post && e10 && e11 &&
                         e_warning_entry && e_predict_entry && e_warning && e_predict && e_fps &&
                         e_hitreg_fire && e_hitreg_feedback && e_hitreg_draw;
    if (!enabled) {
        InterlockedExchange(&g_runtime_armed, 0);
        // Keep any partially enabled hooks resident for process life. All
        // callbacks are explicitly stock-only while runtime_armed is zero;
        // removing a live hook here would race a game thread and violate the
        // no-live-unhook lifecycle contract.
        g_reason.store("hook_enable_failed_pass_through", std::memory_order_release);
        return false;
    }

    if (!ensure_getter_slot()) {
        InterlockedExchange(&g_runtime_armed, 0);
        g_reason.store("getter_slot_install_failed_pass_through", std::memory_order_release);
        return false;
    }

    InterlockedExchange(&g_runtime_armed, 1);
    ensure_telemetry_worker();
    g_reason.store("runtime_candidate_active_unverified", std::memory_order_release);
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
    if (g_config_enabled == 0) {
        g_reason.store("disabled_stock", std::memory_order_release);
        InterlockedExchange(&g_status, k_stock);
    } else {
        g_engine = GetModuleHandleW(nullptr);
        g_cgame = GetModuleHandleW(L"cgamex86.dll");
        if (g_engine == nullptr || g_cgame == nullptr) {
            g_reason.store("waiting_for_engine_or_cgame", std::memory_order_release);
            InterlockedExchange(&g_status, k_waiting_for_modules);
        } else if (!hash_file(module_path(g_engine), g_engine_hash) ||
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
    if (g_config_enabled == 0) {
        g_reason.store("disabled_stock", std::memory_order_release);
        InterlockedExchange(&g_status, k_stock);
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
