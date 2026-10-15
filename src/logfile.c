#include "logfile.h"

#include <fcntl.h>
#include <io.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

/* One newline to end the entry's own line, then three blank lines, so that
   entries are visually separate when the whole file is pasted into a document
   and there is no markup to strip out afterwards. */
#define ENTRY_SEPARATOR "\n\n\n\n"
/* The header rule ends its own line already, so the gap below it is three
   newlines, not four: the same three blank lines that follow an entry. */
#define HEADER_GAP "\n\n\n"
#define UTF8_BOM "\xEF\xBB\xBF"
#define SESSION_RULE "============================\n"

/* Appends one path component, refusing to truncate. */
static BOOL joinPath(wchar_t *out, size_t cch, const wchar_t *base, const wchar_t *leaf) {
    size_t baseLen = wcslen(base);
    size_t leafLen = wcslen(leaf);
    BOOL needsSep = baseLen > 0 && base[baseLen - 1] != L'\\' && base[baseLen - 1] != L'/';

    if (baseLen + (needsSep ? 1u : 0u) + leafLen + 1u > cch) {
        return FALSE;
    }

    memmove(out, base, baseLen * sizeof(wchar_t));
    size_t at = baseLen;
    if (needsSep) {
        out[at++] = L'\\';
    }
    memcpy(out + at, leaf, leafLen * sizeof(wchar_t));
    out[at + leafLen] = L'\0';
    return TRUE;
}

BOOL logBuildFolderPath(wchar_t *out, size_t cch, const wchar_t *desktop) {
    if (out == NULL || cch == 0) {
        return FALSE;
    }
    if (desktop == NULL || desktop[0] == L'\0') {
        /* No Desktop to put it on: fall back to the working directory, which
           is the choice the original made too. */
        if (wcslen(LOG_FOLDER_NAME) + 1 > cch) {
            return FALSE;
        }
        wcscpy(out, LOG_FOLDER_NAME);
        return TRUE;
    }
    return joinPath(out, cch, desktop, LOG_FOLDER_NAME);
}

BOOL logBuildSessionPath(wchar_t *out, size_t cch, const wchar_t *folder,
                         const struct tm *when, int attempt) {
    if (out == NULL || folder == NULL || when == NULL) {
        return FALSE;
    }
    if (attempt == 1 || attempt < 0 || attempt > 99) {
        return FALSE;
    }

    wchar_t name[LOG_NAME_CCH + 1];
    if (wcsftime(name, LOG_NAME_CCH + 1, L"%Y%m%d_%H%M%S", when) != 15) {
        return FALSE;
    }
    if (attempt == 0) {
        wcscat(name, L".txt");
    } else {
        wchar_t suffix[8];
        _snwprintf(suffix, 8, L"_%d.txt", attempt);
        suffix[7] = L'\0';
        wcscat(name, suffix);
    }
    return joinPath(out, cch, folder, name);
}

static BOOL allDigits(const wchar_t *s, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (s[i] < L'0' || s[i] > L'9') {
            return FALSE;
        }
    }
    return TRUE;
}

static int digits(const wchar_t *s, size_t n) {
    int value = 0;
    for (size_t i = 0; i < n; i++) {
        value = value * 10 + (s[i] - L'0');
    }
    return value;
}

