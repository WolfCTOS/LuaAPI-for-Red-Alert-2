#!/usr/bin/env pwsh
# tools/run_benchmark.ps1
# Automated benchmark: spawn gamemd.exe, inject LuaAPI, profile via PresentMon.

# ---------------------------------------------------------------------------
# Path configuration (flexible, project-relative)
# ---------------------------------------------------------------------------
# $ProjectDir — папка со скриптом (один уровень выше tools/).
# $GameDir  — папка с игрой (по умолчанию D:\Games\Red Alert 2 или там, где лежит gamemd.exe).
# $PresentMonPath — путь к PresentMon.exe (приоритет: tools/, game-dir, PATH).

# Попытка определить ProjectDir: каталог, где лежит этот скрипт, один уровень вверх.
$scriptDir   = Split-Path -Parent -Path $MyInvocation.MyCommand.Definition
$ProjectDir  = $scriptDir -replace '\\tools$', ''
if (-not (Test-Path $ProjectDir)) { $ProjectDir = $PSScriptRoot }

# Определяем GameDir: ищем gamemd.exe в соседней папке от скрипта, иначе заданный дефолт.
$DefaultGameDir = "D:\Games\Red Alert 2"
if (Test-Path (Join-Path $ProjectDir "gamemd.exe")) {
    $GameDir = $ProjectDir
} elseif (Test-Path (Join-Path $DefaultGameDir "gamemd.exe")) {
    $GameDir = $DefaultGameDir
} else {
    # Ищем gamemd.exe в дочерних папках ProjectDir (до 3 уровней).
    $GameDir = $null
    $candidates = Get-ChildItem -Path $ProjectDir -Recurse -Filter gamemd.exe -Depth 3 |
        Select-Object -First 1 -ExpandProperty Parent
    if ($candidates) { $GameDir = $candidates.Parent }
}
if (-not $GameDir) { $GameDir = $DefaultGameDir }

# Нормализуем пути (обратные слеши).
$GameDir       = [System.IO.Path]::GetFullPath($GameDir)
$ProjectDir    = [System.IO.Path]::GetFullPath($ProjectDir)

# --- Поиск injector.exe ---
# Последовательный перебор папок: bin\Release, bin, build, корзина проекта, папка игры.
$injectorPaths = @(
    Join-Path $ProjectDir "bin\Release\injector.exe"
    Join-Path $ProjectDir "bin\injector.exe"
    Join-Path $ProjectDir "build\injector.exe"
    Join-Path $ProjectDir "injector.exe"
    Join-Path $GameDir "injector.exe"
)
$injectorExe = $injectorPaths | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $injectorExe) {
    Write-Error "injector.exe not found in expected locations."
    Write-Error "Searched: $($injectorPaths -join ', '
)"
    exit 1
}

# --- Поиск LuaAPI.dll ---
$dllPaths = @(
    Join-Path $ProjectDir "bin\Release\LuaAPI.dll"
    Join-Path $ProjectDir "bin\LuaAPI.dll"
    Join-Path $ProjectDir "build\LuaAPI.dll"
    Join-Path $ProjectDir "LuaAPI.dll"
    Join-Path $GameDir "LuaAPI.dll"
)
$dllPath = $dllPaths | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $dllPath) {
    Write-Error "LuaAPI.dll not found in expected locations."
    Write-Error "Searched: $($dllPaths -join ', ')"
    exit 1
}

# --- Поиск PresentMon.exe ---
# Приоритет: 1) $ProjectDir\tools\PresentMon.exe, 2) $GameDir\PresentMon.exe, 3) $env:PATH
$PresentMonCandidate1 = Join-Path (Join-Path $ProjectDir "tools") "PresentMon.exe"
$PresentMonCandidate2 = Join-Path $GameDir "PresentMon.exe"
$PresentMon_exe = $PresentMonCandidate1
if (-not (Test-Path $PresentMon_exe)) { $PresentMon_exe = $PresentMonCandidate2 }
if (-not (Test-Path $PresentMon_exe)) {
    # Fallback to PATH
    $PresentMon_exe = (Get-Command presentmon.exe -ErrorAction SilentlyContinue)
    if (-not $PresentMon_exe) {
        # Не нашли — выдаем инструкцию и выходим.
        Write-Host "PresentMon.exe not found." -ForegroundColor Yellow
        Write-Host "Please install PresentMon or specify its path manually." -ForegroundColor Yellow
        Write-Host "Download: https://github.com/GameTechDev/PresentMon/releases" -ForegroundColor Yellow
        exit 1
    }
}

# ---------------------------------------------------------------------------
# Проверить наличие gamemd.exe, injector.exe, LuaAPI.dll (уже проверено выше, но на всякий случай)
# ---------------------------------------------------------------------------
if (-not (Test-Path (Join-Path $GameDir "gamemd.exe"))) {
    Write-Error "gamemd.exe not found in $GameDir."
    exit 1
}

# ---------------------------------------------------------------------------
# Запуск PresentMon и сбор данных
# ---------------------------------------------------------------------------
$benchmarkDurSec = 65
$csvOutput = Join-Path $env:USERPROFILE "presentmon_benchmark.csv"
$presentmonArgs = @(
    "-csv", $csvOutput
    "-rt", "csv"
)

Write-Host "=== Red Alert 2 Benchmark Runner ===" -ForegroundColor Cyan
Write-Host "Working directory: $GameDir" -ForegroundColor Yellow
Write-Host "Benchmark duration: $benchmarkDurSec seconds" -ForegroundColor Yellow
Write-Host "CSV output: $csvOutput" -ForegroundColor Yellow
Write-Host ""

# Запуск PresentMon
Write-Host "[1/5] Starting PresentMon..." -ForegroundColor White
$pmProc = Start-Process -FilePath $PresentMon_exe -ArgumentList $presentmonArgs -PassThru -ErrorAction Stop
Write-Host "    PresentMon started (PID: $($pmProc.Id))" -ForegroundColor Gray

# Запуск игры + инжект
Write-Host "[2/5] Launching injector.exe..." -ForegroundColor White
# Используем injector.exe для запуска или инжекта.
# injector.exe по умолчанию пытается запустить gamemd.exe и инжектить.
Write-Host "    injector.exe: $injectorExe" -ForegroundColor Gray

Write-Host "[3/5] Spawning gamemd.exe..." -ForegroundColor White
$gameProc = Start-Process -FilePath (Join-Path $GameDir "gamemd.exe") -PassThru -Wait -ErrorAction Stop

Write-Host "[4/5] Waiting for benchmark duration ($benchmarkDurSec seconds)..." -ForegroundColor White
Start-Sleep -Seconds $benchmarkDurSec

Write-Host "[5/5] Stopping PresentMon and analyzing..." -ForegroundColor White
Stop-Process -Id $pmProc.Id -Force -ErrorAction SilentlyContinue

Write-Host "    PresentMon stopped." -ForegroundColor Gray

Write-Host "    Analyzing CSV with benchmark_analyzer.py..." -ForegroundColor White
$analyzer = Join-Path $ProjectDir "tools/benchmark_analyzer.py"
if (Test-Path $analyzer) {
    & powershell -Command "python3 $analyzer $csvOutput"
} else {
    Write-Warning "benchmark_analyzer.py not found at $analyzer. Please run: python tools/benchmark_analyzer.py $csvOutput"
}

Write-Host "" -ForegroundColor Cyan
Write-Host "=== Benchmark Complete ===" -ForegroundColor Cyan
Write-Host "CSV output: $csvOutput" -ForegroundColor Yellow
Write-Host "Review the analysis above for Avg FPS and 1% Low FPS." -ForegroundColor Yellow