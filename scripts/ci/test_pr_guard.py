#!/usr/bin/env python3
"""Unit tests for pr_guard.py. Run from the repository root: python3 scripts/ci/test_pr_guard.py"""
import contextlib
import io
import json
import os
import subprocess
import sys
from unittest import mock
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import commit_lint  # noqa: E402
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
    def test_conventional_titles_pass(self):
        for title in ("refactor(ui): per-component commands (EN-07)", "feat(grid): incremental listing (A1)",
                      "docs(queue): PR 9 evidence"):
            self.assertEqual(pr_guard.title_errors(title, 19), [], title)

    def test_old_prs_keep_task_titles(self):
        self.assertEqual(pr_guard.title_errors("EN-07: per-component commands", 18), [])
        self.assertTrue(pr_guard.title_errors("EN-07: per-component commands", 19))
        self.assertTrue(pr_guard.title_errors("EN-07: per-component commands", None))

    def test_other_titles_fail(self):
        for title in ("fix stuff", "en-07: lower", "EN-07 missing colon", "", None, "Feat: capital type"):
            self.assertTrue(pr_guard.title_errors(title, 19), title)


class Commits(unittest.TestCase):
    def test_conventional_subjects_pass(self):
        for message in ("feat(grid): show more rows", "fix: keep the selection", "perf(decode)!: drop the copy",
                        "ci(guard): check commit subjects\n\nBody.\n\nTask: A1\nCo-Authored-By: x <x@y>",
                        "refactor(ui): move handlers\n\nBREAKING CHANGE: none really"):
            self.assertEqual(commit_lint.message_errors(message), [], message)

    def test_other_subjects_fail(self):
        for message in ("A1: show more", "LEAD: PR 9 evidence", "update stuff", "feature(grid): x",
                        "fix(Grid): x", "fix(grid):x", "fix(grid): x.", "fix: " + "x" * 100,
                        "fix(grid): x\nno blank line", "", "fixup! fix(grid): x"):
            self.assertTrue(commit_lint.message_errors(message), message)

    def test_merges_and_local_fixups(self):
        self.assertTrue(commit_lint.message_errors("Merge the two caches"))  # real merges are skipped, not matched
        self.assertTrue(commit_lint.message_errors("Merge the two caches", local=True))  # the hook skips via MERGE_HEAD
        self.assertEqual(commit_lint.message_errors("fixup! fix(grid): x", local=True), [])
        self.assertEqual(commit_lint.message_errors("# comment\n\nfix(grid): x\n# more", local=True), [])


class CommitExemptions(unittest.TestCase):
    def test_git_revert_subject_passes(self):
        self.assertEqual(commit_lint.message_errors('Revert "fix(grid): x"\n\nThis reverts commit abc.'), [])
        self.assertEqual(commit_lint.message_errors('Reapply "fix(grid): x"'), [])
        self.assertEqual(pr_guard.title_errors('Revert "feat(grid): x (A1)"', 30), [])

    def test_legacy_prs(self):
        self.assertTrue(commit_lint.message_errors("A1: show more"))
        self.assertTrue(pr_guard.is_legacy_pr(18))
        self.assertFalse(pr_guard.is_legacy_pr(19))
        self.assertFalse(pr_guard.is_legacy_pr(None))


