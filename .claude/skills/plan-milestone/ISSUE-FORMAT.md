# Issue format and creation

How [`plan-milestone`](SKILL.md) writes and creates issues: the body format, the labels and the
`gh` commands.

## Full body

The title is one imperative line. The body:

```md
Branch `<kind>/<slug>` · size <S|M|L>

**Design:** <the doc section, ADRs (by file name) and finding IDs it follows; drop the line if there are none>

**Problem:** <what is wrong today, with where and how much>

**Content:**
- <what to build, far enough that no "how" is left open>

**Out of scope:** <the neighbouring work this leaves out>

**Hazards:** <what the implementer must know before starting; drop the line if there are none>

**Done when:** <conditions someone can check>

**Verification:** <the checks beyond AGENTS.md's default ones, or "defaults">
```

- **Problem** for a bug: what happens, what should happen instead, and the smallest reproduction (an
  `aegis-test` flag, an `Examples` scene, a test). For a feature: the gap it fills, as a fact about
  today's code.
- **Hazards** for a repro or test that can hang the GPU (it freezes the machine, so it runs only with the
  owner's OK, as in [`diagnose`](../diagnose/SKILL.md)), an aegis-samples PR that must merge first, a
  public API it breaks.
- **Verification** names only what AGENTS.md's "Verify each commit" doesn't already require of every
  PR (building clean, `ctest -LE device`, the full `ctest`): a `--screenshot` to look at, a windowed
  run, a resize, a timing before and after, a forced failure. With nothing beyond those, write
  `defaults`.

## Outline body

A Planning milestone's issues are outlines until activation writes the full body:

```md
Outline · size <S|M|L>

<what it delivers, in one or two lines>

The full body is written when the milestone becomes Active.
```

## Labels

- the kind, matching the branch prefix: `fix/` is `bug`, `feat/` is `feature`, `test/` is `tests`,
  `docs/` is `documentation`, and the rest (`refactor`, `cleanup`, `style`, `build`, `ci`) share their name;
- `size/S`, `size/M` or `size/L`, as the labels define them (`gh label list --search size`);
- `area/*` when the issue sits in one area, from `gh label list`; two issues of one area don't run in
  parallel;
- `needs-gpu` when the verification needs a real GPU (the reference images, a windowed run or Windows
  input, which CI's lavapipe job doesn't cover and cloud sessions can't run);
- `needs-design` for an open decision only the owner can make;
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
