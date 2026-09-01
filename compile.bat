@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d "%~dp0"
cl /nologo /LD /MT /std:c++20 /O2 /EHsc /GR- /D_CRT_SECURE_NO_WARNINGS dllmain.cpp /Fe:main.dll /Fo:obj_ 1>compile.out 2>&1
set RC=%ERRORLEVEL%
type compile.out
echo COMPILE_EXIT=%RC%
if exist main.dll (echo MAIN_DLL_OK & for %%F in (main.dll) do echo size=%%~zF)
