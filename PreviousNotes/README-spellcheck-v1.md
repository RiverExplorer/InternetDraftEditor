# Spell checker (English) — v1

Adds English spell checking to the ProtocolDocEditor. Misspelled
words in the Text tab get a red wavy underline as you type.
Right-click a misspelled word for suggestions, for adding the word
to a dictionary, or for ignoring it for this run.

## The two dictionaries

When you add a word, the menu asks which dictionary it goes in:

- **Personal dictionary** — words you always want accepted, in
  every draft. Kept in your profile, in
  `RiverExplorer/CoOp/Dictionaries/user.dic`. One word per line.
- **Project dictionary** — words for this draft only, such as
  protocol terms and names. Kept beside the draft file, same name
  with a `.dic` extension (so `draft-royer-coop-00.xml` gets
  `draft-royer-coop-00.dic` in the same folder). It travels with
  the draft. Opening another draft swaps in that draft's words.

"Ignore word" accepts the word until the program closes. It is
never written to a file.

## What is checked

Body text, the abstract, and notes. Sourcecode and artwork blocks
are never checked (they hold literal text, not prose). Words inside
a web address or an email address are left alone. A word is letters
and digits with apostrophes and hyphens inside it, so don't, CO-OP,
and xml2rfc each count as one word.

## How it is built

Hunspell 1.7.2 (the checker LibreOffice and Firefox use) is compiled
in from source, in the `hunspell/` folder — no separate library to
install. The en_US dictionary (`dictionaries/en_US.dic` and
`en_US.aff`) rides inside the program as a Qt resource and is
installed into your profile on startup, beside the schemas.

## Install

Extract this zip into the DocEditor source folder
(`D:/Unreal/RiverStudio/RelatedProjects/CoOp/DocEditor/`), so the
files land beside the existing sources and the `hunspell/` and
`dictionaries/` folders land inside it. It replaces these files:

- `TextEditor.h`, `TextEditor.cpp` (highlighter, menu actions)
- `MainWindow.h`, `MainWindow.cpp` (owns the checker, installs
  the dictionary, swaps the project dictionary)
- `ProtocolDocEditor.pro` (Hunspell sources, new files, resource)

New files:

- `SpellChecker.h`, `SpellChecker.cpp` (Hunspell + the two word
  lists), `SpellHighlighter.h`, `SpellHighlighter.cpp` (the red
  underlines), `dictionaries.qrc` (embeds en_US.dic/.aff)
- `hunspell/` — Hunspell 1.7.2 sources
- `dictionaries/` — en_US.dic (551,762 bytes) and en_US.aff

After replacing the files, re-run qmake in Qt Creator (it prompts
when the .pro changes), then build. The first run installs
`en_US.dic` and `en_US.aff` into
`%APPDATA%/RiverExplorer/CoOp/Dictionaries/`.

## Test

- Type a misspelled word in a section body (for example `recieve`):
  it gets a red wavy underline.
- Right-click it: the menu offers `receive` (and others). Pick one
  and the word is replaced.
- Right-click a technical word (for example `xml2rfc`): Add to
  dictionary > Personal dictionary. The underline goes away, and
  it stays gone after closing and reopening the program.
- Add another word with Add to dictionary > Project dictionary.
  A `draft-royer-coop-00.dic` file appears beside the draft, with
  the word in it. Open a different draft and the word is flagged
  again there (the project list swapped); reopen this draft and it
  is accepted again.
- Type a misspelled word inside a sourcecode block: no underline.
- With no dictionary installed (delete the Dictionaries folder
  and the folder beside the exe), spell checking stays off and
  nothing is underlined.

## Status

- The Hunspell core was compiled and run on Linux here: correct
  words pass, misspellings are flagged with good suggestions, and
  adding a word makes it pass. The Qt wiring (highlighter, menu,
  dictionary install) is C++ compile-untested here — this build is
  the real test.
- On Windows the Hunspell sources are built with HUNSPELL_STATIC
  defined (in the .pro), so the library links into the exe rather
  than expecting a DLL.
