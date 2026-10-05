"""
Squishes all the required files into a single folder
to upload to the steam workshop or use in-game
"""

import os
import shutil
import sys

# Paths come from this script's location so it works from any working directory.
ROOT: str = os.path.dirname(os.path.abspath(__file__))
BUILD_FOLDER: str = os.path.join(ROOT, "Build")

# Built binaries that ship, by path under Release/.
RELEASE_FILES: list[str] = [
    "exu.dll",
    "exu.pdb",
]

# Source trees copied whole. Each file keeps its path relative to the tree,
# placed under the given folder of Build/ ("" is the Build/ root).
SOURCE_TREES: list[tuple[str, str]] = [
    # Editor metadata (--- @meta). Kept in their own folder so generic names
    # such as Storage.lua cannot shadow a mission's own module on the Lua path.
    ("Definitions", "Definitions"),
    ("Workshop", ""),
]

# Files moved out of their default place after copying, by Build/-relative path.
RELOCATIONS: dict[str, str] = {
    # RequireFix and ExtraUtils.lua have always been published in Scripts/.
    "RequireFix.lua": os.path.join("Scripts", "RequireFix.lua"),
    os.path.join("Definitions", "ExtraUtils.lua"): os.path.join("Scripts", "ExtraUtils.lua"),
    # exu_weather.lua is a real runtime module, not a @meta definition stub, so it
    # has to sit where a bare require() can find it. Scripts/ is already on the
    # default Lua path - RequireFix.lua lives there and is required by bare name
    # before any path setup runs. At the Build root it would only resolve after an
    # explicit RequireFix.Initialize(), which examples/Weather.lua does not call.
    "exu_weather.lua": os.path.join("Scripts", "exu_weather.lua"),
    # Same for the ring gauge module: required by bare name from mission scripts.
    "exu_ring_gauge.lua": os.path.join("Scripts", "exu_ring_gauge.lua"),
    "monkey.jpg": os.path.join("Assets", "monkey.jpg"),
}


class PackagingError(Exception):
    pass


def plan() -> dict[str, str]:
    """Build/-relative destination -> absolute source, failing on any clash."""
    placements: dict[str, tuple[str, str]] = {}

    def place(destination: str, source: str) -> None:
        # Windows and the game treat names case-insensitively.
        key = os.path.normcase(destination)
        if key in placements:
            raise PackagingError(
                f"two files would be packaged as {destination}: {placements[key][1]} and {source}")
        placements[key] = (destination, source)

    for name in RELEASE_FILES:
        source = os.path.join(ROOT, "Release", name)
        if not os.path.isfile(source):
            raise PackagingError(f"missing {os.path.relpath(source, ROOT)}; build Release|x86 first")
        place(os.path.join("Bin", name), source)

    for tree, target in SOURCE_TREES:
        tree_root = os.path.join(ROOT, tree)
        for directory, _, files in os.walk(tree_root):
            for file in sorted(files):
                source = os.path.join(directory, file)
                destination = os.path.join(target, os.path.relpath(source, tree_root))
                destination = RELOCATIONS.get(destination, destination)
                place(destination, source)

    for relocated in RELOCATIONS.values():
        if os.path.normcase(relocated) not in placements:
            raise PackagingError(f"expected packaged file is missing: {relocated}")

    return {destination: source for destination, source in placements.values()}


def squish() -> None:
    placements = plan()

    # Start from an empty folder so files removed from the sources do not
    # linger in a later upload.
    if os.path.isdir(BUILD_FOLDER):
        shutil.rmtree(BUILD_FOLDER)

    for destination, source in sorted(placements.items()):
        target = os.path.join(BUILD_FOLDER, destination)
        os.makedirs(os.path.dirname(target), exist_ok=True)
        shutil.copyfile(source, target)


def main() -> int:
    try:
        squish()
    except PackagingError as error:
        print(f"Failed to build release: {error}")
        return 1

    print("Built release")
    return 0


if __name__ == "__main__":
    status = main()
    # Keep the console open when run by double-click; scripts and CI pass
    # --no-pause.
    if os.name == "nt" and "--no-pause" not in sys.argv[1:] and not os.environ.get("CI"):
        os.system("pause")
    sys.exit(status)
