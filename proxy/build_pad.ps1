# build_pad.ps1 — CliSiTef32I proxy (x86 DLL) com disfarce de tamanho.
# Crava a DLL em 11.292.672 bytes (tamanho da CliSiTef original) via resource
# RCDATA de preenchimento + VERSIONINFO idêntico ao original.
# Uso:  .\build_pad.ps1
param()
$ErrorActionPreference = "Stop"
$root   = "$PSScriptRoot"
$src    = "$root\src"
$out    = "$root\build"
$filler = "$src\filler.bin"
$target = 11292672

# localiza vcvarsall (VS 2026/2022, x86)
$vcvars = ""
foreach ($p in @("$env:ProgramFiles\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat",
                 "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat")) {
    if (Test-Path $p) { $vcvars = $p; break }
}
if (-not $vcvars) { Write-Host "ERRO: vcvarsall.bat nao encontrado" -ForegroundColor Red; exit 1 }

function Set-Filler([long]$nBytes) {
    if ($nBytes -lt 1) { $nBytes = 1 }
    $bytes = New-Object byte[] $nBytes
    for ($i = 0; $i -lt $nBytes; $i++) { $bytes[$i] = ((($i * 31) -bxor 0x5A) -band 0xFF) }
    [IO.File]::WriteAllBytes($filler, $bytes)
}

function Build-Dll {
    $c = @(
        "rc /nologo /fo `"$out\version.res`" `"$src\version.rc`"",
        "cl /nologo /c /std:c++20 /O2 /MT /W3 /EHsc /DNDEBUG /I`"$src`" /Fo`"$out\proxy.obj`" `"$src\proxy.cpp`"",
        "cl /nologo /c /std:c++20 /O2 /MT /W3 /EHsc /DNDEBUG /I`"$src`" /Fo`"$out\pix_client.obj`" `"$src\pix_client.cpp`"",
        "cl /nologo /c /std:c++20 /O2 /MT /W3 /EHsc /DNDEBUG /I`"$src`" /Fo`"$out\crypto_abecs.obj`" `"$src\crypto_abecs.cpp`"",
        "cl /nologo /c /std:c++20 /O2 /MT /W3 /EHsc /DNDEBUG /Fo`"$out\stubs_clean.obj`" `"$src\stubs_clean.cpp`"",
        "cl /nologo /c /std:c++20 /O2 /MT /W3 /EHsc /DNDEBUG /I`"$src`" /Fo`"$out\qrcodegen.obj`" `"$src\qrcodegen.cpp`"",
        "cl /nologo /c /std:c++20 /O2 /MT /W3 /EHsc /DNDEBUG /I`"$src`" /Fo`"$out\qr_gen.obj`" `"$src\qr_gen.cpp`"",
        "link /nologo /DLL /MACHINE:X86 /SUBSYSTEM:WINDOWS /DEF:`"$src\exports.def`" /OUT:`"$out\CliSiTef32I.dll`" /IMPLIB:`"$out\CliSiTef32I.lib`" `"$out\proxy.obj`" `"$out\pix_client.obj`" `"$out\crypto_abecs.obj`" `"$out\stubs_clean.obj`" `"$out\qrcodegen.obj`" `"$out\qr_gen.obj`" ws2_32.lib bcrypt.lib advapi32.lib kernel32.lib `"$out\version.res`""
    )
    $chain = ($c -join " && ")
    cmd /c "call `"$vcvars`" x86 >nul 2>&1 && cd /d `"$root`" && $chain"
    if ($LASTEXITCODE -ne 0) { throw "build falhou (exit $LASTEXITCODE)" }
}

Write-Host "[CLEAN]" -ForegroundColor Yellow
Remove-Item "$out\*.obj","$out\*.res","$out\*.exp","$out\*.lib","$out\CliSiTef32I.dll" -Force -ErrorAction SilentlyContinue

# primeira estimativa: DLL sem recurso ~366KB + overhead do diretório ~1KB
$base = 366400
Set-Filler ($target - $base - 1024)
Write-Host "[BUILD] alvo $target bytes" -ForegroundColor Yellow
Build-Dll

# ajuste fino: converge no tamanho exato (até 8 refinamentos)
for ($t = 0; $t -lt 8; $t++) {
    $size = (Get-Item "$out\CliSiTef32I.dll").Length
    $diff = $target - $size
    if ($diff -eq 0) { break }
    Set-Filler ((Get-Item $filler).Length + $diff)
    Build-Dll
}

$size = (Get-Item "$out\CliSiTef32I.dll").Length
Write-Host "[PESO] $size / $target bytes" -ForegroundColor Cyan
if ($size -eq $target) { Write-Host "[OK] dist pronto" -ForegroundColor Green }
else { Write-Host "[AVISO] nao convergiu exato ($size)" -ForegroundColor Yellow }
