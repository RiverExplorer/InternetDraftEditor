# Installing xml2rfc (and the PDF tools) for InternetDraftEditor

InternetDraftEditor is the editor. **xml2rfc is a separate tool** that
turns your draft (the XML file) into the finished outputs: a `.txt`
file, an `.html` file, and a `.pdf` file. The editor calls xml2rfc
when you press **Run xml2rfc**, so xml2rfc has to be installed on
your computer first. Without it, the editor can edit a draft but
cannot produce any output.

You need four things:

| # | Thing | What it is for |
|---|-------|----------------|
| 1 | **Python 3** (64-bit) | xml2rfc is a Python program; it needs Python to run. |
| 2 | **xml2rfc** | Makes the `.txt`, `.html` and `.pdf` from your draft. |
| 3 | **WeasyPrint + GTK 3** | WeasyPrint is the part that draws the `.pdf`. It needs the GTK 3 libraries (Pango, Cairo) on Windows. |
| 4 | **A monospace font** (Roboto Mono, or Courier New) | The PDF draws ASCII artwork (figures) in a fixed-width font. Without one, the artwork falls back to a proportional font and looks skewed. |

## The easy way: run the installer script

1. Put these two files in the same folder (for example your
   Downloads folder, or next to the editor):
   - `install-xml2rfc.bat`
   - `install-roboto-mono.ps1`
2. **Double-click `install-xml2rfc.bat`.**

That is the whole install. The script:

- checks Python is there and is 64-bit (if Python is missing it
  stops and tells you how to get it — see below);
- installs xml2rfc and WeasyPrint with pip;
- downloads the GTK 3 runtime installer and opens it — you click
  through it yourself (Next / Install / Finish), and the script
  waits until you are done;
- installs the Roboto Mono font for your user account only (no
  administrator rights needed);
- then checks everything worked (`xml2rfc --version` and
  `python -m weasyprint --version`) and prints a plain result.

At the end it waits for you to press a key, so you can read what
it says. If a step failed, it says which one and what to do —
see the manual steps below for that step.

### How to tell it worked

Open a Command Prompt and type these two lines (one at a time):

```
xml2rfc --version
python -m weasyprint --version
```

Both should print a version number and no error. Then the real
check: open a draft that has a figure in the editor, press
**Run xml2rfc**, and open the PDF. The ASCII artwork in the figure
should be lined up in neat columns. If the letters drift and the
drawing looks skewed, the monospace font is still missing — see
"Install a monospace font" below.

---

## The manual way (if the script does not work for you)

Do these in order. You only need the steps that the script
reported as failed, but doing them all is fine too.

### 1. Install Python 3 (64-bit)

- Go to <https://www.python.org/downloads/> and download the
  64-bit Windows installer.
- Run it, and **check the box "Add Python to PATH"** before you
  click Install. (If you forget this, `python` will not be found
  afterwards.)
- Close the Command Prompt and open a new one, then check it:

```
python --version
```

It should print something like `Python 3.14.0`. If it says
`'python' is not recognized`, Python is not on PATH — reinstall
it with the "Add Python to PATH" box checked.

**64-bit matters.** The GTK libraries in step 3 are 64-bit only,
so Python must be 64-bit too. (If you installed Python from the
Microsoft Store, or the 32-bit installer, remove it and install
the 64-bit one from python.org.)

### 2. Install xml2rfc and WeasyPrint

In a Command Prompt, type:

```
python -m pip install xml2rfc weasyprint
```

This downloads both from PyPI and installs them. It may take a
minute. If pip itself is out of date, update it first:

```
python -m pip install --upgrade pip
```

Check them:

```
xml2rfc --version
python -m weasyprint --version
```

`xml2rfc --version` should print a version (3.35.0 at the time of
writing). `python -m weasyprint --version` will fail at this point
if the GTK libraries are not installed yet — that is expected;
do step 3, then try it again.

### 3. Install the GTK 3 libraries (for the PDF)

WeasyPrint needs Pango and Cairo, which on Windows come from the
GTK for Windows Runtime Environment.

1. Go to the releases page:
   <https://github.com/tschoonj/GTK-for-Windows-Runtime-Environment-Installer/releases>
2. Download the latest 64-bit GTK 3 installer. It is named like
   `gtk3-runtime-3.24.31-2022-01-04-ts-win64.exe` (the version and
   date in the name may be newer than this by the time you read
   this — take the latest `gtk3-runtime-*-ts-win64.exe`).
3. Run it. Keep the default folder
   (`C:\Program Files\GTK3-Runtime Win64`) and let it add itself
   to PATH if it asks.
