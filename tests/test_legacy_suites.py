"""Runs the repository's t_*.py regression scripts under unittest discovery.

The t_*.py suites are module-level-assert scripts (they predate this unittest
suite and bootstrap their own stubs), so their t_ prefix never matches the
default ``test*.py`` discovery pattern and a green
``python -m unittest discover -s tests`` used to prove nothing about them.
Each script runs as one subtest here, so one command covers both families.
"""

import pathlib
import subprocess
import sys
import unittest

_TESTS_DIR = pathlib.Path(__file__).resolve().parent


def _crash_note(returncode):
    """Explain Windows fatal NTSTATUS exit codes (e.g. 0xC0000005 access violation)
    so a native interpreter crash is not misread as a failed test assertion."""
    code32 = returncode & 0xFFFFFFFF
    if (-0x80000000 <= returncode <= 0xFFFFFFFF) and (0xC0000000 <= code32 < 0xD0000000):
        return (
            f" (exit code {code32:#010x}: Python interpreter crashed natively, "
            "not a failed assertion; see docs/mechanism/build_deploy.md)"
        )
    return ""


class LegacySuiteTests(unittest.TestCase):
    def test_legacy_assert_scripts_pass(self):
        scripts = sorted(_TESTS_DIR.glob("t_*.py"))
        if not scripts:
            self.skipTest("no t_*.py legacy suites in this checkout")
        for script in scripts:
            with self.subTest(script=script.name):
                result = subprocess.run(
                    [sys.executable, "-X", "faulthandler", str(script)],
                    capture_output=True, text=True, cwd=str(_TESTS_DIR))
                self.assertEqual(
                    result.returncode, 0,
                    f"{script.name} failed{_crash_note(result.returncode)}:\n"
                    f"{result.stdout}\n{result.stderr}")


if __name__ == "__main__":
    unittest.main()
