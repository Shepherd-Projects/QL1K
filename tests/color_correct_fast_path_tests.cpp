#include "color_correct_fast_path.hpp"

#include <cstdlib>
#include <limits>

namespace {

ql1k::ColorCorrectIdentityGate valid_gate() {
    return ql1k::ColorCorrectIdentityGate{
        true, true, true, true, true, true, true, true, true, 1.0F, 1.0F, 0};
}

ql1k::ColorCorrectCommand9 valid_command() {
    return ql1k::ColorCorrectCommand9{9U, 17U, 23U, 0U};
}

void require(const bool condition) {
    if (!condition) {
        std::abort();
    }
}

} // namespace

int main() {
    auto gate = valid_gate();
    auto command = valid_command();
    require(ql1k::should_skip_identity_color_correct(gate, &command));

    gate.gamma = 1.0001F;
    require(!ql1k::should_skip_identity_color_correct(gate, &command));
    gate = valid_gate();
    gate.contrast = 0.9999F;
    require(!ql1k::should_skip_identity_color_correct(gate, &command));
    gate = valid_gate();
    gate.gamma = std::numeric_limits<float>::quiet_NaN();
    require(!ql1k::should_skip_identity_color_correct(gate, &command));
    gate = valid_gate();
    gate.overbright_bits = 1;
    require(!ql1k::should_skip_identity_color_correct(gate, &command));
    gate = valid_gate();
    gate.zero_bloom_fast_path_ran = false;
    require(!ql1k::should_skip_identity_color_correct(gate, &command));
    gate = valid_gate();
    gate.gameplay_eligible = false;
    require(!ql1k::should_skip_identity_color_correct(gate, &command));

    command.command_id = 10U;
    require(!ql1k::should_skip_identity_color_correct(valid_gate(), &command));
    command = valid_command();
    command.scene_texture = 0U;
    require(!ql1k::should_skip_identity_color_correct(valid_gate(), &command));
    command = valid_command();
    command.program = 0U;
    require(!ql1k::should_skip_identity_color_correct(valid_gate(), &command));
    require(!ql1k::should_skip_identity_color_correct(valid_gate(), nullptr));
    return 0;
}
