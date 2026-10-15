# Code Review — clipboard_history

First reviewed 2026-09-04. Remediated 2026-09-17 on branch `cleanup/2026-09-17`.

## Corrections to the first review

**The first review's headline finding was half right.** It said the file is
UTF-8 with no BOM *and* that "the fallback path writes ANSI into the same
file", so that one file held two encodings. The first half is exactly right and
was the worst bug here. The second half is not: the `CF_TEXT` fallback is
effectively unreachable.

Windows synthesises clipboard formats. An application that puts only `CF_TEXT`
on the clipboard still answers `GetClipboardData(CF_UNICODETEXT)`, because the
system converts on demand. Verified rather than reasoned about — a five-line
program was written that opens the clipboard, empties it and sets `CF_TEXT`
alone with CP1252 bytes (`café naïve`), and the original binary logged
`63 61 66 C3 A9` — correct UTF-8, produced by the wide branch. The ANSI branch
did not run.

It has been fixed anyway, because a branch that produces a second encoding
should not be sitting there, but nobody would have hit it. The bug that
mattered was the missing BOM on its own.

**The first review missed a worse bug than either.** See "The one that matters"
below.

## The one that matters

**On an account whose name is not representable in the system ANSI codepage,
the program logged nothing at all.**

The original was narrow end to end: `SHGetFolderPathA`, `CreateDirectoryA`,
`fopen`. `SHGetFolderPathA` does not fail on such an account — it succeeds and
hands back a path with `?` where the unmappable characters were. `?` is not
legal in a Windows filename, so `CreateDirectoryA` fails with
`ERROR_INVALID_NAME`, the folder is never created, and every `fopen` for every
entry fails. The program starts, prints its banner, says it is logging, and
writes an error to stderr on each copy. Measured on this machine, ACP 1252:

```
ANSI path has '?'    : yes (unmappable characters replaced)
CreateDirectoryA     : FAILED (error 123)
fopen(path, "w")     : FAILED (errno 22)
_wfopen(path, L"w")  : opened
```

Everything is wide now: `SHGetFolderPathW`, `CreateDirectoryW`, `_wfopen`,
`RegisterClassW`, `CreateWindowExW`. `tests/clipboard_test.c` creates a folder
named मनीष under `%TEMP%`, does the ANSI conversion the original did, asserts
that the narrow `fopen` fails on it, and then opens and reads back a real
session file in the same folder through the wide path.

This is the same root cause as the encoding bug, one layer down, and it is
worse: the encoding bug made the log unreadable, this one meant there was no
log. It is also the bug most likely to bite the author, whose name is not
Latin-1 even if this Windows account's is.

## Also fixed

**No UTF-8 BOM (P1, confirmed).** `readClipboardText` converted to UTF-8 and
the file was written with no mark saying so, so Notepad and Word's plain-text
import guessed the legacy codepage. Reproduced end to end against a binary
built from `git show main:clipboard_history.c`: a Devanagari copy went in as
`E0 A4 A8 …`, the file began `53 74 61 72 74 65 64 3A` ("Started:"), and read
back as `à¤¨à¤®à¤¸à¥à¤¤à¥‡` in CP1252. The file now opens `EF BB BF`.

This one is worth dwelling on, because `test.html` had a section for it all
along — section 6, Unicode maths symbols, with a note saying it "checks how
Unicode math symbols come through in the plain-text log". The fixture was
right. Nobody opened the resulting file in Notepad, which is the only place the
bug shows.

**A second instance destroyed the first one's log (P2, confirmed).** File names
have one-second resolution and the original opened the log with
`fopen(g_logPath, "w")`, which truncates. Reproduced: two instances launched in
the same second left one file between them. Both halves are fixed —
`logOpenSession` creates with `CreateFileW(CREATE_NEW)` and walks `_2`..`_99`
rather than opening a name that exists, and a named mutex means a second
instance now refuses to start at all. The mutex is the one you want anyway:
two listeners logged every copy twice.

