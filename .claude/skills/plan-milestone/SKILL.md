---
name: plan-milestone
description: Suggest the next targets, plan one as the Planning milestone, activate it, or break a plan into issues.
argument-hint: [target | activate | issues <plan path, doc step or issue number>]
disable-model-invocation: true
allowed-tools: Bash(gh issue view *), Bash(gh issue list *), Bash(gh issue create *), Bash(gh label list*), Bash(gh api repos/*/milestones*), Bash(gh api repos/*/issues/*), Bash(gh api --method POST repos/*/issues/*/dependencies/blocked_by *), Read, Write, Grep, Glob
---

Plan milestones by the rules in AGENTS.md "Milestones": one **Active** and one **Planning** milestone
beside `Backlog`, and the Active one's scope frozen. Pick the branch by `$ARGUMENTS`:

| `$ARGUMENTS` | Branch |
| --- | --- |
| empty | [Suggest targets](#suggest-targets) |
| `activate` | [Activate](#activate) |
| `issues <plan path, doc step or issue number>` | [Issues](#issues) |
| anything else, the target | [Plan the target](#plan-the-target) |

## 1. Gather context

Every branch starts here. Read the open milestones with their descriptions
(`gh api repos/{owner}/{repo}/milestones --jq '.[] | "\(.number) \(.title)\n\(.description)"'`), their
issues and the `Backlog` (`gh issue list --milestone "<title>" --json number,title,labels`), and the
roadmap, the file AGENTS.md "Planned work" names. For a target or a plan, also read what it leans on:
the design docs and decisions it cites, `GLOSSARY.md` for names, and enough of the code to know what
exists. Branches that draft issues read [`ISSUE-FORMAT.md`](ISSUE-FORMAT.md).

Done when you can say, for each piece of the subject, what exists today and what changes.

## Suggest targets

Collect candidate targets from the roadmap's next targets, clusters of `Backlog` issues that share an
area or a chain of blockers, open `needs-design` issues, and what the Active milestone unblocks once it
closes. Propose two to four. For each: the target in one line, the result its "done when" would check,
why now (what it unblocks or fixes), the `Backlog` issues it would take in, and a size in issues.
Recommend one.

Done when every roadmap target and every `Backlog` cluster of three or more issues appears, as a
proposal or in one "passed over" line with its reason. When the owner picks one, continue with
[Plan the target](#plan-the-target).

## Plan the target

1. **Check the slot.** If a Planning milestone is already open, this target becomes a roadmap
   paragraph: draft step 2's description only, and after the owner's OK add it to the roadmap at the
   place they choose, as a docs-only commit on `main`.
2. **Draft the milestone.** Title `M<n>: <name>`, `n` one past the highest `M` number of any milestone,
   open or closed. Description:

   ```md
   Planning. **Target:** <the outcome, in a sentence>

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

Only when no Active milestone is open; otherwise name it and stop. Activation is where the Planning
milestone's detail gets written, against the code as it is now.

1. **Re-check the target.** Compare the Planning milestone's target and "done when" with the current
   code and with what the last milestone changed, and draft any edits. A "done when" still set on
   activation gets written now.
2. **Write the full bodies.** For each issue, re-check what exists and write the full body
   ([format](ISSUE-FORMAT.md#full-body)). Split, merge or drop issues where the code calls for it, and
   fix the blockers.
3. **Show and wait** as in [Plan the target](#plan-the-target) step 4, marking what changed from the
   outlines.
4. **Apply:** rewrite each issue (`gh issue edit`), fix the relations and labels, and change the
   description's first word from `Planning.` to `Active.`.

Done when every issue in the milestone has a full body and none is still an outline. Then tell the
owner the Planning slot is free and `/plan-milestone` suggests the next target.

## Issues

Break the plan path, doc step or issue number after `issues` into full issues, without a new milestone.
Draft them ([Drafting issues](#drafting-issues), full bodies), then show and wait as in
[Plan the target](#plan-the-target) step 4, and create them. Each takes the Planning milestone when the
plan is its work, the Active one only when its "done when" can't be met without the issue (say why
when you show it), and `Backlog` otherwise. Leave the source issue as it is.

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
