@echo off
setlocal EnableExtensions DisableDelayedExpansion

if /I "%VSCMD_ARG_TGT_ARCH%"=="x86" goto :toolchain_ready

set "VCVARS="
if exist "%ProgramFiles%\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat"
if not defined VCVARS if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" set "VCVARS=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
if not defined VCVARS (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if exist "%VSWHERE%" for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VSROOT=%%I"
    if defined VSROOT set "VCVARS=%VSROOT%\VC\Auxiliary\Build\vcvarsall.bat"
)
if not defined VCVARS (
    echo ERRO: vcvarsall.bat nao encontrado.
    exit /b 2
)
call "%VCVARS%" x86
if errorlevel 1 ( echo ERRO: vcvarsall.bat falhou. & exit /b 2 )

:toolchain_ready
set "ROOT=%~dp0"
set "SRC=%ROOT%src"
set "TEST=%ROOT%tests"
set "OUT=%ROOT%build"
if not exist "%OUT%" mkdir "%OUT%"

echo [1/2] Compilando testes...
cl.exe /nologo /std:c++20 /O2 /MT /W3 /EHsc /I"%SRC%" "%SRC%\pix_core.cpp" "%TEST%\test_pix.cpp" /Fe:"%OUT%\pixcore_test.exe" /Fo:"%OUT%\\" /link /SUBSYSTEM:CONSOLE kernel32.lib
if errorlevel 1 ( echo ERRO: compilacao teste falhou. & exit /b 1 )

echo [2/2] Rodando testes...
"%OUT%\pixcore_test.exe"
exit /b %ERRORLEVEL%
