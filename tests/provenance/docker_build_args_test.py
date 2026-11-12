"""REL-01: scripts/docker_build_args.py prints build args the Dockerfile accepts, with one instant for the build time
and the created label, and refuses bad input. Run: python3 -I tests/provenance/docker_build_args_test.py <checkout>"""
import re
import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(sys.argv[1] if len(sys.argv) > 1 else Path(__file__).resolve().parents[2])
SCRIPT = ROOT / "scripts" / "docker_build_args.py"


def run(*args):
    return subprocess.run([sys.executable, "-I", str(SCRIPT), *args], capture_output=True, text=True)


def pairs(stdout):
    tokens = stdout.splitlines()
    out = {}
    tags = []
    for flag, value in zip(tokens[::2], tokens[1::2]):
        if flag == "--build-arg":
            name, _, val = value.partition("=")
            out[name] = val
        elif flag == "-t":
            tags.append(value)
        else:
            raise AssertionError(f"unexpected token {flag!r}")
    return out, tags


class DockerBuildArgs(unittest.TestCase):
    def test_version_comes_from_project(self):
        version = re.search(r"project\(Glancelot VERSION (\d+\.\d+\.\d+)", (ROOT / "CMakeLists.txt").read_text()).group(1)
        result = run("--suffix=-dev.2", "--epoch", "86400")
        self.assertEqual(result.returncode, 0, result.stderr)
        args, tags = pairs(result.stdout)
        self.assertEqual(args["GLANCELOT_VERSION"], version + "-dev.2")
        self.assertEqual(args["GLANCELOT_VERSION_SUFFIX"], "-dev.2")
        self.assertEqual(args["GLANCELOT_BUILD_KIND"], "")
        self.assertEqual(tags, [])

    def test_one_instant_for_build_time_and_label(self):
        args, _ = pairs(run("--epoch", "172861").stdout)
        self.assertEqual(args["SOURCE_DATE_EPOCH"], "172861")
        self.assertEqual(args["GLANCELOT_CREATED"], "1970-01-03T00:01:01Z")

    def test_every_arg_is_declared_in_the_dockerfile(self):
        dockerfile = (ROOT / "Dockerfile").read_text()
        args, _ = pairs(run("--epoch", "0").stdout)
        for name in args:
            self.assertRegex(dockerfile, rf"(?m)^ARG {name}\b")
        for label in ("version", "revision", "created"):
            self.assertIn(f"org.opencontainers.image.{label}=", dockerfile)

    def test_bad_suffix_is_refused(self):
        result = run("--suffix=-rc.01")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("-rc.N", result.stderr)

    def test_revision_and_sha_tag_follow_git(self):
        head = subprocess.run(["git", "-C", str(ROOT), "rev-parse", "HEAD"], capture_output=True, text=True)
        top = subprocess.run(["git", "-C", str(ROOT), "rev-parse", "--show-toplevel"], capture_output=True, text=True)
        if head.returncode != 0 or Path(top.stdout.strip()).resolve() != ROOT.resolve():
            result = run("--kind", "release")
            self.assertNotEqual(result.returncode, 0)  # no SHA, no release image
            self.assertIn("Git SHA", result.stderr)
            return
        sha = head.stdout.strip().lower()
        args, tags = pairs(run("--kind", "release", "--image", "viewer", "--epoch", "0").stdout)
        self.assertEqual(args["GLANCELOT_REVISION"], sha)
        self.assertEqual(args["GLANCELOT_BUILD_KIND"], "release")
        self.assertEqual(tags, [f"viewer:{args['GLANCELOT_VERSION']}", f"viewer:sha-{sha}"])


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]], verbosity=2)