4. **Close the Command Prompt and open a new one** — a running
   Command Prompt does not pick up a PATH change made by an
   installer. Then check:

```
python -m weasyprint --version
```

#### If WeasyPrint cannot find the GTK libraries

You will see an error about a DLL that cannot be loaded, for
example `libpango-1.0-0.dll`. Try these in order:

1. Open a **new** Command Prompt (see step 4 above) and try
   `python -m weasyprint --version` again.
2. Point WeasyPrint straight at the GTK libraries. In a Command
   Prompt, type:

```
setx WEASYPRINT_DLL_DIRECTORIES "C:\Program Files\GTK3-Runtime Win64\bin"
```

   Then close the Command Prompt, open a new one, and try
   `python -m weasyprint --version` again. (`setx` makes the
   setting permanent for your user account.)
3. Check the bitness: 64-bit Python with 64-bit GTK. Run
   `python -c "import struct; print(struct.calcsize('P')*8)"` —
   it should print `64`. If it prints `32`, you have 32-bit
   Python; install the 64-bit Python from python.org instead.
4. Fallback: install GTK through **MSYS2** instead
   (<https://www.msys2.org/> — install MSYS2, then in its
   terminal run `pacman -S mingw-w64-x86_64-gtk3`), and set
   `WEASYPRINT_DLL_DIRECTORIES` to the MSYS2 `mingw64\bin`
   folder. This is the route to take if the runtime installer
   above does not work for you.

### 4. Install a monospace font

The PDF draws ASCII artwork (the drawings in a `<figure>`) in a
monospace font. xml2rfc asks for Roboto Mono first, then Courier,
then Courier New, then any monospace font. If your computer has
**none** of those available to WeasyPrint, the artwork falls back
to a proportional font (Verdana) and the drawing looks skewed —
the letters are different widths, so the columns do not line up.

**Roboto Mono (recommended).** Get it from Google Fonts:

1. Go to <https://fonts.google.com/specimen/Roboto+Mono> and
   click **Get font** (or **Download family**) to download the
   family as a zip.
2. Open the zip. It has **2 font files in the top folder**
   (the upright and italic variable fonts) and a **`static`
   folder** with one file per weight (Thin, Light, Regular,
   Medium, Bold, and so on, upright and italic).
3. **Install ALL of them** — every file in the top folder and
   every file in the `static` folder. Select them all, right-click,
   and choose **Install** (or **Install for all users** if you
   want every account on the computer to have it). Installing
   only one weight will work, but installing all of them is the
   safe choice.
4. Close and reopen any program that was already open, so it
   sees the new font.

**Courier New (the fallback).** Most Windows computers already
have Courier New in `C:\Windows\Fonts`. If it is there, you do
not need to install anything — but if the PDF still comes out
skewed, Courier New may not be visible to WeasyPrint's font
system, and installing Roboto Mono (above) is the fix.

### 5. Verify the PDF

1. Open a draft that has a figure in the editor and press
   **Run xml2rfc**.
2. Open the PDF. The ASCII artwork should be lined up in neat
   columns.
3. If you want to be sure which font the PDF used: in most PDF
   readers, open File → Properties → Fonts. The artwork font
   should be listed as **Roboto Mono** (or Courier New) — not
   just Verdana. If only Verdana is listed, no monospace font
   was available, and the artwork will look skewed.

---

## Two things that look like errors but are not

- **"PermissionError: [Errno 13]" when making a PDF.** This means
  the PDF file is open in a viewer (a PDF reader has it locked),
  so xml2rfc cannot overwrite it. Close the PDF in the viewer and
  run it again. Or tell xml2rfc to write a new file instead, for
  example `xml2rfc --pdf -o draft-royer-00-local.pdf draft.xml`
  (the `-o` name is a new file, so nothing is locked).

- **"Unused reference" warnings.** When xml2rfc runs you may see
  warnings like `Unused reference: [RFC7991]` or
  `Unused reference: [InternetDraftEditor]`. These are harmless:
  they only mean a bibliography entry is defined but never cited
  in the text. The output files are still made correctly.

---

## Summary of the commands

```
python --version
python -m pip install --upgrade pip
python -m pip install xml2rfc weasyprint
xml2rfc --version
python -m weasyprint --version
```

GTK installer:
<https://github.com/tschoonj/GTK-for-Windows-Runtime-Environment-Installer/releases>

Roboto Mono:
<https://fonts.google.com/specimen/Roboto+Mono>

If a DLL cannot be found:
```
setx WEASYPRINT_DLL_DIRECTORIES "C:\Program Files\GTK3-Runtime Win64\bin"
```
