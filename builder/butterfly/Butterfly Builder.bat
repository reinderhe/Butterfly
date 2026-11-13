@echo off
rem Starts Butterfly Builder (ButterflyBuilder.ps1 beside this file) with no console window.
start "" powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File "%~dp0ButterflyBuilder.ps1"
