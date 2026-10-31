---
name: grill
description: Interview the owner on a plan or a needs-design issue until every decision is settled.
disable-model-invocation: true
---

The subject is `$ARGUMENTS`: an issue number (read it with
`gh issue view <n> --json title,body,comments`, and the D-n decisions it cites in
`docs/audit/backlog.md`), or else the plan in this conversation.

Call the Skill tool twice, for "grilling" and "domain-modeling".
