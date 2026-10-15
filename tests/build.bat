@echo off
REM Builds and runs the tests with MinGW-w64 (gcc on PATH).
REM Run from the repository root:  tests\build.bat

gcc -std=c11 -Wall -Wextra -o tests\clipboard_test.exe ^
    tests\clipboard_test.c src\clip_text.c src\logfile.c src\options.c ^
    -lshell32
if %ERRORLEVEL% NEQ 0 (
    echo Test build failed.
    exit /b 1
)

tests\clipboard_test.exe
exit /b %ERRORLEVEL%
