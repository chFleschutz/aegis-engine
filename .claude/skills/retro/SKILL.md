---
name: retro
description: Look back on a coding session and suggest improvements to the agent's environment.
disable-model-invocation: true
---

The owner has asked for a **retrospective**. You are suggesting improvements to the coding agent's
**environment** to improve future runs. The session's code is not the subject: the question is what
in the repo let the struggle happen.

## Steps

1. Call the Skill tool with `writing-for-agents` for the writing style guide.

2. Read the primary sources for the session in `$ARGUMENTS`; default to the current one. Other
   sessions are at `~/.claude/projects/<working directory, with each non-alphanumeric character as
   ->/<session id>.jsonl`. A worktree has its own folder there.

3. Look for candidates in these categories. Every candidate traces to a specific moment in the
   session; drop any you can't trace, so no category gets filled with generic advice.

   - **Navigation**: how easy was it for the agent to find the right files? Are there hidden
     dependencies between files? Would a **navigation pointer** make it easier? _Use when_ the session
     took a long time to find a piece of information.
   - **Automated checks**: is there a check that could catch an error the agent made? Read the
     existing guardrails first (`.github/workflows/build.yml`, `tools/`, the configure checks in the
     CMake files), so a check that already exists but sits unwired or silently broken is the finding,
     not a reinvention. _Use when_ the agent made a mistake a check could have caught.
   - **AGENTS.md rules**: is a rule a **no-op** that doesn't change behaviour against the default? Does
     one belong in a check, a doc or a skill instead? _Use when_ the session shows a rule that was
     ignored, needed by nobody, or only needed by some tasks. AGENTS.md is large, so this is the
     category that shortens it.
   - **Tool economy**: did the agent make expensive tool calls that could be streamlined (a long
     build or log read into context, a repeated search)? _Use when_ a tool call cost a lot for what it
     returned.
   - **Information access**: could the agent have had a crucial piece of information sooner? A log,
     a flag on `aegis-test`, a reachable artifact. _Use when_ it had to guess or ask for something a
     tool could have shown.

4. Give each candidate its place (below) and present them in order of severity, each with the
   session moment that shows it. Edit nothing until the owner OKs a candidate.

## Where findings go

Default to the deterministic fix over a sentence: a check can fail, a rule can only be read.

- **A mechanical rule** (a fixed syntactic pattern, a banned API, an import shape, a file location)
  becomes a check: a `tools/` script, a CI job, or a CMake configure check, whichever is cheapest.
- **A gotcha** goes to `docs/agent-notes.md`.
- **A missing pointer** goes in the file the agent was already reading when it needed the material.
- **AGENTS.md** only for what every session needs: it is pushed into every agent's context. Prefer a
  pointer to a doc or a skill over the material itself.
