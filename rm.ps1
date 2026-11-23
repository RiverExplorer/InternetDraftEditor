# rm replacement for qmake clean on Windows (Bug 480).
# Deletes each file or directory in $args, ignoring missing ones.
# PowerShell handles qmake's forward-slash paths natively.
foreach ($f in $args) { Remove-Item $f -Force -Recurse -ErrorAction SilentlyContinue }
