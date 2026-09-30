@echo off
rem Clears Explorer's thumbnail cache. Only needed when .edg files still show
rem the old blank icon after installing or rebuilding the handler.
rem
rem This deletes cached thumbnails for EVERY file type. Nothing is lost -
rem Explorer regenerates them on demand - but the first browse through a large
rem folder will be slower than usual.

setlocal
echo This clears the thumbnail cache for all file types and restarts Explorer.
choice /c YN /m "Continue"
if errorlevel 2 exit /b 0

taskkill /f /im explorer.exe >nul 2>&1
del /f /q "%LOCALAPPDATA%\Microsoft\Windows\Explorer\thumbcache_*.db" >nul 2>&1
start explorer.exe
echo Cache cleared.
endlocal
