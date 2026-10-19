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
set "LOG_FILE=%INSTALL_DIR%\bin\install.log"

:: 2. Calisan sureci kapat
taskkill /f /im hardwarengine.exe >nul 2>&1

:: 3. Hedef dizinleri olustur
if not exist "%INSTALL_DIR%" mkdir "%INSTALL_DIR%" >nul 2>&1
if not exist "%INSTALL_DIR%\bin" mkdir "%INSTALL_DIR%\bin" >nul 2>&1

echo [INIT] Kurulum: %DATE% %TIME% > "%LOG_FILE%"

:: 4. Dosyalari Kopyala
copy /y "%SRC_DIR%hardwarengine.exe" "%INSTALL_DIR%\bin\" >> "%LOG_FILE%" 2>&1
copy /y "%SRC_DIR%app.manifest" "%INSTALL_DIR%\" >> "%LOG_FILE%" 2>&1
if exist "%SRC_DIR%PawnIOLib.dll" copy /y "%SRC_DIR%PawnIOLib.dll" "%INSTALL_DIR%\bin\" >> "%LOG_FILE%" 2>&1
if exist "%SRC_DIR%IntelMSR.bin" copy /y "%SRC_DIR%IntelMSR.bin" "%INSTALL_DIR%\bin\" >> "%LOG_FILE%" 2>&1
if exist "%SRC_DIR%AMDFamily17.bin" copy /y "%SRC_DIR%AMDFamily17.bin" "%INSTALL_DIR%\bin\" >> "%LOG_FILE%" 2>&1

:: 5. PawnIO Servisi Yoksa Kur
sc query PawnIO >nul 2>&1
if %errorlevel% neq 0 (
    echo [*] PawnIO servisi bulunamadi, kurulum deneniyor... >> "%LOG_FILE%"
    
    :: Once winget uzerinden dene
    winget install -e --id namazso.PawnIO --silent --accept-source-agreements --accept-package-agreements >> "%LOG_FILE%" 2>&1
    
    :: Servis hala yoksa resmi GitHub Setup release'inden indir
    sc query PawnIO >nul 2>&1
    if !errorlevel! neq 0 (
        set "PAWNIO_URL=https://github.com/namazso/PawnIO.Setup/releases/latest/download/PawnIO_setup.exe"
        set "PAWNIO_INSTALLER=%TEMP%\PawnIO_setup.exe"
        
        powershell -NoProfile -ExecutionPolicy Bypass -Command ^
            "[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; " ^
            "$wc = New-Object System.Net.WebClient; " ^
            "$wc.Headers.Add('User-Agent', 'hardwarengine-installer'); " ^
            "$wc.DownloadFile('%PAWNIO_URL%', '%PAWNIO_INSTALLER%')" >> "%LOG_FILE%" 2>&1

        if exist "%PAWNIO_INSTALLER%" (
            start /wait "" "%PAWNIO_INSTALLER%" /S
            del /q "%PAWNIO_INSTALLER%" >nul 2>&1
        )
    )
)

net start PawnIO >nul 2>&1

:: 6. Aktif Kullaniciyi Tespit Et
for /f "tokens=*" %%u in ('powershell -NoProfile -Command "(Get-CimInstance Win32_Process -Filter \"Name = 'explorer.exe'\").GetOwner().User"') do set "LOGGED_USER=%%u"
if "%LOGGED_USER%"=="" set "LOGGED_USER=%USERNAME%"

:: 7. Task Scheduler Kaydi (DOGRADAN BINARY CALISTIRILIR - CMD YOK)
schtasks /delete /tn "hardwarengine_elevated" /f >nul 2>&1
schtasks /create /tn "hardwarengine_elevated" /tr "\"%INSTALL_DIR%\bin\hardwarengine.exe\"" /sc ONCE /st 00:00 /ru "%LOGGED_USER%" /rl HIGHEST /f >> "%LOG_FILE%" 2>&1

:: 8. Eski Kisayollari Temizle
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "$desk = (Get-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\User Shell Folders').Desktop; " ^
    "$desk = [System.Environment]::ExpandEnvironmentVariables($desk); " ^
    "$sm = (Get-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\User Shell Folders').Programs; " ^
    "$sm = [System.Environment]::ExpandEnvironmentVariables($sm); " ^
    "Remove-Item (Join-Path $desk 'hardwarengine.lnk') -Force -ErrorAction SilentlyContinue; " ^
    "Remove-Item (Join-Path $sm 'hardwarengine.lnk') -Force -ErrorAction SilentlyContinue; " ^
    "Remove-Item 'C:\Users\Public\Desktop\hardwarengine.lnk' -Force -ErrorAction SilentlyContinue; " ^
    "Remove-Item 'C:\ProgramData\Microsoft\Windows\Start Menu\Programs\hardwarengine.lnk' -Force -ErrorAction SilentlyContinue;" >nul 2>&1

:: 9. Kisayollari Olustur
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "$desk = (Get-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\User Shell Folders').Desktop; " ^
    "$desk = [System.Environment]::ExpandEnvironmentVariables($desk); " ^
    "$sm = (Get-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\User Shell Folders').Programs; " ^
    "$sm = [System.Environment]::ExpandEnvironmentVariables($sm); " ^
    "$ws = New-Object -ComObject WScript.Shell; " ^
    "$sSm = $ws.CreateShortcut((Join-Path $sm 'hardwarengine.lnk')); " ^
    "$sSm.TargetPath = 'C:\Windows\System32\schtasks.exe'; " ^
    "$sSm.Arguments = '/run /tn \"hardwarengine_elevated\"'; " ^
    "$sSm.WorkingDirectory = '%INSTALL_DIR%\bin'; " ^
    "$sSm.Hotkey = 'Ctrl+Alt+Z'; " ^
    "$sSm.WindowStyle = 7; " ^
    "$sSm.IconLocation = '%INSTALL_DIR%\bin\hardwarengine.exe,0'; " ^
    "$sSm.Save(); " ^
    "$sDesk = $ws.CreateShortcut((Join-Path $desk 'hardwarengine.lnk')); " ^
    "$sDesk.TargetPath = 'C:\Windows\System32\schtasks.exe'; " ^
    "$sDesk.Arguments = '/run /tn \"hardwarengine_elevated\"'; " ^
    "$sDesk.WorkingDirectory = '%INSTALL_DIR%\bin'; " ^
    "$sDesk.WindowStyle = 7; " ^
    "$sDesk.IconLocation = '%INSTALL_DIR%\bin\hardwarengine.exe,0'; " ^
    "$sDesk.Save();" >> "%LOG_FILE%" 2>&1

:: 10. Explorer'i Yeniden Baslat
taskkill /f /im explorer.exe >nul 2>&1
start explorer.exe

:: 11. Bitis Mesaji
if exist "%SRC_DIR%msg.vbs" wscript.exe "%SRC_DIR%msg.vbs"

exit /b 0