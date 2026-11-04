#!/usr/bin/env python3
"""Merge-protection checks for pull requests and for main (AGENTS.md, "Pull requests and merging").

CI runs this in the `guard` job of .github/workflows/build.yml; `ci-ok` cannot pass when it fails. Sessions can
run the same checks before pushing:

    python3 scripts/ci/pr_guard.py --base origin/main --head-ref claude/<task>

It needs PyYAML (on this PC, run it through `wsl -e python3`).
"""
import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path

import yaml

QUEUE = "docs/WORK-QUEUE.yaml"
STATES = {"ready", "implementing", "review", "integration", "done"}
LEAD_ONLY = (".github/workflows/", "scripts/ci/")
SIZE_NOTICE = 1500
SIZE_LIMIT = 3000
MIN_DEADLINE_MS = 10000
SLEEP = re.compile(r"QTest::qWait\s*\(|QThread::(?:u|m)?sleep\s*\(|this_thread::sleep_|\bSleep\s*\(|\bu?sleep\s*\(")
TIMED_WAIT = re.compile(r"QTRY_\w+_WITH_TIMEOUT\s*\(.*,\s*(\d+)\s*\)")


def is_docs_only(path):
    """A docs-only pull request skips the builds: nothing here reaches a compiler or a test."""
    return path.startswith("docs/") or path.endswith(".md") or path == ".gitattributes"


def counts_toward_size(path):
    return not (is_docs_only(path) or path.endswith((".ts", ".png", ".jpg", ".svg")))


def is_integration_branch(head_ref):
    return head_ref.startswith("lead/integrate-")


class StrictLoader(yaml.SafeLoader):
    pass


def _no_duplicate_keys(loader, node, deep=False):
    mapping = {}
    for key_node, value_node in node.value:
        key = loader.construct_object(key_node, deep=deep)
        if key in mapping:
            raise ValueError(f"duplicate key {key!r} at line {key_node.start_mark.line + 1}")
        mapping[key] = loader.construct_object(value_node, deep=deep)
    return mapping


StrictLoader.add_constructor(yaml.resolver.BaseResolver.DEFAULT_MAPPING_TAG, _no_duplicate_keys)


def parse_queue(text):
    return yaml.load(text, Loader=StrictLoader)


def queue_errors(text):
    try:
        queue = parse_queue(text)
    except (yaml.YAMLError, ValueError) as error:
        return [f"{QUEUE} does not parse: {error}"]
    errors = []
    tasks = queue.get("tasks") or []
    ids = [task.get("id") for task in tasks]
    errors += [f"{QUEUE}: duplicate task {i}" for i in sorted({i for i in ids if ids.count(i) > 1}, key=str)]
    errors += [f"{QUEUE}: {task.get('id')} has unknown state {task.get('state')!r}"
               for task in tasks if task.get("state") not in STATES]
    return errors


def owns_row(head_ref, task):
    if task.get("branch") == head_ref:
        return True
    return head_ref.removeprefix("claude/").startswith(str(task.get("id")).lower())


def queue_scope_errors(base_text, head_text, head_ref):
    """A packet branch edits only its own queue row; the lead edits everything else."""
    if not head_ref.startswith("claude/"):
        return []
    base, head = parse_queue(base_text), parse_queue(head_text)
    errors = []
    if {k: v for k, v in base.items() if k != "tasks"} != {k: v for k, v in head.items() if k != "tasks"}:
        errors.append(f"{QUEUE}: {head_ref} changed a top-level key; only the lead edits those")
    old = {task["id"]: task for task in base.get("tasks") or []}
    new = {task["id"]: task for task in head.get("tasks") or []}
    for task_id in sorted(set(old) | set(new), key=str):
        if old.get(task_id) != new.get(task_id):
            if not owns_row(head_ref, new.get(task_id) or {"id": task_id}) and \
               not owns_row(head_ref, old.get(task_id) or {"id": task_id}):
                errors.append(f"{QUEUE}: {head_ref} changed the row of {task_id}; a packet edits only its own row")
    return errors


def wait_errors(path, added_lines):
    """New test code waits on a condition with a deadline of at least 10 s, never for a fixed time."""
    errors = []
    for line in added_lines:
        if "sleep-ok:" in line:
            continue
        if SLEEP.search(line):
            errors.append(f"{path}: fixed sleep in a test: {line.strip()}")
        timed = TIMED_WAIT.search(line)
        if timed and int(timed.group(1)) < MIN_DEADLINE_MS:
            errors.append(f"{path}: deadline under {MIN_DEADLINE_MS} ms: {line.strip()}")
    return errors


