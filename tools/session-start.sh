#!/usr/bin/env bash
# Prepares the checkout a new Claude Code session starts in (the SessionStart hook in
# .claude/settings.json runs it): checks out missing submodules, then configures the preset if it
# has no build directory yet. It does not build; that is left to the session.
#
#   tools/session-start.sh
#
# Prints nothing when there is nothing to do. Its output goes into the session's context, so a
# failed configure is reported there with the end of CMake's output instead of failing the hook.
#
# Run as the hook, it prepares the session's folder, the "cwd" of the hook's JSON input, not the
# folder it runs in: the desktop app starts a session in a .claude/worktrees/ worktree with the main
# checkout as the project root, so the hook runs in the main checkout, with CLAUDE_PROJECT_DIR naming
# it. Run by hand, it prepares the checkout it runs in.
#
# Environment:
#   AEGIS_PRESET    configure preset; default windows-clang-debug on Windows, linux-clang-debug
#                   elsewhere.

set -euo pipefail

tools=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

# Only a hook has CLAUDE_PROJECT_DIR set and its JSON input on stdin. No jq on Git Bash, so sed
# reads the field and undoes JSON's backslash escaping (Windows paths).
if [[ -n "${CLAUDE_PROJECT_DIR:-}" && ! -t 0 ]]; then
    cwd=$(tr -d '\n' | sed -n 's/.*"cwd"[[:space:]]*:[[:space:]]*"\(\([^"\\]\|\\.\)*\)".*/\1/p' |
        sed 's/\\\\/\\/g')
    if [[ -n "$cwd" ]]; then
        if command -v cygpath >/dev/null; then
            cwd=$(cygpath -u "$cwd")
        fi
        cd "$cwd"
    fi
fi

"$tools/init-submodules.sh" --quiet

if [[ -z "${AEGIS_PRESET:-}" ]]; then
    case "$(uname -s)" in
        MINGW* | MSYS* | CYGWIN*) AEGIS_PRESET=windows-clang-debug ;;
        *) AEGIS_PRESET=linux-clang-debug ;;
    esac
fi

cd "$(git rev-parse --show-toplevel)"

# build.ninja is written only by a configure that succeeded, so a failed one is retried next time.
if [[ -e "build/$AEGIS_PRESET/build.ninja" ]]; then
    exit 0
fi

start=$(date +%s)
log=$(mktemp)
trap 'rm -f "$log"' EXIT
if cmake --preset "$AEGIS_PRESET" >"$log" 2>&1; then
    echo "Configured preset $AEGIS_PRESET in $(($(date +%s) - start)) s; not built yet."
else
    echo "Configuring preset $AEGIS_PRESET failed; run \`cmake --preset $AEGIS_PRESET\` again and"
    echo "fix it before building (see the Vulkan SDK notes in AGENTS.md). End of CMake's output:"
    tail -n 20 "$log"
fi
