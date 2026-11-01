@echo off
setlocal
cd /d "%~dp0"
where py >nul 2>nul
if not errorlevel 1 (
    py -3 "%~dp0host\app.py" --port COM5 --connect
    if errorlevel 1 pause
    exit /b
)
python "%~dp0host\app.py" --port COM5 --connect
if errorlevel 1 pause
