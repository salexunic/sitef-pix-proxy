# deploy.ps1 — SiTef Pix Proxy Auto Deployer (sitef-pix-proxy)
# Mesma estrutura do PROD-TEF-GO/deployer/deploy.ps1 (scan -> kill -> backup -> copia -> respawn).
# Só troca a CliSiTef32I.dll (nossa proxy) e garante libenv/libcurl/libemv.
# Uso: .\deploy.ps1 [-DllDir <pasta com as DLLs>]   (sem DllDir, baixa do GitHub raw via curl.exe)
param([string]$DllDir = "")

$ErrorActionPreference = 'Stop'
trap {
    Write-Host ''
    Write-Host '================================================' -ForegroundColor Red
    Write-Host '  ERRO FATAL' -ForegroundColor Red
    Write-Host '================================================' -ForegroundColor Red
    Write-Host "  $_" -ForegroundColor Red
    Write-Host "  Linha: $($_.InvocationInfo.ScriptLineNumber)" -ForegroundColor Red
    Write-Host ''
    Write-Host 'Pressione qualquer tecla para sair...'
    $null = $Host.UI.RawUI.ReadKey('NoEcho,IncludeKeyDown')
    exit 99
}

$root        = "$PSScriptRoot"
$releaseBase = 'https://github.com/salexunic/sitef-pix-proxy/releases/download/v1.0'

Write-Host '================================================' -ForegroundColor Cyan
Write-Host '  SiTef Pix Proxy Deployer' -ForegroundColor Cyan
Write-Host '================================================' -ForegroundColor Cyan
Write-Host ''

# ------------------------------------------------------------------
# 1. Resolve as DLLs: -DllDir | dist/ local | download (curl.exe)
# ------------------------------------------------------------------
Write-Host '[1/7] Preparando DLLs...' -ForegroundColor Yellow
if ($DllDir) {
    $pkg = $DllDir
    Write-Host "  Pacote (DllDir): $pkg" -ForegroundColor Gray
} elseif (Test-Path (Join-Path $root 'dist\CliSiTef32I.dll')) {
    $pkg = Join-Path $root 'dist'
    Write-Host "  Pacote local: $pkg" -ForegroundColor Gray
} else {
    $pkg = Join-Path $env:TEMP 'sitepix-dll'
    New-Item -ItemType Directory -Force -Path $pkg | Out-Null
    foreach ($f in @('CliSiTef32I.dll', 'libenv.dll', 'libcurl32.dll', 'libemv.dll')) {
        $out = Join-Path $pkg $f
        Write-Host "  Baixando $f..." -ForegroundColor Gray
        & curl.exe -s -f -L --retry 10 --retry-delay 3 --retry-all-errors --retry-connrefused --connect-timeout 15 -o "$out" "$releaseBase/$f"
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path $out) -or (Get-Item $out).Length -eq 0) {
            Write-Host "  ERRO: falha ao baixar $f" -ForegroundColor Red
            exit 5
        }
    }
    Write-Host "  Pacote baixado: $pkg" -ForegroundColor Gray
}
Write-Host ''

# ------------------------------------------------------------------
# 2. Scan em loop — aguarda ate detectar PDV (Ctrl+C para sair)
# ------------------------------------------------------------------
Write-Host '[2/7] Aguardando PDV... (Ctrl+C para cancelar)' -ForegroundColor Yellow
$attempt = 0
$found = @()
while ($true) {
    $attempt++
    $found = @()
    Get-Process -ErrorAction SilentlyContinue | ForEach-Object {
        $proc = $_
        try {
            foreach ($m in $proc.Modules) {
                if ($m.ModuleName -like '*CliSiTef*') {
                    $found += [PSCustomObject]@{
                        Processo   = $proc.ProcessName
                        PID        = $proc.Id
                        Executavel = $proc.MainModule.FileName
                        DLL        = $m.FileName
                    }
                }
            }
        } catch {}
    }
    if ($found.Count -gt 0) {
        Write-Host ''
        Write-Host "  PDV detectado! (scan #$attempt)" -ForegroundColor Green
        break
    }
    Write-Host "  [scan #$attempt] Nenhum PDV detectado. Re-tentando em 3s..." -ForegroundColor Gray
    Start-Sleep -Seconds 3
}

