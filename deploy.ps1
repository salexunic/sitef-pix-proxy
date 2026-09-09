# deploy.ps1 — SiTef Pix Proxy Auto Deployer (sitef-pix-proxy)
# Copia SÓ a nossa CliSiTef32I.dll (proxy). libenv.dll / libcurl32 / libemv e
# demais libs sao da instalacao SiTef original e NAO sao tocadas.
# Fluxo: detecta PDV -> mata -> backup -> copia (retry anti-lock) -> verifica -> respawn.
#
# Uso:  .\deploy.ps1 [-Dll <caminho da CliSiTef32I.dll nova>] [-TargetDir <pasta alvo>]
#   Sem params: usa proxy\build\CliSiTef32I.dll e auto-detecta o PDV carregado.
param(
    [string]$Dll = "",
    [string]$TargetDir = ""
)

$ErrorActionPreference = 'Stop'
$root = "$PSScriptRoot"
$srcDll = if ($Dll) { $Dll } else { Join-Path $root 'proxy\build\CliSiTef32I.dll' }

Write-Host '================================================' -ForegroundColor Cyan
Write-Host '  SiTef Pix Proxy Deployer' -ForegroundColor Cyan
Write-Host '================================================' -ForegroundColor Cyan
Write-Host ''

if (-not (Test-Path $srcDll)) {
    Write-Host "ERRO: DLL nova nao encontrada: $srcDll" -ForegroundColor Red
    Write-Host 'Rode proxy\build_pad.ps1 primeiro (gera a DLL).' -ForegroundColor Yellow
    exit 3
}
$srcSize = (Get-Item $srcDll).Length
Write-Host "  DLL nova : $srcDll ($srcSize bytes)" -ForegroundColor Gray
Write-Host ''

# ------------------------------------------------------------------
# 1. Detecta o PDV (processo com CliSiTef32I.dll carregado)
# ------------------------------------------------------------------
$target = $null
if ($TargetDir) {
    $target = @{ DLL = (Join-Path $TargetDir 'CliSiTef32I.dll'); Exe = $null; Pid = $null; Proc = $null }
    Write-Host "[1/6] Alvo fornecido: $TargetDir" -ForegroundColor Yellow
} else {
    Write-Host '[1/6] Procurando PDV com CliSiTef32I.dll...' -ForegroundColor Yellow
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
        # fallback: caminho padrão do SORGES
        $fallback = 'C:\SORIODEV\SORGES\BIN\CliSiTef32I.dll'
        if (Test-Path $fallback) {
            $target = @{ DLL = $fallback; Exe = $null; Pid = $null; Proc = $null }
            Write-Host "  Nenhum PDV rodando. Usando caminho padrao: $fallback" -ForegroundColor Yellow
        } else {
            Write-Host "  ERRO: nenhum PDV detectado e caminho padrao nao existe." -ForegroundColor Red
            Write-Host "  Use: .\deploy.ps1 -TargetDir <pasta do BIN>" -ForegroundColor Yellow
            exit 2
        }
    }
}
$dllPath = $target.DLL
$dllName = Split-Path $dllPath -Leaf
$destDir = Split-Path $dllPath -Parent
Write-Host ''

# ------------------------------------------------------------------
# 2. Mata o PDV (se estiver rodando)
# ------------------------------------------------------------------
if ($target.Pid) {
    Write-Host "[2/6] Encerrando $($target.Proc) (PID $($target.Pid))..." -ForegroundColor Yellow
    try {
        Stop-Process -Id $target.Pid -Force -ErrorAction Stop
        Start-Sleep -Seconds 3
        if (Get-Process -Id $target.Pid -ErrorAction SilentlyContinue) {
            Write-Host 'ERRO: nao consegui matar o processo. Feche o PDV manualmente.' -ForegroundColor Red
            exit 2
        }
        Write-Host '  Processo encerrado.' -ForegroundColor Green
    } catch {
        Write-Host "ERRO ao matar: $_" -ForegroundColor Red
        exit 2
    }
} else {
    Write-Host '[2/6] PDV nao esta rodando (pula kill).' -ForegroundColor Gray
}
Write-Host ''

