/*
 * clipboard_history.c -- the Windows half: a message-only window registered as
 * a clipboard format listener, and the glue between it and the log file.
 *
 * The decisions live elsewhere and are tested there: clip_text.c turns a
 * clipboard payload into a UTF-8 entry, logfile.c decides where the file goes
 * and what may be deleted, options.c reads the command line.
 *
 * Two things in here are easy to get wrong and expensive to debug:
 *
 * 1. Everything is wide. The Desktop path comes from SHGetFolderPathW and the
 *    file is opened with _wfopen. The -A variants cannot be used: on an
 *    account whose name is not representable in the system ANSI codepage,
 *    SHGetFolderPathA hands back a path with "?" where those characters were,
 *    "?" is not legal in a Windows filename, and CreateDirectoryA then fails
 *    with ERROR_INVALID_NAME. The original was narrow throughout and logged
 *    nothing at all for such a user.
 *
 * 2. Windows synthesises clipboard formats. An application that puts only
 *    CF_TEXT on the clipboard still answers GetClipboardData(CF_UNICODETEXT),
 *    because the system converts on demand. The CF_TEXT branch below is
 *    therefore close to unreachable in practice -- but when it does run it
 *    converts to UTF-8 like the other branch, so the file has one encoding
 *    throughout rather than two.
 *
 * Build: src\build.bat, or
 *   gcc -std=c11 -o clipboard_history.exe src\*.c -luser32 -lshell32
 */

/* AddClipboardFormatListener / RemoveClipboardFormatListener are Vista+ APIs.
   windows.h only declares them if it knows we are targeting at least Vista,
   and that has to be said before the first Windows header is pulled in. */
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#ifndef WINVER
#define WINVER 0x0600
#endif

#include <windows.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "clip_text.h"
#include "logfile.h"
#include "options.h"

#define MUTEX_NAME L"Local\\ClipboardHistoryLogger-9f2b"
#define HOTKEY_PAUSE 1
#define OPEN_CLIPBOARD_TRIES 5
#define OPEN_CLIPBOARD_WAIT_MS 40

static HWND g_hwnd = NULL;
static LogSession g_session;
static char *g_lastText = NULL;
static volatile LONG g_closedWritten = 0;
static BOOL g_paused = FALSE;

/* Windows lets an app that puts something sensitive on the clipboard -- a
   password manager, a browser's saved-password autofill -- tag the data so
   that history and monitoring tools skip it. These are the two names in use.
   They have to be registered before IsClipboardFormatAvailable can be asked
   about them; unregistered, the IDs stay 0 and nothing is ever detected. */
static UINT g_cfExcludeFromMonitoring = 0; /* "ExcludeClipboardContentFromMonitorProcessing" */
static UINT g_cfCanIncludeInHistory = 0;   /* "CanIncludeInClipboardHistory", DWORD, 0 = excluded */

static struct tm localNow(void) {
    time_t t = time(NULL);
    struct tm value;
    localtime_s(&value, &t);
    return value;
}

/* Writes the closing line once, from whichever of the three paths gets there
   first: the console control handler (on its own thread), WM_DESTROY, or
   atexit. The interlock is what makes that safe. */
static void writeClosingOnce(void) {
    if (InterlockedCompareExchange(&g_closedWritten, 1, 0) != 0) {
        return;
    }
    struct tm now = localNow();
    logWriteClosing(&g_session, &now);
}

/* Ctrl+C, console window close, logoff and shutdown. Returning FALSE lets
   Windows' default handling proceed -- the process still exits, we just get to
   write our line first. A forced kill (taskkill /f) cannot be intercepted by
   any program, so the closing line is missing in that case by design. */
static BOOL WINAPI consoleCtrlHandler(DWORD ctrlType) {
    switch (ctrlType) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            writeClosingOnce();
            return FALSE;
        default:
            return FALSE;
    }
}

/* TRUE if the source app tagged the clipboard contents as excluded.
   Must be called with the clipboard open. */
static BOOL clipboardMarkedSensitive(void) {
    if (g_cfExcludeFromMonitoring != 0 &&
        IsClipboardFormatAvailable(g_cfExcludeFromMonitoring)) {
        return TRUE;
    }

    if (g_cfCanIncludeInHistory != 0 &&
        IsClipboardFormatAvailable(g_cfCanIncludeInHistory)) {
        HANDLE hFlag = GetClipboardData(g_cfCanIncludeInHistory);
        if (hFlag != NULL) {
            void *blob = GlobalLock(hFlag);
            if (blob != NULL) {
                BOOL excluded = clipHistoryFlagExcludes(blob, (size_t)GlobalSize(hFlag));
                GlobalUnlock(hFlag);
                if (excluded) {
                    return TRUE;
                }
            }
        }
    }

    return FALSE;
}

/* The clipboard is a single system-wide resource and the app that just wrote
   to it often still has it open, which is exactly the moment we are woken up.
   The original gave up on the first refusal and dropped the entry silently. */
