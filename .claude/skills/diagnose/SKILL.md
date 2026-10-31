---
name: diagnose
description: Diagnosis loop for hard bugs and performance regressions. Use when the user says "diagnose"/"debug this", or reports a crash, device loss, validation error, wrong image or slowdown.
---

# Diagnose

A discipline for hard bugs. Skip phases only when explicitly justified. `<preset>` is
`windows-clang-debug` on Windows and `linux-clang-debug` on Linux; `aegis-test` is
`build/<preset>/src/aegis/test/aegis-test`.

## Hazard: a repro that hangs the GPU

A repro that hangs the GPU or loses the device freezes the owner's whole desktop for 30–60 s per run
until the driver resets, and a debug build then blocks on an assertion dialog (#154). **Classify every
loop as safe or hazardous before its first run**, in every phase. It is hazardous if it can reach a
failure reported as a GPU hang, device loss, TDR, a failing `Queue::submit` or a long stall, including
by bringing back the state that caused one (reverting a workaround, a reversed upload order).

A hazardous loop needs the owner's explicit OK in this conversation, given for that command. Until
then, stay on the safe routes: code analysis, a CPU-side doctest, or a safe loop that sits next to
the bug. Sub-agents get this section too.

With the OK, run it one at a time, with a short `--frames` and a kill watchdog, and check the event
log before anything else:

```powershell
$start = Get-Date
$p = Start-Process <aegis-test.exe> -ArgumentList '<args> --frames <n>' -PassThru
if (-not $p.WaitForExit(90000)) { $p.Kill() }
Get-WinEvent -FilterHashtable @{ LogName = 'System'; ProviderName = 'nvlddmkm'; StartTime = $start } -ErrorAction SilentlyContinue
```

(Linux: `timeout -s KILL 90 <command>`, then `journalctl -k --since <start>`.) Every further run takes
the same steps, so a hazardous loop stays out of `--repeat`, parallel runs and `git bisect run`.

## Phase 1: Build a feedback loop

**This is the skill.** Everything else is mechanical. If you have a **tight** pass/fail signal for the
bug (one that goes red on _this_ bug), you will find the cause; bisection, hypothesis-testing, and
instrumentation all just consume it. If you don't have one, no amount of staring at code will save
you.

Spend disproportionate effort here. **Be aggressive. Be creative. Refuse to give up.** The hazard
above is the one limit on that.

### Ways to construct one, in roughly this order

1. **A doctest case** in the module's `aegis-tests-<module>` (`tests/README.md`), when the bug is in
   pure logic or can be lifted out from behind a GPU-owning type: `ctest --preset <preset> -L <module>`.
2. **`aegis-test --headless --frames <n>`.** The signal is its exit code plus validation errors on
   stderr, in a debug build. Add `--scene` (CPU-driven), `--gpu-driven` or `--mutate-scene` to reach
   the path, and lower `--frames` to just past the failure. Headless skips surface, swapchain, present
   and input: for those, `aegis-test --frames 600` runs windowed and exits by itself.
3. **Differential.** The same run CPU- vs GPU-driven (`--scene` vs `--gpu-driven`), or two commits in
   two worktrees. A bug on one side only cuts the suspects at once.
4. **`--screenshot` against the reference images.** For a wrong image that validation doesn't report:
   `--compare tests/golden/<name>.png --tolerance 0`, or `ctest --preset <preset> -L golden`. They
   were generated on the dev GPU, so they only hold on it.
5. **`git bisect run`** for a bug between two known states, in its own worktree. Exit 125 skips a
   commit that doesn't build, and `--no-tests=error` stops a test name that matches nothing from
   passing:
   `git bisect run sh -c 'cmake --build --preset <preset> || exit 125; ctest --preset <preset> -R <test> --no-tests=error'`
6. **The `Examples` scenes**, last, since they have no pass/fail signal: nothing fails them, and
   validation errors only reach stderr. A scene that shows the bug is a reason to make the loop tighter
   (a doctest case, or that scene's content in `aegis-test`), not a loop to stay on.

Build the right feedback loop, and the bug is 90% fixed.

### Tighten the loop

Treat the loop as a product. Once you have _a_ loop, **tighten** it:

- Can I make it faster? (Fewer `--frames`, a smaller scene, the single module's test target.)
- Can I make the signal sharper? (Assert on the specific symptom, not "didn't crash".)
- Can I make it more deterministic? (Fixed frame count, no timing-based input, one device test at a
  time on the machine.)

A 30-second flaky loop is barely better than no loop; a 2-second deterministic one is tight, a
debugging superpower.

### Non-deterministic bugs

The goal is not a clean repro but a **higher reproduction rate**. Loop the trigger
(`ctest --preset <preset> -R <test> --repeat until-fail:50`), add stress, narrow timing windows,
inject sleeps. A 50%-flake bug is debuggable; 1% is not, so keep raising the rate until it's
debuggable. Device runs go one at a time per machine; doctest cases can run in parallel.

### When you genuinely cannot build a loop

Stop and say so explicitly. List what you tried. Ask the owner for: (a) access to the GPU, driver or
machine that reproduces it, (b) a captured artifact (stderr log, screenshot, RenderDoc capture, crash
dump, event log), or (c) the OK for a hazardous run. Do **not** proceed to hypothesise without a loop.

The one exception is a bug whose only red loop is hazardous, while the owner hasn't given the OK: go
on with code analysis, and mark every hypothesis as untested. GPU-assisted validation on a short run
can stand in as the signal; the engine enables only core and synchronization validation, so turn it
on in `vkconfig`.

### Completion criterion: a tight loop that goes red

Phase 1 is done when the loop is **tight** and **red-capable**: you can name **one command** (a test
invocation, an `aegis-test` line) that you have **already run at least once** (show the invocation and
its output), and that is:

- [ ] **Red-capable**: it drives the actual bug code path and asserts the **user's exact symptom**, so
      it can go red on this bug and green once fixed. Not "runs without erroring"; it must be able to
      _catch this specific bug_.
- [ ] **Deterministic**: same verdict every run (flaky bugs: a pinned, high reproduction rate, per
      above).
- [ ] **Fast**: seconds, not minutes.
- [ ] **Agent-runnable**: you can run it unattended.
- [ ] **Safe**: classified against the hazard above, or run with the owner's OK.

If you catch yourself reading code to build a theory before this command exists, **stop: jumping
straight to a hypothesis is the exact failure this skill prevents.** No red-capable command, no
Phase 2.

## Phase 2: Reproduce + minimise

Run the loop. Watch it go red as the bug appears.

Confirm:

- [ ] The loop produces the failure mode the **user** described, not a different failure that happens
      to be nearby. Wrong bug = wrong fix.
- [ ] The failure is reproducible across multiple runs (or, for non-deterministic bugs, reproducible
      at a high enough rate to debug against).
- [ ] You have captured the exact symptom (validation message, wrong image, frame time) so later
      phases can verify the fix actually addresses it.

### Minimise

Once it's red, shrink the repro to the **smallest scenario that still goes red**. Cut scene content,
frames, passes, callers and steps **one at a time**, re-running the loop after each cut, and keep only
what's load-bearing for the failure.

Why bother: a minimal repro shrinks the hypothesis space in Phase 3 (fewer moving parts left to
suspect) and becomes the clean regression test in Phase 5.

Done when **every remaining element is load-bearing**: removing any one of them makes the loop go
green.

Do not proceed until you have reproduced **and** minimised.

## Phase 3: Hypothesise

Generate **3–5 ranked hypotheses** before testing any of them. Single-hypothesis generation anchors on
the first plausible idea.

Each hypothesis must be **falsifiable**: state the prediction it makes.

> Format: "If <X> is the cause, then <changing Y> will make the bug disappear / <changing Z> will make
> it worse."

If you cannot state the prediction, the hypothesis is a vibe: discard or sharpen it.

**Show the ranked list to the user before testing.** They often have domain knowledge that re-ranks
instantly ("#3 changed last week"), or know hypotheses they've already ruled out. Cheap checkpoint,
big time saver. Don't block on it; proceed with your ranking if the user is AFK.

## Phase 4: Instrument

Each probe must map to a specific prediction from Phase 3. **Change one variable at a time.**

Tool preference:

1. **Debugger inspection** if the env supports it. One breakpoint beats ten logs.
2. **Targeted logs** at the boundaries that distinguish hypotheses, through `aegis::log`.
3. Never "log everything and grep".

**Tag every debug log** with a unique prefix: `aegis::log::warn("[DEBUG-a4f2] ...")`. Cleanup at the
end becomes a single grep. Untagged logs survive; tagged logs die. Put probes in `.cpp` files: a
touched `.cppm` recompiles every importer.

**Perf branch.** For performance regressions, logs are usually wrong. Measure first, fix second:
establish a baseline with the profiler (`aegis::ScopeProfiler`, read from `Profiler::instance()`, a
rolling average over 50 frames), in a release preset on a real GPU, then bisect. lavapipe and debug
builds say nothing about performance.

## Phase 5: Fix + regression test

Write the regression test **before the fix**, but only if there is a **correct seam** for it.

A correct seam is one where the test exercises the **real bug pattern** as it occurs at the call site.
If the only available seam is too shallow (single-caller test when the bug needs multiple callers, unit
test that can't replicate the chain that triggered the bug), a regression test there gives false
confidence. In aegis the seam is a doctest case for pure logic, and a `device` test or an `aegis-test`
check for anything that needs a live `rhi::Device`.

**If no correct seam exists, that itself is the finding.** Note it in the PR body: the code's structure
is preventing the bug from being locked down.

If a correct seam exists:

1. Turn the minimised repro into a failing test at that seam.
2. Watch it fail. If you forced the red by mutating code or a fixture, `git diff` to prove the mutation
   landed before you trust it.
3. Apply the fix.
4. Watch it pass.
5. Re-run the Phase 1 feedback loop against the original (un-minimised) scenario.

## Phase 6: Cleanup

Required before declaring done:

- [ ] Original repro no longer reproduces (re-run the Phase 1 loop)
- [ ] Regression test passes (or absence of seam is documented)
- [ ] All debug logs removed (`git grep "DEBUG-"` finds nothing you added)
- [ ] Throwaway harnesses and temporary worktrees deleted
- [ ] The hypothesis that turned out correct is stated in the commit message or PR body, so the next
      debugger learns
