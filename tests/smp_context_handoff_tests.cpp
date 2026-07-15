#include "smp_context_handoff.hpp"

namespace {

using ql1k::SmpContextState;

constexpr long value(const SmpContextState state) noexcept {
    return static_cast<long>(state);
}

static_assert(!ql1k::smp_context_protocol_active(value(SmpContextState::fault)));
static_assert(!ql1k::smp_context_protocol_active(value(SmpContextState::stock)));
static_assert(ql1k::smp_context_protocol_active(value(SmpContextState::renderer_owned)));
static_assert(ql1k::smp_context_protocol_active(value(SmpContextState::main_owned)));
static_assert(ql1k::smp_context_protocol_active(value(SmpContextState::released_to_main)));
static_assert(ql1k::smp_context_protocol_active(value(SmpContextState::released_to_renderer)));

static_assert(ql1k::renderer_should_release_for_sync(
    value(SmpContextState::renderer_owned), 1));
static_assert(!ql1k::renderer_should_release_for_sync(
    value(SmpContextState::renderer_owned), 0));
static_assert(!ql1k::renderer_should_release_for_sync(
    value(SmpContextState::main_owned), 1));

static_assert(ql1k::main_should_acquire_after_sync(
    value(SmpContextState::released_to_main)));
static_assert(!ql1k::main_should_acquire_after_sync(
    value(SmpContextState::renderer_owned)));
static_assert(ql1k::main_should_release_for_render(
    value(SmpContextState::main_owned)));
static_assert(!ql1k::main_should_release_for_render(
    value(SmpContextState::released_to_main)));
static_assert(ql1k::renderer_should_acquire_for_render(
    value(SmpContextState::released_to_renderer)));
static_assert(!ql1k::renderer_should_acquire_for_render(
    value(SmpContextState::main_owned)));

static_assert(ql1k::persistent_context_gameplay_eligible(8, 0));
static_assert(ql1k::persistent_context_gameplay_eligible(8, 1));
static_assert(!ql1k::persistent_context_gameplay_eligible(8, 0, true));
static_assert(!ql1k::persistent_context_gameplay_eligible(8, 2));
static_assert(!ql1k::persistent_context_gameplay_eligible(8, 3));
static_assert(!ql1k::persistent_context_gameplay_eligible(1, 0));
static_assert(!ql1k::persistent_context_gameplay_eligible(9, 0));

static_assert(ql1k::screen_render_allowed(false, false));
static_assert(ql1k::screen_render_allowed(false, true));
static_assert(ql1k::screen_render_allowed(true, true));
static_assert(!ql1k::screen_render_allowed(true, false));

static_assert(!ql1k::persistent_wake_requires_ack(false, false));
static_assert(ql1k::persistent_wake_requires_ack(true, false));
static_assert(ql1k::persistent_wake_requires_ack(false, true));
static_assert(ql1k::persistent_wake_requires_ack(true, true));

} // namespace

int main() {
    return 0;
}
