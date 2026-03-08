"""PlatformIO extra_script: inject GIT_VERSION as a build-time define.

Format:  "v0.0.4" (if on a tag) or "v0.0.4-2-gc020dbc" (commits past tag).
Falls back to the short commit hash if no tags exist.
"""
import subprocess
Import("env")

def git_version():
    try:
        return subprocess.check_output(
            ["git", "describe", "--tags", "--always"],
            stderr=subprocess.DEVNULL
        ).decode().strip()
    except Exception:
        return "unknown"

env.Append(CPPDEFINES=[
    ("GIT_VERSION", env.StringifyMacro(git_version()))
])