static BOOL openClipboardWithRetry(void) {
    for (int attempt = 0; attempt < OPEN_CLIPBOARD_TRIES; attempt++) {
        if (OpenClipboard(g_hwnd)) {
            return TRUE;
        }
        Sleep(OPEN_CLIPBOARD_WAIT_MS);
    }
    fprintf(stderr, "WARNING: clipboard stayed busy for %d ms; entry not logged.\n",
            OPEN_CLIPBOARD_TRIES * OPEN_CLIPBOARD_WAIT_MS);
    return FALSE;
}

/* Reads the clipboard as a newly allocated UTF-8 string, or NULL. NULL also
   means "deliberately not logged" when the contents are tagged sensitive. */
static char *readClipboardText(void) {
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT) &&
        !IsClipboardFormatAvailable(CF_TEXT)) {
        return NULL;
    }

    if (!openClipboardWithRetry()) {
        return NULL;
    }

    if (clipboardMarkedSensitive()) {
        CloseClipboard();
        return NULL;
    }

    char *result = NULL;
    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (hData != NULL) {
        wchar_t *wtext = (wchar_t *)GlobalLock(hData);
        if (wtext == NULL) {
            fprintf(stderr, "WARNING: could not lock clipboard data (error %lu); entry not logged.\n",
                    GetLastError());
        } else {
            result = clipWideToUtf8(wtext);
            GlobalUnlock(hData);
            if (result == NULL) {
                fprintf(stderr, "WARNING: could not convert clipboard text to UTF-8; entry not logged.\n");
            }
        }
    } else {
        /* See the note at the top of this file: Windows normally synthesises
           CF_UNICODETEXT, so this branch is all but unreachable. It converts
           anyway so that one file has one encoding. */
        hData = GetClipboardData(CF_TEXT);
        if (hData != NULL) {
            char *atext = (char *)GlobalLock(hData);
            if (atext == NULL) {
                fprintf(stderr, "WARNING: could not lock clipboard data (error %lu); entry not logged.\n",
                        GetLastError());
            } else {
                result = clipAnsiToUtf8(atext, CP_ACP);
                GlobalUnlock(hData);
                if (result == NULL) {
                    fprintf(stderr, "WARNING: could not convert clipboard text to UTF-8; entry not logged.\n");
                }
            }
        }
    }

    CloseClipboard();
    return result;
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CLIPBOARDUPDATE: {
            if (g_paused) {
                return 0;
            }

            char *text = readClipboardText();
            if (text == NULL) {
                return 0;
            }

            if (clipIsNewEntry(text, g_lastText)) {
                if (logAppendEntry(&g_session, text)) {
                    printf("Logged %zu characters.\n", strlen(text));
                    free(g_lastText);
                    g_lastText = _strdup(text);
                } else if (g_session.capped) {
                    printf("Size limit reached. Nothing further will be logged this session.\n");
                } else {
                    fprintf(stderr, "ERROR: could not write to the log file; entry not logged.\n");
                }
            }
            free(text);
            return 0;
        }
        case WM_HOTKEY:
            if (wParam == HOTKEY_PAUSE) {
                g_paused = !g_paused;
                printf(g_paused ? "Paused -- nothing is being logged.\n"
                                : "Resumed -- logging again.\n");
            }
            return 0;
        case WM_DESTROY:
            writeClosingOnce();
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

/* Prints a wide string on a console that has been put into UTF-8 mode. */
static void printWide(const char *prefix, const wchar_t *wide) {
    char *utf8 = clipWideToUtf8(wide);
    printf("%s%s\n", prefix, utf8 != NULL ? utf8 : "<unprintable path>");
    free(utf8);
}

static void describeMegabytes(long long bytes, char *out, size_t cch) {
    double mb = (double)bytes / (1024.0 * 1024.0);
    if (mb >= 0.05) {
        snprintf(out, cch, "%.1f MB", mb);
    } else {
        snprintf(out, cch, "%lld bytes", bytes);
    }
    out[cch - 1] = '\0';
}

