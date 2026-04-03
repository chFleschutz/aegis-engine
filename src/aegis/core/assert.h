#pragma once

#include <cstdlib>
#include <cassert>
#include <iostream>
#include <format>

#ifdef _MSC_VER
#define AGX_DEBUG_BREAK() __debugbreak()
#elif defined(__GNUC__) || defined(__clang__)
#define AGX_DEBUG_BREAK() __builtin_trap()
#else
#define AGX_DEBUG_BREAK() std::abort()
#endif

#ifndef NDEBUG
#define AGX_ASSERTS_ENABLED 
#endif // !NDEBUG

#ifdef AGX_ASSERTS_ENABLED
#define AGX_ASSERT(expr)					\
	((expr) ? static_cast<void>(0) :		\
	(Aegis::assertFailed(#expr, "", __FILE__, __LINE__, __FUNCTION__), AGX_DEBUG_BREAK()))
#define AGX_ASSERT_X(expr, msg)				\
	((expr) ? static_cast<void>(0) :		\
	(Aegis::assertFailed(#expr, msg, __FILE__, __LINE__, __FUNCTION__), AGX_DEBUG_BREAK()))
#define AGX_UNREACHABLE(msg)					\
	(Aegis::assertFailed("Unreachable code reached", msg, __FILE__, __LINE__, __FUNCTION__), AGX_DEBUG_BREAK())
#else
#define AGX_ASSERT(expr) static_cast<void>(0)
#define AGX_ASSERT_X(expr, msg) static_cast<void>(0)
#define AGX_UNREACHABLE(msg) static_cast<void>(0)
#endif // AGX_ASSERTS_ENABLED

namespace Aegis
{
	inline void assertFailed(const char* expr, const char* what, const char* file, int line, const char* function)
	{
		// TODO: Replace this with logging
		std::cout << std::format("Assertion failed: '{}'\n\tFile: {}, Line: {}, Function: {}\n\t{}\n",
			expr, file, line, function, what);
		assert(false && "Assertion failed!");

		//ALOG::fatal("Assertion failed: '{}'\n\tFile: {}, Line: {}, Function: {}\n\t{}",
		//	expr, file, line, function, what);
	}
}
