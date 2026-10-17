#pragma once

// No standard headers: this header lands in many global module fragments, and std declarations
// used from there break lookup of std concepts for importers of those modules (Clang, `import std`).
// assertFailed is defined in assert.cpp.

#ifdef _MSC_VER
#define AGX_DEBUG_BREAK() __debugbreak()
#elif defined(__GNUC__) || defined(__clang__)
#define AGX_DEBUG_BREAK() __builtin_trap()
#else
#include <cstdlib>
#define AGX_DEBUG_BREAK() std::abort()
#endif

#ifndef NDEBUG
#define AGX_ASSERTS_ENABLED
#endif // !NDEBUG

#ifdef AGX_ASSERTS_ENABLED
// clang-format off
#define AGX_ASSERT(expr) \
    ((expr) ? static_cast<void>(0) : \
    (aegis::assertFailed(#expr, "", __FILE__, __LINE__, __FUNCTION__), AGX_DEBUG_BREAK()))
#define AGX_ASSERT_X(expr, msg) \
    ((expr) ? static_cast<void>(0) : \
    (aegis::assertFailed(#expr, msg, __FILE__, __LINE__, __FUNCTION__), AGX_DEBUG_BREAK()))
#define AGX_UNREACHABLE(msg) \
    (aegis::assertFailed("Unreachable code reached", msg, __FILE__, __LINE__, __FUNCTION__), AGX_DEBUG_BREAK())
// clang-format on
#else
#define AGX_ASSERT(expr) static_cast<void>(0)
#define AGX_ASSERT_X(expr, msg) static_cast<void>(0)
#define AGX_UNREACHABLE(msg) static_cast<void>(0)
#endif // AGX_ASSERTS_ENABLED

namespace aegis
{
void assertFailed(const char* expr, const char* what, const char* file, int line, const char* function);
}
