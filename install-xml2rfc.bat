@echo off
REM ================================================================
REM  install-xml2rfc.bat
REM
REM  Installs the tools InternetDraftEditor needs to turn a draft
REM  into .txt, .html and .pdf files:
REM    1. xml2rfc     - the tool that makes .txt / .html / .pdf
REM    2. WeasyPrint  - the PDF engine xml2rfc uses for .pdf
REM    3. GTK 3 runtime - the Windows libraries WeasyPrint needs
REM       (Pango, Cairo and friends)
REM    4. Roboto Mono - a monospace font, so the ASCII artwork in
REM       a figure lines up in the PDF instead of looking skewed
REM
REM  What this script does NOT do:
REM    - It does NOT install Python. If Python is not on your
REM      computer you must install it first (the script tells
REM      you how and then stops).
REM    - It does NOT click through the GTK installer for you.
REM      The GTK installer is a normal Windows installer: this
REM      script downloads it and opens it, and you click
REM      Next / Install / Finish yourself.
REM    - The Roboto Mono step needs install-roboto-mono.ps1 in the
REM      same folder as this .bat file. If it is not there, the
REM      script tells you and moves on.
REM
REM  Run it by double-clicking it, or from a Command Prompt.
REM  At the end it prints what worked and what did not, and waits
REM  for you to press a key so you can read it.
REM
REM  NOTE: this script was written on a Linux machine and has NOT
REM  been run on Windows yet. If a step fails, the message it
REM  prints says what to do by hand (see INSTALL-xml2rfc.md).
REM ================================================================
setlocal

REM Keep track of whether anything failed (0 = all good so far).
set FAILED=0
set GTKDIR=C:\Program Files\GTK3-Runtime Win64
set GTKDLL=%GTKDIR%\bin\libpango-1.0-0.dll

echo.
echo  ===== InternetDraftEditor - install xml2rfc and friends =====
echo.

REM ---------- Step 1: is Python there, and is it 64-bit? ----------
echo [1/5] Checking for Python...
where python >nul 2>&1
if not errorlevel 1 goto :PythonFound
echo   Python was not found on PATH.
echo.
echo   Install 64-bit Python 3 first:
echo     1. Go to https://www.python.org/downloads/
echo     2. Download the 64-bit Windows installer.
echo     3. Run it, and CHECK the box "Add Python to PATH"
echo        before you click Install.
echo     4. Close this window, open a new Command Prompt,
echo        and run this .bat again.
echo.
set FAILED=1
goto :Summary
:PythonFound
python --version
if not errorlevel 1 goto :PythonRuns
echo   Python is on PATH but "python --version" failed.
echo   Reinstall 64-bit Python 3 from python.org, checking
echo   "Add Python to PATH", then run this .bat again.
set FAILED=1
goto :Summary
:PythonRuns
REM Bitness matters: the GTK libraries below are 64-bit only, so
REM Python must be 64-bit too. (If this check itself fails we warn
REM and carry on; the WeasyPrint check at the end will catch a
REM real mismatch.)
set PYBITS=
for /f %%A in ('python -c "import sys; print(64 if sys.maxsize > 2**32 else 32)"') do set PYBITS=%%A
if not "%PYBITS%"=="32" goto :BitsNot32
echo   Python is 32-bit, but the GTK libraries are 64-bit only.
echo   Install 64-bit Python from https://www.python.org/downloads/
echo   and check "Add Python to PATH", then run this again.
set FAILED=1
goto :Summary
:BitsNot32
if "%PYBITS%"=="64" goto :BitsOk
echo   NOTE: could not confirm Python is 64-bit.
echo   Carrying on; the check at the end will catch a mismatch.
:BitsOk
echo   Python OK.

REM ---------- Step 2: pip install xml2rfc and WeasyPrint ----------
echo.
echo [2/5] Installing xml2rfc and WeasyPrint (this downloads them)...
python -m pip install --upgrade pip
if not errorlevel 1 goto :PipOk
echo   WARNING: "pip install --upgrade pip" failed. Carrying on;
echo   the install below may still work with the old pip.
:PipOk
python -m pip install xml2rfc weasyprint
if not errorlevel 1 goto :XmlOk
echo   FAILED to install xml2rfc / WeasyPrint.
echo   Are you online? Try again, or install them by hand:
echo     python -m pip install xml2rfc weasyprint
echo   See INSTALL-xml2rfc.md for the manual steps.
set FAILED=1
goto :Summary
:XmlOk
echo   xml2rfc and WeasyPrint installed.

