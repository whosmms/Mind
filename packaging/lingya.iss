; 单人开发候选安装器；不声明正式发布，不变更工程或账号数据位置。
#if VER != EncodeVer(6, 7, 4)
  #error 本交付固定使用Inno Setup 6.7.4
#endif
#ifndef StagingDir
  #error 请传入已核对的StagingDir
#endif
#ifndef OutputDir
  #error 请传入OutputDir
#endif
#ifndef SourceCommit
  #error 请传入SourceCommit
#endif

[Setup]
AppId=LingyaMind.Development
AppName=灵芽思维 开发候选
AppVersion=0.1.0
AppVerName=灵芽思维 开发候选 {#Copy(SourceCommit, 1, 7)}
AppPublisher=灵芽思维项目组
DefaultDirName={localappdata}\Programs\LingyaMind-Dev
DefaultGroupName=灵芽思维 开发候选
OutputDir={#OutputDir}
OutputBaseFilename=LingyaMind-Dev-{#Copy(SourceCommit, 1, 7)}-Setup
SetupIconFile=..\resources\icons\app.ico
UninstallDisplayIcon={app}\program\lingya.exe
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
DisableProgramGroupPage=yes
UsePreviousAppDir=no
UsePreviousTasks=no
UsePreviousLanguage=no
CreateUninstallRegKey=not SandboxCheck
CloseApplications=no
RestartApplications=no
AllowNoIcons=yes
InfoBeforeFile=开发候选说明.txt

[Languages]
Name: "chinesesimp"; MessagesFile: "Languages\ChineseSimplified.isl"

[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式"; Flags: unchecked; Check: not SandboxCheck

[Files]
Source: "{#StagingDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "install-marker.txt"; DestDir: "{app}"; DestName: ".lingya-install-root"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\灵芽思维 开发候选"; Filename: "{app}\program\lingya.exe"; WorkingDir: "{app}\program"; Check: not SandboxCheck
Name: "{autodesktop}\灵芽思维 开发候选"; Filename: "{app}\program\lingya.exe"; WorkingDir: "{app}\program"; Tasks: desktopicon; Check: not SandboxCheck

[Run]
Filename: "{app}\program\lingya.exe"; WorkingDir: "{app}\program"; Description: "启动灵芽思维"; Flags: nowait postinstall skipifsilent; Check: not SandboxCheck

[Code]
function SandboxCheck: Boolean;
begin
  Result := ExpandConstant('{param:SANDBOX|0}') = '1';
end;

function HasContents(const Path: String): Boolean;
var Found: TFindRec;
begin
  Result := False;
  if FindFirst(AddBackslash(Path) + '*', Found) then
  begin
    try
      repeat
        if (Found.Name <> '.') and (Found.Name <> '..') then
        begin
          Result := True;
          Break;
        end;
      until not FindNext(Found);
    finally
      FindClose(Found);
    end;
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var Path: String; Marker: AnsiString;
begin
  Result := '';
  Path := ExpandConstant('{app}');
  if DirExists(Path) and HasContents(Path) then
  begin
    if not LoadStringFromFile(AddBackslash(Path) + '.lingya-install-root', Marker) then
      Result := '目标文件夹有其他内容，请选择一个空文件夹。'
    else if Trim(String(Marker)) <> 'LingyaMind-development' then
      Result := '目标文件夹不属于本软件，请选择一个空文件夹。';
  end;
end;
