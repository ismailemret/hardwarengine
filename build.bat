@echo off
setlocal
set "ROOT=%~dp0"
call :clean_artifacts
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64
if errorlevel 1 exit /b %errorlevel%
cl.exe /O2 /EHsc /nologo /Fe"%ROOT%bin\hardwarengine.exe" "%ROOT%src\main.c" "%ROOT%src\ram_sensor.c" "%ROOT%src\cpu_sensor.c" "%ROOT%src\self_sensor.c" "%ROOT%src\kernel_utils.c" /I"%ROOT%include" /link /DEBUG:NONE /MANIFEST:EMBED /MANIFESTUAC:NO /MANIFESTINPUT:"%ROOT%app.manifest" kernel32.lib psapi.lib advapi32.lib
set "BUILD_EXIT=%errorlevel%"
call :clean_artifacts
exit /b %BUILD_EXIT%

:clean_artifacts
for /r "%ROOT%" %%F in (*.obj *.pdb *.ilk *.idb) do del /q "%%F" >nul 2>&1
exit /b 0
