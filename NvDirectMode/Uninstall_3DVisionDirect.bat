@echo off
echo ============================================
echo  wiz3D NvDirectMode (3D Vision Direct) Uninstaller
echo ============================================
echo.
echo This will remove ONLY wiz3D NvDirectMode proxy files from the current
echo directory. No game files will be modified or deleted.
echo.
echo Current directory: %CD%
echo.
pause

echo.
echo Removing NvDirectMode proxy DLLs...
if exist "d3d9.dll"      del /f /q "d3d9.dll"
if exist "d3d10.dll"     del /f /q "d3d10.dll"
if exist "d3d11.dll"     del /f /q "d3d11.dll"
if exist "dxgi.dll"      del /f /q "dxgi.dll"
if exist "opengl32.dll"  del /f /q "opengl32.dll"

echo Removing NvApiProxy DLLs...
if exist "nvapi.dll"     del /f /q "nvapi.dll"
if exist "nvapi64.dll"   del /f /q "nvapi64.dll"

echo Removing NvDirectMode config files...
if exist "3DVision_Config.xml" del /f /q "3DVision_Config.xml"

echo Removing NvDirectMode runtime files...
if exist "nvdirectmode_proxy.log" del /f /q "nvdirectmode_proxy.log"
if exist "nvdirectmode_crash.dmp" del /f /q "nvdirectmode_crash.dmp"
if exist "NvApiProxy.log"         del /f /q "NvApiProxy.log"

echo.
echo NvDirectMode files removed successfully.
echo.

REM Delete this uninstaller last
del /f /q "%~f0"