Write-Host ''
$found | Format-Table -AutoSize Processo, PID, Executavel, DLL
Write-Host ''

if ($found.Count -gt 1) {
    Write-Host "$($found.Count) processos detectados. Usando o primeiro." -ForegroundColor Yellow
}

$target       = $found[0]
$processName  = $target.Processo
$targetPid    = $target.PID
$exePath      = $target.Executavel
$dllPath      = $target.DLL
$dllName      = Split-Path $dllPath -Leaf
$targetDir    = Split-Path $dllPath -Parent

Write-Host '------------------------------------------------' -ForegroundColor Gray
Write-Host "  Processo : $processName" -ForegroundColor White
Write-Host "  PID      : $targetPid" -ForegroundColor White
Write-Host "  EXE      : $exePath" -ForegroundColor White
Write-Host "  DLL      : $dllName" -ForegroundColor White
Write-Host "  Pasta    : $targetDir" -ForegroundColor White
Write-Host '------------------------------------------------' -ForegroundColor Gray
Write-Host ''

# ------------------------------------------------------------------
# 3. Matar o processo do PDV
# ------------------------------------------------------------------
Write-Host '[3/7] Encerrando processo...' -ForegroundColor Yellow
try {
    Stop-Process -Id $targetPid -Force -ErrorAction Stop
    Start-Sleep -Seconds 3
    $stillAlive = Get-Process -Id $targetPid -ErrorAction SilentlyContinue
    if ($stillAlive) {
        Write-Host "ERRO: Nao foi possivel matar $processName (PID $targetPid)" -ForegroundColor Red
        Write-Host 'Feche o PDV manualmente e execute novamente.' -ForegroundColor Red
        Write-Host 'Pressione qualquer tecla para sair...'
        $null = $Host.UI.RawUI.ReadKey('NoEcho,IncludeKeyDown')
        exit 2
    }
    Write-Host "  Processo $processName encerrado." -ForegroundColor Green
} catch {
    Write-Host "ERRO ao matar processo: $_" -ForegroundColor Red
    Write-Host 'Pressione qualquer tecla para sair...'
    $null = $Host.UI.RawUI.ReadKey('NoEcho,IncludeKeyDown')
    exit 2
}
Write-Host ''

# ------------------------------------------------------------------
# 4. Backup da DLL atual
# ------------------------------------------------------------------
Write-Host '[4/7] Backup da DLL atual...' -ForegroundColor Yellow
if (Test-Path $dllPath) {
    $backupPath = "$dllPath.bkp"
    Write-Host "  Origem : $dllPath" -ForegroundColor Gray
    Write-Host "  Backup : $dllName.bkp" -ForegroundColor Gray
    if (Test-Path $backupPath) { Remove-Item $backupPath -Force }
    Rename-Item $dllPath $backupPath -Force
    Write-Host '  Backup concluido.' -ForegroundColor Green
} else {
    Write-Host '  DLL atual nao encontrada no caminho esperado.' -ForegroundColor Yellow
    Write-Host "  Caminho esperado: $dllPath" -ForegroundColor Yellow
    Write-Host '  Prosseguindo com a instalacao das novas DLLs...' -ForegroundColor Yellow
}
Write-Host ''

# ------------------------------------------------------------------
# 5. Instalar a proxy + garantir libs (retry anti-lock)
# ------------------------------------------------------------------
Write-Host '[5/7] Instalando DLLs...' -ForegroundColor Yellow

