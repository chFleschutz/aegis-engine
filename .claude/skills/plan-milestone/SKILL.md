---
name: plan-milestone
description: Suggest the next targets, plan one as a milestone, or break a plan or an outline issue into full issues.
argument-hint: [target | issues <plan path, doc step or issue number>]
disable-model-invocation: true
allowed-tools: Bash(gh issue view *), Bash(gh issue list *), Bash(gh issue create *), Bash(gh issue edit *), Bash(gh label list*), Bash(gh api repos/*/milestones*), Bash(gh api repos/*/issues/*), Bash(gh api --method POST repos/*/issues/*/dependencies/blocked_by *), Read, Write, Grep, Glob
---

Plan milestones by the [milestone rules](#milestone-rules): any number open beside `Backlog`, with no
stages, worked in parallel where their areas don't overlap. Pick the branch by `$ARGUMENTS`:

| `$ARGUMENTS` | Branch |
| --- | --- |
| empty | [Suggest targets](#suggest-targets) |
| `issues <plan path, doc step or issue number>` | [Issues](#issues) |
| anything else, the target | [Plan the target](#plan-the-target) |

## Milestone rules

- Any number of milestones may be open beside `Backlog`, which never closes. A milestone has no stage
  or slot: it is open until its "done when" is met. Titles are `M<n>: <name>`, each `n` one past the
  highest ever used; the numbers are IDs, and the working order lives in the roadmap.
- **Areas decide what runs in parallel.** A milestone's areas are the `area/*` labels of its issues.
  Milestones whose areas don't overlap are worked at the same time. A milestone may be planned on
  areas an open milestone still works; its items then wait for those areas (AGENTS.md: never two
  items with the same `area/*` label in parallel), and the roadmap puts it after that milestone.
- A milestone's description gives its target, a checkable "done when" (a result, not its list of
  issues) and what is out of scope. Its issues have full bodies; a milestone that waits on another
  one's areas may keep outline bodies, each written out against the code
  ([Issues](#issues) with the outline's number) before work on it starts.
- Once a milestone's first item has started, its scope is frozen: an issue joins it only if its
  "done when" can't be met without it (a bug the milestone's own work caused, a missing
  prerequisite). Anything else, a related follow-up or a new feature included, goes to another open
  milestone whose target it serves, or to `Backlog`. `Backlog` bugs may be worked at any time without
  joining a milestone.
- Once "done when" is met the milestone closes, even with issues left, which move to another open
  milestone or `Backlog`, and the roadmap drops it (a docs-only commit).
- The roadmap, `docs/roadmap.md`, holds the open milestones and the targets not yet planned, in
  working order: each one's goal and what it waits for. Feature PRs don't edit it. The decisions are
  ADRs in `docs/adr/`.

## 1. Gather context

Every branch starts here. Read the open milestones with their descriptions
(`gh api repos/{owner}/{repo}/milestones --jq '.[] | "\(.number) \(.title)\n\(.description)"'`), their
issues and the `Backlog` (`gh issue list --milestone "<title>" --json number,title,labels`), and the
roadmap (`docs/roadmap.md`). For a target or a plan, also read what it leans on:
the design docs and ADRs it cites, `GLOSSARY.md` for names, and enough of the code to know what
exists. Branches that draft issues read [`ISSUE-FORMAT.md`](ISSUE-FORMAT.md).

Done when you can say, for each piece of the subject, what exists today and what changes.

## Suggest targets

Collect candidate targets from the roadmap's unplanned targets, clusters of `Backlog` issues that
share an area or a chain of blockers, open `needs-design` issues, and what the open milestones unblock
once they close. Propose two to four. For each: the target in one line, the result its "done when"
would check, its areas and the open milestones that share them, why now (what it unblocks or fixes),
the `Backlog` issues it would take in, and a size in issues. Recommend one, preferring areas no open
milestone holds.

Done when every unplanned roadmap target and every `Backlog` cluster of three or more issues appears,
as a proposal or in one "passed over" line with its reason. When the owner picks one, continue with
[Plan the target](#plan-the-target).

## Plan the target

1. **Check the areas.** Name the areas its issues will carry and the open milestones that share them.
   With none, it can start once created. With some, it waits for them: ask the owner whether to create
   it now (its issues may stay outlines) or keep it as a roadmap paragraph for later; for the
   paragraph, draft step 2's description only and, after the owner's OK, add it to the roadmap at the
   place they choose, as a docs-only commit on `main`.
2. **Draft the milestone.** Title `M<n>: <name>`, numbered by the [milestone rules](#milestone-rules). Description:

   ```md
   **Target:** <the outcome, in a sentence>

   **Done when:**
   - <a result someone can check: a scene that renders, an editor flow, a test or reference image>

   **Out of scope:** <the neighbouring work this leaves out>
   ```

   "Done when" describes the result, never the list of issues.
3. **Draft the issues** (see [Drafting issues](#drafting-issues)): full bodies, or outline bodies
   when the milestone waits on another one's areas. A `Backlog` issue that is part of the target moves
   in rather than being duplicated. Aim for five to ten. A target that needs more is two targets: plan
   the first, and offer the second as a roadmap paragraph.
4. **Show and wait.** Present the milestone draft and a numbered list: for each issue its **title**,
   **blocked by**, what it delivers, size and labels, and whether it is new or moved from `Backlog`. Ask
   whether "done when" is the right bar, whether the granularity is right and whether each blocker
   really gates its issue. Change the drafts until the owner approves them. Create nothing before that OK.
5. **Create** the milestone, then the issues and their relations ([commands](ISSUE-FORMAT.md#commands)),
   and add the milestone to the roadmap in its place (a docs-only commit on `main`).

## Issues

Break the plan path, doc step or issue number after `issues` into full issues, without a new milestone.
Draft them ([Drafting issues](#drafting-issues), full bodies), then show and wait as in
[Plan the target](#plan-the-target) step 4, and create them. Each takes the open milestone whose
target it serves when the plan is that milestone's work (if that milestone has started, only when its
"done when" can't be met without the issue; say why when you show it), and `Backlog` otherwise. Leave
the source issue as it is.

An **outline issue** is the exception: re-check its milestone's target and "done when" against the
current code and what has merged since it was planned, draft any edits to them, and rewrite the
outline in place with its full body (`gh issue edit`), splitting, merging or dropping it where the code
calls for it and fixing its blockers. Done when it has a full body.

## Drafting issues

Each issue is one PR, verifiable on its own and small enough for one session. Write each body to a file
in the scratchpad.

- **Prefactoring first:** make the change easy, then make the easy change.
- **Blockers** only where one issue genuinely gates another; an issue with none can start at once.
- **Existing issues:** a piece an open issue already covers stays in that issue, which you list in the
  breakdown and leave unedited unless this branch moves it into the milestone. It can still block a new one.
- **Open decisions:** one only the owner can make stays open: name it in the body and label the issue
  `needs-design`. Where the plan is merely unclear, pick a reading and put it to the owner when you show
  the breakdown.
- **A wide mechanical change** can't land green as one slice of the work, so sequence it: add the new
  form beside the old, migrate the callers in batches (per module or folder), each its own issue blocked
  by the add, then delete the old form in an issue blocked by every batch.
