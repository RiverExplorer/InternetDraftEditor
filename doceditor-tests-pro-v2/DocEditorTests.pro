# DocEditorTests - Qt Test suite for the InternetDraftsEditor (Bug 462).
#
# qmake project file. This file lives in the tests folder under the
# editor sources (DocEditor/tests/): open THIS file in Qt Creator
# with the same kit as the editor ("Desktop Qt 6.12.0"), build, and
# run. Do NOT open it from the DocEditor folder itself: the
# ../DocModel.cpp paths and the ../../../include paths only resolve
# from inside tests/. See README.md here.
#
# The tests compile the editor sources they drive, unchanged;
# the editor project itself is untouched.

QT += core gui widgets testlib

CONFIG += c++20 testcase
CONFIG -= app_bundle

TARGET = DocEditorTests
TEMPLATE = app

# The editor headers live one folder up.
INCLUDEPATH += $$PWD/..

# libxml2 (static MinGW build) — the editor sources now use libxml2
# directly for the document model, so the test build needs the same
# include and link setup as the editor build (see
# InternetDraftsEditor.pro). The headers and libraries live in
# RelatedProjects/include and RelatedProjects/lib; this folder sits
# one level deeper than the editor folder, so the paths go one level
# further up. LIBXML_STATIC is required: without it the headers
# assume a DLL.
INCLUDEPATH += $$PWD/../../../include/libxml2 $$PWD/../../../include
LIBS += -L$$PWD/../../../lib -lxml2 -lz -liconv -lbcrypt
DEFINES += LIBXML_STATIC

SOURCES += \
    DocEditorTests.cpp \
    ../DocModel.cpp \
    ../TextEditor.cpp

HEADERS += \
    ../DocModel.h \
    ../TextEditor.h

# Windows-native delete for clean (Bug 480, same as the editor).
#
# qmake's default delete command is Unix rm -f, which is not a
# Windows command: Clean fails and removes nothing. rm.ps1 (in the
# DocEditor folder, one level up from here) is run by powershell.exe
# and deletes each file or directory it is given, ignoring missing
# ones. Use $$PWD, NOT $$shell_path(): shell_path() produces a
# Unix-style /D/... path, which PowerShell rejects; $$PWD gives
# D:/... , which PowerShell accepts. Windows-only; other platforms
# keep qmake's default.
win32 {
    QMAKE_DEL_FILE = powershell -NoProfile -ExecutionPolicy Bypass -File $$PWD/../rm.ps1
    QMAKE_DEL_DIR = powershell -NoProfile -ExecutionPolicy Bypass -File $$PWD/../rm.ps1
}
