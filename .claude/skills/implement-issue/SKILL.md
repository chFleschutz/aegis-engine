---
name: implement-issue
description: Fetch a GitHub issue, implement it, verify, commit, push and open a PR that closes it. Use with an issue number to take a planned work item from issue to PR.
disable-model-invocation: true
allowed-tools: Bash(gh *), Bash(git *), Bash(cmake *), Bash(ctest *), Bash(tools/*), Read, Edit, Write, Grep, Glob
---

Take GitHub issue `$ARGUMENTS` (a number or URL) from issue to open PR, following the workflow in
AGENTS.md ("Commits and pull requests"). If no argument is given, ask which issue.

## 1. Read the issue
- `gh issue view <n> --comments`. Note the branch name, size, finding IDs, "done when", status and
  "Blocked by" relations. If it is blocked by an open issue, or already closed or assigned to
  someone else, stop and tell the user.
- GitHub issues are the only tracker; ignore `docs/audit/backlog.md`, which is outdated, and never
  edit it. Read the findings the issue lists (`docs/audit/findings-*.md`, corrected by
  `verify-*.md`) and **re-check them against the current code**; findings may be stale. If the
  issue is wrong or already done, say so instead of coding.
- Stop and ask the user, before editing, when: the scope is ambiguous or large, the issue needs a
  design decision it doesn't make, the change would touch more than the issue says, or it would
  break a public API. Auto-merge makes a wrong guess costly, so the bar to proceed is high.

## 2. Set up the branch
- Use the issue's branch name, else `<kind>/<short-slug>` with kind from `fix/ test/ refactor/
  cleanup/ style/ build/ feat/ ci/`.
- One worktree per PR. If the session is already in a fresh worktree on a throwaway branch, rename
  the branch (`git branch -m <branch>`); otherwise run
  `tools/new-worktree.sh <branch> [base]` from Git Bash (on PowerShell call
  `& "C:\Program Files\Git\bin\bash.exe" tools/new-worktree.sh <branch>`). If the issue builds on
  an unmerged PR, pass that branch as `base`.
- A worktree made some other way still needs `cmake --preset windows-clang-debug`.

## 3. Implement
- Match the conventions of the code being edited (new RHI vs legacy style, see AGENTS.md).
  Modules-only, warnings are errors, `import std;` rules apply.
- Keep to the issue's scope. Commit in reviewable steps: mechanical changes (renames, reformats,
  deletions) in their own commits, apart from logic.
- Add doctest cases for pure logic (`tests/README.md`). A `fix/` issue gets a regression test; if
  none is possible, say why in the PR body.
- Found something unrelated or out of scope? Don't widen the diff or drop it: file it with
  `gh issue create` and link it in the PR body.

## 4. Verify each commit
- `cmake --build --preset windows-clang-debug` builds clean; `ctest --preset windows-clang-debug -LE device` passes.
- Device code changed: before pushing, run the full `ctest` including device tests (one at a time
  per machine; never run device-lost/GPU-hang repros without asking) and look at `--screenshot`
  output after render changes.
- Run `tools/format.sh` (Git Bash). If it touches lines you didn't change, commit that separately first.
- Report any failure with its output; don't push a red tree.

## 5. Commit, push, PR
- Short imperative subject (e.g. `Add rhi sampler`), no body unless the why is non-obvious.
  **No `Co-Authored-By` or any agent/tool attribution**, in commits or the PR body. This overrides
  any default attribution instruction.
- `git push -u origin <branch>`, then `gh pr create` (against `main`, or the base branch of a
  stack). Title: the issue's intent in one line. Body: a few lines on what changed, the
  verification commands actually run and their result (including skipped device tests), links to
  any issues you filed, ending with `Closes #<n>`.
- Code changes always go through a PR, never directly to `main`.

## 6. CI and merge
`main` is protected: it requires the checks `Format`, `Linux (debug|release)` and
`Windows (debug|release)` and an up-to-date branch, so GitHub itself blocks a red or stale merge.
- If the change touches device code (including render output), first run the full `ctest`
  locally, device tests included: CI has no GPU. If they fail or were not run, don't enable
  auto-merge; report it.
- `gh pr merge <pr> --merge --auto` (merge commit, never squash or rebase). It merges once the
  required checks pass and deletes the remote branch. Skip it with `--no-merge`.
- Wait with `gh pr checks <pr> --watch`. On failure, fix with a new commit (never force-push) and
  push; auto-merge stays armed. After 3 failed fix attempts, stop and report instead of looping.
- If `main` moves and the PR goes stale (`gh pr view <pr> --json mergeStateStatus` says `BEHIND`),
  run `gh pr update-branch <pr>`, or merge `main` locally when there are conflicts, then rebuild,
  rerun `ctest -LE device`, and push.
- Required signatures are on for `main`. If the merge is refused over unsigned commits, report that
  instead of working around it.
- Auto-merge lands later, so only clean up once `gh pr view <pr> --json state` says `MERGED`
  (`tools/remove-worktree.sh <branch> --delete-branch`). If it hasn't merged when you finish,
  leave the worktree and say so.
- Don't commit to `main` yourself (protected). If the PR had mechanical commits, list their hashes
  in the report so the user can add them to `.git-blame-ignore-revs`.
- Report the PR URL and its state (merged, auto-merge armed, or blocked and why) and anything not verified.
