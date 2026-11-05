#!/usr/bin/env python3
"""Merge-protection checks for pull requests and for main (AGENTS.md, "Pull requests and merging").

CI runs this in the `guard` job of .github/workflows/build.yml; `ci-ok` cannot pass when it fails. Sessions can
run the same checks before pushing:

    python scripts/ci/pr_guard.py --base origin/main --head-ref claude/<task>

It needs PyYAML (pip install pyyaml==6.0.2).
"""
import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path

import yaml

sys.path.insert(0, str(Path(__file__).parent))
import commit_lint  # noqa: E402

QUEUE = "docs/WORK-QUEUE.yaml"
LEGACY_TITLE_LAST_PR = 18
STATES = {"ready", "implementing", "review", "integration", "done"}
LEAD_ONLY = (".github/workflows/", "scripts/ci/", "scripts/lead-")
SHIPPED_DOCS = {"README.md", "THIRD_PARTY_NOTICES.md"}  # Dockerfile and scripts/build.ps1 copy these
HOSTED_RUNNER = re.compile(r"^(ubuntu|windows|macos)-[\w.-]+$")
PR_TRIGGERS = {"pull_request", "pull_request_target", "workflow_run"}
SIZE_NOTICE = 1500
SIZE_LIMIT = 3000
MIN_DEADLINE_MS = 10000
SLEEP = re.compile(r"QTest::qWait\s*\(|\bq?[mu]?[Ss]leep\s*\(|this_thread::sleep_")
TIMED_WAIT = re.compile(r"QTRY_\w+_WITH_TIMEOUT\s*\(.*?,\s*(\d+)\s*\)\s*;|qWaitFor\s*\(.*?,\s*(\d+)\s*\)|\.wait\s*\(\s*(\d+)\s*\)")


def is_docs_only(path, status="M"):
    """A docs-only pull request skips the builds: nothing here reaches a compiler or a test."""
    if path in SHIPPED_DOCS:
        return status != "D"
    return path.startswith("docs/") or path.endswith(".md")


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
    """New test code waits on a condition with a deadline of at least 10 s, never for a fixed time.
    Best effort: plain QTRY_* (5 s default) and multi-line calls stay with the reviewer."""
    errors = []
    for line in added_lines:
        if "sleep-ok:" in line:
            continue
        if SLEEP.search(line):
            errors.append(f"{path}: fixed sleep in a test: {line.strip()}")
        for timed in TIMED_WAIT.finditer(line):
            if int(next(g for g in timed.groups() if g)) < MIN_DEADLINE_MS:
                errors.append(f"{path}: deadline under {MIN_DEADLINE_MS} ms: {line.strip()}")
                break
    return errors


def size_errors(changed_lines, body, head_ref):
    if is_integration_branch(head_ref) or changed_lines <= SIZE_LIMIT or "Large PR:" in (body or ""):
        return []
    return [f"{changed_lines} changed lines is over {SIZE_LIMIT}: split it, or add a 'Large PR: <reason>' line to the body"]


def is_legacy_pr(number):
    return number is not None and number <= LEGACY_TITLE_LAST_PR


def title_errors(title, number=None):
    # PRs opened before the Conventional Commits rule keep their '<TASK-ID>: <title>' titles.
    if is_legacy_pr(number) and commit_lint.LEGACY.match(title or ""):
        return []
    if commit_lint.REVERT.match(title or ""):  # GitHub's Revert button
        return []
    return [f"PR title: {error}" for error in commit_lint.subject_errors(title)]


def commit_errors(merge_base, head, number, head_ref):
    # PRs opened before the rule used several subject styles and can't reword shared history, so their commits
    # aren't checked; the lead's integration batches carry those commits unchanged. Everything else is.
    if is_legacy_pr(number) or is_integration_branch(head_ref):
        return []
    return commit_lint.range_errors(f"{merge_base}..{head}")


def lead_only_errors(paths, head_ref, labels):
    if not head_ref.startswith("claude/") or "lead-grant" in labels:
        return []
    return [f"{path}: lead-owned; a packet needs the lead's lead-grant label to change it"
            for path in paths if path.startswith(LEAD_ONLY)]


