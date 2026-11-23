# ProtocolDocEditor - Qt 6 desktop editor for Internet-Draft text (RFCXML).
#
# qmake project file. Open this file in Qt Creator (Welcome > Open Project,
# or File > Open File) and Qt Creator will configure it with the
# "Desktop Qt 6.12.0" kit.
#
# See README.md for build instructions.

QT += core gui widgets network

CONFIG += c++20
CONFIG -= app_bundle

TARGET = ProtocolDocEditor
TEMPLATE = app
VERSION = 1.1.0

SOURCES += \
    main.cpp \
    BibCache.cpp \
    DocModel.cpp \
    DocValidator.cpp \
    StartWizard.cpp \
    TextEditor.cpp \
    LegendDialog.cpp \
    MainWindow.cpp \
    SpellChecker.cpp \
    SpellHighlighter.cpp \
    hunspell/affentry.cxx \
    hunspell/affixmgr.cxx \
    hunspell/csutil.cxx \
    hunspell/filemgr.cxx \
    hunspell/hashmgr.cxx \
    hunspell/hunspell.cxx \
    hunspell/hunzip.cxx \
    hunspell/phonet.cxx \
    hunspell/replist.cxx \
    hunspell/suggestmgr.cxx

HEADERS += \
    BibCache.h \
    DocModel.h \
    DocValidator.h \
    StartWizard.h \
    TextEditor.h \
    LegendDialog.h \
    MainWindow.h \
    SpellChecker.h \
    SpellHighlighter.h

# The v3 schemas ride inside the program as Qt resources and are
# installed into the user's roaming profile on startup; see
# EnsureSchemasInstalled in MainWindow.cpp.
RESOURCES += schemas.qrc

# The en_US spell checker dictionary rides inside the program
# as Qt resources and is installed into the user's roaming
# profile on startup; see EnsureDictionariesInstalled in
# MainWindow.cpp. The personal dictionary (user.dic) is not a
# resource: it holds the writer's own words and is created on
# first use.
RESOURCES += dictionaries.qrc

# Hunspell (the spell checker library) is compiled in from
# source, in the hunspell folder; see SpellChecker.h. Its
# headers are included as hunspell/hunspell.hxx. HUNSPELL_STATIC
# is required: without it the headers assume a DLL on Windows
# and the link fails looking for the Hunspell symbols there.
INCLUDEPATH += $$PWD/hunspell
DEFINES += HUNSPELL_STATIC

# The IETF templates ride inside the program as Qt resources and
# are read straight from the resource at creation time; see
# StartWizard::accept (Bug 431).
RESOURCES += templates.qrc

# libxml2 (static MinGW build) for in-app RelaxNG validation. The
# headers and libraries live beside this file, in include/ and lib/
# (moved into the tree for GitHub, 2026-11-23); see
# README-libxml2-mingw.md there. LIBXML_STATIC is required:
# without it the headers assume a DLL.
INCLUDEPATH += $$PWD/include/libxml2 $$PWD/include
LIBS += -L$$PWD/lib -lxml2 -lz -liconv -lbcrypt
DEFINES += LIBXML_STATIC

# Windows-native delete for clean (Bug 480).
#
# qmake's default delete command is Unix rm -f, which is not a
# Windows command: Clean fails with "The system cannot find the
# file specified" and removes nothing, so Rebuild reports
# "Nothing to be done" and new sources never compile. rm.ps1
# (next to this file) is run by powershell.exe (a real Windows
# .exe, always findable) and deletes each file or directory it
# is given, ignoring missing ones. A .bat does NOT work here:
# make runs it under bash, which cannot execute a .bat
# ("Permission denied", error 126). Use $$PWD, NOT
# $$shell_path(): shell_path() produces a Unix-style /D/...
# path, which PowerShell rejects ("does not exist"); $$PWD
# gives D:/... , which PowerShell accepts. Windows-only; other
# platforms keep qmake's default.
win32 {
    QMAKE_DEL_FILE = powershell -NoProfile -ExecutionPolicy Bypass -File $$PWD/rm.ps1
    QMAKE_DEL_DIR = powershell -NoProfile -ExecutionPolicy Bypass -File $$PWD/rm.ps1
}
