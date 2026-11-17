# ADR Format

ADRs live in `docs/adr/`, one file per decision, numbered in sequence: `0001-slug.md`,
`0002-slug.md`. The slug says what was decided (`0007-engine-hosts-the-game.md`). Take the highest
number in `docs/adr/` and add one.

## Template

```md
# {The decision, as a statement}

Decided {date}, see [`docs/{doc}.md`](../{doc}.md).

{What was decided.} Why: {the trade-off}. → {the issues it drives, as links}.
```

An ADR can be a single paragraph: the value is in recording *that* a decision was made and *why*.
Drop the "see" clause when no design doc holds the details. Write links out in full; reference-style
links break when text moves.

## Optional sections

Only include these when they add genuine value. Most ADRs won't need them.

- **Considered options**: only when the rejected alternatives are worth remembering
- **Consequences**: only when non-obvious downstream effects need to be called out

## Status

An accepted ADR has no status line. When a later ADR replaces one, the old one gets
`` Superseded by [`NNNN-slug.md`](NNNN-slug.md). `` under its title, and its row in the index says so.

## Landing it

Add a row to the index, `docs/adr/README.md`. AGENTS.md "Planned work" says which PR the ADR lands
in and how to cite it. With no PR (a grilling session on a `needs-design` issue), give the file to
the owner, who commits it to `main` as a docs-only change.
