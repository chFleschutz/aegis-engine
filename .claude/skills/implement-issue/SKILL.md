---
name: implement-issue
description: Fetch a GitHub issue, implement it, verify, commit, push and open a PR that closes it. Use with an issue number to take a planned work item from issue to PR.
disable-model-invocation: true
allowed-tools: Bash(gh *), Bash(git *), Bash(cmake *), Bash(ctest *), Bash(tools/*), Read, Edit, Write, Grep, Glob
---

Take GitHub issue `$ARGUMENTS` (a number or URL) from issue to open PR, following the workflow in
AGENTS.md ("Commits and pull requests"). If no argument is given, ask which issue.

## 1. Read the issue
- `gh issue view <n> --comments`. Note the branch name, size, labels, finding IDs, content, out of
  scope, "done when", verification and the "Blocked by" relations
  (`gh api repos/{owner}/{repo}/issues/<n>/dependencies/blocked_by --jq '.[] | "\(.number) \(.state)"'`).
  Stop and tell the user if it is closed, blocked by an open issue (unless its blocker's branch can
  be the stack base), assigned to someone else, or already has an open PR.
- **Claim it** before any other work: `gh issue edit <n> --add-assignee @me`. The assignee is the
  status; parallel agents skip assigned issues.
- GitHub issues are the only tracker for status and scope. `docs/audit/backlog.md` holds the
  decisions (D1–D8, F1/F2): read the ones the issue cites, never edit the file. Read the findings
  the issue lists (`docs/audit/findings-*.md`, corrected by `verify-*.md`) and **re-check them
  against the current code**; findings may be stale. If the issue is wrong or already done, say so
  instead of coding.
- Stop and ask the user, before editing, when: the issue has the `needs-design` label, the scope
  is ambiguous or large, the issue needs a design decision it doesn't make, the change would touch
  more than the issue says (its **Out of scope** is a hard line), or it would break a public API.
- `needs-gpu` means the verification needs the local device tests. Without a GPU (cloud session),
  say in the PR body that they were not run. Follow any hazard the issue states (e.g.
  #154: never run a repro that hangs the GPU without asking).

## 2. Set up the branch
- Use the issue's branch name, else `<kind>/<short-slug>` with kind from `fix/ test/ refactor/
  cleanup/ style/ build/ feat/ ci/`.
- One worktree per PR. If the session is already in a fresh worktree on a throwaway branch
  (`claude/…`), rename the branch (`git branch -m <branch>`) before the first commit; otherwise run
  `tools/new-worktree.sh <branch> [base]` from Git Bash (on PowerShell call
  `& "C:\Program Files\Git\bin\bash.exe" tools/new-worktree.sh <branch>`). If the issue builds on
  an unmerged PR, pass that branch as `base`.
- The `SessionStart` hook configures a worktree made some other way; if its output says the
  configure failed, run `cmake --preset windows-clang-debug` again before building.

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
  stack). Title: the issue's intent in one line. Body, written for the owner's review: what changed
  and why, the verification commands actually run and their result (including skipped device
  tests), anything left out or uncertain, links to any issues you filed, ending with `Closes #<n>`.
- Code changes always go through a PR, never directly to `main`.

## 6. CI, then hand over for review
The owner reviews and merges every PR. **Never merge it or enable auto-merge** (`gh pr merge`).
`main` is protected: it requires the checks `Format`, `Linux (debug|release)` and
`Windows (debug|release)` and an up-to-date branch.
- If the change touches device code (including render output), first run the full `ctest`
  locally, device tests included: CI has no GPU. If they fail or were not run, say so in the PR
  body and the report.
- Wait with `gh pr checks <pr> --watch`. On failure, fix with a new commit (never force-push) and
  push. After 3 failed fix attempts, stop and report instead of looping.
- If `main` moves and the PR goes stale (`gh pr view <pr> --json mergeStateStatus` says `BEHIND`),
  run `gh pr update-branch <pr>`, or merge `main` locally when there are conflicts, then rebuild,
  rerun `ctest -LE device`, and push.
- Leave the worktree in place: the owner may ask for changes. It is removed after the merge
  (`tools/remove-worktree.sh <branch> --delete-branch`).
- Don't commit to `main` yourself (protected; only the owner pushes docs there). If the PR has
  mechanical commits, list their hashes in the report so the user can add them to
  `.git-blame-ignore-revs` after the merge.
- Check the issues this one blocks
  (`gh api repos/{owner}/{repo}/issues/<n>/dependencies/blocking --jq '.[] | "\(.number) \(.title)"'`):
  if the PR changes a file, API or plan that one of them cites, say which and what changed, so the
  user can correct that issue before someone starts it. Name the ones the merge will unblock.
- Report the PR URL, that it is ready for review (or blocked and why), and anything not verified.