BOOL logParseSessionName(const wchar_t *name, struct tm *out) {
    if (name == NULL || out == NULL) {
        return FALSE;
    }

    size_t len = wcslen(name);
    if (len < 19 || len > LOG_NAME_CCH) {
        return FALSE;
    }
    if (!allDigits(name, 8) || name[8] != L'_' || !allDigits(name + 9, 6)) {
        return FALSE;
    }

    /* After the 15-character stamp: either ".txt", or "_<1..2 digits>.txt". */
    const wchar_t *tail = name + 15;
    if (wcscmp(tail, L".txt") != 0) {
        size_t tailLen = wcslen(tail);
        if (tailLen < 6 || tailLen > 7 || tail[0] != L'_') {
            return FALSE;
        }
        size_t nDigits = tailLen - 5;
        if (!allDigits(tail + 1, nDigits) || wcscmp(tail + 1 + nDigits, L".txt") != 0) {
            return FALSE;
        }
        if (digits(tail + 1, nDigits) < 2) {
            return FALSE;
        }
    }

    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year = digits(name, 4) - 1900;
    t.tm_mon = digits(name + 4, 2) - 1;
    t.tm_mday = digits(name + 6, 2);
    t.tm_hour = digits(name + 9, 2);
    t.tm_min = digits(name + 11, 2);
    t.tm_sec = digits(name + 13, 2);
    t.tm_isdst = -1;

    if (t.tm_mon < 0 || t.tm_mon > 11 || t.tm_mday < 1 || t.tm_mday > 31 ||
        t.tm_hour > 23 || t.tm_min > 59 || t.tm_sec > 59 || t.tm_year < 70) {
        return FALSE;
    }

    /* mktime normalises, so 20260231 would come back as 3 March. Reject
       anything that did not survive the round trip: a name that is not a real
       date is not a name this program wrote, and prune only deletes names it
       wrote. */
    struct tm check = t;
    if (mktime(&check) == (time_t)-1 ||
        check.tm_mday != t.tm_mday || check.tm_mon != t.tm_mon) {
        return FALSE;
    }

    *out = t;
    return TRUE;
}

BOOL logIsExpired(const struct tm *stamp, time_t now, int days) {
    if (stamp == NULL || days < 1) {
        return FALSE;
    }

    struct tm copy = *stamp;
    time_t when = mktime(&copy);
    if (when == (time_t)-1) {
        return FALSE;
    }

    return difftime(now, when) > (double)days * 86400.0 ? TRUE : FALSE;
}

BOOL logEnsureFolder(const wchar_t *folder, DWORD *errOut) {
    if (errOut != NULL) {
        *errOut = 0;
    }
    if (folder == NULL) {
        return FALSE;
    }
    if (CreateDirectoryW(folder, NULL)) {
        return TRUE;
    }

    DWORD err = GetLastError();
    if (err == ERROR_ALREADY_EXISTS) {
        return TRUE;
    }
    if (errOut != NULL) {
        *errOut = err;
    }
    return FALSE;
}

/* Creates `path` only if nothing is there. NULL if it exists or cannot be made.
   CreateFileW(CREATE_NEW) is the atomic version of "open unless it exists";
   fopen's "x" mode is not dependable across the CRTs this builds against. */
static FILE *createNew(const wchar_t *path) {
    HANDLE h = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                           CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return NULL;
    }

    int fd = _open_osfhandle((intptr_t)h, _O_WRONLY | _O_TEXT);
    if (fd == -1) {
        CloseHandle(h);
        return NULL;
    }

    FILE *f = _fdopen(fd, "w");
    if (f == NULL) {
        _close(fd); /* owns the handle now */
    }
    return f;
}

BOOL logOpenSession(LogSession *session, const wchar_t *folder,
                    const struct tm *when, long long maxBytes) {
    if (session == NULL || folder == NULL || when == NULL) {
        return FALSE;
    }

    memset(session, 0, sizeof(*session));
    session->maxBytes = maxBytes > 0 ? maxBytes : 0;

    wchar_t path[MAX_PATH];
    FILE *f = NULL;
    for (int attempt = 0; attempt <= 99; attempt++) {
        if (attempt == 1) {
            continue; /* "_1" would be a strange name for the second file */
        }
        if (!logBuildSessionPath(path, MAX_PATH, folder, when, attempt)) {
            return FALSE;
        }
        f = createNew(path);
        if (f != NULL) {
            break;
        }
        if (GetLastError() != ERROR_FILE_EXISTS) {
            return FALSE;
        }
    }
    if (f == NULL) {
        return FALSE;
    }

    wcscpy(session->path, path);

    char stamp[32];
    strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", when);

    fputs(UTF8_BOM, f);
    fputs("Started: ", f);
    fputs(stamp, f);
    fputs("\n", f);
    fputs(SESSION_RULE, f);
    fputs(HEADER_GAP, f);

    fflush(f);
    session->bytes = _ftelli64(f);
    fclose(f);
    return TRUE;
}

