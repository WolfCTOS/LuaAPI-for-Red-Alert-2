#!/usr/bin/env pwsh
# tools/run_benchmark.ps1
# Automated 65-second benchmark: spawn gamemd.exe, inject LuaAPI, profile via PresentMon.

# User-configurable settings
$exeDir       = "D:\Games\Red Alert 2"   # game directory (contains gamemd.exe, LuaAPI.dll, injector.exe)
$injectorExe  = Join-Path $exeDir "injector.exe"
$dllPath      = Join-Path $exeDir "LuaAPI.dll"
$presentmonExe = "presentmon.exe"  # ensure presentmon is on PATH or specify full path
$benchmarkDurSec = 65
$csvOutput    = Join-Path $env:USERPROFILE "presentmon_benchmark.csv"

Write-Host "=== Red Alert 2 Benchmark Runner ===" -ForegroundColor Cyan
Write-Host "Working directory: $exeDir" -ForegroundColor Yellow
Write-Host "Benchmark duration: $benchmarkDurSec seconds" -ForegroundColor Yellow
Write-Host "CSV output: $csvOutput" -ForegroundColor Yellow
Write-Host ""

# Verify files exist
if (-not (Test-Path $injectorExe)) {
    Write-Error "injector.exe not found at $injectorExe"
    exit 1
}
if (-not (Test-Path $dllPath)) {
    Write-Error "LuaAPI.dll not found at $dllPath"
    exit 1
}

# Launch gamemd.exe suspended, then inject
Write-Host "[1/5] Spawning gamemd.exe (suspended)..." -ForegroundColor White
$gameProc = Start-Process -FilePath Join-Path $exeDir "gamemd.exe" -ArgumentList "-SPAWN" -PassThru -Wait -ErrorAction Stop
# Note: Start-Process with -Wait will wait for the process to exit.
# We actually want to keep it running, so let's use Start without -Write.

# Actually, let's use the proper approach: spawn and keep running
Write-Host "[1/5] Spawning gamemd.exe (detached)..." -ForegroundColor White
$process = Start-Process -FilePath Join-Path $exeDir "gamemd.exe" -ArgumentList "-SPAWN" -PassThru
$pid = $process.Id

Write-Host "    gamemd.exe spawned PID: $pid" -ForegroundColor Gray

# Wait a moment for process to start
Start-Sleep -Seconds 5

# Launch injector to attach and inject LuaAPI.dll
Write-Host "[2/5] Launching injector.exe to attach and inject LuaAPI.dll..." -ForegroundColor White
$injector = Start-Process -FilePath $injectorExe -ArgumentList "" -PassThru
Start-Sleep -Seconds 3  # give injector time to find and inject

# Verify injection
Write-Host "[3/5] Verifying injection..." -ForegroundColor White
$dllLoaded = $false
for ($i = 0; $i -lt 60; $i++) {
    if (Test-Path $dllPath) {
        # Check if injector process is still running and LuaAPI is loaded
        Write-Host "    Checking injection status (attempt $i)..." -ForegroundColor Gray
        Start-Sleep -Seconds 1
    } else {
        break
    }
}

# Run PresentMon to capture CSV output for the benchmark duration
Write-Host "[4/5] Starting PresentMon profiling for $benchmarkDurSec seconds..." -ForegroundColor White
$presentmonArgs = @(
    "-csv", $csvOutput
    "-rt", "csv"
    "-o", "Logger"  # Logger mode (overkill for simple FPS, but reliable)
)

# PresentMon might need specific flags; adjust as needed for your version.
# Common PresentMon CLI: presentmon.exe -csv output.csv -rt csv
$pmProc = Start-Process -FilePath $presentmonExe -ArgumentList $presentmonArgs -PassThru -ErrorAction SilentlyContinue

if (-not $pmProc) {
    Write-Error "Failed to start PresentMon. Ensure presentmon.exe is accessible and YR is running."
    exit 1
}

Write-Host "    PresentMon PID: $($pmProc.Id)" -ForegroundColor Gray

# Profile for the specified duration, then stop PresentMon
Write-Host "    Profiling... counting down $benchmarkDurSec seconds..." -ForegroundColor Gray
Start-Sleep -Seconds $benchmarkDurSec

Write-Host "    Stopping PresentMon..." -ForegroundColor White
Stop-Process -Id $pmProc.Id -Force -ErrorAction SilentlyContinue

Write-Host "    PresentMon stopped." -ForegroundColor Gray

# Post-processing: analyze the CSV
Write-Host "[5/5] Analyzing benchmark CSV..." -ForegroundColor White
$pythonScript = Join-Path $scriptRoot "tools\benchmark_analyzer.py"
if (-not $pythonScript) {
    pythonScript = "python3 tools/benchmark_analyzer.py"
}

Write-Host "    Running: python $pythonScript $csvOutput" -ForegroundColor Gray
& python3 $pythonScript $csvOutput

Write-Host "" -ForegroundColor Cyan
Write-Host "=== Benchmark Complete ===" -ForegroundColor Cyan
Write-Host "CSV output: $csvOutput" -ForegroundColor Yellow
Write-Host "Review the analysis above for Avg FPS and 1% Low FPS." -ForegroundColor Yellow