@echo off
REM Builds clipboard_history.exe with MinGW-w64 (gcc on PATH).
REM Run from the repository root:  src\build.bat

REM The command line is read with GetCommandLineW inside main(), so this does
REM not need -municode: everything is already wide by the time it is parsed.
gcc -std=c11 -Wall -Wextra -o clipboard_history.exe ^
    src\clipboard_history.c src\clip_text.c src\logfile.c src\options.c ^
    -luser32 -lshell32
if %ERRORLEVEL% NEQ 0 (
    echo Build failed.
    exit /b 1
)

echo Built clipboard_history.exe
exit /b 0
