#!/usr/bin/env python3
"""Unit tests for pr_guard.py. Run from the repository root: python3 scripts/ci/test_pr_guard.py"""
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import pr_guard  # noqa: E402

QUEUE = """schema: 1
integration_lock: null
tasks:
  - id: EN-07
    state: ready
  - id: A1
    state: ready
    branch: claude/perf-enum
"""


class DocsOnly(unittest.TestCase):
    def test_docs_and_markdown_skip_the_builds(self):
        for path in ("docs/WORK-QUEUE.yaml", "docs/verification/x/a.png", "README.md", "translations/README.md"):
            self.assertTrue(pr_guard.is_docs_only(path), path)

    def test_everything_else_builds(self):
        for path in ("src/app/a.cpp", "ui/Main.qml", "CMakeLists.txt", ".github/workflows/build.yml",
                     "translations/glancelot_en_XA.ts", "scripts/ci/pr_guard.py", "docker/start.py",
                     ".gitattributes"):
            self.assertFalse(pr_guard.is_docs_only(path), path)

    def test_deleting_a_shipped_doc_builds(self):
        self.assertTrue(pr_guard.is_docs_only("README.md", "M"))
        self.assertFalse(pr_guard.is_docs_only("README.md", "D"))
        self.assertFalse(pr_guard.is_docs_only("THIRD_PARTY_NOTICES.md", "D"))
        self.assertTrue(pr_guard.is_docs_only("docs/a.md", "D"))


