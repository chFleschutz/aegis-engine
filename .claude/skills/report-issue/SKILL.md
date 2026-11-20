---
name: report-issue
description: Turn something the owner reports (a bug, a gap, an idea) into GitHub issues, grilling first when it leaves decisions open.
argument-hint: [what you saw or want]
disable-model-invocation: true
allowed-tools: Bash(gh issue view *), Bash(gh issue list *), Bash(gh issue create *), Bash(gh issue comment *), Bash(gh pr list *), Bash(gh label list*), Bash(gh api repos/*/milestones*), Bash(gh api repos/*/issues/*), Bash(gh api --method POST repos/*/issues/*/dependencies/blocked_by *), Read, Write, Grep, Glob, Agent, Skill, AskUserQuestion
---

The owner's **report** is `$ARGUMENTS`, or else what they describe in this conversation; with
neither, ask what they saw. A report is raw: one sentence, a log, a screenshot, several things at
once. Turn it into issues `/implement` can take without asking anything: the facts found by you,
the decisions made by the owner.

## 1. Split into findings

A **finding** is one problem or one wish. List the report's findings; unrelated ones become separate
issues. Done when every sentence of the report belongs to a finding.

## 2. Investigate

Facts are your job: find them in the code, the issues and the docs, with sub-agents for wide
searches. For each finding:

- **Where:** the module, files and functions involved, what the code does today, and the `area/*`
  label it sits in (`gh label list`). For a bug, the likely cause as far as the code shows it, marked
  as a guess when it is one.
- **Existing work:** search open and closed issues
  (`gh issue list --state all --search "<words>" --json number,title,state,milestone`) and open PRs.
  A finding an open issue already covers becomes a comment on that issue; a closed one that came back
  becomes a new issue that cites it.
- **Design:** the ADRs (`docs/adr/`), `GLOSSARY.md` terms and plan docs it touches.
- **Reproduction**, for a bug: the smallest run that shows it (an `aegis-test` flag, an `Examples`
  scene, a test). Run it when it is cheap and safe; a device loss or GPU hang repro freezes the
  machine, so write that one down unrun.

Stop at the cause: the fix belongs to `/implement`, and an unknown cause to `/diagnose`. Done when
each finding has its where, today's behaviour, its area and the issues it relates to.

## 3. Grill the open decisions

List what the report leaves to the owner: the expected behaviour where it is unclear, the scope,
whether it is wanted at all, how to split it, and its priority (the lane's Planning milestone or
`Backlog`). When any is open, call the Skill tool for "grilling" with those decisions as the design
tree's roots, and for "domain-modeling" too when a finding brings a concept `GLOSSARY.md` lacks. With
none open (a crash with an obvious expected behaviour), say so in one line and go on.

A decision the owner leaves open stays in the issue: name it in the body and label the issue
`needs-design`.

## 4. Draft

Read [`ISSUE-FORMAT.md`](../plan-milestone/ISSUE-FORMAT.md) for the body, labels and commands, and
follow [Drafting issues](../plan-milestone/SKILL.md#drafting-issues): each issue is one PR, with
blockers only where one gates another. Write each body to a file in the scratchpad.

- **Full body** for a finding whose "how" is settled; a repro you left unrun goes under **Hazards**.
- **Outline body** for an `idea`, which goes to `Backlog`.
- **Milestone:** read the open ones
  (`gh api repos/{owner}/{repo}/milestones --jq '.[] | "\(.number) \(.title)\n\(.description)"'`)
  and the lanes in the [milestone rules](../plan-milestone/SKILL.md#milestone-rules). An issue goes to
  its lane's Planning milestone or `Backlog`, and to the Active one only when that milestone's "done
  when" can't be met without it (a bug its own work caused); then say why.

## 5. Show, wait, file

Show a numbered list: for each issue its title, labels, milestone, blockers and what it delivers, or
the existing issue a comment goes to; the bodies follow the list. Change the drafts until the owner
approves them, and create nothing before that OK.

Then create the issues in dependency order, add the relations and post the comments
([commands](../plan-milestone/ISSUE-FORMAT.md#commands)). Leave the issues unassigned and report
their URLs.
