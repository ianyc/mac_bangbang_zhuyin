@echo off
rem Uninstall Bangbang Zhuyin. Double-click this file. Your learned choices in %APPDATA%\BangbangZhuyin are kept.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0uninstall.ps1"
echo.
pause
