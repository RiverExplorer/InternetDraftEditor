; InternetDraftsEditor installer script (Inno Setup 6)
;
; Build the setup.exe in two steps:
;   1. Build in release mode. The .pro file runs windeployqt
;      automatically after a release link (QMAKE_POST_LINK), so
;      the deploy folder (D:\DocEditor\deploy) is filled with
;      the exe plus the Qt DLLs and plugins it needs, and there
;      is no command to remember. To re-run it by hand, use:
;
;        g:\Qt\6.12.0\llvm-mingw_64\bin\windeployqt.exe --dir D:\DocEditor\deploy D:\DocEditor\build\Desktop_Qt_6_12_0_llvm_mingw_64_bit_Release\release\InternetDraftsEditor.exe
;
;      (If Qt lives somewhere else, use that path to
;      windeployqt.exe.) The schemas, templates, and spell
;      dictionaries are compiled into the exe as resources, so
;      there are no separate data files to ship.
;   2. Open this file in Inno Setup Compiler and press Compile
;      (or Build > Compile). The setup.exe lands in D:\DocEditor.
;
; The build produces InternetDraftsEditor.exe (TARGET in the
; .pro), so the exe, the deploy folder, and the installed
; program all carry the same name. If you would rather ship it
; under a different name, change MyAppExeName below (and
; MyAppName to match) and the TARGET in the .pro.
;
; To ship a new version, bump MyAppVersion below and re-run both
; steps. The AppId must never change: it is how Windows knows
; this is the same program when upgrading.

#define MyAppName "InternetDraftsEditor"
#define MyAppVersion "1.1.0"
#define MyAppPublisher "RiverExplorer® LLC"
#define MyAppExeName "InternetDraftsEditor.exe"
#define MyAppIconFile "D:\DocEditor\InternetDraftsEditor.ico"
#define MyAppWelcomeImage "D:\DocEditor\RiverExplorer-welcome.bmp"
#define MyAppSmallImage "D:\DocEditor\RiverExplorer-small.bmp"
#define MyAppDeployDir "D:\DocEditor\deploy"
#define MyAppOutputDir "D:\DocEditor"

[Setup]
; A unique id for this program. Never change it.
AppId={{C4237EA0-C172-4466-BF72-5AFA54FF0C34}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
; Installs under C:\Program Files\InternetDraftsEditor by default.
DefaultDirName={autopf}\RiverExplorer\{#MyAppName}
DefaultGroupName={#MyAppName}
; The uninstaller can remove the program without asking.
DisableProgramGroupPage=yes
; Where the setup.exe is written.
OutputDir={#MyAppOutputDir}
OutputBaseFilename={#MyAppName}-{#MyAppVersion}-setup
; The installer's own icon (the same .ico the exe carries).
SetupIconFile={#MyAppIconFile}
; The image on the first (welcome) page: the RiverExplorer logo,
; 164 x 314, as a BMP (Inno Setup needs BMP here, not PNG).
WizardImageFile={#MyAppWelcomeImage}
; The small logo in the top-right corner of every other page
; (folder, tasks, ready, installing): 55 x 55, BMP. Without it
; those pages show no logo at all.
WizardSmallImageFile={#MyAppSmallImage}
; Make sure the welcome page is shown (it is the default, but
; say so plainly: the logo belongs on the first page).
DisableWelcomePage=no
Compression=lzma
SolidCompression=yes
WizardStyle=modern
; 64-bit only: the program is a 64-bit build.
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; Everything windeployqt collected, including the exe, the Qt
; DLLs, and the plugin folders (platforms, imageformats, and so
; on). The deploy folder holds only runtime files, so there is
; nothing to exclude.
Source: "{#MyAppDeployDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
; Offer to start the program when the install finishes.
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent
