#include "bloom_fast_path.hpp"

#include <cstdlib>
#include <limits>

namespace {

void require(const bool condition) {
    if (!condition) {
        std::abort();
    }
}

ql1k::ZeroBloomGate valid_gate() {
    return {true, true, true, true, true, true, true, true,
            ql1k::k_minimum_bloom_mode, 0.0F, 10.0F, 1.2F, 0.7F,
            1920, 1080};
}

ql1k::BloomCommand10 valid_command() {
    ql1k::BloomCommand10 command{};
    command.command_id = ql1k::k_bloom_command_id;
    command.scene_texture = 17U;
    command.combine_program = 23U;
    return command;
}

void exact_zero_contribution_is_required() {
    auto gate = valid_gate();
    const auto command = valid_command();
    require(ql1k::should_use_zero_bloom_fast_path(gate, &command));
    gate.bloom_intensity = 0.0001F;
    require(!ql1k::should_use_zero_bloom_fast_path(gate, &command));
    gate.bloom_intensity = std::numeric_limits<float>::quiet_NaN();
    require(!ql1k::should_use_zero_bloom_fast_path(gate, &command));
}

void lifecycle_and_payload_fail_closed() {
    auto gate = valid_gate();
    auto command = valid_command();
    gate.gameplay_eligible = false;
    require(!ql1k::should_use_zero_bloom_fast_path(gate, &command));
    gate.gameplay_eligible = true;
    gate.uniforms_current = false;
    require(!ql1k::should_use_zero_bloom_fast_path(gate, &command));
    gate.uniforms_current = true;
    command.scene_texture = 0U;
    require(!ql1k::should_use_zero_bloom_fast_path(gate, &command));
    command = valid_command();
    command.command_id = 11U;
    require(!ql1k::should_use_zero_bloom_fast_path(gate, &command));
}

void unsupported_passes_and_dimensions_fail_closed() {
    auto gate = valid_gate();
    const auto command = valid_command();
    gate.bloom_mode = 2;
    require(!ql1k::should_use_zero_bloom_fast_path(gate, &command));
    gate = valid_gate();
    gate.width = 0;
    require(!ql1k::should_use_zero_bloom_fast_path(gate, &command));
    gate = valid_gate();
    gate.height = 16385;
    require(!ql1k::should_use_zero_bloom_fast_path(gate, &command));
}

} // namespace

int main() {
    exact_zero_contribution_is_required();
    lifecycle_and_payload_fail_closed();
    unsupported_passes_and_dimensions_fail_closed();
    return 0;
}
