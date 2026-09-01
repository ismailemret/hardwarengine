@echo off
setlocal EnableDelayedExpansion

pushd "%~dp0.."
set "ROOT=%CD%"
popd

cd /d "%ROOT%"

echo [*] Derleme baslatiliyor...
call "%ROOT%\scripts\build.bat"
if errorlevel 1 (
    echo [ERROR] Derleme basarisiz oldu.
    pause
    exit /b 1
)

:: Bağımlılıkları kontrol et ve bin/ altına hazırla
if exist "%ROOT%\PawnIOLib.dll" copy /y "%ROOT%\PawnIOLib.dll" "%ROOT%\bin\" >nul 2>&1
if exist "%ProgramFiles%\PawnIO\PawnIOLib.dll" (
    if not exist "%ROOT%\bin\PawnIOLib.dll" copy /y "%ProgramFiles%\PawnIO\PawnIOLib.dll" "%ROOT%\bin\" >nul 2>&1
)

:: VBS wrapper'ları
(
    echo Set WshShell = CreateObject^("WScript.Shell"^)
    echo WshShell.Run "cmd.exe /c setup.bat", 0, True
) > "%ROOT%\scripts\runner.vbs"

(
    echo MsgBox "hardwarengine basariyla kuruldu!" ^& vbCrLf ^& vbCrLf ^& "Kurulum Dizini:" ^& vbCrLf ^& "C:\Program Files\hardwarengine\bin\" ^& vbCrLf ^& vbCrLf ^& "Global Baslatma Kisayolu:" ^& vbCrLf ^& "[ Ctrl + Alt + Z ]", 64, "hardwarengine Kurulum Sihirbazi"
) > "%ROOT%\scripts\msg.vbs"

echo [*] IExpress SED yapilandirmasi olusturuluyor...
set "SED_FILE=%ROOT%\scripts\installer.sed"

(
    echo [Version]
    echo Class=IEXPRESS
    echo SEDVersion=3
    echo [Options]
    echo PackagePurpose=InstallApp
    echo ShowInstallProgramWindow=0
    echo HideExtractAnimation=1
    echo UseLongFileName=1
    echo InsideCompressed=0
    echo CAB_FixedSize=0
    echo CAB_ResKey=0
    echo QuietMode=0
    echo UserQuietMode=0
    echo SourceFiles=SourceFiles
    echo TargetName=%ROOT%\Setup.exe
    echo FriendlyName=hardwarengine Kurulum Sihirbazi
    echo AppLaunched=wscript.exe runner.vbs
    echo PostInstallCmd=^<None^>
    echo AdminQuietInstCmd=wscript.exe runner.vbs
    echo Prompt=hardwarengine 'C:\Program Files\hardwarengine' konumuna kurulacak. Onayliyor musunuz?
    echo FinishMessage=
    echo [SourceFiles]
    echo SourceFiles0=%ROOT%\
    echo SourceFiles1=%ROOT%\scripts\
    echo SourceFiles2=%ROOT%\bin\
    echo [SourceFiles0]
    echo %%FILE0%%=
    echo [SourceFiles1]
    echo %%FILE1%%=
    echo %%FILE2%%=
    echo %%FILE3%%=
    echo [SourceFiles2]
    echo %%FILE4%%=
    echo %%FILE5%%=
    echo %%FILE6%%=
    echo %%FILE7%%=
    echo [Strings]
    echo FILE0="app.manifest"
    echo FILE1="setup.bat"
    echo FILE2="runner.vbs"
    echo FILE3="msg.vbs"
    echo FILE4="hardwarengine.exe"
    echo FILE5="PawnIOLib.dll"
    echo FILE6="IntelMSR.bin"
    echo FILE7="Memory"
) > "%SED_FILE%"

echo [*] Setup.exe olusturuluyor...
cd /d "%ROOT%\scripts"
iexpress.exe /N installer.sed

del /q "%SED_FILE%" >nul 2>&1
del /q "%ROOT%\scripts\runner.vbs" >nul 2>&1
del /q "%ROOT%\scripts\msg.vbs" >nul 2>&1
cd /d "%ROOT%"

if exist "%ROOT%\Setup.exe" (
    echo ============================================================================
    echo [SUCCESS] Standart Windows yukleyicisi olusturuldu: %ROOT%\Setup.exe
    echo ============================================================================
) else (
    echo [ERROR] Setup.exe olusturulamadi.
)

pause
exit /b 0