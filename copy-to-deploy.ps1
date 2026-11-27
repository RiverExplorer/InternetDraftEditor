# copy-to-deploy.ps1 - copy the built exe into the deploy folder.
#
# Called from the .pro file's QMAKE_POST_LINK after windeployqt.
# windeployqt copies the Qt DLLs and plugins, but it does NOT
# copy the exe itself, so this step puts the exe next to them
# and the deploy folder becomes a complete, runnable copy of
# the program (double-click the exe there to test it).
#
# Usage: copy-to-deploy.ps1 <exe-path> <deploy-dir>
param([string]$ExePath, [string]$DeployDir)
Copy-Item $ExePath $DeployDir -Force
