// Compiles the doctest framework implementation and provides main() exactly once.
// Every test executable links this via the shared aegis-test-main static library, so the
// framework impl is never recompiled per test binary. This TU contains no test cases.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