def size_errors(changed_lines, body, head_ref):
    if is_integration_branch(head_ref) or changed_lines <= SIZE_LIMIT or "Large PR:" in (body or ""):
        return []
    return [f"{changed_lines} changed lines is over {SIZE_LIMIT}: split it, or add a 'Large PR: <reason>' line to the body"]


def title_errors(title):
    if re.match(r"^[A-Z][A-Z0-9-]*: \S", title or ""):
        return []
    return [f"PR title {title!r} must be '<TASK-ID>: <title>'"]


def lead_only_errors(paths, head_ref, labels):
    if not head_ref.startswith("claude/") or "lead-grant" in labels:
        return []
    return [f"{path}: lead-owned; a packet needs the lead's lead-grant label to change it"
            for path in paths if path.startswith(LEAD_ONLY)]


def workflow_errors(workflows):
    """A workflow that can reach the self-hosted runner never takes pull requests (public repository)."""
    errors = []
    for name, text in workflows.items():
        data = yaml.safe_load(text) or {}
        triggers = data.get("on", data.get(True)) or {}
        names = [triggers] if isinstance(triggers, str) else list(triggers)
        pull_request = any(t in ("pull_request", "pull_request_target") for t in names)
        if pull_request and "self-hosted" in text:
            errors.append(f"{name}: takes pull requests and can reach the self-hosted runner")
    return errors


def git(*args, check=True):
    return subprocess.run(["git", *args], check=check, capture_output=True, text=True, encoding="utf-8").stdout


def added_lines_by_file(merge_base, head):
    result, path = {}, None
    for line in git("diff", "-U0", merge_base, head, "--", "tests/").splitlines():
        if line.startswith("+++ "):
            path = line[6:] if line.startswith("+++ b/") else None
        elif line.startswith("+") and path:
            result.setdefault(path, []).append(line[1:])
    return result


def changed_size(merge_base, head):
    total = 0
    for line in git("diff", "--numstat", merge_base, head).splitlines():
        added, deleted, path = line.split("\t", 2)
        if added != "-" and counts_toward_size(path):
            total += int(added) + int(deleted)
    return total


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--base", help="base commit or ref of the pull request")
    parser.add_argument("--head", default="HEAD")
    parser.add_argument("--head-ref", default="", help="head branch name")
    args = parser.parse_args()

    event = {}
    if os.environ.get("GITHUB_EVENT_PATH"):
        event = json.loads(Path(os.environ["GITHUB_EVENT_PATH"]).read_text(encoding="utf-8")).get("pull_request") or {}
    labels = {label["name"] for label in event.get("labels", [])}
    if os.environ.get("PR_LABELS"):
        labels |= set(os.environ["PR_LABELS"].split())
    base = args.base or (event.get("base") or {}).get("sha")
    head = (event.get("head") or {}).get("sha") or args.head
    head_ref = args.head_ref or (event.get("head") or {}).get("ref") or ""

    errors = queue_errors(git("show", f"{head}:{QUEUE}"))
    workflow_dir = Path(".github/workflows")
    errors += workflow_errors({p.name: p.read_text(encoding="utf-8") for p in workflow_dir.glob("*.yml")})

    code = True
    if base:
        merge_base = git("merge-base", base, head).strip()
        paths = [p for p in git("diff", "--name-only", merge_base, head).splitlines() if p]
        code = not all(is_docs_only(p) for p in paths)
        size = changed_size(merge_base, head)
        errors += lead_only_errors(paths, head_ref, labels)
        errors += size_errors(size, event.get("body"), head_ref)
        if size > SIZE_NOTICE:
            print(f"::notice::{size} changed lines; aim for about {SIZE_NOTICE}")
        if event:
            errors += title_errors(event.get("title"))
        if QUEUE in paths and not errors:
            errors += queue_scope_errors(git("show", f"{merge_base}:{QUEUE}"), git("show", f"{head}:{QUEUE}"), head_ref)
        if not is_integration_branch(head_ref):
            for path, lines in added_lines_by_file(merge_base, head).items():
                errors += wait_errors(path, lines)

    if os.environ.get("GITHUB_OUTPUT"):
        with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as output:
            output.write(f"code={'true' if code else 'false'}\n")
    for error in errors:
        print(f"::error::{error}")
    print(f"pr_guard: code={code}, {len(errors)} error(s)")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
