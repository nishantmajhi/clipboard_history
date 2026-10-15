/*
 * options.h -- the command line.
 *
 * Parsing is separated from acting on it for the usual reason: the tests can
 * check that "--prune-days 0" is refused without deleting anybody's logs.
 */
#ifndef OPTIONS_H
#define OPTIONS_H

#include <windows.h>
#include <stddef.h>

#define OPT_DEFAULT_MAX_MB 10

typedef struct {
    BOOL showHelp;
    int pruneDays;      /* 0 means "do not prune"; otherwise 1..3650 */
    long long maxBytes; /* 0 means "no size cap" */
} Options;

/* Fills *out with the defaults, then applies argv[1..argc-1]. Returns FALSE and
   writes a one-line explanation into err on a bad argument, in which case *out
   should not be used. argv[0] is ignored if present. */
BOOL optParse(int argc, char **argv, Options *out, char *err, size_t errCch);

/* The --help text, as a block of lines each ending in \n. */
const char *optUsage(void);

#endif /* OPTIONS_H */
