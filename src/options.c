#include "options.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_PRUNE_DAYS 3650
#define MAX_CAP_MB 4096

/* strtol with none of the "0x", leading-sign or trailing-junk latitude: an
   option value is a run of decimal digits and nothing else. */
static BOOL parseCount(const char *text, long *out) {
    if (text == NULL || text[0] == '\0') {
        return FALSE;
    }

    long value = 0;
    for (const char *p = text; *p != '\0'; p++) {
        if (*p < '0' || *p > '9') {
            return FALSE;
        }
        if (value > (MAX_CAP_MB > MAX_PRUNE_DAYS ? MAX_CAP_MB : MAX_PRUNE_DAYS)) {
            return FALSE; /* already out of every range we accept */
        }
        value = value * 10 + (*p - '0');
    }
    *out = value;
    return TRUE;
}

static void fail(char *err, size_t errCch, const char *message) {
    if (err != NULL && errCch > 0) {
        snprintf(err, errCch, "%s", message);
        err[errCch - 1] = '\0';
    }
}

BOOL optParse(int argc, char **argv, Options *out, char *err, size_t errCch) {
    if (out == NULL) {
        return FALSE;
    }
    if (err != NULL && errCch > 0) {
        err[0] = '\0';
    }

    out->showHelp = FALSE;
    out->pruneDays = 0;
    out->maxBytes = (long long)OPT_DEFAULT_MAX_MB * 1024 * 1024;

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (arg == NULL) {
            continue;
        }

        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0 || strcmp(arg, "/?") == 0) {
            out->showHelp = TRUE;
            return TRUE; /* nothing after --help can matter */
        }

        if (strcmp(arg, "--prune-days") == 0 || strcmp(arg, "--max-mb") == 0) {
            if (i + 1 >= argc) {
                fail(err, errCch, "missing value after the option");
                return FALSE;
            }

            long value = 0;
            if (!parseCount(argv[++i], &value)) {
                fail(err, errCch, "the value must be a whole number of days or megabytes");
                return FALSE;
            }

            if (strcmp(arg, "--prune-days") == 0) {
                /* 0 is rejected rather than treated as "delete everything":
                   pruning removes files permanently, so the one value most
                   likely to be a mistake must not be the destructive one. */
                if (value < 1 || value > MAX_PRUNE_DAYS) {
                    fail(err, errCch, "--prune-days takes 1 to 3650");
                    return FALSE;
                }
                out->pruneDays = (int)value;
            } else {
                if (value > MAX_CAP_MB) {
                    fail(err, errCch, "--max-mb takes 0 (no cap) to 4096");
                    return FALSE;
                }
                out->maxBytes = value * 1024 * 1024;
            }
            continue;
        }

        fail(err, errCch, "unrecognised option");
        return FALSE;
    }

    return TRUE;
}

const char *optUsage(void) {
    return
        "clipboard_history -- log everything you copy to a file on your Desktop.\n"
        "\n"
        "  clipboard_history [--max-mb N] [--prune-days N]\n"
        "\n"
        "  --max-mb N       stop logging once this session's file reaches N MB.\n"
        "                   0 removes the cap. Default 10.\n"
        "  --prune-days N   before starting, delete logs in the folder older than\n"
        "                   N days. Only files named YYYYMMDD_HHMMSS.txt are\n"
        "                   touched, and never the session about to start.\n"
        "  --help           this text.\n"
        "\n"
        "Everything you copy while this runs is written to disk in plain text,\n"
        "including anything you paste into a password box afterwards. Press\n"
        "Ctrl+Alt+P to pause and resume logging, Ctrl+C to stop.\n";
}
