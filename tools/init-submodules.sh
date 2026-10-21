#!/usr/bin/env bash
# Checks out the submodules a worktree is missing, reusing the main checkout's copies.
#
#   tools/init-submodules.sh [--quiet]
#
#   --quiet   print nothing when every submodule is already checked out
#
# Run from anywhere inside a worktree. Each missing submodule is checked out with --reference to the
# main checkout's copy of the same submodule, so its objects stay in the main checkout (git
# alternates): nothing is downloaded or copied, and the worktree's git data stays around 1 MB. The
# submodules check out in parallel. Submodules already checked out are left alone, so a second run
# does nothing. In the main checkout itself, or for a submodule the main checkout lacks, it falls
# back to a normal clone.
#
# The worktree's submodules depend on the main checkout's submodule object stores. To make one
# self-contained: `git -C <path> repack -a -d`, then delete objects/info/alternates in its git dir.
#
# The SessionStart hook in .claude/settings.json runs this with --quiet (through
# tools/session-start.sh) for every new Claude Code session, which covers worktrees that
# tools/new-worktree.sh did not create.

set -euo pipefail

quiet=0
for arg in "$@"; do
    case "$arg" in
        --quiet) quiet=1 ;;
        *)
            sed -n '2,6p' "$0" | sed 's/^# \{0,1\}//'
            exit 2
            ;;
    esac
done

top=$(git rev-parse --show-toplevel)
main=$(dirname "$(git rev-parse --path-format=absolute --git-common-dir)")
cd "$top"

# A checked-out submodule has a .git file pointing at a git dir with a HEAD (an interrupted clone
# leaves the .git file without one). This check starts no process, so it is much faster than
# `git submodule status`, which matters for the hook that runs on every session start.
checked_out() {
    local dir=$1/.git
    if [[ -f "$dir" ]]; then
        local gitdir
        read -r _ gitdir <"$dir" || return 1
        [[ "$gitdir" == /* || "$gitdir" == [A-Za-z]:* ]] || gitdir=$1/$gitdir
        dir=$gitdir
    fi
    [[ -e "$dir/HEAD" ]]
}

missing=()
while read -r _ path; do
    if ! checked_out "$path"; then
        missing+=("$path")
    fi
done < <(git config --file .gitmodules --get-regexp '^submodule\..*\.path$')

if [[ ${#missing[@]} -eq 0 ]]; then
    if [[ $quiet -eq 0 ]]; then
        echo "All submodules are checked out."
    fi
    exit 0
fi

start=$(date +%s)
logs=$(mktemp -d)
trap 'rm -rf "$logs"' EXIT

# Register the URLs in one process, so the parallel updates below only clone and check out.
git submodule init --quiet -- "${missing[@]}"

# Returns its failures explicitly: the retry below calls it under `if !`, where set -e is off.
checkout() {
    local path=$1
    local reference=()
    if [[ "$top" != "$main" ]] && checked_out "$main/$path"; then
        local gitdir
        gitdir=$(git -C "$main/$path" rev-parse --path-format=absolute --git-dir) || return
        reference=(--reference "$gitdir")
    fi
    # Some submodule files pass Windows' 260-character path limit under a deep worktree path.
    git -c core.longpaths=true submodule update --quiet "${reference[@]}" -- "$path" || return
    checked_out "$path" || return
    git -C "$path" config core.longpaths true
}

pids=()
for path in "${missing[@]}"; do
    checkout "$path" >"$logs/${path//\//_}.log" 2>&1 &
    pids+=($!)
done
failed=()
for i in "${!pids[@]}"; do
    if ! wait "${pids[$i]}"; then
        failed+=("${missing[$i]}")
    fi
done

# A parallel update can lose a race for one of git's lock files; retry those one at a time.
errors=0
for path in "${failed[@]}"; do
    if ! checkout "$path" >"$logs/retry.log" 2>&1; then
        echo "error: could not check out submodule $path:" >&2
        cat "$logs/${path//\//_}.log" "$logs/retry.log" >&2
        errors=1
    fi
done
if [[ $errors -ne 0 ]]; then
    exit 1
fi

echo "Checked out ${#missing[@]} submodules in $(($(date +%s) - start)) s."
