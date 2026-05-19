"""
get_build_flags.py — PlatformIO extra_scripts (pre-build hook).
Injects build-time constants into all build environments:
  - FIRMWARE_VERSION: "<project>-<channel>-<build_number>"
  - GIT_COMMIT:       short git commit hash (8 chars), or "unknown"
  - BUILD_TIMESTAMP:  ISO-8601 UTC timestamp
  - OTA_CHANNEL:      value of env var X4_OTA_CHANNEL, defaulting to "dev"
"""
import subprocess
import datetime
import os

Import("env")  # noqa: F821 — provided by PlatformIO build context


def get_git_commit():
    try:
        result = subprocess.run(
            ["git", "rev-parse", "--short=8", "HEAD"],
            capture_output=True, text=True, timeout=5
        )
        if result.returncode == 0:
            return result.stdout.strip()
    except Exception:
        pass
    return "unknown"


def get_build_number():
    # Use X4_BUILD_NUMBER env var if set; otherwise use 0
    return os.environ.get("X4_BUILD_NUMBER", "0")


channel = os.environ.get("X4_OTA_CHANNEL", "dev")
build_num = get_build_number()
git_commit = get_git_commit()
timestamp = datetime.datetime.utcnow().strftime("%Y-%m-%dT%H:%M:%SZ")
version = "x4-agent-{}-{}".format(channel, build_num)

flags = [
    '-DFIRMWARE_VERSION=\\"{}\\"'.format(version),
    '-DGIT_COMMIT=\\"{}\\"'.format(git_commit),
    '-DBUILD_TIMESTAMP=\\"{}\\"'.format(timestamp),
    '-DCONFIG_OTA_CHANNEL=\\"{}\\"'.format(channel),
]

env.Append(CPPFLAGS=flags)  # noqa: F821
print("[get_build_flags] version={} git={} ts={}".format(version, git_commit, timestamp))
