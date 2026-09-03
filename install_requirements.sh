#!/usr/bin/env bash
#
# Linux counterpart of install_requirements.bat.
# Creates a local venv and installs Python deps used by workshop packaging.
#
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if ! command -v python3 >/dev/null 2>&1; then
    echo "error: python3 was not found on PATH" >&2
    exit 1
fi

python3 -m venv "$root/.venv"
# shellcheck disable=SC1091
source "$root/.venv/bin/activate"
python3 -m pip install -r "$root/requirements.txt"
echo "Python venv ready at $root/.venv"