class CommitRange(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.addCleanup(os.chdir, os.getcwd())
        os.chdir(self.directory.name)
        for args in (("init", "-q"), ("config", "user.email", "test@example.invalid"), ("config", "user.name", "t")):
            subprocess.run(["git", *args], check=True, capture_output=True)

    def commit(self, subject, date):
        env = {**os.environ, "GIT_COMMITTER_DATE": date, "GIT_AUTHOR_DATE": date}
        subprocess.run(["git", "commit", "-q", "--allow-empty", "--no-verify", "-m", subject],
                       check=True, capture_output=True, env=env)

    def test_only_commits_after_the_cutoff_count(self):
        self.commit("base", "2026-11-01T00:00:00Z")
        self.commit("A1: before the rule", "2026-11-05T23:59:59Z")
        self.commit("feat(grid): after the rule", "2026-11-06T00:00:01Z")
        self.assertEqual(commit_lint.range_errors("HEAD~2..HEAD"), [])
        self.commit("A1: after the rule", "2026-11-06T00:00:02Z")
        errors = commit_lint.range_errors("HEAD~3..HEAD")
        self.assertEqual(len(errors), 1)
        self.assertIn("A1: after the rule", errors[0])

    def test_old_prs_and_integration_batches_skip_the_subject_check(self):
        self.commit("base", "2026-11-01T00:00:00Z")
        for subject in ("EN-07: after the rule", "test(EN-07): an old style with an uppercase scope", "wip: x"):
            self.commit(subject, "2026-11-06T00:00:02Z")
        self.assertEqual(pr_guard.commit_errors("HEAD~3", "HEAD", 18, "claude/en-07"), [])
        self.assertEqual(pr_guard.commit_errors("HEAD~3", "HEAD", 30, "lead/integrate-9"), [])
        self.assertEqual(len(pr_guard.commit_errors("HEAD~3", "HEAD", 19, "claude/en-07")), 3)
        self.assertEqual(len(pr_guard.commit_errors("HEAD~3", "HEAD", None, "claude/en-07")), 3)

    def test_ci_reads_the_pr_number_from_the_event(self):
        Path("docs").mkdir()
        Path("docs/WORK-QUEUE.yaml").write_text(QUEUE, encoding="utf-8")
        subprocess.run(["git", "add", "-A"], check=True, capture_output=True)
        self.commit("base", "2026-11-01T00:00:00Z")
        self.commit("test(EN-07): after the rule", "2026-11-06T00:00:02Z")
        sha = lambda ref: subprocess.run(["git", "rev-parse", ref], check=True, capture_output=True, text=True).stdout.strip()
        saved = {k: os.environ.pop(k) for k in ("GITHUB_EVENT_PATH", "GITHUB_OUTPUT", "PR_TITLE", "PR_BODY", "PR_LABELS")
                 if k in os.environ}
        self.addCleanup(os.environ.update, saved)
        self.addCleanup(os.environ.pop, "GITHUB_EVENT_PATH", None)
        for number, expected in ((18, 0), (19, 1)):
            event = {"pull_request": {"number": number, "title": "refactor(ui): move handlers (EN-07)", "body": "",
                                      "labels": [], "base": {"sha": sha("HEAD~1")},
                                      "head": {"sha": sha("HEAD"), "ref": "claude/en-07"}}}
            Path("event.json").write_text(json.dumps(event), encoding="utf-8")
            os.environ["GITHUB_EVENT_PATH"] = str(Path("event.json").resolve())
            with mock.patch.object(sys, "argv", ["pr_guard"]), contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(pr_guard.main(), expected, number)

    def test_main_passes_the_pr_number_through(self):
        Path("docs").mkdir()
        Path("docs/WORK-QUEUE.yaml").write_text(QUEUE, encoding="utf-8")
        subprocess.run(["git", "add", "-A"], check=True, capture_output=True)
        self.commit("base", "2026-11-01T00:00:00Z")
        self.commit("EN-07: after the rule", "2026-11-06T00:00:02Z")
        saved = {k: os.environ.pop(k) for k in ("GITHUB_EVENT_PATH", "GITHUB_OUTPUT") if k in os.environ}
        self.addCleanup(os.environ.update, saved)
        argv = ["pr_guard", "--base", "HEAD~1", "--head", "HEAD", "--head-ref", "claude/en-07", "--pr"]
        for number, expected in (("18", 0), ("19", 1)):
            with mock.patch.object(sys, "argv", argv + [number]), contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(pr_guard.main(), expected, number)


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
