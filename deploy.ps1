# deploy.ps1 — SiTef Pix Proxy Auto Deployer (sitef-pix-proxy)
# Baseado no PROD-TEF-GO/deployer/deploy.ps1 (scan -> kill -> backup -> copia -> respawn).
# Diferença: só troca a CliSiTef32I.dll (nossa proxy) e garante libenv/libcurl/libemv.
# As DLLs vêm de dist/ (local) ou baixam do GitHub raw (curl.exe) quando roda remoto.
#
# Uso:  .\deploy.ps1                    (espera detectar o PDV)
#       .\deploy.ps1 -TargetDir <pasta> (deploy direto, sem PDV rodando)
param([string]$TargetDir = "")

$ErrorActionPreference = 'Stop'
trap {
    Write-Host ''
    Write-Host '================================================' -ForegroundColor Red
    Write-Host '  ERRO FATAL' -ForegroundColor Red
    Write-Host "  $_" -ForegroundColor Red
    Write-Host "  Linha: $($_.InvocationInfo.ScriptLineNumber)" -ForegroundColor Red
    Write-Host '================================================' -ForegroundColor Red
    exit 99
}

$root    = "$PSScriptRoot"
$baseUrl = 'https://raw.githubusercontent.com/salexunic/sitef-pix-proxy/main'

Write-Host '================================================' -ForegroundColor Cyan
Write-Host '  SiTef Pix Proxy Deployer' -ForegroundColor Cyan
Write-Host '================================================' -ForegroundColor Cyan
Write-Host ''

# ------------------------------------------------------------------
# 1. Resolve as DLLs: dist/ local OU baixa do GitHub raw (curl.exe)
# ------------------------------------------------------------------
Write-Host '[1/7] Preparando DLLs...' -ForegroundColor Yellow
$localDll = Join-Path $root 'dist\CliSiTef32I.dll'
if (Test-Path $localDll) {
    $pkg = Join-Path $root 'dist'
    Write-Host "  Pacote local: $pkg" -ForegroundColor Gray
} else {
    $pkg = Join-Path $env:TEMP 'sitepix-dll'
    New-Item -ItemType Directory -Force -Path $pkg | Out-Null
    foreach ($f in @('CliSiTef32I.dll', 'libenv.dll', 'libcurl32.dll', 'libemv.dll')) {
        $out = Join-Path $pkg $f
        Write-Host "  Baixando $f..." -ForegroundColor Gray
        & curl.exe -s -L --retry 6 --retry-delay 3 --retry-all-errors -o "$out" "$baseUrl/dist/$f"
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path $out) -or (Get-Item $out).Length -eq 0) {
            Write-Host "  ERRO: falha ao baixar $f" -ForegroundColor Red
            exit 5
        }
    }
    Write-Host "  Pacote remoto: $pkg" -ForegroundColor Gray
}
Write-Host ''

# ------------------------------------------------------------------
# 2. Detecta o alvo (PDV carregado) ou usa -TargetDir
# ------------------------------------------------------------------
Write-Host '[2/7] Detectando PDV...' -ForegroundColor Yellow
$target = $null
if ($TargetDir) {
    $target = @{ DLL = (Join-Path $TargetDir 'CliSiTef32I.dll'); Exe = $null; Pid = $null; Proc = $null }
    Write-Host "  Alvo fornecido: $TargetDir" -ForegroundColor Gray
} else {
    $attempt = 0
    while (-not $target) {
        $attempt++
        $found = @()
        Get-Process -ErrorAction SilentlyContinue | ForEach-Object {
            $proc = $_
            try {
                foreach ($m in $proc.Modules) {
                    if ($m.ModuleName -like '*CliSiTef*') {
                        $found += [PSCustomObject]@{ Proc = $proc.ProcessName; Pid = $proc.Id; Exe = $proc.MainModule.FileName; DLL = $m.FileName }
                    }
                }
            } catch {}
        }
        if ($found.Count -gt 0) {
            $f = $found[0]
            $target = @{ DLL = $f.DLL; Exe = $f.Exe; Pid = $f.Pid; Proc = $f.Proc }
            Write-Host "  PDV detectado: $($f.Proc) (PID $($f.Pid))" -ForegroundColor Green
            Write-Host "  DLL alvo: $($f.DLL)" -ForegroundColor Gray
        } else {
            Write-Host "  [scan #$attempt] Nenhum PDV detectado. Abra o PDV, ou Ctrl+C e use -TargetDir. Re-tentando em 3s..." -ForegroundColor Yellow
            Start-Sleep -Seconds 3
        }
    }
}
$dllPath = $target.DLL
$dllName = Split-Path $dllPath -Leaf
$destDir = Split-Path $dllPath -Parent
Write-Host ''

