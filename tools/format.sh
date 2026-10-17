#!/usr/bin/env bash
# Formats the engine's C++ sources with the root .clang-format (clang-format 22.x).
#
#   tools/format.sh            files changed on this branch: committed vs origin/main, staged,
#                              unstaged and untracked
#   tools/format.sh --all      every tracked source file
#   tools/format.sh --check    with either mode: report unformatted files, change nothing,
#                              exit 1 if any
#
# Covers src/, tests/ and examples/ (*.cppm, *.cpp, *.h, *.hpp); external/ and modules/ are
# third-party and never formatted. Set CLANG_FORMAT to pick a specific binary.

set -euo pipefail

all=0
check=0
for arg in "$@"; do
    case "$arg" in
        --all) all=1 ;;
        --check) check=1 ;;
        *)
            sed -n '2,11p' "$0" | sed 's/^# \{0,1\}//'
            exit 2
            ;;
    esac
done

clang_format=${CLANG_FORMAT:-clang-format}
version=$("$clang_format" --version | sed -n 's/.*version \([0-9]*\)\..*/\1/p')
if [[ "$version" != "22" ]]; then
    echo "error: $clang_format is version ${version:-unknown}; .clang-format needs 22.x" >&2
    exit 1
fi

cd "$(git rev-parse --show-toplevel)"
pathspec=(src tests examples)
# src/aegis/ai/ is unbuilt and slated for deletion; leave it alone.
is_source() { grep -E '^(src|tests|examples)/.*\.(cppm|cpp|h|hpp)$' | grep -v '^src/aegis/ai/' || true; }

if [[ $all -eq 1 ]]; then
    files=$(git ls-files -- "${pathspec[@]}" | is_source)
else
    base=$(git merge-base origin/main HEAD)
    files=$({
        git diff --name-only --diff-filter=d "$base" -- "${pathspec[@]}"
        git ls-files --others --exclude-standard -- "${pathspec[@]}"
    } | sort -u | is_source)
fi

if [[ -z "$files" ]]; then
    echo "No source files to format."
    exit 0
fi

if [[ $check -eq 1 ]]; then
    unformatted=()
    while IFS= read -r file; do
        if ! "$clang_format" --dry-run --Werror "$file" >/dev/null 2>&1; then
            unformatted+=("$file")
        fi
    done <<<"$files"
    if [[ ${#unformatted[@]} -gt 0 ]]; then
        printf 'Not formatted:\n'
        printf '  %s\n' "${unformatted[@]}"
        exit 1
    fi
    echo "All $(wc -l <<<"$files" | tr -d ' ') files are formatted."
else
    while IFS= read -r file; do
        "$clang_format" -i "$file"
    done <<<"$files"
    echo "Formatted $(wc -l <<<"$files" | tr -d ' ') files."
fi
