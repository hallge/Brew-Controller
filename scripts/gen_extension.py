# PlatformIO pre-script: packs browser-extension/ into .pio/gen/ExtensionZip.h before the firmware is
# built, so the controller serves the extension as it is in the repository (/extension.zip).
Import("env")  # noqa: F821  (SCons)
import os
import subprocess
import sys

project = env["PROJECT_DIR"]  # noqa: F821
subprocess.check_call([sys.executable, os.path.join(project, "browser-extension", "make_zip.py")], cwd=project)