int main(void) {
    SetConsoleOutputCP(CP_UTF8);

    /* The command line as UTF-8, so options.c can stay narrow and testable. */
    int wargc = 0;
    wchar_t **wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    char *argv[16];
    int argc = 0;
    for (int i = 0; i < wargc && argc < 16; i++) {
        char *converted = clipWideToUtf8(wargv[i]);
        if (converted == NULL) {
            fprintf(stderr, "ERROR: could not read the command line.\n");
            return 1;
        }
        argv[argc++] = converted;
    }
    if (wargv != NULL) {
        LocalFree(wargv);
    }

    Options options;
    char parseError[128];
    BOOL parsed = optParse(argc, argv, &options, parseError, sizeof(parseError));
    for (int i = 0; i < argc; i++) {
        free(argv[i]);
    }
    if (!parsed) {
        fprintf(stderr, "%s\n\n%s", parseError, optUsage());
        return 2;
    }
    if (options.showHelp) {
        printf("%s", optUsage());
        return 0;
    }

    /* One listener at a time. Two of them means every copy is logged twice,
       and -- because file names have one-second resolution -- a second start
       within the same second used to truncate the first one's file. */
    HANDLE mutex = CreateMutexW(NULL, TRUE, MUTEX_NAME);
    if (mutex == NULL) {
        fprintf(stderr, "ERROR: could not claim the single-instance lock (error %lu).\n", GetLastError());
        return 1;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        fprintf(stderr, "clipboard_history is already running. Stop that one first.\n");
        CloseHandle(mutex);
        return 3;
    }

    g_cfExcludeFromMonitoring = RegisterClipboardFormatW(L"ExcludeClipboardContentFromMonitorProcessing");
    g_cfCanIncludeInHistory = RegisterClipboardFormatW(L"CanIncludeInClipboardHistory");

    wchar_t desktop[MAX_PATH];
    wchar_t folder[MAX_PATH];
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, SHGFP_TYPE_CURRENT, desktop))) {
        desktop[0] = L'\0'; /* logBuildFolderPath falls back to the working directory */
    }
    if (!logBuildFolderPath(folder, MAX_PATH, desktop)) {
        fprintf(stderr, "ERROR: the Desktop path is too long to put a log folder under.\n");
        return 1;
    }

    DWORD folderError = 0;
    if (!logEnsureFolder(folder, &folderError)) {
        printWide("ERROR: could not create ", folder);
        fprintf(stderr, "       (error %lu)\n", folderError);
        return 1;
    }

    if (options.pruneDays > 0) {
        long long freed = 0;
        int deleted = logPrune(folder, options.pruneDays, time(NULL), NULL, &freed);
        if (deleted < 0) {
            fprintf(stderr, "WARNING: could not read the log folder to prune it.\n");
        } else {
            char size[32];
            describeMegabytes(freed, size, sizeof(size));
            printf("Pruned %d log%s older than %d days (%s).\n",
                   deleted, deleted == 1 ? "" : "s", options.pruneDays, size);
        }
    }

    LogSurvey survey;
    if (logSurveyFolder(folder, &survey) && survey.files > 0) {
        char size[32];
        describeMegabytes(survey.bytes, size, sizeof(size));
        printf("This folder already holds %d log%s (%s), oldest %.8ls.\n",
               survey.files, survey.files == 1 ? "" : "s", size, survey.oldest);
        printf("They are plain text and nothing deletes them. --prune-days N does.\n\n");
    }

    struct tm startedAt = localNow();
    if (!logOpenSession(&g_session, folder, &startedAt, options.maxBytes)) {
        printWide("ERROR: could not create a log file in ", folder);
        return 1;
    }

    SetConsoleCtrlHandler(consoleCtrlHandler, TRUE);
    atexit(writeClosingOnce);

    const wchar_t CLASS_NAME[] = L"ClipboardHistoryListener";
    WNDCLASSW wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = CLASS_NAME;
    if (RegisterClassW(&wc) == 0) {
        fprintf(stderr, "ERROR: could not register the listener window class (error %lu).\n", GetLastError());
        writeClosingOnce();
        return 1;
    }

    /* Message-only window: no UI, it exists to receive WM_CLIPBOARDUPDATE. */
    g_hwnd = CreateWindowExW(0, CLASS_NAME, L"ClipboardHistoryListener",
                             0, 0, 0, 0, 0,
                             HWND_MESSAGE, NULL, wc.hInstance, NULL);
    if (g_hwnd == NULL) {
        fprintf(stderr, "ERROR: could not create the listener window (error %lu).\n", GetLastError());
        writeClosingOnce();
        return 1;
    }

    if (!AddClipboardFormatListener(g_hwnd)) {
        fprintf(stderr, "ERROR: could not register as a clipboard listener (error %lu).\n", GetLastError());
        writeClosingOnce();
        return 1;
    }

    BOOL hotkey = RegisterHotKey(g_hwnd, HOTKEY_PAUSE, MOD_CONTROL | MOD_ALT, 'P');

    printf("Logging everything you copy, in plain text, to:\n");
    printWide("  ", g_session.path);
    if (options.maxBytes > 0) {
        printf("Stops at %lld MB. ", options.maxBytes / (1024 * 1024));
    }
    printf(hotkey ? "Ctrl+Alt+P pauses, Ctrl+C stops.\n\n"
                  : "Ctrl+C stops. (Ctrl+Alt+P is taken by another program, so pausing is off.)\n\n");

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (hotkey) {
        UnregisterHotKey(g_hwnd, HOTKEY_PAUSE);
    }
    RemoveClipboardFormatListener(g_hwnd);
    free(g_lastText);
    writeClosingOnce();
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return 0;
}
