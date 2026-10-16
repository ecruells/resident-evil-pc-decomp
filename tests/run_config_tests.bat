@echo off
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "RE1_TEST_VS=%%i"
if not defined RE1_TEST_VS exit /b 1
call "%RE1_TEST_VS%\Common7\Tools\VsDevCmd.bat" -arch=x86 >nul
if errorlevel 1 exit /b 1
if not exist obj\config-tests mkdir obj\config-tests
cl /nologo /EHsc /std:c++17 /D_CRT_SECURE_NO_WARNINGS /Isrc /Foobj\config-tests\ /Feobj\config-tests\test_config_save.exe tests\test_config_save.cpp src\platform\win32\platform.cpp /link user32.lib winmm.lib ws2_32.lib
if errorlevel 1 exit /b 1
obj\config-tests\test_config_save.exe "%CD%\obj\config-tests"
