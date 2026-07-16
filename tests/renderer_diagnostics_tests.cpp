#include "renderer_diagnostics.hpp"

#include <array>
#include <cstdlib>
#include <string_view>

namespace {

void require(const bool condition) {
    if (!condition) {
        std::abort();
    }
}

void queue_preserves_order_and_accounts_for_loss() {
    ql1k::RendererDiagnosticQueue<3U> queue;
    ql1k::RendererDiagnosticRecord first{};
    first.qpc = 10;
    ql1k::RendererDiagnosticRecord second{};
    second.qpc = 20;
    ql1k::RendererDiagnosticRecord third{};
    third.qpc = 30;
    ql1k::RendererDiagnosticRecord overflow{};
    overflow.qpc = 40;
    require(queue.push(first));
    require(queue.push(second));
    require(queue.push(third));
    require(!queue.push(overflow));
    require(queue.size() == 3U);
    require(queue.dropped() == 1U);

    std::array<ql1k::RendererDiagnosticRecord, 2> first_batch{};
    require(queue.drain(first_batch) == 2U);
    require(first_batch[0].qpc == 10);
    require(first_batch[1].qpc == 20);
    require(queue.push(overflow));

    std::array<ql1k::RendererDiagnosticRecord, 3> second_batch{};
    require(queue.drain(second_batch) == 2U);
    require(second_batch[0].qpc == 30);
    require(second_batch[1].qpc == 40);
    require(queue.size() == 0U);
    require(queue.dropped() == 1U);
}

void rate_limit_and_rotation_boundaries_are_exact() {
    require(ql1k::renderer_diagnostic_sample_due(100, 0, 50));
    require(!ql1k::renderer_diagnostic_sample_due(149, 100, 50));
    require(ql1k::renderer_diagnostic_sample_due(150, 100, 50));
    require(!ql1k::renderer_diagnostic_sample_due(150, 100, 0));
    require(!ql1k::renderer_diagnostic_log_needs_rotation(7, 8));
    require(ql1k::renderer_diagnostic_log_needs_rotation(8, 8));
    require(!ql1k::renderer_diagnostic_log_needs_rotation(8, 0));
}

void event_names_are_stable() {
    require(ql1k::renderer_diagnostic_event_name(
                ql1k::RendererDiagnosticEvent::session_start) ==
            std::string_view("session_start"));
    require(ql1k::renderer_diagnostic_event_name(
                ql1k::RendererDiagnosticEvent::font_replay_after) ==
            std::string_view("font_replay_after"));
}

} // namespace

int main() {
    queue_preserves_order_and_accounts_for_loss();
    rate_limit_and_rotation_boundaries_are_exact();
    event_names_are_stable();
    return 0;
}
