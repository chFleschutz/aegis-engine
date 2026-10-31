# Third-party skills

Some skills in this folder derive from [mattpocock/skills](https://github.com/mattpocock/skills) at
commit `a496f5513d4a4cf889bc48e189e579f4b0bdf6aa`, adapted to aegis. Nothing installs or updates
them: to take an upstream change, read its diff against that commit, carry over what applies by hand
and move the commit here.

| Skill | Upstream skill | How close |
| --- | --- | --- |
| `diagnose` | `skills/engineering/diagnosing-bugs` | Same phases; aegis's feedback loops, plus the GPU hang hazard |
| `domain-modeling` | `skills/engineering/domain-modeling` | Same discipline; one glossary, and D-n decisions instead of ADRs |
| `grill` | `skills/engineering/grill-with-docs` | A wrapper, as upstream, that also names the subject |
| `grilling` | `skills/productivity/grilling` | Verbatim |
| `implement-issue` | `skills/engineering/code-review`, `skills/engineering/tdd` | Only the spec check (the Spec axis) and the test-first step; the rest is aegis's own |
| `retro` | `skills/engineering/retro` | Same steps; aegis's categories and where findings go |
| `writing-for-agents` | `skills/productivity/writing-for-agents` | Nearly verbatim |

Each upstream skill also carries an `agents/openai.yaml` for Codex, left out here.

## Upstream notice

```
MIT License

Copyright (c) 2026 Matt Pocock

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
