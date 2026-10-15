#include "clip_text.h"

#include <stdlib.h>
#include <string.h>

char *clipWideToUtf8(const wchar_t *wide) {
    if (wide == NULL) {
        return NULL;
    }

    int bytes = WideCharToMultiByte(CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL);
    if (bytes <= 0) {
        return NULL;
    }

    char *out = (char *)malloc((size_t)bytes);
    if (out == NULL) {
        return NULL;
    }

    if (WideCharToMultiByte(CP_UTF8, 0, wide, -1, out, bytes, NULL, NULL) <= 0) {
        free(out);
        return NULL;
    }
    return out;
}

char *clipAnsiToUtf8(const char *narrow, UINT codepage) {
    if (narrow == NULL) {
        return NULL;
    }

    int chars = MultiByteToWideChar(codepage, 0, narrow, -1, NULL, 0);
    if (chars <= 0) {
        return NULL;
    }

    wchar_t *wide = (wchar_t *)malloc((size_t)chars * sizeof(wchar_t));
    if (wide == NULL) {
        return NULL;
    }

    char *out = NULL;
    if (MultiByteToWideChar(codepage, 0, narrow, -1, wide, chars) > 0) {
        out = clipWideToUtf8(wide);
    }
    free(wide);
    return out;
}

BOOL clipHistoryFlagExcludes(const void *blob, size_t bytes) {
    if (blob == NULL || bytes < sizeof(DWORD)) {
        return FALSE;
    }

    DWORD value;
    memcpy(&value, blob, sizeof(value));
    return value == 0 ? TRUE : FALSE;
}

BOOL clipIsNewEntry(const char *text, const char *lastText) {
    if (text == NULL || text[0] == '\0') {
        return FALSE;
    }
    if (lastText == NULL) {
        return TRUE;
    }
    return strcmp(text, lastText) != 0 ? TRUE : FALSE;
}
