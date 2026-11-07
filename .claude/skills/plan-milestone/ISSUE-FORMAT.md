# Issue format and creation

How [`plan-milestone`](SKILL.md) writes and creates issues: the body format, the labels and the
`gh` commands.

## Full body

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

## Outline body

A Planning milestone's issues are outlines until activation writes the full body:

```md
Outline · size <S|M|L>

<what it delivers, in one or two lines>

The full body is written when the milestone becomes Active.
```

## Labels

Explained in AGENTS.md "Planned work":
- the kind, matching the branch prefix: `fix/` is `bug`, `feat/` is `feature`, `test/` is `tests`,
  `docs/` is `documentation`, and the rest share their name;
- `size/S`, `size/M` or `size/L`;
- `area/*` when the issue sits in one area, from `gh label list`;
- `needs-gpu` when the verification needs a real GPU, `needs-design` for an open decision;
- `idea` for an unplanned nice-to-have, which goes to `Backlog`.

## Commands

Create issues in dependency order, blockers first, so the relations can name real issues:

    gh issue create --title "<title>" --label <label> --label <label> --milestone "<milestone>" --body-file <file>

Rewrite an existing issue (an outline on activation, a Backlog issue taken into a milestone):

    gh issue edit <n> --milestone "<milestone>" --body-file <file>

Add each blocker as a native relation, by its database id rather than its number:

    gh api repos/{owner}/{repo}/issues/<blocker> --jq .id
    gh api --method POST repos/{owner}/{repo}/issues/<n>/dependencies/blocked_by -F issue_id=<id>

Check each with `gh api repos/{owner}/{repo}/issues/<n>/dependencies/blocked_by --jq '.[].number'`.

Create or update a milestone, with its description in a file:

    gh api --method POST repos/{owner}/{repo}/milestones -f title="<title>" -F description=@<file>
    gh api --method PATCH repos/{owner}/{repo}/milestones/<number> -F description=@<file>

Leave issues unassigned, since the assignee means started. Report the URLs of what you created.
