/*
 * logfile.h -- where the session log lives, what goes in it, and what may be
 * deleted from the folder it lives in.
 *
 * Paths are wide (UTF-16) end to end and the file content is UTF-8 with a BOM.
 * The narrow/ANSI equivalents of these calls cannot open a path containing a
 * character outside the system codepage at all -- see the note at the top of
 * src/clipboard_history.c.
 *
 * The functions that only compute something (path building, name parsing, the
 * age test) take their "now" as an argument rather than calling time(), so the
 * tests can pin it.
 */
#ifndef LOGFILE_H
#define LOGFILE_H

#include <windows.h>
#include <stdio.h>
#include <time.h>

#define LOG_FOLDER_NAME L"Clipboard History"

/* A log file name is YYYYMMDD_HHMMSS.txt, optionally with a _2.._99 suffix
   when a run starts in the same second as one that already has a file. */
#define LOG_NAME_CCH 23

typedef struct {
    wchar_t path[MAX_PATH];
    long long bytes;     /* size of the file on disk after the last write */
    long long maxBytes;  /* 0 means no limit */
    BOOL capped;         /* TRUE once maxBytes was reached and the notice written */
    BOOL closed;         /* guards the closing line; see logWriteClosing */
} LogSession;

typedef struct {
    int files;
    long long bytes;
    wchar_t oldest[LOG_NAME_CCH + 1];
    wchar_t newest[LOG_NAME_CCH + 1];
} LogSurvey;

/* "<desktop>\Clipboard History". FALSE (and out untouched) if it would not fit,
   rather than silently truncating to a path that is not the one meant. */
BOOL logBuildFolderPath(wchar_t *out, size_t cch, const wchar_t *desktop);

/* "<folder>\YYYYMMDD_HHMMSS.txt" for the given local time. `attempt` 0 is the
   bare name; 2..99 append "_<attempt>" before the extension. */
BOOL logBuildSessionPath(wchar_t *out, size_t cch, const wchar_t *folder,
                         const struct tm *when, int attempt);

/* Recognises exactly the names logBuildSessionPath produces, and fills *out
   with the encoded time. Anything else -- another extension, a renamed file, a
   date that does not exist -- is rejected. Pruning deletes files, so it only
   ever deletes names this returns TRUE for. */
BOOL logParseSessionName(const wchar_t *name, struct tm *out);

/* TRUE when a log stamped `stamp` is strictly more than `days` days older than
   `now`. `days` below 1 is rejected (FALSE) so a bad option cannot delete today. */
BOOL logIsExpired(const struct tm *stamp, time_t now, int days);

/* Creates the folder if it is not there. TRUE if it exists afterwards. */
BOOL logEnsureFolder(const wchar_t *folder, DWORD *errOut);

/* Creates this session's file in `folder`, writes the UTF-8 BOM and the
   "Started" header, and initialises *session. Never opens a file that already
   exists: it walks the _2.._99 suffixes instead. maxBytes of 0 disables the
   size cap. */
BOOL logOpenSession(LogSession *session, const wchar_t *folder,
                    const struct tm *when, long long maxBytes);

/* Appends one UTF-8 entry followed by the blank-line separator. Returns FALSE
   if the entry was not written -- because the file would not open, or because
   the size cap has been reached. */
BOOL logAppendEntry(LogSession *session, const char *utf8Text);

/* Appends the closing line. Safe to call more than once; only the first call
   writes. */
void logWriteClosing(LogSession *session, const struct tm *when);

/* Counts the log files already in the folder and finds the oldest and newest. */
BOOL logSurveyFolder(const wchar_t *folder, LogSurvey *out);

/* Deletes log files older than `days`, skipping `keepPath` (the live session).
   Returns the number deleted, or -1 if the folder could not be read. */
int logPrune(const wchar_t *folder, int days, time_t now, const wchar_t *keepPath,
             long long *bytesFreed);

#endif /* LOGFILE_H */
