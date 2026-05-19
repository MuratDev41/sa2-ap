#!/usr/bin/env bash
# SA2 Launcher – macOS / Linux entry point
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
python3 "$SCRIPT_DIR/sa2_launcher.py" "$@"
