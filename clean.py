"""
Clears the Workshop output directory
"""

import os
import shutil
import sys

# Paths come from this script's location so it works from any working directory.
ROOT: str = os.path.dirname(os.path.abspath(__file__))
BUILD_FOLDER = os.path.join(ROOT, "Build")


def clean():
    if os.path.isdir(BUILD_FOLDER):
        shutil.rmtree(BUILD_FOLDER)
    print("Cleaned build folder")
    if os.name == "nt" and "--no-pause" not in sys.argv[1:] and not os.environ.get("CI"):
        os.system("pause")


if __name__ == "__main__":
    clean()
    