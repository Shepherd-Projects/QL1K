#include "transient_camera.hpp"

#include <array>
#include <cstdlib>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace {

void require(const bool condition) {
    if (!condition) {
        std::abort();
    }
}

void scheduler_is_fail_closed() {
    using ql1k::FrameOwnership;
    require(ql1k::scheduler_floor(false, false) == 1);
    require(ql1k::scheduler_floor(true, false) == 1);
    require(ql1k::scheduler_floor(false, true) == 1);
    require(ql1k::scheduler_floor(true, true) == 0);

    const auto zero = ql1k::classify_frame(0, true, true);
    require(zero == FrameOwnership::render_only);
    require(!ql1k::owns_gameplay_cadence(zero));
    require(ql1k::owns_gameplay_cadence(
        ql1k::classify_frame(1, false, true)));
    require(ql1k::classify_frame(0, true, false) == FrameOwnership::stock);

    require(!ql1k::should_reuse_cgame_cvars(false, false, false));
    require(!ql1k::should_reuse_cgame_cvars(true, false, true));
    require(!ql1k::should_reuse_cgame_cvars(true, true, false));
    require(!ql1k::should_reuse_cgame_cvars(false, true, true));
    require(ql1k::should_reuse_cgame_cvars(true, true, true));

}

void state_restores_byte_exactly() {
    std::array<std::uint8_t, 284> state{};
    for (std::size_t index = 0; index < state.size(); ++index) {
        state[index] = static_cast<std::uint8_t>((index * 37U) & 0xffU);
    }
    const auto original = state;
    ql1k::ByteSnapshot<284> snapshot;
    snapshot.capture(state.data());
    std::memset(state.data(), 0xA5, state.size());
    require(snapshot.restore(state.data()));
    require(snapshot.matches(state.data()));
    require(state == original);
}

void overlay_is_bounded_to_one_draw() {
    float angles[3]{10.0F, 350.0F, 3.0F};
    const std::array<float, 3> original{angles[0], angles[1], angles[2]};
    ql1k::ViewOverlay overlay;
    require(overlay.apply(angles, 2.5F, 15.0F));
    require(!overlay.apply(angles, 1.0F, 1.0F));
    require(angles[0] == 12.5F);
    require(angles[1] == 365.0F);
    angles[2] = 99.0F;  // Render-side mutation must not escape the transaction.
    require(overlay.restore(angles));
    require(!overlay.restore(angles));
    require(std::memcmp(angles, original.data(), sizeof(angles)) == 0);
}

void deltas_wrap_and_reject_non_finite_input() {
    require(std::fabs(ql1k::wrapped_angle_delta(1.0F, 359.0F) - 2.0F) < 0.001F);
    require(std::fabs(ql1k::wrapped_angle_delta(359.0F, 1.0F) + 2.0F) < 0.001F);
    require(ql1k::wrapped_angle_delta(INFINITY, 0.0F) == 0.0F);
}

void submitted_view_change_is_material_and_wrap_safe() {
    require(!ql1k::materially_changed_view(10.0F, 20.0F, 10.0F, 20.0F));
    require(!ql1k::materially_changed_view(10.0005F, 20.0F, 10.0F, 20.0F));
    require(ql1k::materially_changed_view(10.01F, 20.0F, 10.0F, 20.0F));
    require(ql1k::materially_changed_view(10.0F, 0.01F, 10.0F, 359.99F));
    require(!ql1k::materially_changed_view(INFINITY, 20.0F, 10.0F, 20.0F));
    require(!ql1k::materially_changed_view(10.0F, 20.0F, 10.0F, 20.0F, -1.0F));
}

void submitted_forward_axis_converts_to_viewangles() {
    const float forward_zero[] = {1.0F, 0.0F, 0.0F};
    const auto zero_angles = ql1k::viewangles_from_forward_axis(forward_zero);
    require(zero_angles.valid);
    require(std::fabs(zero_angles.pitch) < 0.001F);
    require(std::fabs(zero_angles.yaw) < 0.001F);

    const float forward_quadrant[] = {0.0F, 0.70710678F, -0.70710678F};
    const auto quadrant_angles = ql1k::viewangles_from_forward_axis(forward_quadrant);
    require(quadrant_angles.valid);
    require(std::fabs(quadrant_angles.pitch - 45.0F) < 0.001F);
    require(std::fabs(quadrant_angles.yaw - 90.0F) < 0.001F);

    const float invalid_forward[] = {0.0F, 0.0F, 0.0F};
    require(!ql1k::viewangles_from_forward_axis(invalid_forward).valid);
    require(!ql1k::viewangles_from_forward_axis(nullptr).valid);
}

}  // namespace

int main() {
    scheduler_is_fail_closed();
    state_restores_byte_exactly();
    overlay_is_bounded_to_one_draw();
    deltas_wrap_and_reject_non_finite_input();
    submitted_view_change_is_material_and_wrap_safe();
    submitted_forward_axis_converts_to_viewangles();
    return 0;
}
