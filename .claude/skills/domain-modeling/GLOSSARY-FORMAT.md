# GLOSSARY.md Format

## Structure

```md
## Language

### {Area}

**Handle**:
{One or two sentences on what the term is.}
_Avoid_: descriptor handle, resource pointer
```

## Rules

- **Be opinionated.** When multiple words exist for the same concept, pick the best one and list the others under `_Avoid_`. Leave out the line when there are no synonyms in use.
- **Keep definitions tight.** One or two sentences max. Define what it IS, not what it does.
- **Only include terms specific to aegis.** General programming concepts (timeouts, error types, utility patterns) don't belong even if the project uses them extensively. Before adding a term, ask: is this a concept unique to this engine, or a general programming concept? Only the former belongs.
- **Group terms under the area subheadings**, adding one when a new cluster emerges.
