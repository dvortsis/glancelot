#!/usr/bin/env python3
"""Conventional Commit checks for Glancelot (docs/RELEASING.md, "Commits").

The commit-msg hook (scripts/hooks/commit-msg) checks each commit as it is made; pr_guard.py checks every
non-merge commit of a pull request. Commits made before CUTOFF are exempt, because shared history is never
rewritten to fit the rule.

    python scripts/ci/commit_lint.py --file .git/COMMIT_EDITMSG
    python scripts/ci/commit_lint.py --range origin/main..HEAD
"""
import argparse
import re
import subprocess
import sys
from datetime import datetime, timezone

TYPES = ("feat", "fix", "perf", "refactor", "test", "docs", "build", "ci", "chore", "revert")
SUBJECT = re.compile(r"^(?P<type>[A-Za-z]+)(?:\((?P<scope>[^()]*)\))?(?P<bang>!)?: (?P<description>.*)$")
SCOPE = re.compile(r"^[a-z0-9]+(?:-[a-z0-9]+)*$")
MAX_SUBJECT = 100
CUTOFF = datetime(2026, 11, 6, tzinfo=timezone.utc)
# Real merge commits are skipped in a range, and the hook skips merges (MERGE_HEAD). Fixups pass only locally.
LOCAL_ONLY = ("fixup! ", "squash! ", "amend! ")
REVERT = re.compile(r'^(Revert|Reapply) ".+"$')  # git's and GitHub's own revert subjects
LEGACY = re.compile(r"^[A-Z][A-Z0-9-]*: \S")  # '<TASK-ID>: <title>' PR titles, allowed only for PRs before the rule
FORMAT = "use 'type(scope): description', for example 'fix(grid): keep the selection during arrival' (docs/RELEASING.md)"


def subject_errors(subject):
    subject = subject or ""
    match = SUBJECT.match(subject)
    if not match:
        return [f"{subject!r}: {FORMAT}"]
    errors = []
    if match["type"] not in TYPES:
        errors.append(f"{subject!r}: type {match['type']!r} is not one of {', '.join(TYPES)}")
    if match["scope"] is not None and not SCOPE.match(match["scope"]):
        errors.append(f"{subject!r}: scope {match['scope']!r} must be lowercase words joined by hyphens")
    description = match["description"]
    if not description.strip() or description != description.strip():
        errors.append(f"{subject!r}: the description must follow ': ' with no extra spaces")
    elif description.endswith("."):
        errors.append(f"{subject!r}: no full stop at the end of the subject")
    if len(subject) > MAX_SUBJECT:
        errors.append(f"{subject!r}: the subject is {len(subject)} characters; keep it to {MAX_SUBJECT}")
    return errors


def message_errors(message, local=False):
    lines = [line for line in (message or "").splitlines() if not line.startswith("#")]
    while lines and not lines[0].strip():
        lines.pop(0)
    if not lines:
        return ["empty commit message"]
    subject = lines[0].rstrip()
    if (local and subject.startswith(LOCAL_ONLY)) or REVERT.match(subject):
        return []
    errors = subject_errors(subject)
    if len(lines) > 1 and lines[1].strip():
        errors.append(f"{subject!r}: leave a blank line between the subject and the body")
    return errors


def range_errors(rev_range, cutoff=CUTOFF):
    out = subprocess.run(["git", "log", "--no-merges", "--format=%H%x1f%cI%x1f%B%x1e", rev_range],
                         check=True, capture_output=True, text=True, encoding="utf-8").stdout
    errors = []
    for record in out.split("\x1e"):
        record = record.strip("\n")
        if not record:
            continue
        sha, committed, message = record.split("\x1f", 2)
        if datetime.fromisoformat(committed) < cutoff:
            continue
        errors += [f"commit {sha[:9]}: {error}" for error in message_errors(message)]
    return errors


def main():
    parser = argparse.ArgumentParser()
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--file", help="commit message file (the commit-msg hook)")
    group.add_argument("--range", help="revision range, such as origin/main..HEAD")
    args = parser.parse_args()
    if args.file:
        with open(args.file, encoding="utf-8") as f:
            errors = message_errors(f.read(), local=True)
    else:
        errors = range_errors(args.range)
    for error in errors:
        print(f"commit_lint: {error}", file=sys.stderr)
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