**A failed `OpenClipboard` silently dropped the entry (P2).** The clipboard is
one system-wide resource and the application that just wrote to it often still
holds it, which is exactly when we are woken. Five attempts, 40 ms apart, and a
warning on stderr if it never opens. Not reproduced deliberately — it is a race
and provoking it reliably was not worth the time — so this is a robustness
change rather than a demonstrated fix.

**`GlobalLock` and conversion failures dropped entries silently.** All four
paths now say so on stderr.

**The sensitive-content flag was read without a size check.** The original did
`*(DWORD *)GlobalLock(hFlag)` on whatever the source application put there. A
payload shorter than four bytes was a read past the end of the block.
`clipHistoryFlagExcludes` takes the size from `GlobalSize` and treats anything
under `sizeof(DWORD)` as "no opinion".

**The path was built with `snprintf` and the result ignored**, so a long enough
Desktop path silently produced a truncated name. The builder refuses now and
leaves the caller's buffer untouched.

## The privacy posture (P1)

The first review was right that this deserved to be stated far more loudly, and
right that it is inherent rather than a defect. Four things changed:

- **The README leads with it** under a heading that says to read it before
  running, in the words the tool deserves: this is a keylogger for the
  clipboard. It names the OneDrive problem explicitly, which is real on this
  machine — the Desktop here is `C:\Users\manis\OneDrive\Desktop`, so every log
  this program has ever written was uploaded as it was written.
- **A size cap**, `--max-mb`, default 10. At the cap the session writes a line
  saying it has stopped and logs nothing further. It does not roll over to a
  second file: silently continuing under a new name is how you end up with the
  unbounded folder the cap exists to prevent.
- **Retention**, `--prune-days N`, and deliberately *not* automatic. The first
  review asked for an age-based cleanup on startup. Deleting the user's data by
  default, on a tool whose entire output is data they chose to keep, is not a
  decision to make on their behalf — so it is an explicit flag, it refuses `0`
  and negative values rather than treating them as "delete everything", it only
  deletes files whose names match exactly what this program writes, and it
  never deletes the session about to start. `logParseSessionName` rejecting
  `important notes.txt` and `20260105_120000 keep.txt` is tested, because that
  test is the only thing standing between a typo and somebody's files.
- **A pause hotkey**, Ctrl+Alt+P, so the answer to "I need to copy a password"
  is no longer "quit the program". If the hotkey is already taken the program
  says so and runs without it.

## Structure

Split as the archive convention asks, so the decisions can be tested without a
clipboard:

| | |
| --- | --- |
| `src/clipboard_history.c` | Window, message loop, clipboard reads, hotkey, mutex. |
| `src/clip_text.c` | Payload to UTF-8 entry; is it worth logging. |
| `src/logfile.c` | Path building, file format, name recognition, retention. |
| `src/options.c` | The command line. |

The 74-line header comment that was a duplicate user manual is gone; the README
carries all of it. What is left at the top of `src/clipboard_history.c` is the
two things a code reader needs and cannot infer: why `_WIN32_WINNT` is set
before the first Windows header, and the two traps above — that `-A` APIs
cannot spell some Desktop paths, and that Windows synthesises clipboard
formats, which is why the `CF_TEXT` branch looks dead.

## Tests

`tests\build.bat` — 156 checks, no framework, no dependencies.

Nine of them were confirmed to fail against the original behaviour, by copying
the tree, mechanically reverting each fix, and running the same suite:

```
FAIL  CP1252 e-acute becomes UTF-8 C3 A9
FAIL  REGRESSION: the result differs from _strdup(atext) ...
FAIL  a zero-length payload is ignored
FAIL  REGRESSION: refuses to build a path that would not fit
FAIL  and leaves the caller's buffer alone when it refuses
FAIL  REGRESSION: the file starts with a UTF-8 BOM
FAIL  REGRESSION: it gets a file of its own rather than truncating the first
FAIL  REGRESSION: the first session's entry is still there
FAIL  the second file is the _2 name
156 checks, 9 failed
```

