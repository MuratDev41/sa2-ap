@echo off
:: SA2 Launcher – Windows entry point
:: Requires Python 3.8+ in PATH (or from the Microsoft Store)
setlocal
set SCRIPT_DIR=%~dp0
python "%SCRIPT_DIR%sa2_launcher.py" %*
if errorlevel 1 (
    echo.
    echo [ERROR] Python launcher failed. Make sure Python 3.8+ is installed.
    echo Download from https://python.org
    pause
)
