@echo off
rem Runs a command inside the MSVC x64 developer environment.
rem Usage: scripts\msvc.cmd cmake --preset debug
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
%*