REM ---------- Step 3: GTK 3 runtime (only if not already there) ----------
echo.
echo [3/5] Checking for the GTK 3 runtime (WeasyPrint needs it)...
if not exist "%GTKDLL%" goto :GtkDownload
echo   GTK 3 runtime already installed, skipping the download.
goto :Fonts
:GtkDownload
echo   Downloading the GTK 3 runtime installer (about 47 MB)...
powershell -NoProfile -Command "Invoke-WebRequest -Uri 'https://github.com/tschoonj/GTK-for-Windows-Runtime-Environment-Installer/releases/download/2022-01-04/gtk3-runtime-3.24.31-2022-01-04-ts-win64.exe' -OutFile '%TEMP%\gtk3-runtime.exe'"
if not errorlevel 1 goto :GtkDownloaded
echo   FAILED to download the GTK installer. Are you online?
echo   Download it by hand from:
echo     https://github.com/tschoonj/GTK-for-Windows-Runtime-Environment-Installer/releases
echo   (pick the latest gtk3-runtime-*-ts-win64.exe, 64-bit),
echo   run it, let it add itself to PATH, then run this .bat again.
set FAILED=1
goto :Summary
:GtkDownloaded
if exist "%TEMP%\gtk3-runtime.exe" goto :GtkRun
echo   The GTK installer did not arrive (no file in %TEMP%).
echo   Download it by hand from the releases page above.
set FAILED=1
goto :Summary
:GtkRun
echo   Opening the GTK installer. Click through it:
echo     Next / Install / Finish (keep the default folder, and
echo     let it add itself to PATH if it asks).
echo   This window waits until the installer closes.
start "" /wait "%TEMP%\gtk3-runtime.exe"
if exist "%GTKDLL%" goto :GtkOk
echo   The GTK libraries are still not there after the installer
echo   ran. It may have been cancelled, or installed somewhere
echo   else. Re-run the installer, or see INSTALL-xml2rfc.md.
set FAILED=1
goto :Summary
:GtkOk
echo   GTK 3 runtime installed.

:Fonts
REM ---------- Step 4: Roboto Mono font (per-user, no admin) ----------
echo.
echo [4/5] Installing the Roboto Mono font (per-user, no admin)...
if exist "%~dp0install-roboto-mono.ps1" goto :FontRun
echo   install-roboto-mono.ps1 was not found next to this .bat,
echo   so the font step is skipped. To install the font by hand,
echo   see "Install a monospace font" in INSTALL-xml2rfc.md.
echo   (Roboto Mono from Google Fonts, or Courier New.)
goto :Verify
:FontRun
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install-roboto-mono.ps1"
if not errorlevel 1 goto :FontOk
echo   The font install reported a problem (see above).
echo   The PDF will still be made, but the artwork may fall back
echo   to a proportional font and look skewed. See
echo   INSTALL-xml2rfc.md ("Install a monospace font") to do it
echo   by hand.
set FAILED=1
goto :Verify
:FontOk
echo   Roboto Mono installed.

:Verify
REM ---------- Step 5: verify everything works ----------
REM xml2rfc on Windows writes a tracker.log file into the
REM current folder (its syslog fallback, when "import syslog"
REM fails). This .bat often runs from Program Files, which a
REM normal user cannot write to, so "xml2rfc --version" dies
REM with PermissionError on tracker.log instead of printing
REM its version. Run the checks from %TEMP%, which is always
REM writable, so the check passes. (The editor avoids this
REM the same way: it runs xml2rfc from the draft's folder and
REM deletes tracker.log afterwards.)
cd /d "%TEMP%"
echo.
echo [5/5] Verifying the install...
REM A running Command Prompt does not pick up a PATH change made by
REM an installer, so for this check we point WeasyPrint straight
REM at the GTK libraries if they are there. (In a NEW Command
REM Prompt after this, PATH should already include GTK.)
if exist "%GTKDLL%" set "WEASYPRINT_DLL_DIRECTORIES=%GTKDIR%\bin"
echo   xml2rfc:
xml2rfc --version
if not errorlevel 1 goto :XmlVersionOk
echo   FAILED: "xml2rfc --version" did not run.
set FAILED=1
goto :WeasyCheck
:XmlVersionOk
echo   xml2rfc OK.
:WeasyCheck
echo   WeasyPrint:
python -m weasyprint --version
if not errorlevel 1 goto :WeasyOk
echo   FAILED: WeasyPrint could not start (usually it cannot
echo   find the GTK libraries, Pango and friends).
echo   - Close this window, open a NEW Command Prompt (so it
echo     picks up the new PATH), and run:
echo       python -m weasyprint --version
echo   - If it still fails, set this and try again:
echo       setx WEASYPRINT_DLL_DIRECTORIES "C:\Program Files\GTK3-Runtime Win64\bin"
echo     then open a new Command Prompt and retry.
echo   - See INSTALL-xml2rfc.md ("If WeasyPrint cannot find
echo     the GTK libraries").
set FAILED=1
goto :Summary
:WeasyOk
echo   WeasyPrint OK.

:Summary
echo.
echo  ==================== Result ====================
if "%FAILED%"=="0" goto :AllOk
echo   Something above failed. Read the messages for the step
echo   that failed, fix it by hand using INSTALL-xml2rfc.md,
echo   then run this .bat again. Steps that already worked do
echo   not need doing again.
goto :Done
:AllOk
echo   All steps finished. You should be able to make .txt,
echo   .html and .pdf from InternetDraftEditor now.
echo.
echo   Quick check: open a draft with a figure in the editor,
echo   run xml2rfc, and open the PDF. The ASCII artwork should
echo   be lined up (fixed-width), not skewed.
:Done
echo  ================================================
echo.
pause
exit /b %FAILED%
