#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace ql1k {

struct FontUploadLayout {
    int x{};
    int y{};
    int right{};
    int bottom{};
    int stride{};
};

struct FontUploadSizes {
    std::size_t width{};
    std::size_t height{};
    std::size_t packed_bytes{};
    std::size_t staging_bytes{};
};

[[nodiscard]] constexpr bool should_defer_font_upload(
    const bool late_activation_configured, const bool late_activation_active,
    const bool registration_complete, const bool smp_active,
    const bool persistent_context_configured,
    const bool persistent_main_context_available) noexcept {
    return late_activation_configured && late_activation_active &&
           registration_complete && smp_active &&
           (!persistent_context_configured || !persistent_main_context_available);
}

inline bool describe_font_upload(const FontUploadLayout& layout,
                                 const std::size_t maximum_dimension,
                                 const std::size_t maximum_bytes,
                                 FontUploadSizes& sizes) noexcept {
    sizes = {};
    if (layout.x < 0 || layout.y < 0 || layout.right <= layout.x ||
        layout.bottom <= layout.y || layout.stride <= 0) {
        return false;
    }

    const auto x = static_cast<std::size_t>(layout.x);
    const auto right = static_cast<std::size_t>(layout.right);
    const auto bottom = static_cast<std::size_t>(layout.bottom);
    const auto stride = static_cast<std::size_t>(layout.stride);
    if (stride > maximum_dimension || bottom > maximum_dimension || right > stride) {
        return false;
    }

    const std::size_t width = right - x;
    const std::size_t height = bottom - static_cast<std::size_t>(layout.y);
    if (width > maximum_bytes / height || stride > maximum_bytes / bottom) {
        return false;
    }

    sizes.width = width;
    sizes.height = height;
    sizes.packed_bytes = width * height;
    sizes.staging_bytes = stride * bottom;
    return sizes.packed_bytes <= maximum_bytes && sizes.staging_bytes <= maximum_bytes;
}

inline void pack_font_upload(const std::uint8_t* const source,
                             const FontUploadLayout& layout,
                             const FontUploadSizes& sizes,
                             std::uint8_t* const packed) noexcept {
    const auto stride = static_cast<std::size_t>(layout.stride);
    const auto x = static_cast<std::size_t>(layout.x);
    const auto y = static_cast<std::size_t>(layout.y);
    for (std::size_t row = 0; row < sizes.height; ++row) {
        std::memcpy(packed + row * sizes.width,
                    source + (y + row) * stride + x, sizes.width);
    }
}

inline bool expand_font_upload(const std::uint8_t* const packed,
                               const FontUploadLayout& layout,
                               const FontUploadSizes& sizes,
                               std::uint8_t* const staging,
                               const std::size_t staging_capacity) noexcept {
    if (staging_capacity < sizes.staging_bytes) {
        return false;
    }
    const auto stride = static_cast<std::size_t>(layout.stride);
    const auto x = static_cast<std::size_t>(layout.x);
    const auto y = static_cast<std::size_t>(layout.y);
    for (std::size_t row = 0; row < sizes.height; ++row) {
        std::memcpy(staging + (y + row) * stride + x,
                    packed + row * sizes.width, sizes.width);
    }
    return true;
}

} // namespace ql1k