def _runner_labels(job):
    runs_on = job.get("runs-on", [])
    if isinstance(runs_on, dict):
        runs_on = runs_on.get("labels", [runs_on.get("group", "")])
    return [runs_on] if isinstance(runs_on, str) else list(runs_on)


def workflow_errors(workflows):
    """A workflow that pull requests can start uses hosted runners only (public repository). This is a backstop:
    a pull request runs its own copy of this file, so the real barrier is the fork approval policy."""
    errors = []
    for name, text in workflows.items():
        data = yaml.safe_load(text) or {}
        triggers = data.get("on", data.get(True)) or {}
        names = [triggers] if isinstance(triggers, str) else list(triggers)
        if not PR_TRIGGERS & set(names):
            continue
        if "self-hosted" in text or "glancelot-gate" in text:
            errors.append(f"{name}: takes pull requests and can reach the self-hosted runner")
            continue
        for job_name, job in (data.get("jobs") or {}).items():
            for label in _runner_labels(job or {}):
                if "${{" not in str(label) and not HOSTED_RUNNER.match(str(label)):
                    errors.append(f"{name}: job {job_name} takes pull requests and runs on {label}")
    return errors


def git(*args):
    return subprocess.run(["git", "-c", "core.quotepath=off", *args], check=True, capture_output=True, text=True,
                          encoding="utf-8", errors="replace").stdout


def changed_files(merge_base, head):
    """(status, path) pairs. Renames count as a delete plus an add, so moving code into docs/ is still a code change."""
    pairs = []
    for line in git("diff", "--name-status", "--no-renames", merge_base, head).splitlines():
        status, path = line.split("\t", 1)
        pairs.append((status, path))
    return pairs


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


def show_queue(commit):
    try:
        return git("show", f"{commit}:{QUEUE}")
    except subprocess.CalledProcessError:
        return None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--base", help="base commit or ref of the pull request")
    parser.add_argument("--head", default="HEAD")
    parser.add_argument("--head-ref", default="", help="head branch name")
    parser.add_argument("--pr", type=int, help="pull request number, for a local run of a PR opened before PR 19")
    args = parser.parse_args()

    event = {}
    if os.environ.get("GITHUB_EVENT_PATH"):
        event = json.loads(Path(os.environ["GITHUB_EVENT_PATH"]).read_text(encoding="utf-8")).get("pull_request") or {}
    # The build.yml step reads these live, because a rerun replays the old event.
    title = os.environ.get("PR_TITLE", event.get("title"))
    body = os.environ.get("PR_BODY", event.get("body"))
    labels = set(os.environ["PR_LABELS"].split()) if "PR_LABELS" in os.environ else \
        {label["name"] for label in event.get("labels", [])}
    base = args.base or (event.get("base") or {}).get("sha")
    head = (event.get("head") or {}).get("sha") or args.head
    head_ref = args.head_ref or (event.get("head") or {}).get("ref") or ""
    number = event.get("number") or args.pr

    errors = []
    head_queue = show_queue(head)
    if head_queue is None:
        errors.append(f"{QUEUE} is missing at {head}")
    else:
        errors += queue_errors(head_queue)
    workflow_dir = Path(".github/workflows")
    workflow_files = [*workflow_dir.glob("*.yml"), *workflow_dir.glob("*.yaml")]
    errors += workflow_errors({p.name: p.read_text(encoding="utf-8") for p in workflow_files})

    code = True
    if base:
        merge_base = git("merge-base", base, head).strip()
        files = changed_files(merge_base, head)
        paths = [path for _, path in files]
        code = not all(is_docs_only(path, status) for status, path in files)
        size = changed_size(merge_base, head)
        errors += lead_only_errors(paths, head_ref, labels)
        errors += size_errors(size, body, head_ref)
        if size > SIZE_NOTICE:
            print(f"::notice::{size} changed lines; aim for about {SIZE_NOTICE}")
        if event:
            errors += title_errors(title, number)
        errors += commit_errors(merge_base, head, number, head_ref)
        base_queue = show_queue(merge_base)
        if QUEUE in paths and base_queue and head_queue and not errors:
            errors += queue_scope_errors(base_queue, head_queue, head_ref)
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
