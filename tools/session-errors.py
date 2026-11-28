#!/usr/bin/env python3
"""Counts the failed tool calls of recent Claude Code sessions on this repository, grouped by error.

A gotcha that comes back in session after session shows up here, where a look at one session can't
see it: the Bash heredoc failures behind tools/check-heredoc.sh hit 18 of 60 sessions. `/retro recent`
starts from this list.

Reads the transcripts in ~/.claude/projects/, from the folders of the main checkout and of every
worktree (their folder names start with the main checkout's), and takes the most recently changed
sessions. An error's signature is one line of its message (the last for a shell command, where the
error follows any output; the first otherwise), with numbers, paths and quoted text folded so that
repeats group. Prints, by the number of sessions
it hit: sessions, calls, the tool, the signature and one call that hit it.

Usage: tools/session-errors.py [--sessions N] [--top K] [--min-sessions M]
       (defaults: the last 60 sessions, the top 20 signatures, those hitting at least 2 sessions)
"""

import argparse
import json
import re
import subprocess
from collections import defaultdict
from pathlib import Path


def main_checkout() -> str:
    common = subprocess.run(["git", "rev-parse", "--path-format=absolute", "--git-common-dir"],
                            capture_output=True, text=True, check=True).stdout.strip()
    return str(Path(common).parent)


def transcripts(count: int) -> list[Path]:
    # Claude Code names a project's folder after its path, each non-alphanumeric character as "-".
    prefix = re.sub(r"[^A-Za-z0-9]", "-", main_checkout())
    root = Path.home() / ".claude" / "projects"
    files = [f for d in root.glob(prefix + "*") if d.is_dir() for f in d.glob("*.jsonl")]
    return sorted(files, key=lambda f: f.stat().st_mtime)[-count:]


def text_of(content) -> str:
    if isinstance(content, str):
        return content
    if isinstance(content, list):
        return "\n".join(part.get("text", "") for part in content if isinstance(part, dict))
    return ""


def signature(tool: str, message: str) -> str:
    lines = [line.strip() for line in message.splitlines() if line.strip()]
    meaningful = [line for line in lines if not re.fullmatch(r"(Error: )?Exit code \d+", line)]
    if not meaningful:
        return lines[0] if lines else "(empty)"
    # A shell command's error is its last line, after any output (a Python traceback ends in it too).
    line = meaningful[-1] if tool in ("Bash", "PowerShell") else meaningful[0]
    line = re.sub(r"<tool_use_error>|</tool_use_error>", "", line)
    line = re.sub(r"([A-Za-z]:)?[/\\][^\s:'\"`]*[/\\][^\s:'\"`]*", "<path>", line)
    line = re.sub(r"\b0x[0-9a-fA-F]+\b|\b\d+\b", "N", line)
    line = re.sub(r"'[^']{24,}'|\"[^\"]{24,}\"", "<text>", line)
    return line[:140]


def call_summary(tool_input: dict) -> str:
    value = tool_input.get("command") or tool_input.get("file_path") or tool_input.get("pattern") or ""
    first = str(value).strip().splitlines()[0] if str(value).strip() else ""
    return first[:120]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--sessions", type=int, default=60)
    parser.add_argument("--top", type=int, default=20)
    parser.add_argument("--min-sessions", type=int, default=2)
    args = parser.parse_args()

    files = transcripts(args.sessions)
    sessions: dict[tuple[str, str], set[str]] = defaultdict(set)
    calls: dict[tuple[str, str], int] = defaultdict(int)
    examples: dict[tuple[str, str], str] = {}
    for file in files:
        uses: dict[str, tuple[str, dict]] = {}
        for raw in file.open(encoding="utf-8", errors="replace"):
            try:
                content = json.loads(raw).get("message", {}).get("content")
            except (json.JSONDecodeError, AttributeError):
                continue
            if not isinstance(content, list):
                continue
            for block in content:
                if not isinstance(block, dict):
                    continue
                if block.get("type") == "tool_use":
                    uses[block.get("id", "")] = (block.get("name", "?"), block.get("input") or {})
                elif block.get("type") == "tool_result" and block.get("is_error"):
                    tool, tool_input = uses.get(block.get("tool_use_id", ""), ("?", {}))
                    key = (tool, signature(tool, text_of(block.get("content"))))
                    sessions[key].add(file.stem)
                    calls[key] += 1
                    examples.setdefault(key, call_summary(tool_input))

    ranked = sorted(sessions, key=lambda k: (len(sessions[k]), calls[k]), reverse=True)
    ranked = [k for k in ranked if len(sessions[k]) >= args.min_sessions][: args.top]
    print(f"{len(files)} sessions; failed tool calls by error, most sessions first\n")
    print(f"{'sessions':>8} {'calls':>5}  tool        signature")
    for key in ranked:
        tool, sig = key
        print(f"{len(sessions[key]):>8} {calls[key]:>5}  {tool:<11} {sig}")
        if examples[key]:
            print(f"{'':>16}e.g. {examples[key]}")


if __name__ == "__main__":
    main()
