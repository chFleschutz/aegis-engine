---
name: plan-issues
description: Break a plan, doc step or conversation into GitHub issues in aegis's format, with labels and blocked-by relations.
argument-hint: [plan path, doc step or issue number]
disable-model-invocation: true
allowed-tools: Bash(gh issue view *), Bash(gh issue list *), Bash(gh issue create *), Bash(gh label list*), Bash(gh api repos/*/milestones*), Bash(gh api repos/*/issues/*), Bash(gh api --method POST repos/*/issues/*/dependencies/blocked_by *), Read, Write, Grep, Glob
---

Break `$ARGUMENTS` (a plan file, a doc step, an issue number) or, with no argument, this conversation
into GitHub **issues**: each one PR, each declaring the issues that **block** it.

## 1. Gather context

Read the source in full; for an issue, `gh issue view <n> --json title,body,comments`. Then read what
it leans on: the design docs and decisions it cites, `GLOSSARY.md` for names, and enough of the code
to know what exists. Check the open issues: a piece one already covers stays in that issue, which you
list in the breakdown and leave unedited; it can still block a new one.

Done when you can say, for each piece, what exists today and what changes.

## 2. Draft the issues

Each issue is one PR, verifiable on its own and small enough for one session. Prefactoring comes
first: make the change easy, then make the easy change. Give each issue its **blockers**, only the
ones that genuinely gate it; an issue with none can start at once.

A decision only the owner can make stays open: name it in the body and label the issue `needs-design`.
Where the plan is merely unclear, pick a reading and put it to the owner in step 3.

A **wide mechanical change** can't land green as one slice of the work, so sequence it: add the new
form beside the old, migrate the callers in batches (per module or folder), each its own issue blocked
by the add, then delete the old form in an issue blocked by every batch.

Write each body to a file in the scratchpad, in the format below.

## 3. Show the breakdown and wait

Present a numbered list. For each issue: **title**, **blocked by**, **what it delivers**, then its
branch, size, labels and milestone, and say where the bodies are. Ask whether the granularity is
right, whether each blocker really gates its issue, and whether any should merge or split.

Change the breakdown until the owner approves it. Create nothing before that OK.

## 4. Create the issues

Create them in dependency order, blockers first, so the relations can name real issues:

    gh issue create --title "<title>" --label <label> --label <label> --milestone "<milestone>" --body-file <file>

Then add each blocker as a native relation, by its database id rather than its number:

    gh api repos/{owner}/{repo}/issues/<blocker> --jq .id
    gh api --method POST repos/{owner}/{repo}/issues/<n>/dependencies/blocked_by -F issue_id=<id>

Check each with `gh api repos/{owner}/{repo}/issues/<n>/dependencies/blocked_by --jq '.[].number'`.
Leave the issues unassigned, since the assignee means started, and leave the source issue as it is.
Report the new issues' URLs.

## Issue format

The title is one imperative line. The body:

```md
Branch `<kind>/<slug>` · size <S|M|L>

**Design:** <the doc section, decisions and finding IDs it follows; drop the line if there are none>

**Problem:** <what is wrong today, with where and how much>

**Content:**
- <what to build, far enough that no "how" is left open>

**Out of scope:** <the neighbouring work this leaves out>

**Done when:** <conditions someone can check>

**Verification:** <the commands and runs that prove it>
```

Labels, explained in AGENTS.md "Planned work":
- the kind, matching the branch prefix: `fix/` is `bug`, `feat/` is `feature`, `test/` is `tests`,
  `docs/` is `documentation`, and the rest share their name;
- `size/S`, `size/M` or `size/L`;
- `area/*` when the issue sits in one area, from `gh label list`;
- `needs-gpu` when the verification needs a real GPU, `needs-design` for an open decision.

Each issue also gets a milestone (`gh api repos/{owner}/{repo}/milestones --jq '.[].title'`). Take
`Backlog` when the plan names none, or `Ideas` with the `idea` label for an unplanned nice-to-have.