class Renames(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.addCleanup(os.chdir, os.getcwd())
        os.chdir(self.directory.name)
        self.run_git("init", "-q")
        self.run_git("config", "user.email", "test@example.invalid")
        self.run_git("config", "user.name", "test")
        Path("CMakeLists.txt").write_text("project(x)\n" * 20)
        Path("docs").mkdir()
        Path("docs/a.md").write_text("a\n" * 20)
        self.run_git("add", "-A")
        self.run_git("commit", "-qm", "base")

    def run_git(self, *args):
        subprocess.run(["git", *args], check=True, capture_output=True)

    def classify(self):
        files = pr_guard.changed_files("HEAD~1", "HEAD")
        return all(pr_guard.is_docs_only(path, status) for status, path in files)

    def test_moving_code_into_docs_is_a_code_change(self):
        self.run_git("mv", "CMakeLists.txt", "docs/CMakeLists.md")
        self.run_git("commit", "-qm", "move")
        self.assertFalse(self.classify())

    def test_moving_docs_within_docs_is_docs_only(self):
        self.run_git("mv", "docs/a.md", "docs/b.md")
        self.run_git("commit", "-qm", "move")
        self.assertTrue(self.classify())


class Queue(unittest.TestCase):
    def test_valid_queue_passes(self):
        self.assertEqual(pr_guard.queue_errors(QUEUE), [])

    def test_duplicate_task_fails(self):
        self.assertTrue(pr_guard.queue_errors(QUEUE + "  - id: A1\n    state: ready\n"))

    def test_duplicate_key_fails(self):
        self.assertTrue(pr_guard.queue_errors(QUEUE + "  - id: B\n    state: ready\n    state: done\n"))

    def test_unknown_state_fails(self):
        self.assertTrue(pr_guard.queue_errors(QUEUE.replace("state: ready", "state: finished", 1)))

    def test_broken_yaml_fails(self):
        self.assertTrue(pr_guard.queue_errors("tasks: [\n"))


class QueueScope(unittest.TestCase):
    def test_packet_edits_row_matched_by_branch_field(self):
        head = QUEUE.replace("state: ready\n    branch", "state: review\n    branch")
        self.assertEqual(pr_guard.queue_scope_errors(QUEUE, head, "claude/perf-enum"), [])

    def test_packet_edits_row_matched_by_id(self):
        head = QUEUE.replace("EN-07\n    state: ready", "EN-07\n    state: review")
        self.assertEqual(pr_guard.queue_scope_errors(QUEUE, head, "claude/en-07-commands"), [])

    def test_packet_cannot_edit_another_row(self):
        head = QUEUE.replace("EN-07\n    state: ready", "EN-07\n    state: done")
        self.assertTrue(pr_guard.queue_scope_errors(QUEUE, head, "claude/perf-enum"))

    def test_packet_cannot_edit_a_top_level_key(self):
        head = QUEUE.replace("integration_lock: null", "integration_lock: x")
        self.assertTrue(pr_guard.queue_scope_errors(QUEUE, head, "claude/perf-enum"))

    def test_lead_may_edit_anything(self):
        head = QUEUE.replace("integration_lock: null", "integration_lock: x").replace("state: ready", "state: done")
        self.assertEqual(pr_guard.queue_scope_errors(QUEUE, head, "lead/queue-update"), [])


class Waits(unittest.TestCase):
    def test_fixed_sleeps_fail(self):
        for line in ("QTest::qWait(100);", "QThread::msleep(5);", "std::this_thread::sleep_for(1s);", "Sleep(10);",
                     "QTest::qSleep(5);", "usleep(5);"):
            self.assertTrue(pr_guard.wait_errors("tests/a.cpp", [line]), line)

    def test_short_deadline_fails(self):
        self.assertTrue(pr_guard.wait_errors("tests/a.cpp", ["QTRY_VERIFY_WITH_TIMEOUT(ready(), 5000);"]))

    def test_other_short_waits_fail(self):
        for line in ("QTest::qWaitFor([&] { return ready(); }, 500);", "QVERIFY(spy.wait(100));"):
            self.assertTrue(pr_guard.wait_errors("tests/a.cpp", [line]), line)

    def test_each_wait_on_a_line_is_read(self):
        ok = "QTRY_VERIFY_WITH_TIMEOUT(a(),10000);QCOMPARE(b().pixelColor(0,0),QColor(Qt::red));"
        self.assertEqual(pr_guard.wait_errors("tests/a.cpp", [ok]), [])
        short = "QTRY_VERIFY_WITH_TIMEOUT(a(),10000);QTRY_VERIFY_WITH_TIMEOUT(b(),100);"
        self.assertTrue(pr_guard.wait_errors("tests/a.cpp", [short]))

    def test_long_deadline_passes(self):
        self.assertEqual(pr_guard.wait_errors("tests/a.cpp", ["QTRY_VERIFY_WITH_TIMEOUT(ready(), 10000);"]), [])

    def test_stated_exception_passes(self):
        self.assertEqual(pr_guard.wait_errors("tests/a.cpp", ["QTest::qWait(50); // sleep-ok: asserts nothing fires"]), [])


class Size(unittest.TestCase):
    def test_over_limit_needs_a_reason(self):
        self.assertTrue(pr_guard.size_errors(3001, "", "claude/x"))
        self.assertEqual(pr_guard.size_errors(3001, "Large PR: one generated table", "claude/x"), [])

    def test_under_limit_passes(self):
        self.assertEqual(pr_guard.size_errors(3000, None, "claude/x"), [])

    def test_integration_batches_are_exempt(self):
        self.assertEqual(pr_guard.size_errors(9000, "", "lead/integrate-7"), [])

    def test_generated_and_docs_do_not_count(self):
        for path in ("translations/glancelot_en_XA.ts", "docs/a.md", "docs/x/a.png"):
            self.assertFalse(pr_guard.counts_toward_size(path), path)
        self.assertTrue(pr_guard.counts_toward_size("tests/a.cpp"))


class Titles(unittest.TestCase):
    def test_task_titles_pass(self):
        for title in ("EN-07: per-component commands", "A1: incremental listing", "LEAD: integrate 7"):
            self.assertEqual(pr_guard.title_errors(title), [], title)

    def test_other_titles_fail(self):
        for title in ("fix stuff", "en-07: lower", "EN-07 missing colon", "", None):
            self.assertTrue(pr_guard.title_errors(title), title)


class LeadOnly(unittest.TestCase):
    PATHS = [".github/workflows/build.yml", "scripts/ci/pr_guard.py", "scripts/lead-merge.ps1"]

    def test_packet_cannot_change_the_gate(self):
        self.assertEqual(len(pr_guard.lead_only_errors(self.PATHS, "claude/x", set())), 3)

    def test_grant_label_allows_it(self):
        self.assertEqual(pr_guard.lead_only_errors(self.PATHS, "claude/x", {"lead-grant"}), [])

    def test_lead_branch_allowed(self):
        self.assertEqual(pr_guard.lead_only_errors(self.PATHS, "lead/x", set()), [])


class Workflows(unittest.TestCase):
    def test_pull_request_with_self_hosted_fails(self):
        for trigger in ("pull_request:", "pull_request_target:"):
            text = f"on:\n  {trigger}\njobs:\n  a:\n    runs-on: [self-hosted, windows]\n"
            self.assertTrue(pr_guard.workflow_errors({"a.yml": text}), trigger)

    def test_other_pull_request_triggers_and_yaml_labels_fail(self):
        text = "on:\n  workflow_run:\njobs:\n  a:\n    runs-on: glancelot-gate\n"
        self.assertTrue(pr_guard.workflow_errors({"a.yaml": text}))

    def test_pull_request_on_matrix_runner_passes(self):
        text = "on:\n  pull_request:\njobs:\n  a:\n    runs-on: ${{ matrix.os }}\n"
        self.assertEqual(pr_guard.workflow_errors({"a.yml": text}), [])

    def test_pull_request_on_hosted_runner_passes(self):
        text = "on:\n  pull_request:\njobs:\n  a:\n    runs-on: windows-2022\n"
        self.assertEqual(pr_guard.workflow_errors({"a.yml": text}), [])

    def test_self_hosted_without_pull_request_passes(self):
        text = "on:\n  push:\n    branches: ['gate/**']\njobs:\n  a:\n    runs-on: [self-hosted]\n"
        self.assertEqual(pr_guard.workflow_errors({"a.yml": text}), [])

    def test_shipped_workflows_pass(self):
        files = Path(".github/workflows").glob("*.yml")
        self.assertEqual(pr_guard.workflow_errors({p.name: p.read_text(encoding="utf-8") for p in files}), [])


if __name__ == "__main__":
    unittest.main()
