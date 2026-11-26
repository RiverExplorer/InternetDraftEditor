# Blank lines v1.1 — compile fix for v1

## What this fixes
v1 did not compile. `DocModel.cpp:4421` and `4512` called
`BlankLinesIn(Reader.text())`, but `QXmlStreamReader::text()` returns
a `QStringView` in Qt 6, and `BlankLinesIn` took a `const QString&`.
There is no conversion from `QStringView` to `QString`, so the build
failed with "no matching function for call to 'BlankLinesIn'".

The fix is one line: `BlankLinesIn` now takes a `QStringView`
instead of a `const QString&`. A `QString` still converts to a
`QStringView`, so the four other call sites (which pass a `QString`)
are unchanged and still compile. Nothing else changed.

## Files
- `DocModel.cpp` (197,253 bytes) — only this file changed from v1.
  Drop it over the v1 `DocModel.cpp` and rebuild.

## How to install
Extract flat into the DocEditor source folder, replacing
`DocModel.cpp`, then rebuild. Close the editor first.

## Notes
- C++ code-reviewed only; not compiled here. Your build is the real
  test. If this still fails, paste the error and I will fix it.
