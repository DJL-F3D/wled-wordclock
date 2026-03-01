"""
pio-scripts/set_version.py
Stamps the firmware binary filename with the git SHA and build date.
Runs as a PlatformIO pre-script (extra_scripts in platformio.ini).
"""
import subprocess, datetime, os

Import("env")  # noqa – PlatformIO provides this

try:
    sha = subprocess.check_output(
        ["git", "rev-parse", "--short", "HEAD"],
        stderr=subprocess.DEVNULL
    ).decode().strip()
except Exception:
    sha = "unknown"

date   = datetime.date.today().strftime("%Y%m%d")
target = env.subst("$PIOENV")
name   = f"wordclock_{target}_{date}_{sha}"

env.Replace(PROGNAME=name)
print(f"[WordClock] firmware name: {name}.bin")
