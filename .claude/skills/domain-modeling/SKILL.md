---
name: domain-modeling
description: Build and sharpen aegis's domain model. Use when naming a new concept, discussing terminology, editing GLOSSARY.md, or recording an ADR.
---

# Domain Modeling

Actively build and sharpen the engine's domain model as you design. This is the *active* discipline:
challenging terms, inventing edge-case scenarios, and writing the glossary and decisions down the
moment they crystallise. (Merely *reading* `GLOSSARY.md` for vocabulary is not this skill: this skill
is for when you're changing the model, not just consuming it.)

## During the session

### Challenge against the glossary

When the user uses a term that conflicts with the existing language in `GLOSSARY.md`, call it out
immediately. "The glossary defines a Descriptor as a bindless slot index, but you seem to mean a
Handle. Which is it?"

### Sharpen fuzzy language

When the user uses vague or overloaded terms, propose a precise canonical term. "You're saying
'asset': do you mean the source file, a sub-asset or the `AssetRef`? Those are different things."

### Discuss concrete scenarios

When relationships between concepts are being discussed, stress-test them with specific scenarios.
Invent scenarios that probe edge cases and force the user to be precise about the boundaries between
concepts.

### Cross-reference with code

When the user states how something works, check whether the code agrees. If you find a contradiction,
surface it: "`GameShell` updates only the main world, but you just said every world ticks in the
runtime. Which is right?"

### Update GLOSSARY.md inline

When a term is resolved, update `GLOSSARY.md` right there. Don't batch these up: capture them as they
happen. Use the format in [GLOSSARY-FORMAT.md](./GLOSSARY-FORMAT.md).

`GLOSSARY.md` should be totally devoid of implementation details. Do not treat it as a spec, a scratch
pad, or a repository for implementation decisions. It is a glossary and nothing else. When a term
moves in from AGENTS.md, a rule that uses it stays there.

### Offer ADRs sparingly

Decisions are ADRs in `docs/adr/`. Only offer to write one when all three are true:

1. **Hard to reverse**: the cost of changing your mind later is meaningful
2. **Surprising without context**: a future reader will wonder "why did they do it this way?"
3. **The result of a real trade-off**: there were genuine alternatives and you picked one for specific
   reasons

If any of the three is missing, skip it. What qualifies: the shape of the architecture, boundaries and
explicit no-s, technology that carries lock-in, deliberate deviations from the obvious path, and
constraints not visible in the code.

Write it in the format of [ADR-FORMAT.md](./ADR-FORMAT.md), which also says where it lands.
