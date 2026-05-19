# SA2 Cross-Platform Launcher

One-click installer for **Sonic Advance 2** (SAT-R/sa2).  
Clones the repo, installs build dependencies, compiles the SDL port, and drops a desktop shortcut — all automatically.

---

## Quick Start

### macOS / Linux

```bash
python3 launcher/launch.sh
# or directly:
python3 launcher/sa2_launcher.py
```

### Windows

Double-click **`launch.bat`** (requires Python 3.8+ installed).

Or from PowerShell / CMD:
```bat
launcher\launch.bat
```

---

## What it does

| Step | macOS | Linux / WSL | Windows |
|------|-------|-------------|---------|
| Install deps | `brew install libpng sdl2 mingw-w64 arm-none-eabi-gcc` | `apt install …` | `wsl apt install …` |
| Clone repo | `git clone SAT-R/sa2` | same | same (in WSL) |
| Build | `make sdl -jN` | same | `make sdl_win32 -jN` (in WSL) |
| Desktop shortcut | `.app` bundle | `.desktop` file | `.lnk` shortcut |

---

## Options

```
--install-dir PATH   Where to clone the repo (default: ~/sa2)
--cli                Force terminal-only mode (skip the GUI)
```

Environment variable: `SA2_CLI=1` also forces CLI mode.

---

## Requirements

| Platform | Requirement |
|----------|-------------|
| macOS    | Python 3.8+, Homebrew, Xcode CLT |
| Linux    | Python 3.8+, `sudo` access for apt |
| Windows  | Python 3.8+, WSL 2 (Ubuntu) |

The launcher will tell you exactly what to install if anything is missing.
