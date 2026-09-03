#!/usr/bin/env python3
"""Verify that a short smoke cannot be mistaken for release qualification."""

import json
from pathlib import Path
import subprocess
import sys
import tempfile


MINIMUM_RELEASE_SOAK_SECONDS = 12 * 60 * 60


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_soak_release_gate.py <runner> <scratchircd>")

    runner = Path(sys.argv[1]).resolve()
    binary = Path(sys.argv[2]).resolve()
    with tempfile.TemporaryDirectory(prefix="scratchircd-soak-gate-") as temporary:
        report_path = Path(temporary) / "report.json"
        result = subprocess.run(
            [
                sys.executable,
                str(runner),
                str(binary),
                "--release-candidate",
                "--duration-seconds",
                "1",
                "--report",
                str(report_path),
            ],
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=15,
        )

        assert result.returncode != 0, result.stdout
        assert report_path.is_file(), result.stdout
        report = json.loads(report_path.read_text(encoding="utf-8"))
        assert report["schema"] == "scratchircd-milestone1-soak-v2"
        assert report["protocol_limits"] == {
            "nick": 15,
            "user": 10,
            "channel_name": 32,
            "validated": False,
        }
        assert report["passed"] is False
        assert report["release_qualified"] is False
        assert report["qualification"] == {
            "minimum_duration_seconds": MINIMUM_RELEASE_SOAK_SECONDS,
            "preflight_passed": False,
            "requested": True,
        }
        assert any(
            f"at least {MINIMUM_RELEASE_SOAK_SECONDS} seconds" in failure
            for failure in report["failures"]
        ), report["failures"]
        assert report["traffic"]["churn_connections"] == 0
        assert report["samples"] == []


if __name__ == "__main__":
    main()
