/*
 * Tests for src/clip_text.c, src/logfile.c and src/options.c.
 *
 *   tests\build.bat
 *
 * No framework. The project had no dependencies and does not want any, so this
 * is sixty lines of harness and then the checks.
 *
 * The project had no tests at all before this pass, which is how it shipped a
 * log file that Notepad renders as mojibake and a second instance that wiped
 * the first one's file. Those two are pinned here as REGRESSION checks, along
 * with the original line each one replaces, so that a future change cannot
 * quietly undo them without a name and a reason appearing in the output.
 *
 * The file-writing tests use a scratch folder under %TEMP% and delete it at
 * the end. One of them deliberately creates a folder whose name is not
 * representable in the system ANSI codepage.
 */

#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <time.h>

#include "../src/clip_text.h"
#include "../src/logfile.h"
#include "../src/options.h"

/* ---------- harness ---------- */

static int checks = 0;
static int failures = 0;
static const char *current = "";

static void group(const char *name) {
    current = name;
    printf("\n%s\n", name);
}

static void ok(int condition, const char *what) {
    checks++;
    if (condition) {
        printf("  ok    %s\n", what);
    } else {
        failures++;
        printf("  FAIL  %s  [%s]\n", what, current);
    }
}

static void eq_str(const char *actual, const char *expected, const char *what) {
    checks++;
    if (actual != NULL && strcmp(actual, expected) == 0) {
        printf("  ok    %s\n", what);
    } else {
        failures++;
        printf("  FAIL  %s  [%s]\n        expected \"%s\"\n        got      \"%s\"\n",
               what, current, expected, actual != NULL ? actual : "(null)");
    }
}

static void eq_wstr(const wchar_t *actual, const wchar_t *expected, const char *what) {
    checks++;
    if (actual != NULL && wcscmp(actual, expected) == 0) {
        printf("  ok    %s\n", what);
    } else {
        failures++;
        printf("  FAIL  %s  [%s]\n        expected \"%ls\"\n        got      \"%ls\"\n",
               what, current, expected, actual != NULL ? actual : L"(null)");
    }
}

static void eq_int(long long actual, long long expected, const char *what) {
    checks++;
    if (actual == expected) {
        printf("  ok    %s\n", what);
    } else {
        failures++;
        printf("  FAIL  %s  [%s]\n        expected %lld\n        got      %lld\n",
               what, current, expected, actual);
    }
}

/* ---------- scratch files ---------- */

static wchar_t g_scratch[MAX_PATH];

static void scratchPath(wchar_t *out, const wchar_t *leaf) {
    wcscpy(out, g_scratch);
    wcscat(out, L"\\");
    wcscat(out, leaf);
}

static void makeScratch(void) {
    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    _snwprintf(g_scratch, MAX_PATH, L"%sclipboard_history_tests_%lu", temp, GetCurrentProcessId());
    g_scratch[MAX_PATH - 1] = L'\0';
    CreateDirectoryW(g_scratch, NULL);
}

static void removeTree(const wchar_t *folder) {
    wchar_t pattern[MAX_PATH];
    wcscpy(pattern, folder);
    wcscat(pattern, L"\\*");

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) {
                continue;
            }
            wchar_t full[MAX_PATH];
            wcscpy(full, folder);
            wcscat(full, L"\\");
            wcscat(full, fd.cFileName);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                removeTree(full);
            } else {
                DeleteFileW(full);
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(folder);
}

/* Reads a whole file as bytes. Returns NULL if it will not open. */
static unsigned char *readAll(const wchar_t *path, long *sizeOut) {
    FILE *f = _wfopen(path, L"rb");
    if (f == NULL) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    unsigned char *buf = (unsigned char *)malloc((size_t)size + 1);
    if (buf == NULL) {
        fclose(f);
        return NULL;
    }
    size_t got = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[got] = '\0';
    if (sizeOut != NULL) {
        *sizeOut = (long)got;
    }
    return buf;
}

static BOOL touchFile(const wchar_t *path, const char *content) {
    FILE *f = _wfopen(path, L"wb");
    if (f == NULL) {
        return FALSE;
    }
    fputs(content, f);
    fclose(f);
    return TRUE;
}

