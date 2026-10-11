@echo off
rem Build if needed, start the emulator and open http://localhost:8090
"%~dp0..\.venv\Scripts\python.exe" "%~dp0build.py" --run %*
