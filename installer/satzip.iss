; SAT-ZIP Windows installer (Inno Setup). Mirrors the proven SAT-OBELISK setup:
; Root: HKA writes HKLM for a per-machine install or HKCU for per-user, so the
; file association + context menus land in the hive the current user actually sees.
#define AppName "SAT-ZIP"
#ifndef AppVersion
  #define AppVersion "0.2.0"     ; overridden by build_installer.ps1 (ISCC /DAppVersion=...)
#endif
#define AppPublisher "S.A. Thiers"
#define AppExeGui "satzip_gui.exe"
#define AppExeCli "satzip.exe"

[Setup]
AppId={{B7E1F4C2-3A56-4D8E-9F01-2C3D4E5F6A7B}}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\{#AppExeGui}
OutputDir=dist
OutputBaseFilename=SATZ-Setup-{#AppVersion}
Compression=lzma2
SolidCompression=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern
SetupIconFile=..\assets\icon.ico
LicenseFile=..\LICENSE
ChangesAssociations=yes

[Tasks]
Name: "assocsatz";  Description: "Associate .satz archives with SAT-ZIP"; GroupDescription: "Integration:"
Name: "contextmenu"; Description: "Add ""Add to SAT-ZIP archive"" / ""Extract"" to the right-click menu"; GroupDescription: "Integration:"
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "..\build\Release\{#AppExeGui}"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\Release\{#AppExeCli}"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\THIRD_PARTY_NOTICES.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExeGui}"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeGui}"; Tasks: desktopicon

[Registry]
; ---- .satz file type (Tasks: assocsatz) ----
Root: HKA; Subkey: "Software\Classes\.satz"; ValueType: string; ValueName: ""; ValueData: "SATZIP.Archive"; Flags: uninsdeletevalue; Tasks: assocsatz
Root: HKA; Subkey: "Software\Classes\SATZIP.Archive"; ValueType: string; ValueName: ""; ValueData: "SAT-ZIP Archive"; Flags: uninsdeletekey; Tasks: assocsatz
Root: HKA; Subkey: "Software\Classes\SATZIP.Archive\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#AppExeGui},0"; Tasks: assocsatz
Root: HKA; Subkey: "Software\Classes\SATZIP.Archive\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExeGui}"" --xto ""%1"""; Tasks: assocsatz
Root: HKA; Subkey: "Software\Classes\SATZIP.Archive\shell\satzip_here"; ValueType: string; ValueName: ""; ValueData: "Extract Here"; Tasks: assocsatz
Root: HKA; Subkey: "Software\Classes\SATZIP.Archive\shell\satzip_here"; ValueType: string; ValueName: "Icon"; ValueData: "{app}\{#AppExeGui},0"; Tasks: assocsatz
Root: HKA; Subkey: "Software\Classes\SATZIP.Archive\shell\satzip_here\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExeGui}"" --xhere ""%1"""; Tasks: assocsatz
Root: HKA; Subkey: "Software\Classes\SATZIP.Archive\shell\satzip_to"; ValueType: string; ValueName: ""; ValueData: "Extract to folder"; Tasks: assocsatz
Root: HKA; Subkey: "Software\Classes\SATZIP.Archive\shell\satzip_to\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExeGui}"" --xto ""%1"""; Tasks: assocsatz
Root: HKA; Subkey: "Software\Classes\SATZIP.Archive\shell\satzip_view"; ValueType: string; ValueName: ""; ValueData: "Open in SAT-ZIP"; Tasks: assocsatz
Root: HKA; Subkey: "Software\Classes\SATZIP.Archive\shell\satzip_view"; ValueType: string; ValueName: "Icon"; ValueData: "{app}\{#AppExeGui},0"; Tasks: assocsatz
Root: HKA; Subkey: "Software\Classes\SATZIP.Archive\shell\satzip_view\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExeGui}"" ""%1"""; Tasks: assocsatz

; ---- one cascading "SAT-ZIP" menu on files and folders: [Add to archive, Zip now] ----
Root: HKA; Subkey: "Software\Classes\*\shell\SATZIP"; ValueType: string; ValueName: "MUIVerb"; ValueData: "SAT-ZIP"; Flags: uninsdeletekey; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\*\shell\SATZIP"; ValueType: string; ValueName: "Icon"; ValueData: "{app}\{#AppExeGui},0"; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\*\shell\SATZIP"; ValueType: string; ValueName: "SubCommands"; ValueData: ""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\*\shell\SATZIP\shell\010add"; ValueType: string; ValueName: "MUIVerb"; ValueData: "Add to archive"; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\*\shell\SATZIP\shell\010add\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExeGui}"" ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\*\shell\SATZIP\shell\020zip"; ValueType: string; ValueName: "MUIVerb"; ValueData: "Zip now"; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\*\shell\SATZIP\shell\020zip\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExeGui}"" --zip ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\*\shell\SATZIP\shell\030zipmail"; ValueType: string; ValueName: "MUIVerb"; ValueData: "Zip now & email"; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\*\shell\SATZIP\shell\030zipmail\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExeGui}"" --zipmail ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\Directory\shell\SATZIP"; ValueType: string; ValueName: "MUIVerb"; ValueData: "SAT-ZIP"; Flags: uninsdeletekey; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\Directory\shell\SATZIP"; ValueType: string; ValueName: "Icon"; ValueData: "{app}\{#AppExeGui},0"; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\Directory\shell\SATZIP"; ValueType: string; ValueName: "SubCommands"; ValueData: ""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\Directory\shell\SATZIP\shell\010add"; ValueType: string; ValueName: "MUIVerb"; ValueData: "Add to archive"; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\Directory\shell\SATZIP\shell\010add\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExeGui}"" ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\Directory\shell\SATZIP\shell\020zip"; ValueType: string; ValueName: "MUIVerb"; ValueData: "Zip now"; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\Directory\shell\SATZIP\shell\020zip\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExeGui}"" --zip ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\Directory\shell\SATZIP\shell\030zipmail"; ValueType: string; ValueName: "MUIVerb"; ValueData: "Zip now & email"; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\Directory\shell\SATZIP\shell\030zipmail\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExeGui}"" --zipmail ""%1"""; Tasks: contextmenu
; ---- cascading "SAT-ZIP" menu on .zip: [Extract Here, Extract to folder] ----
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.zip\shell\SATZIP"; ValueType: string; ValueName: "MUIVerb"; ValueData: "SAT-ZIP"; Flags: uninsdeletekey; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.zip\shell\SATZIP"; ValueType: string; ValueName: "Icon"; ValueData: "{app}\{#AppExeGui},0"; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.zip\shell\SATZIP"; ValueType: string; ValueName: "SubCommands"; ValueData: ""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.zip\shell\SATZIP\shell\005open"; ValueType: string; ValueName: "MUIVerb"; ValueData: "Open in SAT-ZIP"; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.zip\shell\SATZIP\shell\005open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExeGui}"" ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.zip\shell\SATZIP\shell\010xhere"; ValueType: string; ValueName: "MUIVerb"; ValueData: "Extract Here"; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.zip\shell\SATZIP\shell\010xhere\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExeGui}"" --xhere ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.zip\shell\SATZIP\shell\020xto"; ValueType: string; ValueName: "MUIVerb"; ValueData: "Extract to folder"; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.zip\shell\SATZIP\shell\020xto\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExeGui}"" --xto ""%1"""; Tasks: contextmenu
; ---- cascading "SAT-ZIP" menu on .7z: [Extract Here, Extract to folder] ----
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.7z\shell\SATZIP"; ValueType: string; ValueName: "MUIVerb"; ValueData: "SAT-ZIP"; Flags: uninsdeletekey; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.7z\shell\SATZIP"; ValueType: string; ValueName: "Icon"; ValueData: "{app}\{#AppExeGui},0"; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.7z\shell\SATZIP"; ValueType: string; ValueName: "SubCommands"; ValueData: ""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.7z\shell\SATZIP\shell\005open"; ValueType: string; ValueName: "MUIVerb"; ValueData: "Open in SAT-ZIP"; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.7z\shell\SATZIP\shell\005open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExeGui}"" ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.7z\shell\SATZIP\shell\010xhere"; ValueType: string; ValueName: "MUIVerb"; ValueData: "Extract Here"; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.7z\shell\SATZIP\shell\010xhere\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExeGui}"" --xhere ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.7z\shell\SATZIP\shell\020xto"; ValueType: string; ValueName: "MUIVerb"; ValueData: "Extract to folder"; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.7z\shell\SATZIP\shell\020xto\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExeGui}"" --xto ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.tar\shell\SATZIP"; ValueType: string; ValueName: "MUIVerb"; ValueData: "SAT-ZIP"; Flags: uninsdeletekey; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.tar\shell\SATZIP"; ValueType: string; ValueName: "Icon"; ValueData: "{app}\{#AppExeGui},0"; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.tar\shell\SATZIP"; ValueType: string; ValueName: "SubCommands"; ValueData: ""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.tar\shell\SATZIP\shell\005open"; ValueType: string; ValueName: "MUIVerb"; ValueData: "Open in SAT-ZIP"; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.tar\shell\SATZIP\shell\005open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExeGui}"" ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.tar\shell\SATZIP\shell\010xhere"; ValueType: string; ValueName: "MUIVerb"; ValueData: "Extract Here"; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.tar\shell\SATZIP\shell\010xhere\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExeGui}"" --xhere ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.tar\shell\SATZIP\shell\020xto"; ValueType: string; ValueName: "MUIVerb"; ValueData: "Extract to folder"; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.tar\shell\SATZIP\shell\020xto\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExeGui}"" --xto ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.xz\shell\SATZIP"; ValueType: string; ValueName: "MUIVerb"; ValueData: "SAT-ZIP"; Flags: uninsdeletekey; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.xz\shell\SATZIP"; ValueType: string; ValueName: "Icon"; ValueData: "{app}\{#AppExeGui},0"; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.xz\shell\SATZIP"; ValueType: string; ValueName: "SubCommands"; ValueData: ""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.xz\shell\SATZIP\shell\005open"; ValueType: string; ValueName: "MUIVerb"; ValueData: "Open in SAT-ZIP"; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.xz\shell\SATZIP\shell\005open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExeGui}"" ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.xz\shell\SATZIP\shell\010xhere"; ValueType: string; ValueName: "MUIVerb"; ValueData: "Extract Here"; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.xz\shell\SATZIP\shell\010xhere\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExeGui}"" --xhere ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.xz\shell\SATZIP\shell\020xto"; ValueType: string; ValueName: "MUIVerb"; ValueData: "Extract to folder"; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.xz\shell\SATZIP\shell\020xto\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExeGui}"" --xto ""%1"""; Tasks: contextmenu

[Run]
Filename: "{app}\{#AppExeGui}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent
