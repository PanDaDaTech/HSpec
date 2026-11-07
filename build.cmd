@echo off
rem Build HSpec for the current Visual Studio developer prompt.
rem Usage: open "x64 Native Tools Command Prompt" (or run VsDevCmd.bat) and execute build.cmd.
setlocal
if not exist build mkdir build
if not exist dist mkdir dist

rc /nologo /c65001 /fo build\hspec.res hspec.rc
if errorlevel 1 exit /b 1

cl /nologo /utf-8 /O2 /MT /EHsc /std:c++17 /D_WIN32_WINNT=0x0601 ^
   hspec.cpp build\hspec.res ^
   /link /OUT:dist\HSpec.exe /SUBSYSTEM:WINDOWS ^
   gdiplus.lib crypt32.lib advapi32.lib shell32.lib setupapi.lib user32.lib gdi32.lib
if errorlevel 1 exit /b 1

echo Built dist\HSpec.exe
