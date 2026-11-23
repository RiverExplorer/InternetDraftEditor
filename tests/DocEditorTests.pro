# DocEditorTests - Qt Test suite for the ProtocolDocEditor (Bug 462).
#
# qmake project file. This folder sits beside the editor sources:
# open this file in Qt Creator with the same kit as the editor
# ("Desktop Qt 6.12.0"), build, and run. See README.md here.
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

SOURCES += \
    DocEditorTests.cpp \
    ../DocModel.cpp \
    ../TextEditor.cpp

HEADERS += \
    ../DocModel.h \
    ../TextEditor.h
