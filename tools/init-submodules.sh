#!/usr/bin/env bash
# Checks out the submodules a worktree is missing, reusing the main checkout's copies, and moves
# the ones left behind their recorded commit.
#
#   tools/init-submodules.sh [--quiet]
#
#   --quiet   print nothing when every submodule is checked out at its recorded commit
#
# Run from anywhere inside a worktree. Each missing submodule is checked out with --reference to the
# main checkout's copy of the same submodule, so its objects stay in the main checkout (git
# alternates): nothing is downloaded or copied, and the worktree's git data stays around 1 MB. The
# submodules check out in parallel. In the main checkout itself, for a submodule the main checkout lacks, and for one
# whose copy there is shallow (git takes no shallow repository as a reference), it falls back to a
# normal clone. No submodule sets `shallow = true` in .gitmodules: the samples' full history is
# barely larger than the pinned commit (#327), and zstd's pinned release isn't the tip of its
# default branch, so a depth-1 clone fetches the release's whole history anyway (41 MB, against
# 43 MB for a full clone; #370). An older main checkout still has shallow copies; run once there,
# so worktrees share them: `git -C games/examples/project fetch --unshallow` if it was cloned before #327, and
# `git -C external/zstd fetch --unshallow` if before #370.
#
# A submodule already checked out stays as it is when it sits at the commit the superproject records.
# Off it (a reused worktree whose branch moved on, as the samples did for #464), it moves there when
# its HEAD is detached and it has no local changes, fetching the commit into the main checkout's copy
# if no store has it; on a branch or with changes it is left alone with a warning, as that is someone's
# work (a samples change on its way to a PR). A second run does nothing.
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

# A submodule's git dir: its .git, or the folder its .git file points at.
git_dir() {
    local dir=$1/.git
    if [[ -f "$dir" ]]; then
        local gitdir
        read -r _ gitdir <"$dir" || return 1
        [[ "$gitdir" == /* || "$gitdir" == [A-Za-z]:* ]] || gitdir=$1/$gitdir
        dir=$gitdir
    fi
    echo "$dir"
}

# A checked-out submodule has a .git file pointing at a git dir with a HEAD (an interrupted clone
# leaves the .git file without one). These checks start no process, so they are much faster than
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

paths=()
while read -r _ path; do
    paths+=("$path")
done < <(git config --file .gitmodules --get-regexp '^submodule\..*\.path$')

# The commit the superproject records for each submodule, in one process.
declare -A recorded
while read -r _ type commit path; do
    if [[ "$type" == commit ]]; then
        recorded[$path]=$commit
    fi
done < <(git ls-tree HEAD -- "${paths[@]}")

missing=()
behind=() # Checked out, detached, off the recorded commit
for path in "${paths[@]}"; do
    if ! checked_out "$path"; then
        missing+=("$path")
        continue
    fi
    want=${recorded[$path]:-}
    read -r head <"$(git_dir "$path")/HEAD"
    if [[ -z "$want" || "$head" == "$want" ]]; then
        continue
    fi
    if [[ "$head" == ref:* ]]; then
        echo "warning: submodule $path is on ${head#ref: refs/heads/}, not at the recorded ${want:0:7}; left alone" >&2
    else
        behind+=("$path")
    fi
done

# Moves a detached submodule without local changes to its recorded commit. Never fails: a submodule
# it can't move is reported and left as it is.
move_to_recorded() {
    local path=$1 want=${recorded[$1]}
    local from
    from=$(git -C "$path" rev-parse --short HEAD)
    if [[ -n $(git -C "$path" status --porcelain --untracked-files=no) ]]; then
        echo "warning: submodule $path has local changes at $from, not the recorded ${want:0:7}; left alone" >&2
        return 0
    fi
    if ! git -C "$path" cat-file -e "$want^{commit}" 2>/dev/null; then
        # Into the main checkout's copy when there is one: the worktrees' copies borrow its objects.
        local store=$path
        if [[ "$top" != "$main" ]] && checked_out "$main/$path"; then
            store=$main/$path
        fi
        if ! git -C "$store" fetch --quiet origin; then
            echo "warning: could not fetch submodule $path; it stays at $from, not the recorded ${want:0:7}" >&2
            return 0
        fi
    fi
    if ! git -c core.longpaths=true -C "$path" checkout --quiet --detach "$want"; then
        echo "warning: could not check out the recorded ${want:0:7} in submodule $path; it stays at $from" >&2
        return 0
    fi
    echo "Moved submodule $path from $from to the recorded ${want:0:7}."
}

for path in "${behind[@]}"; do
    move_to_recorded "$path"
done

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
    if [[ "$top" != "$main" ]] && checked_out "$main/$path" &&
        [[ $(git -C "$main/$path" rev-parse --is-shallow-repository) == false ]]; then
        local gitdir
        gitdir=$(git -C "$main/$path" rev-parse --path-format=absolute --git-dir) || return
        reference=(--reference "$gitdir")
    fi
    # Some submodule files pass Windows' 260-character path limit under a deep worktree path.
    # --checkout also checks out the opt-in samples, which `update = none` in .gitmodules skips otherwise.
    git -c core.longpaths=true submodule update --quiet --checkout "${reference[@]}" -- "$path" || return
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
