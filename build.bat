@echo off
rem Builds EdgThumb.dll (the shell extension) and edgdump.exe (the test harness).
rem Output lands in build\. Statically linked (/MT), so no VC++ redistributable
rem is needed on the machine that installs the DLL.

setlocal
set ROOT=%~dp0

rem vswhere sits under a path containing parentheses. Running it straight into
rem for /f trips up cmd's quoting, so route the answer through a temp file.
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSPATH="
set "VSTMP=%TEMP%\edgthumb_vspath.txt"
"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath > "%VSTMP%" 2>nul
for /f "usebackq tokens=*" %%i in ("%VSTMP%") do set "VSPATH=%%i"
del "%VSTMP%" 2>nul

if "%VSPATH%"=="" (
    echo No MSVC x64 toolset found. Install the "Desktop development with C++"
    echo workload, then run this again.
    exit /b 1
)

rem vcvars64 runs its own copy of vswhere and is noisy about it; silence both.
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 exit /b 1

if not exist "%ROOT%build" mkdir "%ROOT%build"
pushd "%ROOT%build"

set CFLAGS=/nologo /O2 /MT /W4 /EHsc /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN

rem Third-party code, left as is: C4005 (miniz defines WIN32_LEAN_AND_MEAN itself),
rem C4127 (constant conditions) and C4132 (an empty const table).
cl %CFLAGS% /wd4005 /wd4127 /wd4132 /c "%ROOT%third_party\miniz\miniz.c"
if errorlevel 1 goto fail

cl %CFLAGS% /c "%ROOT%src\edg.cpp"
if errorlevel 1 goto fail

rc /nologo /fo version.res "%ROOT%src\version.rc"
if errorlevel 1 goto fail

cl %CFLAGS% /LD "%ROOT%src\dllmain.cpp" edg.obj miniz.obj version.res ^
    /Fe:EdgThumb.dll /link /DEF:"%ROOT%src\EdgThumb.def" ole32.lib gdi32.lib
if errorlevel 1 goto fail

cl %CFLAGS% "%ROOT%src\edgdump.cpp" edg.obj miniz.obj ^
    /Fe:edgdump.exe /link ole32.lib
if errorlevel 1 goto fail

echo.
echo Built: %ROOT%build\EdgThumb.dll
echo Built: %ROOT%build\edgdump.exe
popd
endlocal
exit /b 0

:fail
echo.
echo BUILD FAILED
popd
endlocal
exit /b 1