Two notes on that run. The check named "a payload smaller than a DWORD is
ignored, not read past" did *not* fail against the unchecked version — reading
three bytes past a one-byte array is undefined, and on that run the bytes that
followed happened to be non-zero. The zero-length case catches it
deterministically, which is why both are there. And the non-ANSI-folder checks
do not flip: they exercise the original's failure mode in-process (the narrow
`fopen` failing) alongside the wide path succeeding, which demonstrates the bug
rather than depending on which version is compiled.

## Deliberately not done

**`test.html` is unchanged.** It is a good manual fixture — ten sections
covering a word, a paragraph, code, Unicode maths, a table, fake credentials,
an input field and a deliberate double-copy — and it already had the section
that would have caught the encoding bug. It is now documented in the README,
with the two sections worth doing deliberately called out. It was tempting to
add a Devanagari sample; that is UI, and the maths section covers the same
ground.

**The log file format is unchanged apart from the BOM**, deliberately, since
the whole point of the three-blank-line separator is that the file can be
pasted into a document without cleanup. One thing did move by a character: the
original's opening rule was 28 `=` and its closing rule 27. Both are 28 now.
Flagging it because it is a visible change to program output, small as it is.

**`appendToLog` still reopens the file for every entry.** The first review
called it a deliberate trade and it is the right one: an entry is on disk
before the next copy happens, so a forced kill loses nothing already logged.

## Notes for a future session

- Build: `src\build.bat`. Tests: `tests\build.bat`. Both want `gcc` on `PATH`;
  TDM-GCC 10.3 at `D:\TDM-GCC-64\bin` builds all of it clean under
  `-Wall -Wextra`.
- The closing line is still reachable from three places — the console control
  handler (on its own thread), `WM_DESTROY`, and `atexit`. `writeClosingOnce`
  holds the `InterlockedCompareExchange` guard and `logWriteClosing` has a
  second flag of its own for the tests. Do not add a fourth caller without
  checking both.
- `_snprintf` is not `snprintf` under MinGW: the underscore version routes to
  msvcrt, which has no `%lld`. Use the plain names; with `-std=c11` MinGW's own
  C99 implementations are what you get.
- The pause hotkey and the clipboard message loop are the only things here no
  test touches. The manual pass is the README's "Manually checking the parts
  the tests cannot reach", about ten minutes with `test.html`.

## Action checklist

- [x] **P1** Write a UTF-8 BOM and convert the `CF_TEXT` fallback to UTF-8.
- [x] **P1** Put the privacy consequences at the top of the README; add a size
      or age cap on logs.
- [x] **P2** Add a single-instance mutex.
- [x] **P2** Retry `OpenClipboard` a few times before giving up.
- [x] **P3** Move the user-manual portion of the header comment into the README.
- [x] **P3** Add a pause hotkey, a `LICENSE`, and an explanation of `test.html`.
- [x] Not on the first review's list: wide paths end to end, the unchecked
      `DWORD` read, the silent path truncation, and the same-second truncation
      of another session's file.

## Licence, 2026-10-02

The author chose one licence for all of their projects: free for personal,
non-commercial use, modifiable for the user's own use, and not to be
distributed, original or modified. No standard licence says that, so `LICENSE`
is a short custom text, the same in every project apart from the year and what
it excludes. The README's licence section says the same. Anything above that
calls the licence an open question, or calls the project MIT, is superseded.

## CI, 2026-10-02

`.github/workflows/ci.yml` runs `tests\build.bat` with the runner's MinGW gcc
on `windows-latest`. Nothing has been pushed, so it has not run on GitHub yet.
Every job was run before committing, from a fresh clone of this branch, the
way a runner would run it: Windows jobs here, in PowerShell 7 or the shell the
step names, with only the tools the job sets up on `PATH`. 156.
