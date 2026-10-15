# clipboard_history

A Windows console program that writes **everything you copy** to a plain-text
file on your Desktop, so you can copy from a dozen places in a row and paste
them into a document all at once at the end.

## Read this before you run it

This is a keylogger for the clipboard. That is not a criticism of it — it is
what it is for — but it means:

- **Every copy lands on disk in plain text.** Passwords, API keys, bank
  details, private messages: if you copy it while this is running, it is in a
  `.txt` file, unencrypted, readable by anything running as you.
- **Your Desktop may be synced.** On a default Windows install with OneDrive
  signed in, `Desktop` is a synced folder, so the log is uploaded as you type.
  Check where your Desktop actually is before you decide this is fine.
- **Nothing deletes the logs.** A session file per run, kept forever, until you
  remove them. `--prune-days N` exists for that; it is not automatic.
- **The exclusion tag is a courtesy, not a guarantee.** Windows lets an
  application mark clipboard data as "do not record" — password managers and
  browser autofill generally do. This program honours both forms of that tag.
  Everything else gets logged, including a password you copy out of a web page,
  a spreadsheet, or a text file.

Press **Ctrl+Alt+P** to pause logging before copying something you do not want
recorded, and again to resume. Press **Ctrl+C** to stop.

## Using it

```
clipboard_history.exe [--max-mb N] [--prune-days N]
```

| | |
| --- | --- |
| `--max-mb N` | Stop logging once this session's file reaches N megabytes. `0` removes the cap. Default 10. |
| `--prune-days N` | Before starting, delete logs older than N days. 1 to 3650. |
| `--help` | The same list. |

Only one copy runs at a time. A second launch says so and exits rather than
logging every copy twice.

## What it writes

On launch it creates `Clipboard History\` on your Desktop, and inside it a file
named for the moment it started:

```
Clipboard History\20260917_210244.txt
```

If the Desktop cannot be located, the folder is created in the working
directory instead. If a file of that name already exists — two runs in the same
second — the new session takes `20260917_210244_2.txt`; it never writes over a
log that is already there.

The file is **UTF-8 with a byte-order mark**, so Notepad, Word and VS Code all
read it back correctly whatever language you copied. Entries are separated by
three blank lines and nothing else, so the whole file can be pasted into a
document and formatted once:

```
Started: 2026-09-17 21:02:44
============================



First thing I copied



नमस्ते



============================
Closed: 2026-09-17 21:41:12
```

Copying the same thing twice in a row logs it once. An empty clipboard, and a
copy tagged as sensitive by the application that made it, are both skipped.

### The closing line

`Closed:` is written when the program shuts down in any way it is allowed to
notice: Ctrl+C, Ctrl+Break, closing the console window, "End Task", logging off
and shutting down Windows.

It is **not** written if the process is force-killed — "End Process" in Task
Manager's Details tab, or `taskkill /f`. A forced kill ends the process at the
OS level before any of its own code runs, and no Windows program can intercept
that. Entries already logged are safe: each one is flushed to disk as it
arrives, not held in a buffer.

### The size cap

Once a session's file passes `--max-mb`, the program writes a line saying so
and stops logging for the rest of that session. It does not start a second
file. Restart it for a fresh session, or raise the cap.

## Building

Needs MinGW-w64 (`gcc` on `PATH`). From the repository root:

```sh
src\build.bat
```

or directly:

```sh
gcc -std=c11 -o clipboard_history.exe src\clipboard_history.c src\clip_text.c ^
    src\logfile.c src\options.c -luser32 -lshell32
```

With MSVC, from a Developer Command Prompt:

```sh
cl /W3 src\clipboard_history.c src\clip_text.c src\logfile.c src\options.c ^
   user32.lib shell32.lib /Fe:clipboard_history.exe
```

## Tests

```sh
tests\build.bat
```

154 checks, no framework and no dependencies. They cover the text conversions,
the log file's format and naming, the retention rules and the command line —
everything except the Win32 message loop itself, which needs a real clipboard.

Several checks are labelled `REGRESSION`. Each one pins a bug this program
shipped with, and each was confirmed to fail before the fix went in. Do not
delete one without reading why it is there.

## Manually checking the parts the tests cannot reach

`test.html` is a page of copy buttons covering ten kinds of clipboard content —
a word, a paragraph, a code block, Unicode maths symbols, a table, a fake
username and password, an input field, and a button that copies the same text
twice in a row. Open it in a browser with the program running and work down it.

The two worth doing deliberately:

- **Section 6, the maths symbols.** Copy it, then open the log in Notepad. The
  symbols must come back as symbols. Reading them as `âˆ«` means the BOM is
  missing and the encoding fix has been undone.
- **Section 8, the fake credentials.** They *will* be logged, in plain text.
  That is the point of the section: a web page cannot set Windows' exclusion
  tag, so nothing stops them. See what that looks like once.

## How it works

`AddClipboardFormatListener` against a message-only window: Windows sends
`WM_CLIPBOARDUPDATE` when the clipboard changes, so nothing is polled and the
program is idle between copies.

The source is split so that the parts that decide something can be tested
without a clipboard:

| | |
| --- | --- |
| `src/clipboard_history.c` | The window, the message loop, the clipboard reads, the hotkey. |
| `src/clip_text.c` | Turning a clipboard payload into a UTF-8 entry, and deciding whether it is worth logging. |
| `src/logfile.c` | Where the file goes, what goes in it, and which files may be deleted. |
| `src/options.c` | The command line. |

Everything is UTF-16 at the Windows boundary and UTF-8 on disk. The narrow
`-A` APIs are not used anywhere, deliberately: they cannot open a path that
contains a character outside the system ANSI codepage, which includes the
Desktop of any account whose name is not Latin-1.

## Requirements

Windows Vista or later (`AddClipboardFormatListener` is the Vista-era API), and
MinGW-w64 or MSVC to build it.

Windows has a built-in clipboard history at `Win+V`. If you do not need a
per-session text file, use that instead — it keeps its contents out of a file
on your Desktop.

## License

Free for personal, non-commercial use. You may use, study and modify this
software for yourself, but you may not use it commercially, and you may not
share or publish it, in its original form or modified. See [LICENSE](LICENSE)
for the full terms.
