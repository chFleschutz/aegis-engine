#!/usr/bin/env bash
# Fails when a doctest case name contains ";". CTest discovery splits the name there, and both
# halves then run zero cases while showing green, so the case silently stops running.
#
#   tools/check-test-names.sh
#
# Checks TEST_CASE and its variants (TEST_CASE_TEMPLATE, ...) in every tracked C++ file and lists
# each offender as file:line. The Format job in .github/workflows/build.yml runs it.

set -euo pipefail

cd "$(git rev-parse --show-toplevel)"

offenders=$(git grep -nE '(^|[^A-Za-z0-9_])TEST_CASE[A-Z_]*[[:space:]]*\([[:space:]]*"[^"]*;' -- \
    '*.cpp' '*.cppm' '*.h' '*.hpp' || true)

if [[ -n "$offenders" ]]; then
    echo "error: a TEST_CASE name contains \";\", which CTest discovery splits into two empty tests." >&2
    echo "Reword the name without \";\":" >&2
    sed 's/^/  /' <<<"$offenders" >&2
    exit 1
fi
echo "No TEST_CASE name contains \";\"."
