#include "font_upload.hpp"

#include <array>
#include <cstdint>
#include <cstdio>

namespace {

bool test_pack_and_expand_preserve_dirty_rectangle() {
    std::array<std::uint8_t, 40> source{};
    for (std::size_t index = 0; index < source.size(); ++index) {
        source[index] = static_cast<std::uint8_t>(index + 1U);
    }

    constexpr ql1k::FontUploadLayout layout{2, 1, 6, 4, 8};
    ql1k::FontUploadSizes sizes{};
    if (!ql1k::describe_font_upload(layout, 64U, 4096U, sizes) ||
        sizes.width != 4U || sizes.height != 3U || sizes.packed_bytes != 12U ||
        sizes.staging_bytes != 32U) {
        return false;
    }

    std::array<std::uint8_t, 12> packed{};
    std::array<std::uint8_t, 32> staging{};
    ql1k::pack_font_upload(source.data(), layout, sizes, packed.data());
    if (!ql1k::expand_font_upload(packed.data(), layout, sizes, staging.data(),
                                  staging.size())) {
        return false;
    }

    for (std::size_t row = 0; row < sizes.height; ++row) {
        for (std::size_t column = 0; column < sizes.width; ++column) {
            const std::size_t source_index = (row + 1U) * 8U + column + 2U;
            if (packed[row * sizes.width + column] != source[source_index] ||
                staging[source_index] != source[source_index]) {
                return false;
            }
        }
    }
    return true;
}

bool test_rejects_invalid_or_excessive_layouts() {
    ql1k::FontUploadSizes sizes{};
    return !ql1k::describe_font_upload({-1, 0, 2, 2, 8}, 64U, 4096U, sizes) &&
           !ql1k::describe_font_upload({0, 0, 9, 2, 8}, 64U, 4096U, sizes) &&
           !ql1k::describe_font_upload({0, 0, 8, 65, 8}, 64U, 4096U, sizes) &&
           !ql1k::describe_font_upload({0, 0, 8, 8, 8}, 64U, 32U, sizes);
}

bool test_defers_only_after_registration_with_smp_active() {
    return ql1k::should_defer_font_upload(true, true, true, true, false, false) &&
           ql1k::should_defer_font_upload(true, true, true, true, true, false) &&
           !ql1k::should_defer_font_upload(true, true, true, true, true, true) &&
           !ql1k::should_defer_font_upload(false, true, true, true, false, false) &&
           !ql1k::should_defer_font_upload(true, false, true, true, false, false) &&
           !ql1k::should_defer_font_upload(true, true, false, true, false, false) &&
           !ql1k::should_defer_font_upload(true, true, true, false, false, false);
}

bool test_upload_restores_and_synchronizes_texture_binding() {
    int binding = 37;
    std::array<int, 2> renderer_cache{11, 12};
    int current_tmu = 1;
    std::array<int, 4> sequence{};
    std::size_t step = 0;
    ql1k::run_font_upload_preserving_binding(
        [&](int* const snapshot) noexcept {
            sequence[step++] = 1;
            *snapshot = binding;
        },
        [&]() noexcept {
            sequence[step++] = 2;
            binding = 91;
        },
        [&](const int snapshot) noexcept {
            sequence[step++] = 3;
            binding = snapshot;
        },
        [&](const int snapshot) noexcept {
            sequence[step++] = 4;
            (void)ql1k::synchronize_texture_binding_cache(
                snapshot, current_tmu, renderer_cache);
        });
    if (binding != 37 || renderer_cache != std::array<int, 2>{11, 37} ||
        sequence != std::array<int, 4>{1, 2, 3, 4}) {
        return false;
    }

    binding = 0;
    current_tmu = 0;
    renderer_cache = {91, 37};
    ql1k::run_font_upload_preserving_binding(
        [&](int* const snapshot) noexcept { *snapshot = binding; },
        [&]() noexcept { binding = 91; },
        [&](const int snapshot) noexcept { binding = snapshot; },
        [&](const int snapshot) noexcept {
            (void)ql1k::synchronize_texture_binding_cache(
                snapshot, current_tmu, renderer_cache);
        });
    if (binding != 0 || renderer_cache != std::array<int, 2>{0, 37}) {
        return false;
    }

    const auto unchanged = renderer_cache;
    return !ql1k::synchronize_texture_binding_cache(91, -1, renderer_cache) &&
           !ql1k::synchronize_texture_binding_cache(91, 2, renderer_cache) &&
           renderer_cache == unchanged;
}

} // namespace

int main() {
    const bool packed = test_pack_and_expand_preserve_dirty_rectangle();
    const bool rejected = test_rejects_invalid_or_excessive_layouts();
    const bool ownership = test_defers_only_after_registration_with_smp_active();
    const bool binding = test_upload_restores_and_synchronizes_texture_binding();
    std::printf("font upload pack/expand: %s\n", packed ? "pass" : "fail");
    std::printf("font upload validation: %s\n", rejected ? "pass" : "fail");
    std::printf("font upload ownership gate: %s\n", ownership ? "pass" : "fail");
    std::printf("font upload binding/cache sync: %s\n", binding ? "pass" : "fail");
    return packed && rejected && ownership && binding ? 0 : 1;
}
