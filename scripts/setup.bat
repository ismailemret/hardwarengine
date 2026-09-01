@echo off
setlocal EnableDelayedExpansion

:: 1. Yonetici Yetkisi Kontrolu
net session >nul 2>&1
if %errorlevel% neq 0 (
    powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -FilePath '%~f0' -WorkingDirectory '%~dp0' -Verb RunAs -WindowStyle Hidden -Wait"
    exit /b 0
)

set "INSTALL_DIR=%ProgramFiles%\hardwarengine"
set "SRC_DIR=%~dp0"

:: 2. Calisan sureci zorla kapat
taskkill /f /im hardwarengine.exe >nul 2>&1

:: 3. Hedef dizinleri olustur
if not exist "%INSTALL_DIR%" mkdir "%INSTALL_DIR%" >nul 2>&1
if not exist "%INSTALL_DIR%\bin" mkdir "%INSTALL_DIR%\bin" >nul 2>&1

:: 4. Eski artik dosyalari temizle
if exist "%INSTALL_DIR%\bin\run.bat" del /q "%INSTALL_DIR%\bin\run.bat" >nul 2>&1
if exist "%INSTALL_DIR%\bin\hardwarengine_core.dll" del /q "%INSTALL_DIR%\bin\hardwarengine_core.dll" >nul 2>&1

:: 5. Binary, Manifest ve Bagimliliklari Kopyala
copy /y "%SRC_DIR%hardwarengine.exe" "%INSTALL_DIR%\bin\" >nul 2>&1
copy /y "%SRC_DIR%app.manifest" "%INSTALL_DIR%\" >nul 2>&1
if exist "%SRC_DIR%PawnIOLib.dll" copy /y "%SRC_DIR%PawnIOLib.dll" "%INSTALL_DIR%\bin\" >nul 2>&1
if exist "%SRC_DIR%IntelMSR.bin" copy /y "%SRC_DIR%IntelMSR.bin" "%INSTALL_DIR%\bin\" >nul 2>&1
if exist "%SRC_DIR%Memory" copy /y "%SRC_DIR%Memory" "%INSTALL_DIR%\bin\" >nul 2>&1

:: 6. PawnIO Surucu Servisini Baslat
net start PawnIO >nul 2>&1

:: 7. Aktif Kullaniciyi Tespit Et
for /f "tokens=*" %%u in ('powershell -NoProfile -Command "(Get-CimInstance Win32_Process -Filter \"Name = 'explorer.exe'\").GetOwner().User"') do set "LOGGED_USER=%%u"
if "%LOGGED_USER%"=="" set "LOGGED_USER=%USERNAME%"

:: 8. Task Scheduler Kaydi (UAC Bypass + UI Firlatma)
schtasks /delete /tn "hardwarengine_elevated" /f >nul 2>&1
schtasks /create /tn "hardwarengine_elevated" /tr "cmd.exe /c start \"\" /d \"%INSTALL_DIR%\bin\" \"%INSTALL_DIR%\bin\hardwarengine.exe\"" /sc ONCE /st 00:00 /ru "%LOGGED_USER%" /rl HIGHEST /f >nul 2>&1

:: 9. Tum Eski Cakisan Kisayollari Temizle
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "$desk = (Get-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\User Shell Folders').Desktop; " ^
    "$desk = [System.Environment]::ExpandEnvironmentVariables($desk); " ^
    "$sm = (Get-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\User Shell Folders').Programs; " ^
    "$sm = [System.Environment]::ExpandEnvironmentVariables($sm); " ^
    "Remove-Item (Join-Path $desk 'hardwarengine.lnk') -Force -ErrorAction SilentlyContinue; " ^
    "Remove-Item (Join-Path $sm 'hardwarengine.lnk') -Force -ErrorAction SilentlyContinue; " ^
    "Remove-Item 'C:\Users\Public\Desktop\hardwarengine.lnk' -Force -ErrorAction SilentlyContinue; " ^
    "Remove-Item 'C:\ProgramData\Microsoft\Windows\Start Menu\Programs\hardwarengine.lnk' -Force -ErrorAction SilentlyContinue;" >nul 2>&1

:: 10. Hotkey SADECE Start Menu\Programs Altindaki Kisayola Yazilir
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "$desk = (Get-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\User Shell Folders').Desktop; " ^
    "$desk = [System.Environment]::ExpandEnvironmentVariables($desk); " ^
    "$sm = (Get-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\User Shell Folders').Programs; " ^
    "$sm = [System.Environment]::ExpandEnvironmentVariables($sm); " ^
    "$ws = New-Object -ComObject WScript.Shell; " ^
    "$sSm = $ws.CreateShortcut((Join-Path $sm 'hardwarengine.lnk')); " ^
    "$sSm.TargetPath = 'C:\Windows\System32\schtasks.exe'; " ^
    "$sSm.Arguments = '/run /tn \"hardwarengine_elevated\"'; " ^
    "$sSm.WorkingDirectory = 'C:\Windows\System32'; " ^
    "$sSm.Hotkey = 'Ctrl+Alt+Z'; " ^
    "$sSm.WindowStyle = 7; " ^
    "$sSm.IconLocation = '%INSTALL_DIR%\bin\hardwarengine.exe,0'; " ^
    "$sSm.Save(); " ^
    "$sDesk = $ws.CreateShortcut((Join-Path $desk 'hardwarengine.lnk')); " ^
    "$sDesk.TargetPath = 'C:\Windows\System32\schtasks.exe'; " ^
    "$sDesk.Arguments = '/run /tn \"hardwarengine_elevated\"'; " ^
    "$sDesk.WorkingDirectory = 'C:\Windows\System32'; " ^
    "$sDesk.WindowStyle = 7; " ^
    "$sDesk.IconLocation = '%INSTALL_DIR%\bin\hardwarengine.exe,0'; " ^
    "$sDesk.Save();" >nul 2>&1

:: 11. Explorer'i Yeniden Baslat (Hotkey tablosunu aninda kaydeder)
taskkill /f /im explorer.exe >nul 2>&1
start explorer.exe

:: 12. Bitis Mesaji
wscript.exe "%SRC_DIR%msg.vbs"

exit /b 0