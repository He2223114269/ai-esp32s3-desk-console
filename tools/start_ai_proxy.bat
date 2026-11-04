@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0run_ai_proxy.ps1"
if errorlevel 1 pause
