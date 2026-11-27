#!/usr/bin/env bash
# Refuses Bash tool commands whose heredocs the Bash tool mangles (the PreToolUse hook in
# .claude/settings.json runs it before every Bash call). Two cases, both seen on Windows:
#
#   - A command of more than 80 lines with a heredoc can fail as a whole with "unexpected EOF while
#     looking for matching `''", although bash parses it fine. 29 commands in the last 80 sessions
#     failed so; every one had 99 lines or more.
#   - "\\" in a quoted heredoc (<<'EOF') arrives as "\", so the written file differs from the
#     command with no error.
#
# Either way the agent is told to write the content with the Write tool and run it with a short
# command. Exit 2 refuses the call and passes stderr to the agent; exit 0 lets it run.
#
#   tools/check-heredoc.sh < <hook JSON>
#
# Reads the hook's JSON input on stdin and only looks at tool_input.command. No jq on Git Bash, so
# awk decodes the JSON string.

set -euo pipefail

awk -v max_lines=80 '
# Decodes the JSON string that starts after the opening quote at s[i]; \uXXXX becomes "?".
function json_string(s, i,    out, c, e) {
    out = ""
    for (; i <= length(s); i++) {
        c = substr(s, i, 1)
        if (c == "\"") break
        if (c != "\\") { out = out c; continue }
        e = substr(s, ++i, 1)
        if (e == "n") out = out "\n"
        else if (e == "t") out = out "\t"
        else if (e == "r") out = out "\r"
        else if (e == "u") { out = out "?"; i += 4 }
        else if (e == "b" || e == "f") out = out " "
        else out = out e  # \" \\ \/
    }
    return out
}

{ input = input $0 "\n" }

END {
    if (!match(input, /"command"[ \t\r\n]*:[ \t\r\n]*"/)) exit 0
    command = json_string(input, RSTART + RLENGTH)
    n = split(command, lines, "\n")

    heredocs = 0; head = 1; tail = 0; backslash_line = 0
    for (l = 1; l <= n; l++) {
        line = lines[l]
        if (head <= tail) {  # inside the body of the oldest open heredoc
            end = line
            if (strip[head]) sub(/^\t+/, "", end)
            if (end == delim[head]) head++
            else if (quoted[head] && !backslash_line && index(line, "\\\\")) backslash_line = l
            continue
        }
        # Openers on this line: <<WORD, <<-WORD, << "WORD", <<\WORD; not the here-string <<<.
        rest = line
        while (match(rest, /<<-?[ \t]*(\\|\047|")?[A-Za-z_][A-Za-z0-9_]*/)) {
            token = substr(rest, RSTART, RLENGTH)
            before = RSTART > 1 ? substr(rest, RSTART - 1, 1) : ""
            rest = substr(rest, RSTART + RLENGTH)
            if (before == "<") continue
            tail++; heredocs++
            strip[tail] = token ~ /^<<-/
            sub(/^<<-?[ \t]*/, "", token)
            quoted[tail] = token ~ /^(\\|\047|")/
            sub(/^(\\|\047|")/, "", token)
            delim[tail] = token
        }
    }

    if (heredocs && n > max_lines) {
        printf "This command has a heredoc and %d lines. The Bash tool can fail on a heredoc command over %d lines with \"unexpected EOF while looking for matching quote\", although bash parses it. Write the content with the Write tool (a script goes in the scratchpad), then run it with a short Bash command.\n", n, max_lines > "/dev/stderr"
        exit 2
    }
    if (backslash_line) {
        printf "Line %d has a double backslash inside a quoted heredoc. The Bash tool delivers it as a single backslash, so the content would differ from what you wrote, with no error. Write the content with the Write tool (a script goes in the scratchpad), then run it with a short Bash command.\n", backslash_line > "/dev/stderr"
        exit 2
    }
}
'
