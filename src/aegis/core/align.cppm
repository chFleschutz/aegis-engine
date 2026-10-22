module;

#include <cassert>

export module aegis.core:align;

import std;

export namespace aegis::core
{
/// @brief Aligns 'size' to the next multiple of 'alignment'.
/// @note 'alignment' must be a power of 2 greater than 0.
constexpr auto alignTo(std::size_t size, std::size_t alignment) -> std::size_t
{
    assert(alignment > 0 && "Alignment must be greater than 0");
    assert((alignment & (alignment - 1)) == 0 && "Alignment must be a power of 2");
    return (size + alignment - 1) & ~(alignment - 1);
}

/// @brief Rounds 'size' up to the next multiple of 'multiple'.
/// @note Unlike alignTo, 'multiple' need not be a power of 2 -- Vulkan texel block sizes are not
///       (R32G32B32 is 12 bytes). Prefer alignTo when the alignment is known to be a power of 2.
constexpr auto roundUpTo(std::size_t size, std::size_t multiple) -> std::size_t
{
    assert(multiple > 0 && "Multiple must be greater than 0");
    return ((size + multiple - 1) / multiple) * multiple;
}
}
