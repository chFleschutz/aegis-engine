#!/usr/bin/env bash
# Removes a worktree created by tools/new-worktree.sh, after checking that nothing is lost.
#
#   tools/remove-worktree.sh <branch> [--delete-branch]
#
# Refuses if the worktree has uncommitted changes or commits that are not on its upstream.
# Build output (ignored) is deleted with it. --delete-branch also deletes the local branch, which
# git only allows once it is merged.

set -euo pipefail

delete_branch=0
args=()
for arg in "$@"; do
    case "$arg" in
        --delete-branch) delete_branch=1 ;;
        *) args+=("$arg") ;;
    esac
done
if [[ ${#args[@]} -ne 1 ]]; then
    sed -n '2,8p' "$0" | sed 's/^# \{0,1\}//'
    exit 2
fi
branch=${args[0]}

main=$(dirname "$(git rev-parse --path-format=absolute --git-common-dir)")
dir="$main.worktrees/${branch//\//-}"

if [[ ! -d "$dir" ]]; then
    echo "error: no worktree at $dir" >&2
    exit 1
fi
if [[ -n "$(git -C "$dir" status --porcelain)" ]]; then
    echo "error: $dir has uncommitted changes:" >&2
    git -C "$dir" status --short >&2
    exit 1
fi
if upstream=$(git -C "$dir" rev-parse --abbrev-ref --symbolic-full-name '@{upstream}' 2>/dev/null); then
    if [[ -n "$(git -C "$dir" log --oneline "$upstream..HEAD")" ]]; then
        echo "error: $branch has commits that are not pushed to $upstream" >&2
        exit 1
    fi
else
    echo "error: $branch has no upstream; push it first or remove the worktree by hand" >&2
    exit 1
fi

# --force is needed because the worktree contains submodules; the checks above stand in for it.
git -C "$main" worktree remove --force "$dir"
if [[ $delete_branch -eq 1 ]]; then
    git -C "$main" branch -d "$branch"
fi
echo "Removed $dir"
