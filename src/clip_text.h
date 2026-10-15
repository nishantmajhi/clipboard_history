/*
 * clip_text.h -- the decisions that turn a clipboard payload into a log entry.
 *
 * Everything here is a pure function of its arguments. Nothing in this file
 * opens the clipboard, creates a window or touches a file, which is what makes
 * it testable: tests/clipboard_test.c calls these directly.
 */
#ifndef CLIP_TEXT_H
#define CLIP_TEXT_H

#include <windows.h>
#include <stddef.h>

/* Converts a NUL-terminated wide string to a newly allocated UTF-8 string.
   Returns NULL if the conversion fails or allocation fails. Caller frees. */
char *clipWideToUtf8(const wchar_t *wide);

/* Converts a NUL-terminated narrow string in the given codepage (CP_ACP for
   the system ANSI codepage) to a newly allocated UTF-8 string. Returns NULL on
   failure. Caller frees.

   This exists so that the CF_TEXT path and the CF_UNICODETEXT path put the
   same encoding in the file. See the note on clipboard format synthesis in
   src/clipboard_history.c. */
char *clipAnsiToUtf8(const char *narrow, UINT codepage);

/* TRUE when a "CanIncludeInClipboardHistory" payload says "do not record this".
   The convention is a DWORD; 0 means excluded. A payload that is not at least
   DWORD-sized is not a flag we understand, and is treated as "no opinion"
   rather than read past its end. */
BOOL clipHistoryFlagExcludes(const void *blob, size_t bytes);

/* TRUE when text is worth appending: non-empty, and not a repeat of the entry
   logged immediately before it. lastText may be NULL (nothing logged yet). */
BOOL clipIsNewEntry(const char *text, const char *lastText);

#endif /* CLIP_TEXT_H */
