@echo off
echo ============================================
echo  Wiz3D Uninstaller - OpenGL (x86)
echo ============================================
echo.
echo This will remove ONLY wiz3D files from the current directory.
echo No game files will be modified or deleted.
echo.
echo Current directory: %CD%
echo.
pause

echo.
echo Removing wiz3D proxy and wrapper files...
if exist "opengl32.dll" del /f /q "opengl32.dll"
if exist "nvapi.dll" del /f /q "nvapi.dll"
if exist "S3DWrapperOGL.dll" del /f /q "S3DWrapperOGL.dll"
if exist "ZLOg.dll" del /f /q "ZLOg.dll"

echo Removing wiz3D config files...
if exist "wiz3D_Config.xml" del /f /q "wiz3D_Config.xml"
if exist "BaseProfile.xml" del /f /q "BaseProfile.xml"

echo Removing wiz3D runtime files...
if exist "Statistic.xml" del /f /q "Statistic.xml"
if exist "wiz3D_proxy.log" del /f /q "wiz3D_proxy.log"
if exist "wiz3D_opengl32_proxy.log" del /f /q "wiz3D_opengl32_proxy.log"
if exist "wiz3D_ogl_crash.dmp" del /f /q "wiz3D_ogl_crash.dmp"
if exist "wiz3d.dll" del /f /q "wiz3d.dll"
if exist "NvApiProxy.log" del /f /q "NvApiProxy.log"
if exist "wiz3D_crash.dmp" del /f /q "wiz3D_crash.dmp"

echo.
echo Wiz3D files removed successfully.
echo.

REM Delete this uninstaller last
del /f /q "%~f0"
