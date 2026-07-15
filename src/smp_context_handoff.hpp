#pragma once

namespace ql1k {

enum class SmpContextState : long {
    fault = -1,
    stock = 0,
    renderer_owned = 1,
    main_owned = 2,
    released_to_main = 3,
    released_to_renderer = 4,
};

inline constexpr long k_client_state_active = 8;
inline constexpr long k_keycatch_ui = 2;

[[nodiscard]] constexpr bool persistent_context_gameplay_eligible(
    const long client_state, const long key_catchers,
    const bool ui_fullscreen = false) noexcept {
    return client_state == k_client_state_active &&
           (key_catchers & k_keycatch_ui) == 0 && !ui_fullscreen;
}

[[nodiscard]] constexpr bool screen_render_allowed(
    const bool runtime_armed, const bool context_reconciled) noexcept {
    return !runtime_armed || context_reconciled;
}

[[nodiscard]] constexpr bool persistent_wake_requires_ack(
    const bool null_command, const bool synchronous_requested) noexcept {
    return null_command || synchronous_requested;
}

[[nodiscard]] constexpr bool smp_context_protocol_active(const long state) noexcept {
    return state >= static_cast<long>(SmpContextState::renderer_owned) &&
           state <= static_cast<long>(SmpContextState::released_to_renderer);
}

[[nodiscard]] constexpr bool renderer_should_release_for_sync(
    const long state, const long pending_syncs) noexcept {
    return state == static_cast<long>(SmpContextState::renderer_owned) &&
           pending_syncs > 0;
}

[[nodiscard]] constexpr bool main_should_acquire_after_sync(const long state) noexcept {
    return state == static_cast<long>(SmpContextState::released_to_main);
}

[[nodiscard]] constexpr bool main_should_release_for_render(const long state) noexcept {
    return state == static_cast<long>(SmpContextState::main_owned);
}

[[nodiscard]] constexpr bool renderer_should_acquire_for_render(const long state) noexcept {
    return state == static_cast<long>(SmpContextState::released_to_renderer);
}

} // namespace ql1k
