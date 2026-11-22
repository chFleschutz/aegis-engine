#!/usr/bin/env bash
# Formats the engine's C++ sources with the root .clang-format (clang-format 22.x) and its CMake
# files with the root .gersemirc (gersemi 0.29.x).
#
#   tools/format.sh            files changed on this branch: committed vs origin/main, staged,
#                              unstaged and untracked
#   tools/format.sh --all      every tracked source file
#   tools/format.sh --check    with either mode: report unformatted files, change nothing,
#                              exit 1 if any
#
# C++: aegis/, tests/ and games/ (*.cppm, *.cpp, *.h, *.hpp). CMake: every CMakeLists.txt and *.cmake
# in the tree. The submodules under external/ are third-party and never formatted. Each tool is only
# needed when there are files of its kind; set CLANG_FORMAT or GERSEMI to pick a specific binary.

set -euo pipefail

all=0
check=0
for arg in "$@"; do
    case "$arg" in
        --all) all=1 ;;
        --check) check=1 ;;
        *)
            sed -n '2,13p' "$0" | sed 's/^# \{0,1\}//'
            exit 2
            ;;
    esac
done

clang_format=${CLANG_FORMAT:-clang-format}
gersemi=${GERSEMI:-gersemi}

cd "$(git rev-parse --show-toplevel)"
is_source() { grep -E '^(aegis|tests|games)/.*\.(cppm|cpp|h|hpp)$' || true; }
is_cmake() { grep -E '(^|/)CMakeLists\.txt$|\.cmake$' || true; }

if [[ $all -eq 1 ]]; then
    candidates=$(git ls-files)
else
    base=$(git merge-base origin/main HEAD)
    candidates=$({
        git diff --name-only --diff-filter=d "$base"
        git ls-files --others --exclude-standard
    } | sort -u)
fi
sources=$(is_source <<<"$candidates")
cmake_files=$(is_cmake <<<"$candidates")

if [[ -z "$sources" && -z "$cmake_files" ]]; then
    echo "No files to format."
    exit 0
fi

count() { [[ -z "$1" ]] && echo 0 || wc -l <<<"$1" | tr -d ' '; }
unformatted=()

if [[ -n "$sources" ]]; then
    version=$("$clang_format" --version | sed -n 's/.*version \([0-9]*\)\..*/\1/p')
    if [[ "$version" != "22" ]]; then
        echo "error: $clang_format is version ${version:-unknown}; .clang-format needs 22.x" >&2
        exit 1
    fi
    while IFS= read -r file; do
        if [[ $check -eq 1 ]]; then
            "$clang_format" --dry-run --Werror "$file" >/dev/null 2>&1 || unformatted+=("$file")
        else
            "$clang_format" -i "$file"
        fi
    done <<<"$sources"
fi

if [[ -n "$cmake_files" ]]; then
    version=$("$gersemi" --version | sed -n '1s/^gersemi \([0-9]*\.[0-9]*\)\..*/\1/p')
    if [[ "$version" != "0.29" ]]; then
        echo "error: $gersemi is version ${version:-unknown}; .gersemirc needs 0.29.x" >&2
        exit 1
    fi
    mapfile -t cmake_list <<<"$cmake_files"
    if [[ $check -eq 1 ]]; then
        # gersemi names each file it would change by its absolute path ("<path> would be
        # reformatted", with backslashes on Windows); report them relative to the root as above.
        # Exit code 1 with no such line (a missing definitions path) or any other code (a parse
        # error) is a failure of its own, not an unformatted file.
        root=$(pwd -W 2>/dev/null || pwd)
        status=0
        output=$("$gersemi" --check -- "${cmake_list[@]}" 2>&1) || status=$?
        changed=$(sed -n 's/ would be reformatted$//p' <<<"$output" | sed 's|\\|/|g' | sed "s|^$root/||")
        if [[ $status -ne 0 && ($status -ne 1 || -z "$changed") ]]; then
            printf '%s\n' "$output" >&2
            echo "error: $gersemi failed (exit code $status)" >&2
            exit 1
        fi
        if [[ -n "$changed" ]]; then
            mapfile -t -O "${#unformatted[@]}" unformatted <<<"$changed"
        fi
    else
        "$gersemi" --in-place --quiet -- "${cmake_list[@]}"
    fi
fi

total=$(($(count "$sources") + $(count "$cmake_files")))
if [[ $check -eq 1 ]]; then
    if [[ ${#unformatted[@]} -gt 0 ]]; then
        printf 'Not formatted:\n'
        printf '  %s\n' "${unformatted[@]}"
        exit 1
    fi
    echo "All $total files are formatted."
else
    echo "Formatted $total files."
fi