if (-not (Test-Path $targetDir -PathType Container)) {
    Write-Host "ERRO: Pasta destino nao existe: $targetDir" -ForegroundColor Red
    Write-Host 'Pressione qualquer tecla para sair...'
    $null = $Host.UI.RawUI.ReadKey('NoEcho,IncludeKeyDown')
    exit 3
}

function Copy-DllWithRetry($Source, $Dest, $Description) {
    if (-not (Test-Path $Source)) {
        Write-Host "  ERRO: $Description nao encontrado no pacote!" -ForegroundColor Red
        return $false
    }
    $maxRetries = 5
    for ($retry = 1; $retry -le $maxRetries; $retry++) {
        try {
            if (Test-Path $Dest) { Remove-Item $Dest -Force -ErrorAction Stop }
            Copy-Item $Source $Dest -Force -ErrorAction Stop
            $size = (Get-Item $Dest).Length
            Write-Host "  OK: $Description ($size bytes)" -ForegroundColor Green
            return $true
        } catch {
            if ($retry -lt $maxRetries) {
                Write-Host "  [retry $retry/$maxRetries] $Description locked, aguardando 2s..." -ForegroundColor Yellow
                Start-Sleep -Seconds 2
            } else {
                Write-Host "  ERRO: $Description -- acesso negado apos $maxRetries tentativas" -ForegroundColor Red
                return $false
            }
        }
    }
    return $false
}

$errors = @()

# nossa proxy
if (-not (Copy-DllWithRetry (Join-Path $pkg 'CliSiTef32I.dll') (Join-Path $targetDir $dllName) $dllName)) {
    $errors += $dllName
}

# libs obrigatorias (so copia se faltar)
foreach ($r in @('libenv.dll', 'libcurl32.dll', 'libemv.dll')) {
    $dst = Join-Path $targetDir $r
    if (-not (Test-Path $dst)) {
        if (-not (Copy-DllWithRetry (Join-Path $pkg $r) $dst $r)) { $errors += $r }
    } else {
        Write-Host "  OK: $r ja presente." -ForegroundColor Green
    }
}
Write-Host ''

# ------------------------------------------------------------------
# 6. Verificacao
# ------------------------------------------------------------------
Write-Host '[6/7] Verificando instalacao...' -ForegroundColor Yellow
$srcHash = (Get-FileHash (Join-Path $pkg 'CliSiTef32I.dll') -Algorithm SHA256).Hash
$dstHash = (Get-FileHash (Join-Path $targetDir $dllName) -Algorithm SHA256).Hash
Write-Host "  SHA256 destino: $dstHash" -ForegroundColor Gray
if ($srcHash -ne $dstHash) {
    Write-Host '  FALHA: checksum nao confere!' -ForegroundColor Red
    exit 4
}
Write-Host '  Checksum OK.' -ForegroundColor Green

if ($errors.Count -gt 0) {
    Write-Host ''
    Write-Host "ERROS: $($errors -join ', ') nao foram instalados!" -ForegroundColor Red
    Write-Host 'Pressione qualquer tecla para sair...'
    $null = $Host.UI.RawUI.ReadKey('NoEcho,IncludeKeyDown')
    exit 3
}
Write-Host ''

# ------------------------------------------------------------------
# 7. Respawn do PDV
# ------------------------------------------------------------------
Write-Host '[7/7] Iniciando PDV...' -ForegroundColor Yellow
if ($exePath -and (Test-Path $exePath)) {
    $exeDir = Split-Path $exePath -Parent
    Start-Process -FilePath $exePath -WorkingDirectory $exeDir
    Write-Host '  PDV iniciado.' -ForegroundColor Green
} else {
    Write-Host '  AVISO: Executavel do PDV nao encontrado. Abra manualmente.' -ForegroundColor Yellow
}
Write-Host ''
Write-Host '================================================' -ForegroundColor Cyan
Write-Host '  Deploy concluido com sucesso!' -ForegroundColor Green
Write-Host '================================================' -ForegroundColor Cyan
Write-Host ''
