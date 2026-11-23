#!/bin/sh
PATH=/G/Qt/6.12.0/llvm-mingw_64/bin:$PATH
export PATH
QT_PLUGIN_PATH=/G/Qt/6.12.0/llvm-mingw_64/plugins${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}
export QT_PLUGIN_PATH
exec "$@"