# ------------------------------------------------------------------
# 3. Backup da DLL atual
# ------------------------------------------------------------------
Write-Host '[3/6] Backup da DLL atual...' -ForegroundColor Yellow
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
# 4. Copia a DLL nova (retry anti-lock)
# ------------------------------------------------------------------
Write-Host '[4/6] Instalando nova CliSiTef32I.dll...' -ForegroundColor Yellow
$ok = $false
for ($retry = 1; $retry -le 5 -and -not $ok; $retry++) {
    try {
        Copy-Item $srcDll $dllPath -Force -ErrorAction Stop
        $ok = $true
    } catch {
        if ($retry -lt 5) {
            Write-Host "  [retry $retry/5] lockado, aguardando 2s..." -ForegroundColor Yellow
            Start-Sleep -Seconds 2
        } else {
            Write-Host "ERRO: acesso negado apos 5 tentativas ($dllPath)" -ForegroundColor Red
            exit 3
        }
    }
}
$newSize = (Get-Item $dllPath).Length
Write-Host "  OK: $dllName ($newSize bytes)" -ForegroundColor Green
Write-Host ''

# ------------------------------------------------------------------
# 4b. Garante DLLs obrigatorias do SiTef (copia do dist/ se faltar)
# ------------------------------------------------------------------
Write-Host '[4b/6] Garantindo DLLs obrigatorias (libenv/libcurl/libemv)...' -ForegroundColor Yellow
$package = Join-Path $root 'dist'
$required = @('libenv.dll', 'libcurl32.dll', 'libemv.dll')
foreach ($r in $required) {
    $src = Join-Path $package $r
    $dst = Join-Path $destDir $r
    if (-not (Test-Path $src)) {
        Write-Host "  AVISO: $r nao esta no pacote dist\ (ignorando)." -ForegroundColor Yellow
        continue
    }
    if (Test-Path $dst) {
        Write-Host "  OK: $r ja presente." -ForegroundColor Green
    } else {
        Copy-Item $src $dst -Force
        Write-Host "  Copiado: $r (faltava)." -ForegroundColor Cyan
    }
}
Write-Host ''

# ------------------------------------------------------------------
# 5. Verificacao (checksum)
# ------------------------------------------------------------------
Write-Host '[5/6] Verificando...' -ForegroundColor Yellow
$srcHash = (Get-FileHash $srcDll -Algorithm SHA256).Hash
$dstHash = (Get-FileHash $dllPath -Algorithm SHA256).Hash
Write-Host "  SHA256 origem : $srcHash" -ForegroundColor Gray
Write-Host "  SHA256 destino: $dstHash" -ForegroundColor Gray
if ($srcHash -ne $dstHash) {
    Write-Host '  FALHA: checksum nao confere!' -ForegroundColor Red
    exit 4
}
Write-Host '  Checksum OK.' -ForegroundColor Green
Write-Host ''

# ------------------------------------------------------------------
# 6. Respawn do PDV
# ------------------------------------------------------------------
Write-Host '[6/6] Reabrindo PDV...' -ForegroundColor Yellow
if ($target.Exe -and (Test-Path $target.Exe)) {
    $exeDir = Split-Path $target.Exe -Parent
    Start-Process -FilePath $target.Exe -WorkingDirectory $exeDir
    Write-Host "  PDV iniciado: $($target.Exe)" -ForegroundColor Green
} elseif ($target.Pid -and $target.Exe) {
    Write-Host "  AVISO: executavel do PDV nao encontrado ($($target.Exe)). Abra manualmente." -ForegroundColor Yellow
} else {
    Write-Host '  Abra o PDV manualmente pelo atalho.' -ForegroundColor Yellow
}
Write-Host ''
Write-Host '================================================' -ForegroundColor Cyan
Write-Host '  Deploy concluido!' -ForegroundColor Green
Write-Host '================================================' -ForegroundColor Cyan