BOOL logAppendEntry(LogSession *session, const char *utf8Text) {
    if (session == NULL || utf8Text == NULL || session->path[0] == L'\0') {
        return FALSE;
    }
    if (session->capped) {
        return FALSE;
    }

    if (session->maxBytes > 0 &&
        session->bytes + (long long)strlen(utf8Text) > session->maxBytes) {
        session->capped = TRUE;
        FILE *cap = _wfopen(session->path, L"a");
        if (cap != NULL) {
            fputs(SESSION_RULE, cap);
            fputs("Size limit reached -- nothing after this point was logged.\n", cap);
            fputs("Restart for a new session file, or raise --max-mb.\n", cap);
            fflush(cap);
            session->bytes = _ftelli64(cap);
            fclose(cap);
        }
        return FALSE;
    }

    /* Reopened per entry on purpose: an entry is on disk before the next copy
       happens, so a hard kill loses nothing that was already logged. */
    FILE *f = _wfopen(session->path, L"a");
    if (f == NULL) {
        return FALSE;
    }

    fputs(utf8Text, f);
    fputs(ENTRY_SEPARATOR, f);
    fflush(f);
    session->bytes = _ftelli64(f);
    fclose(f);
    return TRUE;
}

void logWriteClosing(LogSession *session, const struct tm *when) {
    if (session == NULL || session->closed || session->path[0] == L'\0') {
        return;
    }
    session->closed = TRUE;

    FILE *f = _wfopen(session->path, L"a");
    if (f == NULL) {
        return;
    }

    char stamp[32];
    strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", when);
    fputs(SESSION_RULE, f);
    fputs("Closed: ", f);
    fputs(stamp, f);
    fputs("\n", f);
    fflush(f);
    session->bytes = _ftelli64(f);
    fclose(f);
}

static long long fileSize(const WIN32_FIND_DATAW *fd) {
    return ((long long)fd->nFileSizeHigh << 32) | (long long)fd->nFileSizeLow;
}

BOOL logSurveyFolder(const wchar_t *folder, LogSurvey *out) {
    if (folder == NULL || out == NULL) {
        return FALSE;
    }
    memset(out, 0, sizeof(*out));

    wchar_t pattern[MAX_PATH];
    if (!joinPath(pattern, MAX_PATH, folder, L"*.txt")) {
        return FALSE;
    }

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        return TRUE; /* an empty or absent folder is not an error */
    }

    do {
        struct tm stamp;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            continue;
        }
        if (!logParseSessionName(fd.cFileName, &stamp)) {
            continue;
        }

        out->files++;
        out->bytes += fileSize(&fd);
        if (out->oldest[0] == L'\0' || wcscmp(fd.cFileName, out->oldest) < 0) {
            wcscpy(out->oldest, fd.cFileName);
        }
        if (out->newest[0] == L'\0' || wcscmp(fd.cFileName, out->newest) > 0) {
            wcscpy(out->newest, fd.cFileName);
        }
    } while (FindNextFileW(h, &fd));

    FindClose(h);
    return TRUE;
}

int logPrune(const wchar_t *folder, int days, time_t now, const wchar_t *keepPath,
             long long *bytesFreed) {
    if (bytesFreed != NULL) {
        *bytesFreed = 0;
    }
    if (folder == NULL || days < 1) {
        return -1;
    }

    wchar_t pattern[MAX_PATH];
    if (!joinPath(pattern, MAX_PATH, folder, L"*.txt")) {
        return -1;
    }

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        return 0;
    }

    int deleted = 0;
    do {
        struct tm stamp;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            continue;
        }
        if (!logParseSessionName(fd.cFileName, &stamp)) {
            continue;
        }
        if (!logIsExpired(&stamp, now, days)) {
            continue;
        }

        wchar_t full[MAX_PATH];
        if (!joinPath(full, MAX_PATH, folder, fd.cFileName)) {
            continue;
        }
        if (keepPath != NULL && _wcsicmp(full, keepPath) == 0) {
            continue;
        }

        long long size = fileSize(&fd);
        if (DeleteFileW(full)) {
            deleted++;
            if (bytesFreed != NULL) {
                *bytesFreed += size;
            }
        }
    } while (FindNextFileW(h, &fd));

    FindClose(h);
    return deleted;
}
