---
name: plan-milestone
description: Suggest the next targets, plan one as a lane's Planning milestone, activate it, or break a plan into issues.
argument-hint: [target | activate | issues <plan path, doc step or issue number>]
disable-model-invocation: true
allowed-tools: Bash(gh issue view *), Bash(gh issue list *), Bash(gh issue create *), Bash(gh label list*), Bash(gh api repos/*/milestones*), Bash(gh api repos/*/issues/*), Bash(gh api --method POST repos/*/issues/*/dependencies/blocked_by *), Read, Write, Grep, Glob
---

Plan milestones by the [milestone rules](#milestone-rules): two **lanes** of different areas, each
with one **Active** and one **Planning** milestone beside `Backlog`, and each Active one's scope
frozen. Pick the branch by `$ARGUMENTS`:

| `$ARGUMENTS` | Branch |
| --- | --- |
| empty | [Suggest targets](#suggest-targets) |
| `activate [lane]` | [Activate](#activate) |
| `issues <plan path, doc step or issue number>` | [Issues](#issues) |
| anything else, the target | [Plan the target](#plan-the-target) |

## Milestone rules

- Milestones run in two **lanes**, each owning a set of `area/*` labels, so that their items can run
  at the same time:

  | Lane | Areas |
  | --- | --- |
  | `render` | `render_graph`, `render_passes`, `renderer`, `rhi` |
  | `content` | `assets`, `world`, `app`, `editor`, `platform`, `math` |

  A new `area/*` label joins one lane here. A milestone's issues sit in its lane's areas; one may also
  carry an area of the other lane (`render_passes` + `world`), and then waits while the other lane
  runs an item with that area (AGENTS.md: never two items with the same `area/*` label in parallel).
- Each lane has at most one **Active** milestone, being worked on, and one **Planning** milestone,
  its next. `Backlog` never closes and belongs to no lane. Titles are `M<n>: <name>`, each `n` one past
  the highest ever used across both lanes. The description's first line names the lane
  (`**Lane:** render`); of a lane's two milestones the lower number is Active. A lane with no Active
  milestone works its areas' `Backlog` issues.
- A milestone's description gives its lane, its target, a checkable "done when" (a result, not its
  list of issues) and what is out of scope. Planning issues may be outlines.
- Activating a milestone re-checks its target against the code and writes the full issue bodies; from
  then on its scope is frozen. An issue joins an Active milestone only if its "done when" can't be met
  without it: a bug the milestone's own work caused, a missing prerequisite. Anything else, a related
  follow-up or a new feature included, goes to its lane's Planning milestone or `Backlog`. `Backlog`
  bugs may be worked beside the Active milestones without joining them.
- Once "done when" is met the milestone closes, even with issues left, which move to its lane's
  Planning milestone or `Backlog`; that lane's Planning becomes Active, the roadmap's next target for
  the lane becomes Planning, and the roadmap drops the closed milestone (a docs-only commit). The
  other lane is untouched.
- The roadmap, `docs/roadmap.md`, holds the targets after Planning, in order per lane: each
  milestone's goal and order. Feature PRs don't edit it. The decisions are ADRs in `docs/adr/`.

## 1. Gather context

Every branch starts here. Read the open milestones with their descriptions
(`gh api repos/{owner}/{repo}/milestones --jq '.[] | "\(.number) \(.title)\n\(.description)"'`), their
issues and the `Backlog` (`gh issue list --milestone "<title>" --json number,title,labels`), and the
roadmap (`docs/roadmap.md`). For a target or a plan, also read what it leans on:
the design docs and ADRs it cites, `GLOSSARY.md` for names, and enough of the code to know what
exists. Branches that draft issues read [`ISSUE-FORMAT.md`](ISSUE-FORMAT.md).

Done when you can say, for each piece of the subject, what exists today and what changes.

## Suggest targets

Suggest for a lane with a free Planning slot; with both free, for each. Collect candidate targets from
the roadmap's next targets for that lane, clusters of `Backlog` issues in its areas that share an area
or a chain of blockers, open `needs-design` issues, and what the lane's Active milestone unblocks once
it closes. Propose two to four per lane. For each: the target in one line, the result its "done when"
would check, why now (what it unblocks or fixes), the `Backlog` issues it would take in, and a size in
issues. Recommend one per lane.

Done when every roadmap target of the lane and every `Backlog` cluster of three or more issues in its
areas appears, as a proposal or in one "passed over" line with its reason. When the owner picks one, continue with
[Plan the target](#plan-the-target).

## Plan the target

1. **Check the slot.** Pick the target's lane by the areas its issues will carry; a target that
   spreads evenly over both lanes is two targets. If that lane already has an Active and a Planning
   milestone, this target becomes a roadmap paragraph: draft step 2's description only, and after the
   owner's OK add it to the roadmap at the place they choose in that lane's order, as a docs-only
   commit on `main`. If the lane has neither, the new milestone is created as Planning and activated
   right after.
2. **Draft the milestone.** Title `M<n>: <name>`, numbered by the [milestone rules](#milestone-rules). Description:

   ```md
   **Lane:** <render | content>

   **Target:** <the outcome, in a sentence>

   **Done when:**
   - <a result someone can check: a scene that renders, an editor flow, a test or reference image>

   **Out of scope:** <the neighbouring work this leaves out>
   ```

   "Done when" describes the result, never the list of issues.
3. **Draft the outline issues** (see [Drafting issues](#drafting-issues)), with outline bodies. A
   `Backlog` issue that is part of the target moves in rather than being duplicated. Aim for five to ten.
   A target that needs more is two targets: plan the first, and offer the second as a roadmap paragraph.
4. **Show and wait.** Present the milestone draft and a numbered list: for each issue its **title**,
   **blocked by**, what it delivers, size and labels, and whether it is new or moved from `Backlog`. Ask
   whether "done when" is the right bar, whether the granularity is right and whether each blocker
   really gates its issue. Change the drafts until the owner approves them. Create nothing before that OK.
5. **Create** the milestone, then the issues and their relations ([commands](ISSUE-FORMAT.md#commands)).

## Activate

Run for one lane (the argument, or the only lane that qualifies) once its Active milestone has
closed, leaving its Planning one as the lane's only open `M` milestone; otherwise name the lanes'
open milestones and stop. Activation is where the Planning milestone's detail gets written, against
the code as it is now.

1. **Re-check the target.** Compare the Planning milestone's target and "done when" with the current
   code and with what the last milestones of both lanes changed, and draft any edits. A "done when"
   still set on activation gets written now.
2. **Write the full bodies.** For each issue, re-check what exists and write the full body
   ([format](ISSUE-FORMAT.md#full-body)). Split, merge or drop issues where the code calls for it, and
   fix the blockers.
3. **Show and wait** as in [Plan the target](#plan-the-target) step 4, marking what changed from the
   outlines.
4. **Apply:** rewrite each issue (`gh issue edit`) and fix the relations and labels.

Done when every issue in the milestone has a full body and none is still an outline. Then tell the
owner the lane's Planning slot is free and `/plan-milestone` suggests its next target.

## Issues

Break the plan path, doc step or issue number after `issues` into full issues, without a new milestone.
Draft them ([Drafting issues](#drafting-issues), full bodies), then show and wait as in
[Plan the target](#plan-the-target) step 4, and create them. Each takes its lane's Planning milestone
when the plan is its work, the lane's Active one only when its "done when" can't be met without the
issue (say why when you show it), and `Backlog` otherwise. Leave the source issue as it is.

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
