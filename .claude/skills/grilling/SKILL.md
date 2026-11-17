---
name: grilling
description: Grill the user relentlessly about a plan, decision, or idea. Use when the user wants to stress-test their thinking, or uses any 'grill' trigger phrases.
---

Interview the user relentlessly until you reach a shared understanding. Map this as a **design tree**: every decision branches into the decisions that hang off it.

Work the tree in **rounds**. The **frontier** is every decision whose prerequisites are already settled: the questions you can ask _now_ without guessing at answers you haven't heard yet. Ask a round with one `AskUserQuestion` call, then wait for the user's answers before the next round.

Each frontier decision is one question in that call:

- **Options:** 2–4 distinct answers, your recommended answer first with ` (Recommended)` at the end of its label. A decision with no fixed set of answers (a name, a number, an open "how") gets your recommendation and the likeliest alternative; the tool adds "Other" for anything else. Set `multiSelect` when the answers combine.
- **Question text stands alone:** the user may see only the dialog, so the question carries the context the decision needs, and each option's description carries its trade-off.

One call holds at most 4 questions. When the frontier is larger, ask the 4 decisions that unblock the most of the tree and carry the rest into the next round.

Each round the user answers reshapes the tree: settled decisions push the frontier outward and unblock questions that depended on them. Recompute the frontier and ask the next round. A question whose answer depends on another question still open in this round belongs to a _later_ round, not this one.

Finding _facts_ is your job, never the user's. When a frontier question needs a fact from the environment (filesystem, tools, etc.), dispatch a sub-agent with a fresh context window to find it; don't ask the user for anything you could look up yourself. Don't block on it: a running exploration is an unsettled prerequisite, so only the questions downstream of it wait for the sub-agent to report; ask the rest of the frontier now. The _decisions_ are the user's: put each to them and wait.

The session is done when the frontier is empty: every branch of the design tree visited, nothing left silently assumed. Do not act on it until the user confirms you have reached a shared understanding.
