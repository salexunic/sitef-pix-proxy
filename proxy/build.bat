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

echo [1/4] Compilando teste do pix_client...
cl.exe /nologo /std:c++20 /O2 /MT /W3 /EHsc /DNDEBUG /I"%SRC%" "%SRC%\pix_client.cpp" "%TEST%\test_pix_client.cpp" /Fe:"%OUT%\pix_client_test.exe" /Fo:"%OUT%\\" /link /SUBSYSTEM:CONSOLE bcrypt.lib kernel32.lib
if errorlevel 1 ( echo ERRO: compilacao teste falhou. & exit /b 1 )

echo [2/6] Rodando teste...
"%OUT%\pix_client_test.exe"
if errorlevel 1 exit /b 1

echo [3/6] Compilando + rodando teste de cripto ABECS...
cl.exe /nologo /std:c++20 /O2 /MT /W3 /EHsc /DNDEBUG /I"%SRC%" "%SRC%\crypto_abecs.cpp" "%TEST%\test_crypto.cpp" /Fe:"%OUT%\test_crypto.exe" /Fo:"%OUT%\\" /link /SUBSYSTEM:CONSOLE bcrypt.lib advapi32.lib kernel32.lib
if errorlevel 1 ( echo ERRO: compilacao test_crypto falhou. & exit /b 1 )
"%OUT%\test_crypto.exe"
if errorlevel 1 ( echo ERRO: teste cripto falhou. & exit /b 1 )

echo [4/6] Compilando proxy DLL...
cl.exe /nologo /c /std:c++20 /O2 /MT /W3 /EHsc /DNDEBUG /I"%SRC%" /Fo:"%OUT%\\proxy.obj" "%SRC%\proxy.cpp"
if errorlevel 1 ( echo ERRO: compilacao proxy.cpp falhou. & exit /b 1 )
cl.exe /nologo /c /std:c++20 /O2 /MT /W3 /EHsc /DNDEBUG /I"%SRC%" /Fo:"%OUT%\\pix_client.obj" "%SRC%\pix_client.cpp"
if errorlevel 1 ( echo ERRO: compilacao pix_client.cpp falhou. & exit /b 1 )
cl.exe /nologo /c /std:c++20 /O2 /MT /W3 /EHsc /DNDEBUG /I"%SRC%" /Fo:"%OUT%\\crypto_abecs.obj" "%SRC%\crypto_abecs.cpp"
if errorlevel 1 ( echo ERRO: compilacao crypto_abecs.cpp falhou. & exit /b 1 )
cl.exe /nologo /c /std:c++20 /O2 /MT /W3 /EHsc /DNDEBUG /Fo:"%OUT%\\stubs_clean.obj" "%SRC%\stubs_clean.cpp"
if errorlevel 1 ( echo ERRO: compilacao stubs_clean.cpp falhou. & exit /b 1 )
cl.exe /nologo /c /std:c++20 /O2 /MT /W3 /EHsc /DNDEBUG /I"%SRC%" /Fo:"%OUT%\\qrcodegen.obj" "%SRC%\qrcodegen.cpp"
if errorlevel 1 ( echo ERRO: compilacao qrcodegen.cpp falhou. & exit /b 1 )
cl.exe /nologo /c /std:c++20 /O2 /MT /W3 /EHsc /DNDEBUG /I"%SRC%" /Fo:"%OUT%\\qr_gen.obj" "%SRC%\qr_gen.cpp"
if errorlevel 1 ( echo ERRO: compilacao qr_gen.cpp falhou. & exit /b 1 )

echo [5/6] Linkando DLL...
link.exe /nologo /DLL /MACHINE:X86 /SUBSYSTEM:WINDOWS /DEF:"%SRC%\exports.def" /OUT:"%OUT%\CliSiTef32I.dll" /IMPLIB:"%OUT%\CliSiTef32I.lib" "%OUT%\proxy.obj" "%OUT%\pix_client.obj" "%OUT%\crypto_abecs.obj" "%OUT%\stubs_clean.obj" "%OUT%\qrcodegen.obj" "%OUT%\qr_gen.obj" ws2_32.lib bcrypt.lib advapi32.lib kernel32.lib
if errorlevel 1 ( echo ERRO: link falhou. & exit /b 1 )
echo [6/6] OK: %OUT%\CliSiTef32I.dll
if errorlevel 1 ( echo ERRO: link falhou. & exit /b 1 )
echo OK: %OUT%\CliSiTef32I.dll
exit /b 0
