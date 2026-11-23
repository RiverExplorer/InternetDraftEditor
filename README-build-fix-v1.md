# Build fix v3: Clean actually cleans (Bug 480)

Two files, flat, into the DocEditor folder (next to the sources),
replacing the current ProtocolDocEditor.pro (and deleting the
rm.bat from v1 if you extracted it):

- ProtocolDocEditor.pro (3,382 bytes)
- rm.ps1 (267 bytes)

Then in Qt Creator: Build > Run qmake, then Clean, then Rebuild.

## What was wrong

qmake's default delete command is Unix `rm -f`, which is not a
Windows command. Every Clean step failed with "The system
cannot find the file specified", the errors were marked
"(ignored)", so Clean reported success but deleted nothing.
The old .o files stayed, and because a newly extracted source
file can carry an older timestamp than the .o (the dl-paste
TextEditor.cpp was 13:43, the .o built ~13:55), make said
"Nothing to be done for 'first'" and never compiled the fix.
That is why the definition-list paste showed no change.

## Why v1 and v2 did not work

v1 pointed the delete commands at rm.bat. That failed: make
runs a command whose path has slashes under bash, and bash
cannot execute a .bat file (`/usr/bin/bash: Permission denied`,
error 126).

v2 pointed them at rm.ps1 via `$$shell_path($$PWD/rm.ps1)`.
That failed too: shell_path() produces a Unix-style `/D/...`
path, and PowerShell rejects it ("The argument
'/D/.../rm.ps1' to the -File parameter does not exist").
PowerShell needs a Windows path with a drive letter.

## What v3 does

The .pro now points the delete commands at powershell.exe (a
real Windows .exe, always findable, run directly — no bash, no
cmd, no rm, no Git needed), using `$$PWD` (which gives
`D:/...`, a path PowerShell accepts) instead of shell_path():

    win32 {
        QMAKE_DEL_FILE = powershell -NoProfile -ExecutionPolicy Bypass -File $$PWD/rm.ps1
        QMAKE_DEL_DIR = powershell -NoProfile -ExecutionPolicy Bypass -File $$PWD/rm.ps1
    }

rm.ps1 deletes each file or directory it is given, ignoring
missing ones. PowerShell handles qmake's forward-slash paths
natively, so no slash conversion is needed. Other platforms
keep qmake's default; the win32 block does not affect them.

## After this

Run qmake first (so the Makefiles pick up the new delete
command), then Clean — it will actually delete the .o files
this time — then Rebuild, which will compile the new
TextEditor.cpp (the Bug 478 definition-list paste fix).

C++ / build-system change only; no editor behaviour changes.
The .pro and the .ps1 are the only files in this zip.