static int countOccurrences(const char *haystack, const char *needle) {
    if (haystack == NULL) {
        return -1;
    }
    int found = 0;
    size_t step = strlen(needle);
    for (const char *at = strstr(haystack, needle); at != NULL; at = strstr(at + step, needle)) {
        found++;
    }
    return found;
}

static BOOL fileExists(const wchar_t *path) {
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

static struct tm makeTm(int year, int mon, int mday, int hour, int min, int sec) {
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year = year - 1900;
    t.tm_mon = mon - 1;
    t.tm_mday = mday;
    t.tm_hour = hour;
    t.tm_min = min;
    t.tm_sec = sec;
    t.tm_isdst = -1;
    return t;
}

/* ---------- clip_text ---------- */

static void testWideToUtf8(void) {
    group("clip_text: CF_UNICODETEXT to UTF-8");

    char *ascii = clipWideToUtf8(L"plain text");
    eq_str(ascii, "plain text", "ASCII passes through unchanged");
    free(ascii);

    char *empty = clipWideToUtf8(L"");
    ok(empty != NULL && empty[0] == '\0', "an empty clipboard string is \"\", not NULL");
    free(empty);

    /* U+0928 U+092E U+0938 U+094D U+0924 U+0947 -- Devanagari, the case the
       original's missing BOM turned into mojibake. */
    char *devanagari = clipWideToUtf8(L"\u0928\u092E\u0938\u094D\u0924\u0947");
    eq_str(devanagari,
           "\xE0\xA4\xA8\xE0\xA4\xAE\xE0\xA4\xB8\xE0\xA5\x8D\xE0\xA4\xA4\xE0\xA5\x87",
           "Devanagari becomes the right UTF-8 bytes");
    free(devanagari);

    char *emoji = clipWideToUtf8(L"\U0001F4CB");
    eq_str(emoji, "\xF0\x9F\x93\x8B", "a surrogate pair becomes one 4-byte sequence");
    free(emoji);

    ok(clipWideToUtf8(NULL) == NULL, "NULL in, NULL out");
}

static void testAnsiToUtf8(void) {
    group("clip_text: CF_TEXT fallback to UTF-8");

    /* REGRESSION. The original did result = _strdup(atext) here, so CP1252
       bytes went into a file that was UTF-8 everywhere else. The fallback is
       nearly unreachable because Windows synthesises CF_UNICODETEXT, but when
       it runs it must not produce a second encoding. */
    const char cp1252[] = { 'c', 'a', 'f', (char)0xE9, '\0' };
    char *converted = clipAnsiToUtf8(cp1252, 1252);
    eq_str(converted, "caf\xC3\xA9", "CP1252 e-acute becomes UTF-8 C3 A9");
    ok(converted != NULL && strcmp(converted, cp1252) != 0,
       "REGRESSION: the result differs from _strdup(atext), which is what the original returned");
    free(converted);

    char *plain = clipAnsiToUtf8("ordinary", 1252);
    eq_str(plain, "ordinary", "ASCII is unchanged by the conversion");
    free(plain);

    char *already = clipAnsiToUtf8("caf\xC3\xA9", CP_UTF8);
    eq_str(already, "caf\xC3\xA9", "CP_UTF8 in, the same bytes out");
    free(already);

    ok(clipAnsiToUtf8(NULL, CP_ACP) == NULL, "NULL in, NULL out");
}

static void testSensitiveFlag(void) {
    group("clip_text: the CanIncludeInClipboardHistory flag");

    DWORD zero = 0;
    DWORD one = 1;
    ok(clipHistoryFlagExcludes(&zero, sizeof(zero)) == TRUE, "a DWORD 0 means do not record");
    ok(clipHistoryFlagExcludes(&one, sizeof(one)) == FALSE, "a DWORD 1 means recording is allowed");

    /* The original read *(DWORD *)blob with no size check, so a one-byte
       payload was a three-byte read past the end of the block. */
    unsigned char tiny[1] = { 0 };
    ok(clipHistoryFlagExcludes(tiny, sizeof(tiny)) == FALSE,
       "REGRESSION: a payload smaller than a DWORD is ignored, not read past");
    ok(clipHistoryFlagExcludes(NULL, 4) == FALSE, "a NULL payload is ignored");
    ok(clipHistoryFlagExcludes(&zero, 0) == FALSE, "a zero-length payload is ignored");
}

static void testNewEntry(void) {
    group("clip_text: is this worth logging");

    ok(clipIsNewEntry("hello", NULL) == TRUE, "the first entry of a session is new");
    ok(clipIsNewEntry("hello", "hello") == FALSE, "copying the same thing twice logs once");
    ok(clipIsNewEntry("hello", "world") == TRUE, "different text is a new entry");
    ok(clipIsNewEntry("", NULL) == FALSE, "an empty clipboard is not an entry");
    ok(clipIsNewEntry(NULL, "hello") == FALSE, "NULL is not an entry");
    ok(clipIsNewEntry("hello world", "hello") == TRUE, "a superstring is a new entry");
}

/* ---------- logfile: paths ---------- */

static void testFolderPath(void) {
    group("logfile: building the folder path");

    wchar_t out[MAX_PATH];
    ok(logBuildFolderPath(out, MAX_PATH, L"C:\\Users\\a\\Desktop"), "builds under the Desktop");
    eq_wstr(out, L"C:\\Users\\a\\Desktop\\Clipboard History", "the folder is named Clipboard History");

    ok(logBuildFolderPath(out, MAX_PATH, L"C:\\"), "accepts a root path");
    eq_wstr(out, L"C:\\Clipboard History", "a trailing separator is not doubled");

    ok(logBuildFolderPath(out, MAX_PATH, L""), "an empty Desktop path is not an error");
    eq_wstr(out, L"Clipboard History", "it falls back to the working directory");

    ok(logBuildFolderPath(out, MAX_PATH, NULL), "a NULL Desktop path falls back too");

    /* The original used snprintf into a MAX_PATH buffer and ignored the
       return, so a long Desktop path silently produced a truncated name. */
    wchar_t sentinel[8] = L"keepme";
    ok(logBuildFolderPath(sentinel, 8, L"C:\\Users\\somebody\\Desktop") == FALSE,
       "REGRESSION: refuses to build a path that would not fit");
    eq_wstr(sentinel, L"keepme", "and leaves the caller's buffer alone when it refuses");
}

static void testSessionPath(void) {
    group("logfile: building the session file name");

    struct tm when = makeTm(2026, 9, 17, 21, 2, 44);
    wchar_t out[MAX_PATH];

    ok(logBuildSessionPath(out, MAX_PATH, L"C:\\logs", &when, 0), "builds the first name of a second");
    eq_wstr(out, L"C:\\logs\\20260917_210244.txt", "named for the launch moment");

    ok(logBuildSessionPath(out, MAX_PATH, L"C:\\logs", &when, 2), "builds a disambiguated name");
    eq_wstr(out, L"C:\\logs\\20260917_210244_2.txt", "the second file of a second gets _2");

    ok(logBuildSessionPath(out, MAX_PATH, L"C:\\logs", &when, 99), "accepts _99");
    eq_wstr(out, L"C:\\logs\\20260917_210244_99.txt", "and spells it _99");

    ok(logBuildSessionPath(out, MAX_PATH, L"C:\\logs", &when, 1) == FALSE, "_1 is not a name it makes");
    ok(logBuildSessionPath(out, MAX_PATH, L"C:\\logs", &when, 100) == FALSE, "and neither is _100");
    ok(logBuildSessionPath(out, MAX_PATH, NULL, &when, 0) == FALSE, "a NULL folder is refused");
    ok(logBuildSessionPath(out, MAX_PATH, L"C:\\logs", NULL, 0) == FALSE, "a NULL time is refused");
}

static void testParseSessionName(void) {
    group("logfile: recognising a name this program wrote");

    struct tm parsed;
    ok(logParseSessionName(L"20260917_210244.txt", &parsed), "accepts a name it writes");
    eq_int(parsed.tm_year + 1900, 2026, "year comes back");
    eq_int(parsed.tm_mon + 1, 9, "month comes back");
    eq_int(parsed.tm_mday, 17, "day comes back");
    eq_int(parsed.tm_hour, 21, "hour comes back");
    eq_int(parsed.tm_min, 2, "minute comes back");
    eq_int(parsed.tm_sec, 44, "second comes back");

    ok(logParseSessionName(L"20260917_210244_2.txt", &parsed), "accepts the _2 form");
    ok(logParseSessionName(L"20260917_210244_99.txt", &parsed), "accepts the _99 form");

    /* Pruning deletes. Everything below is something in the folder that this
       program did not write, and must survive. */
    ok(logParseSessionName(L"notes.txt", &parsed) == FALSE, "rejects an unrelated .txt");
    ok(logParseSessionName(L"20260917_210244.log", &parsed) == FALSE, "rejects another extension");
    ok(logParseSessionName(L"20260917_2102.txt", &parsed) == FALSE, "rejects a short stamp");
    ok(logParseSessionName(L"20260917_210244 copy.txt", &parsed) == FALSE, "rejects a renamed copy");
    ok(logParseSessionName(L"20260917_210244_1.txt", &parsed) == FALSE, "rejects _1, which it never writes");
    ok(logParseSessionName(L"20260917_210244_100.txt", &parsed) == FALSE, "rejects _100");
    ok(logParseSessionName(L"2026091x_210244.txt", &parsed) == FALSE, "rejects a non-digit in the date");
    ok(logParseSessionName(L"20260231_120000.txt", &parsed) == FALSE, "rejects 31 February");
    ok(logParseSessionName(L"20261317_120000.txt", &parsed) == FALSE, "rejects month 13");
    ok(logParseSessionName(L"20260917_250000.txt", &parsed) == FALSE, "rejects hour 25");
    ok(logParseSessionName(L"20260917_206044.txt", &parsed) == FALSE, "rejects minute 60");
    ok(logParseSessionName(L"", &parsed) == FALSE, "rejects an empty name");
    ok(logParseSessionName(NULL, &parsed) == FALSE, "rejects NULL");
}

static void testExpiry(void) {
    group("logfile: what counts as old");

    struct tm stamp = makeTm(2026, 8, 1, 12, 0, 0);
    struct tm copy = stamp;
    time_t base = mktime(&copy);

    ok(logIsExpired(&stamp, base + 31 * 86400, 30) == TRUE, "31 days old is older than 30 days");
    ok(logIsExpired(&stamp, base + 29 * 86400, 30) == FALSE, "29 days old is not");
    ok(logIsExpired(&stamp, base + 30 * 86400, 30) == FALSE, "exactly 30 days is not yet older than 30");
    ok(logIsExpired(&stamp, base, 30) == FALSE, "a log written now is not old");
    ok(logIsExpired(&stamp, base + 3650 * 86400, 1) == TRUE, "ten years old is older than a day");

    /* A destructive operation must not treat a missing or zero argument as
       "delete everything". */
    ok(logIsExpired(&stamp, base + 3650 * 86400, 0) == FALSE, "REGRESSION: 0 days expires nothing");
    ok(logIsExpired(&stamp, base + 3650 * 86400, -5) == FALSE, "a negative age expires nothing");
    ok(logIsExpired(NULL, base, 30) == FALSE, "a NULL stamp expires nothing");
}

/* ---------- logfile: writing ---------- */

static void testSessionFile(void) {
    group("logfile: the session file on disk");

    wchar_t folder[MAX_PATH];
    scratchPath(folder, L"session");
    CreateDirectoryW(folder, NULL);

    struct tm when = makeTm(2026, 9, 17, 21, 2, 44);
    LogSession session;
    ok(logOpenSession(&session, folder, &when, 0), "creates the session file");

    wchar_t expected[MAX_PATH];
    logBuildSessionPath(expected, MAX_PATH, folder, &when, 0);
    eq_wstr(session.path, expected, "at the path built from the launch time");

    long size = 0;
    unsigned char *bytes = readAll(session.path, &size);
    ok(bytes != NULL, "and the file can be read back");

    /* REGRESSION. The original wrote "Started:" as the first bytes of the
       file. UTF-8 without a BOM is guessed as the legacy codepage by Notepad
       and by Word's plain-text import, so every non-ASCII entry in a log this
       program wrote came back as mojibake. Verified against the original
       binary: its first eight bytes were 53 74 61 72 74 65 64 3A. */
    ok(bytes != NULL && size >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF,
       "REGRESSION: the file starts with a UTF-8 BOM");
    ok(bytes != NULL && strstr((char *)bytes, "Started: 2026-09-17 21:02:44") != NULL,
       "the opening line carries the launch timestamp");
    /* The header gap is three blank lines, the same as between entries. The
       separator constant starts with the newline that ends an entry's own
       line, and reusing it here put a fourth blank line under the rule. */
    ok(bytes != NULL && strstr((char *)bytes,
                               "============================\r\n\r\n\r\n\r\n") != NULL,
       "the header is followed by three blank lines, as in the original");
    ok(bytes != NULL && strstr((char *)bytes,
                               "============================\r\n\r\n\r\n\r\n\r\n") == NULL,
       "and not four");
    free(bytes);

    ok(logAppendEntry(&session, "first thing"), "appends an entry");
    ok(logAppendEntry(&session, "\xE0\xA4\xA8\xE0\xA4\xAE\xE0\xA4\xB8"), "appends non-ASCII text");

    bytes = readAll(session.path, &size);
    ok(bytes != NULL && strstr((char *)bytes, "first thing\r\n\r\n\r\n\r\n") != NULL,
       "each entry is followed by three blank lines");
    ok(bytes != NULL && strstr((char *)bytes, "\xE0\xA4\xA8\xE0\xA4\xAE\xE0\xA4\xB8") != NULL,
       "the non-ASCII entry is stored as UTF-8, matching the BOM");
    free(bytes);

    logWriteClosing(&session, &when);
    bytes = readAll(session.path, &size);
    ok(bytes != NULL && strstr((char *)bytes, "Closed: 2026-09-17 21:02:44") != NULL,
       "the closing line is written");
    long afterClose = size;
    free(bytes);

    /* The closing line is reachable from the console handler, WM_DESTROY and
       atexit. It must not appear three times. */
    logWriteClosing(&session, &when);
    bytes = readAll(session.path, &size);
    eq_int(size, afterClose, "a second close writes nothing more");
    free(bytes);

    removeTree(folder);
}

static void testNoTruncationOfAnotherSession(void) {
    group("logfile: two sessions starting in the same second");

    wchar_t folder[MAX_PATH];
    scratchPath(folder, L"collide");
    CreateDirectoryW(folder, NULL);

    struct tm when = makeTm(2026, 9, 17, 21, 2, 44);

    LogSession first;
    ok(logOpenSession(&first, folder, &when, 0), "the first session opens");
    ok(logAppendEntry(&first, "something the first session logged"), "and logs something");

    /* REGRESSION. File names have one-second resolution and the original
       opened the log with fopen(path, "w"), which truncates. A second launch
       inside the same second wiped whatever the first had already written and
       both then appended to the one file. Reproduced against the original
       binary: two instances left one file between them. */
    LogSession second;
    ok(logOpenSession(&second, folder, &when, 0), "a second session in the same second also opens");
    ok(wcscmp(first.path, second.path) != 0,
       "REGRESSION: it gets a file of its own rather than truncating the first");

    long size = 0;
    unsigned char *bytes = readAll(first.path, &size);
    ok(bytes != NULL && strstr((char *)bytes, "something the first session logged") != NULL,
       "REGRESSION: the first session's entry is still there");
    free(bytes);

    wchar_t expectedSecond[MAX_PATH];
    logBuildSessionPath(expectedSecond, MAX_PATH, folder, &when, 2);
    eq_wstr(second.path, expectedSecond, "the second file is the _2 name");

    removeTree(folder);
}

static void testSizeCap(void) {
    group("logfile: the size cap");

    wchar_t folder[MAX_PATH];
    scratchPath(folder, L"cap");
    CreateDirectoryW(folder, NULL);

    struct tm when = makeTm(2026, 9, 17, 21, 2, 44);
    LogSession session;
    /* The header is already ~60 bytes, so a 200-byte cap leaves room for a
       short entry and not for a long one. */
    ok(logOpenSession(&session, folder, &when, 200), "opens with a 200-byte cap");
    ok(session.maxBytes == 200, "the cap is remembered");
    ok(logAppendEntry(&session, "short"), "a short entry fits");
    ok(session.capped == FALSE, "and does not trip the cap");

    char big[300];
    memset(big, 'x', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    ok(logAppendEntry(&session, big) == FALSE, "an entry that would exceed the cap is refused");
    ok(session.capped == TRUE, "and the session is marked capped");

    long size = 0;
    unsigned char *bytes = readAll(session.path, &size);
    ok(bytes != NULL && strstr((char *)bytes, "Size limit reached") != NULL,
       "the file says why it stops");
    ok(bytes != NULL && strstr((char *)bytes, "xxxxxxxxxx") == NULL,
       "and does not contain the entry it refused");
    free(bytes);

    ok(logAppendEntry(&session, "later") == FALSE, "nothing is logged after the cap");
    bytes = readAll(session.path, &size);
    ok(bytes != NULL && strstr((char *)bytes, "later") == NULL, "not even a short entry");
    eq_int(countOccurrences((char *)bytes, "Size limit reached"), 1,
           "and the notice is written once, not once per refusal");
    free(bytes);

    LogSession uncapped;
    ok(logOpenSession(&uncapped, folder, &when, 0), "a session with maxBytes 0 opens");
    ok(logAppendEntry(&uncapped, big), "and has no cap at all");

    removeTree(folder);
}

static void testNonAnsiFolder(void) {
    group("logfile: a Desktop path the ANSI codepage cannot spell");

    /* U+092E U+0928 U+0940 U+0937 -- a perfectly ordinary Windows account
       name, and one that CP1252 has no characters for. */
    wchar_t folder[MAX_PATH];
    scratchPath(folder, L"\u092E\u0928\u0940\u0937");
    BOOL made = CreateDirectoryW(folder, NULL);
    ok(made || GetLastError() == ERROR_ALREADY_EXISTS, "the folder can be created with the wide API");

    /* REGRESSION. The original was narrow end to end: SHGetFolderPathA,
       CreateDirectoryA, fopen. Converting this path to the ANSI codepage
       replaces the unmappable characters with '?', which is not legal in a
       Windows filename, so the narrow calls fail with ERROR_INVALID_NAME and
       the program logged nothing at all for such a user. This check does the
       conversion the original did and shows the narrow open failing on the
       same path the wide one is about to succeed on. */
    char narrow[MAX_PATH];
    WideCharToMultiByte(CP_ACP, 0, folder, -1, narrow, MAX_PATH, NULL, NULL);
    BOOL ansiLossy = strchr(narrow, '?') != NULL;
    ok(ansiLossy, "the ANSI form of the path loses characters to '?'");

    char narrowFile[MAX_PATH];
    snprintf(narrowFile, MAX_PATH, "%s\\probe.txt", narrow);
    FILE *viaAnsi = fopen(narrowFile, "w");
    ok(viaAnsi == NULL, "REGRESSION: fopen on the ANSI path fails, as it did in the original");
    if (viaAnsi != NULL) {
        fclose(viaAnsi);
    }

    struct tm when = makeTm(2026, 9, 17, 21, 2, 44);
    LogSession session;
    ok(logOpenSession(&session, folder, &when, 0),
       "REGRESSION: the wide path opens a session in the same folder");
    ok(logAppendEntry(&session, "logged under a name CP1252 cannot spell"), "and logs into it");

    long size = 0;
    unsigned char *bytes = readAll(session.path, &size);
    ok(bytes != NULL && strstr((char *)bytes, "logged under a name CP1252 cannot spell") != NULL,
       "and the entry can be read back");
    free(bytes);

    removeTree(folder);
}

/* ---------- logfile: the folder as a whole ---------- */

static void testSurvey(void) {
    group("logfile: surveying the folder");

    wchar_t folder[MAX_PATH];
    scratchPath(folder, L"survey");
    CreateDirectoryW(folder, NULL);

    wchar_t path[MAX_PATH];
    wcscpy(path, folder); wcscat(path, L"\\20260101_090000.txt"); touchFile(path, "0123456789");
    wcscpy(path, folder); wcscat(path, L"\\20260917_210244.txt"); touchFile(path, "01234");
    wcscpy(path, folder); wcscat(path, L"\\20260605_120000_2.txt"); touchFile(path, "0");
    wcscpy(path, folder); wcscat(path, L"\\shopping list.txt"); touchFile(path, "eggs");

    LogSurvey survey;
    ok(logSurveyFolder(folder, &survey), "surveys the folder");
    eq_int(survey.files, 3, "counts only the files this program writes");
    eq_int(survey.bytes, 16, "and adds up their sizes");
    eq_wstr(survey.oldest, L"20260101_090000.txt", "finds the oldest");
    eq_wstr(survey.newest, L"20260917_210244.txt", "finds the newest");

    wchar_t empty[MAX_PATH];
    scratchPath(empty, L"survey-empty");
    CreateDirectoryW(empty, NULL);
    ok(logSurveyFolder(empty, &survey), "an empty folder is not an error");
    eq_int(survey.files, 0, "and holds nothing");
    eq_wstr(survey.oldest, L"", "with no oldest file");

    removeTree(empty);
    removeTree(folder);
}

static void testPrune(void) {
    group("logfile: pruning old logs");

    wchar_t folder[MAX_PATH];
    scratchPath(folder, L"prune");
    CreateDirectoryW(folder, NULL);

    struct tm recent = makeTm(2026, 9, 17, 12, 0, 0);
    struct tm old = makeTm(2026, 1, 5, 12, 0, 0);
    struct tm copy = recent;
    time_t now = mktime(&copy) + 3600;

    wchar_t recentPath[MAX_PATH], oldPath[MAX_PATH], oldSuffixed[MAX_PATH];
    wchar_t strangerPath[MAX_PATH], renamedPath[MAX_PATH];
    logBuildSessionPath(recentPath, MAX_PATH, folder, &recent, 0);
    logBuildSessionPath(oldPath, MAX_PATH, folder, &old, 0);
    logBuildSessionPath(oldSuffixed, MAX_PATH, folder, &old, 2);
    wcscpy(strangerPath, folder); wcscat(strangerPath, L"\\important notes.txt");
    wcscpy(renamedPath, folder); wcscat(renamedPath, L"\\20260105_120000 keep.txt");

    touchFile(recentPath, "recent");
    touchFile(oldPath, "0123456789");
    touchFile(oldSuffixed, "0123456789");
    touchFile(strangerPath, "not ours");
    touchFile(renamedPath, "renamed by hand");

    long long freed = 0;
    eq_int(logPrune(folder, 30, now, NULL, &freed), 2, "deletes both logs older than 30 days");
    eq_int(freed, 20, "and reports how much space that freed");
    ok(fileExists(recentPath), "today's log survives");
    ok(!fileExists(oldPath), "the old log is gone");
    ok(!fileExists(oldSuffixed), "so is the old _2 log");
    ok(fileExists(strangerPath), "a file this program did not write is never deleted");
    ok(fileExists(renamedPath), "and neither is one somebody renamed");

    /* REGRESSION. days below 1 is a refusal, not "delete everything". */
    touchFile(oldPath, "back again");
    eq_int(logPrune(folder, 0, now, NULL, NULL), -1, "REGRESSION: 0 days is refused");
    ok(fileExists(oldPath), "and nothing is deleted");
    eq_int(logPrune(folder, -3, now, NULL, NULL), -1, "a negative age is refused");
    ok(fileExists(oldPath), "and still nothing is deleted");

    eq_int(logPrune(folder, 30, now, oldPath, NULL), 0, "the live session is skipped");
    ok(fileExists(oldPath), "even when it is old enough to prune");

    eq_int(logPrune(folder, 3650, now, NULL, NULL), 0, "a long retention deletes nothing here");
    ok(fileExists(oldPath), "and leaves the old log alone");

    removeTree(folder);
}

/* ---------- options ---------- */

static void testOptions(void) {
    group("options: the command line");

    Options opts;
    char err[128];

    char *none[] = { "clipboard_history.exe" };
    ok(optParse(1, none, &opts, err, sizeof(err)), "no arguments is valid");
    ok(opts.showHelp == FALSE, "and does not ask for help");
    eq_int(opts.pruneDays, 0, "pruning is off by default");
    eq_int(opts.maxBytes, (long long)OPT_DEFAULT_MAX_MB * 1024 * 1024, "the default cap is 10 MB");

    char *help[] = { "x.exe", "--help" };
    ok(optParse(2, help, &opts, err, sizeof(err)) && opts.showHelp, "--help is recognised");

    char *shortHelp[] = { "x.exe", "-h" };
    ok(optParse(2, shortHelp, &opts, err, sizeof(err)) && opts.showHelp, "-h is recognised");

    char *cap[] = { "x.exe", "--max-mb", "50" };
    ok(optParse(3, cap, &opts, err, sizeof(err)), "--max-mb takes a number");
    eq_int(opts.maxBytes, 50LL * 1024 * 1024, "and is read as megabytes");

    char *uncapped[] = { "x.exe", "--max-mb", "0" };
    ok(optParse(3, uncapped, &opts, err, sizeof(err)), "--max-mb 0 is allowed");
    eq_int(opts.maxBytes, 0, "and means no cap");

    char *prune[] = { "x.exe", "--prune-days", "30" };
    ok(optParse(3, prune, &opts, err, sizeof(err)), "--prune-days takes a number");
    eq_int(opts.pruneDays, 30, "and is read as days");

    char *both[] = { "x.exe", "--prune-days", "7", "--max-mb", "1" };
    ok(optParse(5, both, &opts, err, sizeof(err)), "the two options combine");
    eq_int(opts.pruneDays, 7, "prune days survives");
    eq_int(opts.maxBytes, 1024 * 1024, "and so does the cap");

    /* REGRESSION-adjacent: --prune-days deletes files, so the value most
       likely to be a typo must not be the one that deletes everything. */
    char *zeroDays[] = { "x.exe", "--prune-days", "0" };
    ok(optParse(3, zeroDays, &opts, err, sizeof(err)) == FALSE, "--prune-days 0 is refused");
    ok(err[0] != '\0', "and says why");

    char *negative[] = { "x.exe", "--prune-days", "-1" };
    ok(optParse(3, negative, &opts, err, sizeof(err)) == FALSE, "a negative day count is refused");

    char *words[] = { "x.exe", "--prune-days", "thirty" };
    ok(optParse(3, words, &opts, err, sizeof(err)) == FALSE, "a word is not a day count");

    char *trailing[] = { "x.exe", "--max-mb", "50x" };
    ok(optParse(3, trailing, &opts, err, sizeof(err)) == FALSE, "trailing junk is refused");

    char *missing[] = { "x.exe", "--prune-days" };
    ok(optParse(2, missing, &opts, err, sizeof(err)) == FALSE, "a missing value is refused");

    char *huge[] = { "x.exe", "--max-mb", "99999" };
    ok(optParse(3, huge, &opts, err, sizeof(err)) == FALSE, "an absurd cap is refused");

    char *tooLong[] = { "x.exe", "--prune-days", "99999" };
    ok(optParse(3, tooLong, &opts, err, sizeof(err)) == FALSE, "an absurd retention is refused");

    char *unknown[] = { "x.exe", "--delete-everything" };
    ok(optParse(2, unknown, &opts, err, sizeof(err)) == FALSE, "an unrecognised option is refused");

    ok(strstr(optUsage(), "plain text") != NULL,
       "the help text says the log is plain text before it lists the flags");
}

int main(void) {
    makeScratch();

    testWideToUtf8();
    testAnsiToUtf8();
    testSensitiveFlag();
    testNewEntry();

    testFolderPath();
    testSessionPath();
    testParseSessionName();
    testExpiry();

    testSessionFile();
    testNoTruncationOfAnotherSession();
    testSizeCap();
    testNonAnsiFolder();

    testSurvey();
    testPrune();

    testOptions();

    removeTree(g_scratch);

    printf("\n%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
