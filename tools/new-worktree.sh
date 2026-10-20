#!/usr/bin/env bash
# Creates a git worktree for one backlog item, with submodules and a configured, built preset.
#
#   tools/new-worktree.sh <branch> [base] [--no-build]
#
#   <branch>    new branch name, e.g. fix/test-case-semicolon
#   [base]      start point; default origin/main. For a stacked PR, pass the branch it builds on.
#   --no-build  stop after configure
#
# The worktree lands next to the main checkout in <main>.worktrees/<branch with / replaced by ->.
# Submodules check out in parallel through tools/init-submodules.sh, which shares the main
# checkout's submodule object stores (--reference), so nothing is downloaded or copied.
#
# Environment:
#   Vulkan_ROOT     SDK to build against (CMake searches it before $VULKAN_SDK). It must be the
#                   pinned 1.4.363.0 SDK (see AGENTS.md); set it once in your user environment.
#   AEGIS_PRESET    configure/build preset; default windows-clang-debug on Windows,
#                   linux-clang-debug elsewhere.

set -euo pipefail

build=1
args=()
for arg in "$@"; do
    case "$arg" in
        --no-build) build=0 ;;
        *) args+=("$arg") ;;
    esac
done
if [[ ${#args[@]} -lt 1 || ${#args[@]} -gt 2 ]]; then
    sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'
    exit 2
fi
branch=${args[0]}
base=${args[1]:-origin/main}
tools=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

# The main checkout is the parent of the common .git directory, also when run from a worktree.
main=$(dirname "$(git rev-parse --path-format=absolute --git-common-dir)")
dir="$main.worktrees/${branch//\//-}"

if [[ -e "$dir" ]]; then
    echo "error: $dir already exists" >&2
    exit 1
fi

if [[ "$base" == origin/* ]]; then
    git -C "$main" fetch --quiet origin
fi
git -C "$main" worktree add -b "$branch" "$dir" "$base"

# This script's own copy, so a base branch that predates init-submodules.sh works too.
(cd "$dir" && "$tools/init-submodules.sh")

if [[ -z "${AEGIS_PRESET:-}" ]]; then
    case "$(uname -s)" in
        MINGW* | MSYS* | CYGWIN*) AEGIS_PRESET=windows-clang-debug ;;
        *) AEGIS_PRESET=linux-clang-debug ;;
    esac
fi
if [[ -z "${Vulkan_ROOT:-}" ]]; then
    echo "warning: Vulkan_ROOT is not set; CMake falls back to \$VULKAN_SDK (${VULKAN_SDK:-unset})," >&2
    echo "         and configure fails if that is not the pinned 1.4.363.0 SDK." >&2
fi

cd "$dir"
cmake --preset "$AEGIS_PRESET"
if [[ $build -eq 1 ]]; then
    cmake --build --preset "$AEGIS_PRESET"
fi

echo
echo "Worktree ready: $dir (branch $branch from $base, preset $AEGIS_PRESET)"
