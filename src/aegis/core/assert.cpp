#include "core/assert.h"

#include <cassert>

import std;

namespace aegis
{
void assertFailed(const char* expr, const char* what, const char* file, int line, const char* function)
{
    // TODO: Replace this with logging
    std::cout << std::format("Assertion failed: '{}'\n\tFile: {}, Line: {}, Function: {}\n\t{}\n",
        expr, file, line, function, what);
    assert(false && "Assertion failed!");

    //ALOG::fatal("Assertion failed: '{}'\n\tFile: {}, Line: {}, Function: {}\n\t{}",
    //    expr, file, line, function, what);
}
}
