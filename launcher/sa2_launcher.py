#!/usr/bin/env python3
"""
SA2 (Sonic Advance 2) Cross-Platform Launcher
=============================================
Clones the SAT-R/sa2 repo, installs dependencies,
builds the SDL port, and creates a desktop shortcut.

Supports: macOS, Linux (including WSL), Windows
"""

import sys
import os
import platform
import subprocess
import shutil
import textwrap
import threading
import time
from pathlib import Path

# ─────────────────────────────────────────────
#  Guard: require Python 3.8+
# ─────────────────────────────────────────────
if sys.version_info < (3, 8):
    sys.exit("Python 3.8 or newer is required.")

# ─────────────────────────────────────────────
#  Try to import Tkinter (optional fancy UI)
#
#  On some macOS builds Tkinter calls abort() at
#  the C level before Python can catch anything.
#  We probe it in a child process first so a crash
#  there never kills the launcher itself.
# ─────────────────────────────────────────────
def _probe_tkinter() -> bool:
    """Return True only if tkinter can be imported without crashing."""
    try:
        probe = subprocess.run(
            [sys.executable, "-c",
             "import tkinter; tkinter.Tk().destroy()"],
            timeout=6,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        return probe.returncode == 0
    except Exception:
        return False

if "--cli" not in sys.argv and os.environ.get("SA2_CLI") != "1" and _probe_tkinter():
    import tkinter as tk
    from tkinter import ttk, messagebox, filedialog
    HAS_TK = True
else:
    HAS_TK = False

# ─────────────────────────────────────────────
#  Constants
# ─────────────────────────────────────────────
REPO_URL  = "https://github.com/MuratDev41/sa2-ap.git"
REPO_NAME = "sa2-ap"
APP_NAME  = "Sonic Advance 2"
VERSION   = "1.0.0"

SYSTEM  = platform.system()   # "Darwin" | "Linux" | "Windows"
HOME    = Path.home()
DESKTOP = HOME / "Desktop"


# ─────────────────────────────────────────────────────────────────────────────
#  Utility helpers
# ─────────────────────────────────────────────────────────────────────────────

def run(cmd, cwd=None, env=None, log=None, check=True):
    """Run a shell command, streaming output to `log` callable if given."""
    if log:
        log(f"$ {cmd}")
    proc = subprocess.Popen(
        cmd, shell=True,
        cwd=str(cwd) if cwd else None,
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    output_lines = []
    for line in proc.stdout:
        line = line.rstrip()
        output_lines.append(line)
        if log:
            log(line)
    proc.wait()
    if check and proc.returncode != 0:
        raise subprocess.CalledProcessError(proc.returncode, cmd)
    return "\n".join(output_lines)


def which(cmd):
    return shutil.which(cmd)


def require(cmd, install_hint):
    if not which(cmd):
        raise EnvironmentError(f"'{cmd}' not found.\n{install_hint}")


def nproc():
    try:
        return os.cpu_count() or 4
    except Exception:
        return 4


# ─────────────────────────────────────────────────────────────────────────────
#  Build steps
# ─────────────────────────────────────────────────────────────────────────────

class Step:
    def __init__(self, name):
        self.name = name
    def run(self, ctx, log):
        raise NotImplementedError


class StepCheckDependencies(Step):
    def __init__(self):
        super().__init__("Check core dependencies")

    def run(self, ctx, log):
        system = ctx["system"]
        if system == "Darwin":
            require("brew",
                "Homebrew is required. Install from https://brew.sh")
            require("git", "Run: brew install git")
        elif system == "Linux":
            require("git",  "Run: sudo apt install git")
            require("make", "Run: sudo apt install build-essential")
        elif system == "Windows":
            log("Windows detected – using WSL for build.")
            if not which("wsl"):
                raise EnvironmentError(
                    "WSL is not installed.\n"
                    "Enable it with:  wsl --install\n"
                    "Restart and re-run this launcher.")
        log("✔ Core dependency check passed.")


class StepInstallDeps(Step):
    def __init__(self):
        super().__init__("Install build dependencies")

    def run(self, ctx, log):
        system = ctx["system"]
        if system == "Darwin":
            for pkg in ["libpng", "sdl2", "mingw-w64", "arm-none-eabi-gcc"]:
                log(f"  brew install {pkg} …")
                run(f"brew install {pkg}", log=log, check=False)
        elif system == "Linux":
            run("sudo apt-get update -y", log=log, check=False)
            pkgs = ("build-essential binutils-arm-none-eabi gcc-arm-none-eabi "
                    "libpng-dev xorg-dev libsdl2-dev g++-mingw-w64 "
                    "gcc-mingw-w64 libarchive-tools")
            run(f"sudo apt-get install -y {pkgs}", log=log, check=False)
        elif system == "Windows":
            pkgs = ("build-essential binutils-arm-none-eabi gcc-arm-none-eabi "
                    "libpng-dev xorg-dev libsdl2-dev g++-mingw-w64 "
                    "gcc-mingw-w64 libarchive-tools")
            run(f'wsl bash -lc "sudo apt-get update -y && sudo apt-get install -y {pkgs}"',
                log=log, check=False)
        log("✔ Build dependencies installed.")


class StepCloneRepo(Step):
    def __init__(self):
        super().__init__("Clone / update repository")

    def run(self, ctx, log):
        install_dir = ctx["install_dir"]
        if (install_dir / ".git").exists():
            log(f"  Repo already cloned – pulling latest changes …")
            run("git pull --rebase", cwd=install_dir, log=log, check=False)
        else:
            log(f"  Cloning {REPO_URL} → {install_dir} …")
            run(f'git clone --depth=1 "{REPO_URL}" "{install_dir}"', log=log)
        log("✔ Repository ready.")


# Pinned commits matching the working tree that was used to develop this build.
# Each entry: (subdir_in_ext, git_url, commit_sha)
_EXT_DEPS = [
    ("imgui",       "https://github.com/ocornut/imgui.git",
                    "00ad3c65bc256a16521288505f26fb335440f8f5"),
    ("apclientpp",  "https://github.com/black-sliver/apclientpp.git",
                    "79621690a3e845645f43888b0fe234a99c74892e"),
    ("websocketpp", "https://github.com/zaphoyd/websocketpp.git",
                    "4dfe1be74e684acca19ac1cf96cce0df9eac2a2d"),
    ("wswrap",      "https://github.com/black-sliver/wswrap.git",
                    "aeba7ac428028723fb26ce92488f260660f786b1"),
    ("valijson",    "https://github.com/tristanpenman/valijson.git",
                    "b12841e3fa23a8cc477179face0a5ce5f80b64ab"),
]
# nlohmann/json is a single header – download directly.
_NLOHMANN_URL = ("https://raw.githubusercontent.com/nlohmann/json/"
                 "v3.11.3/single_include/nlohmann/json.hpp")


class StepFetchExtDeps(Step):
    def __init__(self):
        super().__init__("Fetch third-party ext/ dependencies")

    def run(self, ctx, log):
        system  = ctx["system"]
        ext_dir = ctx["install_dir"] / "ext"
        ext_dir.mkdir(exist_ok=True)

        for name, url, sha in _EXT_DEPS:
            dest = ext_dir / name
            if dest.exists():
                log(f"  ext/{name} already present – skipping.")
                continue
            log(f"  Cloning {name} …")
            run(f'git clone "{url}" "{dest}"', log=log)
            # Checkout the pinned commit so the build is reproducible
            run(f'git -C "{dest}" checkout {sha}', log=log)

        # nlohmann/json single header
        nlohmann_dir  = ext_dir / "nlohmann"
        nlohmann_file = ext_dir / "json.hpp"
        if not nlohmann_file.exists():
            log("  Downloading nlohmann/json.hpp …")
            if system == "Windows":
                run(f'wsl bash -lc "curl -fsSL {_NLOHMANN_URL} -o \\\"{ _win_to_wsl(nlohmann_file) }\\\""',
                    log=log, check=False)
            else:
                run(f'curl -fsSL "{_NLOHMANN_URL}" -o "{nlohmann_file}"', log=log, check=False)
        else:
            log("  ext/json.hpp already present – skipping.")

        if not nlohmann_dir.exists():
            nlohmann_dir.mkdir(exist_ok=True)
            import shutil as _shutil
            _shutil.copy2(nlohmann_file, nlohmann_dir / "json.hpp")

        log("✔ ext/ dependencies ready.")


class StepBuild(Step):
    def __init__(self):
        super().__init__("Build SA2 SDL port")

    def run(self, ctx, log):
        system = ctx["system"]
        install_dir = ctx["install_dir"]
        j = nproc()

        if system in ("Darwin", "Linux"):
            run(f"make sdl -j{j}", cwd=install_dir, log=log)
            exe = install_dir / "sa2.sdl"
        elif system == "Windows":
            wsl_path = _win_to_wsl(install_dir)
            run(f'wsl bash -lc "cd \\"{wsl_path}\\" && make SDL2.dll -j{j} && make sdl_win32 -j{j}"',
                log=log)
            exe = install_dir / "sa2.sdl_win32.exe"

        if not exe.exists():
            raise FileNotFoundError(
                f"Build finished but binary not found: {exe}\n"
                "Check the log for compiler errors.")
        ctx["executable"] = exe
        log(f"✔ Build complete → {exe}")


class StepDesktopShortcut(Step):
    def __init__(self):
        super().__init__("Create desktop shortcut")

    def run(self, ctx, log):
        system = ctx["system"]
        exe = ctx["executable"]
        idir = ctx["install_dir"]

        if system == "Darwin":
            _shortcut_macos(exe, idir, log)
        elif system == "Linux":
            _shortcut_linux(exe, idir, log)
        elif system == "Windows":
            _shortcut_windows(exe, idir, log)
        log("✔ Desktop shortcut created.")


# ─────────────────────────────────────────────────────────────────────────────
#  Shortcut implementations
# ─────────────────────────────────────────────────────────────────────────────

def _shortcut_macos(exe: Path, idir: Path, log):
    app = DESKTOP / f"{APP_NAME}.app"
    macos = app / "Contents" / "MacOS"
    res   = app / "Contents" / "Resources"
    macos.mkdir(parents=True, exist_ok=True)
    res.mkdir(parents=True, exist_ok=True)

    launcher = macos / APP_NAME
    launcher.write_text(textwrap.dedent(f"""\
        #!/bin/bash
        cd "{idir}"
        exec "{exe}"
    """))
    launcher.chmod(0o755)

    (app / "Contents" / "Info.plist").write_text(textwrap.dedent(f"""\
        <?xml version="1.0" encoding="UTF-8"?>
        <!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
            "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
        <plist version="1.0">
        <dict>
            <key>CFBundleExecutable</key>   <string>{APP_NAME}</string>
            <key>CFBundleIdentifier</key>   <string>com.sat-r.sa2</string>
            <key>CFBundleName</key>         <string>{APP_NAME}</string>
            <key>CFBundleVersion</key>      <string>{VERSION}</string>
            <key>CFBundlePackageType</key>  <string>APPL</string>
            <key>CFBundleIconFile</key>     <string>AppIcon</string>
            <key>LSMinimumSystemVersion</key><string>11.0</string>
            <key>NSHighResolutionCapable</key><true/>
        </dict>
        </plist>
    """))

    # Try to convert the title screen PNG to ICNS
    for ic in [idir / ".github" / "media" / "titlescreen.png"]:
        if ic.exists() and which("sips") and which("iconutil"):
            _make_icns(ic, res / "AppIcon.icns", log)
            break

    log(f"  Created {app}")


def _make_icns(png: Path, out: Path, log):
    try:
        iconset = out.with_suffix(".iconset")
        iconset.mkdir(exist_ok=True)
        for s in [16, 32, 64, 128, 256, 512]:
            run(f'sips -z {s} {s} "{png}" --out "{iconset}/icon_{s}x{s}.png"', check=False)
            run(f'sips -z {s*2} {s*2} "{png}" --out "{iconset}/icon_{s}x{s}@2x.png"', check=False)
        run(f'iconutil -c icns "{iconset}" -o "{out}"', check=False)
        shutil.rmtree(iconset, ignore_errors=True)
    except Exception as e:
        log(f"  (icon conversion skipped: {e})")


def _shortcut_linux(exe: Path, idir: Path, log):
    icon = next(
        (str(p) for p in [idir / ".github" / "media" / "titlescreen.png"] if p.exists()),
        "application-x-executable"
    )
    df = DESKTOP / f"{APP_NAME}.desktop"
    df.write_text(textwrap.dedent(f"""\
        [Desktop Entry]
        Version=1.0
        Type=Application
        Name={APP_NAME}
        Comment=Sonic Advance 2 SDL Port
        Exec=bash -c 'cd "{idir}" && "{exe}"'
        Icon={icon}
        Terminal=false
        Categories=Game;
        StartupWMClass=sa2
    """))
    df.chmod(0o755)
    if which("xdg-desktop-menu"):
        run(f'xdg-desktop-menu install --novendor "{df}"', check=False)
    log(f"  Created {df}")


def _shortcut_windows(exe: Path, idir: Path, log):
    sc = DESKTOP / f"{APP_NAME}.lnk"
    ps = (
        f"$ws = New-Object -ComObject WScript.Shell; "
        f"$s = $ws.CreateShortcut('{sc}'); "
        f"$s.TargetPath = '{exe}'; "
        f"$s.WorkingDirectory = '{idir}'; "
        f"$s.Description = '{APP_NAME} SDL Port'; "
        f"$s.Save()"
    )
    run(f'powershell -NoProfile -Command "{ps}"', log=log, check=False)
    log(f"  Created {sc}")


def _win_to_wsl(p: Path) -> str:
    s = str(p).replace("\\", "/")
    if len(s) > 1 and s[1] == ":":
        s = f"/mnt/{s[0].lower()}/{s[3:]}"
    return s


# ─────────────────────────────────────────────────────────────────────────────
#  Pipeline runner
# ─────────────────────────────────────────────────────────────────────────────

STEPS = [
    StepCheckDependencies(),
    StepInstallDeps(),
    StepCloneRepo(),
    StepFetchExtDeps(),
    StepBuild(),
    StepDesktopShortcut(),
]


def run_pipeline(install_dir: Path, log, done_callback=None):
    ctx = {"system": SYSTEM, "install_dir": install_dir, "executable": None}
    try:
        for step in STEPS:
            log(f"\n{'─'*60}")
            log(f"  ▶  {step.name}")
            log(f"{'─'*60}")
            step.run(ctx, log)

        log(f"\n{'═'*60}")
        log(f"  ✅  All done!  {APP_NAME} is ready.")
        log(f"  Binary : {ctx.get('executable', 'N/A')}")
        log(f"  A desktop shortcut has been placed on your Desktop.")
        log(f"{'═'*60}\n")
        if done_callback:
            done_callback(success=True, message="Installation complete!")
    except Exception as exc:
        log(f"\n❌  ERROR: {exc}")
        if done_callback:
            done_callback(success=False, message=str(exc))


# ─────────────────────────────────────────────────────────────────────────────
#  GUI
# ─────────────────────────────────────────────────────────────────────────────

class LauncherGUI:
    BG      = "#0d1117"
    PANEL   = "#161b22"
    ACCENT  = "#58a6ff"
    GREEN   = "#3fb950"
    FG      = "#e6edf3"
    FG_DIM  = "#8b949e"
    RED     = "#f85149"
    FONT    = "Helvetica"

    def __init__(self):
        self.root = tk.Tk()
        self.root.title(f"{APP_NAME}  ·  Installer")
        self.root.configure(bg=self.BG)
        self.root.resizable(True, True)
        self._center(860, 620)
        self._build_ui()

    def _center(self, w, h):
        sw, sh = self.root.winfo_screenwidth(), self.root.winfo_screenheight()
        self.root.geometry(f"{w}x{h}+{(sw-w)//2}+{(sh-h)//2}")

    def _build_ui(self):
        root = self.root

        # ── Gradient-ish header ────────────────────────────────────────────
        hdr = tk.Frame(root, bg=self.PANEL, pady=24)
        hdr.pack(fill="x")

        tk.Label(hdr, text="🦔  Sonic Advance 2",
                 font=(self.FONT, 30, "bold"),
                 bg=self.PANEL, fg=self.ACCENT).pack()
        tk.Label(hdr, text="Cross-Platform Installer",
                 font=(self.FONT, 13),
                 bg=self.PANEL, fg=self.FG_DIM).pack(pady=(4, 0))

        # Thin accent stripe
        tk.Frame(root, bg=self.ACCENT, height=2).pack(fill="x")

        # ── Body ──────────────────────────────────────────────────────────
        body = tk.Frame(root, bg=self.BG, padx=28, pady=18)
        body.pack(fill="both", expand=True)

        # Dir selector
        drow = tk.Frame(body, bg=self.BG)
        drow.pack(fill="x", pady=(0, 14))
        tk.Label(drow, text="Install directory:",
                 font=(self.FONT, 11), bg=self.BG, fg=self.FG).pack(side="left")
        self.dir_var = tk.StringVar(value=str(HOME / REPO_NAME))
        tk.Entry(drow, textvariable=self.dir_var,
                 font=(self.FONT, 11),
                 bg=self.PANEL, fg=self.FG, insertbackground=self.FG,
                 relief="flat", bd=6, width=44
                 ).pack(side="left", padx=10, ipady=4)
        tk.Button(drow, text="Browse…",
                  font=(self.FONT, 10),
                  bg=self.ACCENT, fg="white",
                  activebackground="#79c0ff",
                  relief="flat", bd=0, padx=10, pady=4,
                  cursor="hand2", command=self._browse
                  ).pack(side="left")

        # Info panel
        info = tk.Frame(body, bg=self.PANEL, padx=16, pady=12)
        info.pack(fill="x", pady=(0, 14))
        for lbl, val in [
            ("Repository",   REPO_URL),
            ("Platform",     SYSTEM),
            ("Build target", "sa2.sdl" if SYSTEM in ("Darwin","Linux")
                              else "sa2.sdl_win32.exe"),
            ("CPU cores",    f"{nproc()} (parallel make)"),
        ]:
            r = tk.Frame(info, bg=self.PANEL)
            r.pack(fill="x", pady=2)
            tk.Label(r, text=f"{lbl}:", width=16, anchor="w",
                     font=(self.FONT, 10, "bold"),
                     bg=self.PANEL, fg=self.FG_DIM).pack(side="left")
            tk.Label(r, text=val, anchor="w",
                     font=(self.FONT, 10),
                     bg=self.PANEL, fg=self.FG).pack(side="left")

        # Progress
        self.progress_var = tk.DoubleVar()
        style = ttk.Style()
        style.theme_use("clam")
        style.configure("SA2.Horizontal.TProgressbar",
                         troughcolor=self.PANEL,
                         background=self.GREEN,
                         bordercolor=self.PANEL,
                         lightcolor=self.GREEN,
                         darkcolor=self.GREEN)
        self.progress = ttk.Progressbar(
            body, variable=self.progress_var,
            maximum=100, mode="indeterminate",
            length=780, style="SA2.Horizontal.TProgressbar")
        self.progress.pack(fill="x", pady=(0, 6))

        self.status_var = tk.StringVar(value="Ready to install.")
        tk.Label(body, textvariable=self.status_var,
                 font=(self.FONT, 10, "italic"),
                 bg=self.BG, fg=self.FG_DIM).pack(anchor="w")

        # Log console
        lf = tk.Frame(body, bg=self.BG)
        lf.pack(fill="both", expand=True, pady=8)
        self.log_text = tk.Text(
            lf, font=("Courier", 9),
            bg="#010409", fg="#c9d1d9",
            insertbackground="#c9d1d9",
            relief="flat", state="disabled",
            wrap="word", bd=0)
        sb = ttk.Scrollbar(lf, command=self.log_text.yview)
        self.log_text.configure(yscrollcommand=sb.set)
        sb.pack(side="right", fill="y")
        self.log_text.pack(side="left", fill="both", expand=True)

        # ── Footer ────────────────────────────────────────────────────────
        foot = tk.Frame(root, bg=self.BG, padx=28, pady=14)
        foot.pack(fill="x")

        self.install_btn = tk.Button(
            foot, text="▶  Install & Build",
            font=(self.FONT, 13, "bold"),
            bg=self.GREEN, fg="white",
            activebackground="#56d364",
            relief="flat", bd=0,
            padx=28, pady=12,
            cursor="hand2",
            command=self._start_install)
        self.install_btn.pack(side="left")

        tk.Button(foot, text="Quit",
                  font=(self.FONT, 11),
                  bg=self.PANEL, fg=self.FG_DIM,
                  activebackground=self.RED, activeforeground="white",
                  relief="flat", bd=0,
                  padx=18, pady=12,
                  cursor="hand2",
                  command=root.destroy).pack(side="right")

        tk.Label(foot,
                 text=f"v{VERSION}  ·  github.com/MuratDev41/sa2-ap",
                 font=(self.FONT, 9),
                 bg=self.BG, fg=self.FG_DIM).pack(side="right", padx=20)

    def _browse(self):
        d = filedialog.askdirectory(initialdir=self.dir_var.get())
        if d:
            self.dir_var.set(d)

    def _log(self, msg: str):
        def _a():
            self.log_text.configure(state="normal")
            self.log_text.insert("end", msg + "\n")
            self.log_text.see("end")
            self.log_text.configure(state="disabled")
        self.root.after(0, _a)

    def _start_install(self):
        idir = Path(self.dir_var.get()).expanduser().resolve()
        self.install_btn.configure(state="disabled")
        self.progress.configure(mode="indeterminate")
        self.progress.start(12)
        self.status_var.set("Installing …")
        threading.Thread(
            target=run_pipeline,
            kwargs=dict(install_dir=idir, log=self._log,
                        done_callback=self._on_done),
            daemon=True).start()

    def _on_done(self, success: bool, message: str):
        def _u():
            self.progress.stop()
            self.progress.configure(mode="determinate")
            self.progress_var.set(100 if success else 0)
            self.install_btn.configure(state="normal")
            if success:
                self.status_var.set("✅  Installation complete!")
                messagebox.showinfo("Done!",
                    f"{APP_NAME} installed successfully.\n"
                    "A shortcut has been placed on your Desktop.")
            else:
                self.status_var.set(f"❌  {message}")
                messagebox.showerror("Installation failed", message)
        self.root.after(0, _u)

    def run(self):
        self.root.mainloop()


# ─────────────────────────────────────────────────────────────────────────────
#  CLI fallback
# ─────────────────────────────────────────────────────────────────────────────

def cli_main():
    import argparse
    p = argparse.ArgumentParser(description=f"{APP_NAME} – Cross-platform installer")
    p.add_argument("--install-dir",
                   default=str(HOME / REPO_NAME),
                   help=f"Where to clone/build (default: ~/{REPO_NAME})")
    args = p.parse_args()
    idir = Path(args.install_dir).expanduser().resolve()
    print(f"\n{'═'*60}")
    print(f"  {APP_NAME}  Installer  v{VERSION}")
    print(f"  Install dir : {idir}")
    print(f"  Platform    : {SYSTEM}")
    print(f"{'═'*60}\n")
    run_pipeline(idir, log=print)


# ─────────────────────────────────────────────────────────────────────────────
#  Entry point
# ─────────────────────────────────────────────────────────────────────────────

if __name__ == "__main__":
    if HAS_TK:
        try:
            LauncherGUI().run()
        except Exception:
            cli_main()
    else:
        cli_main()
