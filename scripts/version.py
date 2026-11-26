# PlatformIO pre-build step: stamps the firmware with a version string.
#   CI release builds: PODHUB_VERSION from the git tag (e.g. "v1.0.0")
#   local builds: `git describe`, e.g. "v1.0.0-3-gabc1234-dirty", or "dev"
Import("env")  # noqa: F821  (provided by PlatformIO)

import os
import subprocess

version = os.environ.get("PODHUB_VERSION", "").strip()
if not version:
    try:
        version = subprocess.check_output(
            ["git", "describe", "--tags", "--always", "--dirty"],
            cwd=env.subst("$PROJECT_DIR"),  # noqa: F821
            stderr=subprocess.DEVNULL,
        ).decode().strip()
    except Exception:
        version = "dev"
version = "".join(c for c in version if c.isalnum() or c in ".-_+")[:31] or "dev"
env.Append(CPPDEFINES=[("PODHUB_VERSION", '\\"%s\\"' % version)])  # noqa: F821
print("Pod Hub version: %s" % version)
