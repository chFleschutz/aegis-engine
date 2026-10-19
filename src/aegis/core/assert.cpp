#include "core/assert.h"

import std;

namespace aegis
{
void assertFailed(const char* expr, const char* what, const char* file, int line, const char* function)
{
    // Unbuffered stderr, so the message survives the trap even when output is piped or redirected.
    // The macro traps at the call site afterwards (AGX_DEBUG_BREAK).
    std::cerr << std::format("Assertion failed: '{}'\n\tFile: {}, Line: {}, Function: {}\n\t{}\n",
                     expr, file, line, function, what)
              << std::flush;
}
}
