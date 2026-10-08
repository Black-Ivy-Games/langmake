@echo off
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0studio\open-packaged.ps1"
if errorlevel 1 pause
