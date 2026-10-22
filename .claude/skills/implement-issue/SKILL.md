---
name: implement-issue
description: Fetch a GitHub issue, implement it, verify, commit, push and open a PR that closes it. Use with an issue number to take a planned work item from issue to PR.
disable-model-invocation: true
allowed-tools: Bash(gh *), Bash(git *), Bash(cmake *), Bash(ctest *), Bash(tools/*), Read, Edit, Write, Grep, Glob
---

Take GitHub issue `$ARGUMENTS` (a number or URL) from issue to open PR, following the workflow in
AGENTS.md ("Commits and pull requests"). If no argument is given, ask which issue.

## 1. Read the issue
- Read it as JSON: `gh issue view <n> --json title,state,assignees,labels,milestone,body,comments`
  (add `--jq '.body'` or `--jq '[.comments[].body]'` for the long fields). Plain
  `gh issue view <n> --comments` can print nothing in Git Bash, with no error. Note the branch name,
  size, labels, finding IDs, content, out of scope, "done when", verification and the "Blocked by" relations
  (`gh api repos/{owner}/{repo}/issues/<n>/dependencies/blocked_by --jq '.[] | "\(.number) \(.state)"'`).
  Stop and tell the user if it is closed, blocked by an open issue (unless its blocker's branch can
  be the stack base), assigned to someone else, or already has an open PR
  (`gh pr list --state open --search "<n>" --json number,title,headRefName`, and look for its branch name).
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
- **Design checkpoint (size M and larger, or any issue that leaves a "how" open).** Before editing,
  list the decisions the issue doesn't make: file layout, where data or files come from, new
  defines or options, precedence rules, anything copied or generated at build time. If a decision
  is cheap to reverse and the issue or a cited D-n implies it, say it in one line and go on. Otherwise
  ask the user before coding, with your recommendation. Either way, name every one in the PR body
  under "Decisions to review".
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
- The `SessionStart` hook sets up a worktree made some other way, but don't assume it ran. Before
  the first build, check `git submodule status`: a line starting with `-` (e.g. `external/glaze`)
  means the submodules are missing, so run `bash tools/init-submodules.sh`. Then configure if
  `build/windows-clang-debug/` doesn't exist or the hook's configure failed
  (`cmake --preset windows-clang-debug`; a failed `--fresh` can need a second run).
- Build the untouched tree once before the first edit (`cmake --build --preset windows-clang-debug`),
  and wait for it to finish: a build running while you edit fails on your half-made changes and
  says nothing about the starting state.

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
- **Every commit on its own**, not just the final tree:
  `cmake --build --preset windows-clang-debug` builds clean and `ctest --preset windows-clang-debug -LE device`
  passes. Commit as you go and build after each one. If several commits were made together, check
  each before pushing (`git rebase --exec "cmake --build --preset windows-clang-debug && ctest --preset windows-clang-debug -LE device" origin/main`
  runs both per commit and stops at the first failure; only before the first push, never on a PR
  under review).
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
- Wait with `gh pr checks <pr> --watch`, started about 20 s after the push (`sleep 20; gh pr checks <pr> --watch`):
  right after a push the new runs may not be registered yet, and the watch then reports the old
  commit's result or no checks. Confirm the result is for the pushed commit
  (`gh pr view <pr> --json headRefOid`). On failure, fix with a new commit (never force-push) and
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