# ------------------------------------------------------------------
# 3. Mata o PDV
# ------------------------------------------------------------------
Write-Host '[3/7] Encerrando PDV...' -ForegroundColor Yellow
if ($target.Pid) {
    try {
        Stop-Process -Id $target.Pid -Force -ErrorAction Stop
        Start-Sleep -Seconds 3
        if (Get-Process -Id $target.Pid -ErrorAction SilentlyContinue) {
            Write-Host '  ERRO: nao consegui matar o processo. Feche o PDV manualmente.' -ForegroundColor Red
            exit 2
        }
        Write-Host "  Processo $($target.Proc) encerrado." -ForegroundColor Green
    } catch {
        Write-Host "  ERRO ao matar: $_" -ForegroundColor Red
        exit 2
    }
} else {
    Write-Host '  PDV nao estava rodando (pula).' -ForegroundColor Gray
}
Write-Host ''

# ------------------------------------------------------------------
# 4. Backup da DLL atual
# ------------------------------------------------------------------
Write-Host '[4/7] Backup da DLL atual...' -ForegroundColor Yellow
if (Test-Path $dllPath) {
    $ts = Get-Date -Format 'yyyyMMdd_HHmmss'
    $backup = "$dllPath.bak-$ts"
    Copy-Item $dllPath $backup -Force
    Write-Host "  Backup: $backup" -ForegroundColor Green
} else {
    Write-Host "  DLL atual nao encontrada em $dllPath (instalacao nova)." -ForegroundColor Yellow
}
Write-Host ''

# ------------------------------------------------------------------
# 5. Copia a proxy + garante libs (retry anti-lock)
# ------------------------------------------------------------------
Write-Host '[5/7] Instalando DLLs...' -ForegroundColor Yellow
function Copy-DllWithRetry($Source, $Dest, $Desc) {
    if (-not (Test-Path $Source)) { Write-Host "  ERRO: $Desc nao encontrado no pacote!" -ForegroundColor Red; return $false }
    for ($r = 1; $r -le 5; $r++) {
        try {
            if (Test-Path $Dest) { Remove-Item $Dest -Force -ErrorAction Stop }
            Copy-Item $Source $Dest -Force -ErrorAction Stop
            Write-Host "  OK: $Desc ($((Get-Item $Dest).Length) bytes)" -ForegroundColor Green
            return $true
        } catch {
            if ($r -lt 5) { Write-Host "  [retry $r/5] $Desc lockado, aguardando 2s..." -ForegroundColor Yellow; Start-Sleep 2 }
            else { Write-Host "  ERRO: $Desc — acesso negado apos 5 tentativas" -ForegroundColor Red; return $false }
        }
    }
    return $false
}

$errors = @()
if (-not (Copy-DllWithRetry (Join-Path $pkg 'CliSiTef32I.dll') $dllPath $dllName)) { $errors += $dllName }
foreach ($r in @('libenv.dll', 'libcurl32.dll', 'libemv.dll')) {
    $dst = Join-Path $destDir $r
    if (-not (Test-Path $dst)) {
        if (-not (Copy-DllWithRetry (Join-Path $pkg $r) $dst $r)) { $errors += $r }
    } else {
        Write-Host "  OK: $r ja presente." -ForegroundColor Green
    }
}
Write-Host ''

# ------------------------------------------------------------------
# 6. Verificacao (checksum)
# ------------------------------------------------------------------
Write-Host '[6/7] Verificando...' -ForegroundColor Yellow
$srcHash = (Get-FileHash (Join-Path $pkg 'CliSiTef32I.dll') -Algorithm SHA256).Hash
$dstHash = (Get-FileHash $dllPath -Algorithm SHA256).Hash
Write-Host "  SHA256 destino: $dstHash" -ForegroundColor Gray
if ($srcHash -ne $dstHash) {
    Write-Host '  FALHA: checksum nao confere!' -ForegroundColor Red
    exit 4
}
Write-Host '  Checksum OK.' -ForegroundColor Green
if ($errors.Count -gt 0) {
    Write-Host "  ERROS: $($errors -join ', ') nao instalados!" -ForegroundColor Red
    exit 3
}
Write-Host ''

# ------------------------------------------------------------------
# 7. Respawn do PDV
# ------------------------------------------------------------------
Write-Host '[7/7] Reabrindo PDV...' -ForegroundColor Yellow
if ($target.Exe -and (Test-Path $target.Exe)) {
    Start-Process -FilePath $target.Exe -WorkingDirectory (Split-Path $target.Exe -Parent)
    Write-Host "  PDV iniciado: $($target.Exe)" -ForegroundColor Green
} else {
    Write-Host '  Abra o PDV manualmente pelo atalho.' -ForegroundColor Yellow
}
Write-Host ''
Write-Host '================================================' -ForegroundColor Cyan
Write-Host '  Deploy concluido!' -ForegroundColor Green
Write-Host '================================================' -ForegroundColor Cyan
