"""Copy the built SquillysDelay.vst3 into the system VST3 folder so FL can see it.

Run from anywhere:  python deploy.py
It will pop a UAC admin prompt (needed to write to Program Files).
"""
import ctypes
import os
import shutil
import sys

PROJECT = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(PROJECT, "Builds", "VisualStudio2026", "x64", "Debug", "VST3", "SquillysDelay.vst3")
DEST = r"C:\Program Files\Common Files\VST3\SquillysDelay.vst3"


def main():
    try:
        if not os.path.isdir(SRC):
            print(f"Build output not found:\n  {SRC}\nBuild the solution in VS first.")
            return 1
        if os.path.isdir(DEST):
            shutil.rmtree(DEST)  # remove the old copy so stale files never linger
        shutil.copytree(SRC, DEST)
        print(f"Deployed:\n  {SRC}\n  -> {DEST}")
        return 0
    except Exception as e:
        print(f"Failed: {e}")
        return 1


if __name__ == "__main__":
    if ctypes.windll.shell32.IsUserAnAdmin():
        code = main()
        input("Press Enter to close...")  # keep the elevated window open so you can read the result
        sys.exit(code)
    else:
        # Relaunch this same script with admin rights (triggers the UAC prompt)
        rc = ctypes.windll.shell32.ShellExecuteW(
            None, "runas", sys.executable, f'"{os.path.abspath(__file__)}"', None, 1)
        if rc <= 32:
            sys.exit("Elevation was cancelled or failed - nothing was copied.")
