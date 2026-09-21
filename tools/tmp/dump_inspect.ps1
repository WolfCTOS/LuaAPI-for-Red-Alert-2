# Minimal minidump inspector (no debugger needed).
# Parses: exception stream (code/address/thread), module list (resolve any
# address to module+offset), faulting-thread stack top (return addresses ->
# module+offset). Usage: powershell -File tools/tmp/dump_inspect.ps1 <dump>
param([string]$DumpPath)

$ErrorActionPreference = "Stop"
$fs = [System.IO.File]::OpenRead($DumpPath)
$br = New-Object System.IO.BinaryReader($fs)
function U4([long]$off) { $fs.Position = $off; $br.ReadUInt32() }
function U8([long]$off) { $fs.Position = $off; $br.ReadUInt64() }
function Bytes([long]$off, [int]$n) { $fs.Position = $off; $br.ReadBytes($n) }

$sig = U4 0
if ($sig -ne 0x504D444D) { Write-Host "NOT A MINIDUMP"; exit 1 }
$nStreams = U4 8
$dirRva = U4 12
Write-Host ("streams={0}" -f $nStreams)

$streams = @{}
for ($i = 0; $i -lt $nStreams; $i++) {
    $o = $dirRva + $i * 12
    $streams[[int](U4 $o)] = @{ size = (U4 ($o + 4)); rva = (U4 ($o + 8)) }
}
Write-Host ("stream types: " + (($streams.Keys | Sort-Object) -join ","))

# --- modules (type 4) ---
$mods = @()
if ($streams.ContainsKey(4)) {
    $r = $streams[4].rva
    $n = U4 $r
    for ($i = 0; $i -lt $n; $i++) {
        $o = $r + 4 + $i * 108
        $base = U8 $o; $size = U4 ($o + 8); $nameRva = U4 ($o + 20)
        $len = U4 $nameRva
        $nm = [System.Text.Encoding]::Unicode.GetString((Bytes ($nameRva + 4) $len))
        $mods += [pscustomobject]@{ Base = $base; Size = $size; Name = (Split-Path $nm -Leaf) }
    }
}
function Resolve([uint64]$a) {
    foreach ($m in $mods) {
        if ($a -ge $m.Base -and $a -lt ($m.Base + [uint64]$m.Size)) {
            return ("{0}+0x{1:X}" -f $m.Name, ($a - $m.Base))
        }
    }
    return ("+0x{0:X} (unmapped)" -f $a)
}
Write-Host ("modules={0}" -f $mods.Count)
$mods | Where-Object { $_.Name -match 'gamemd|LuaAPI|DDraw|ddraw|Syringe|cnc' } | ForEach-Object {
    Write-Host ("  {0} base=0x{1:X} size=0x{2:X}" -f $_.Name, $_.Base, $_.Size)
}

# --- exception (type 6): find empirically (code 0xC0000005 or plausible) ---
if ($streams.ContainsKey(6)) {
    $r = $streams[6].rva
    $tid = U4 $r
    $code = U4 ($r + 8)
    $addr = U8 ($r + 24)
    Write-Host ("EXCEPTION code=0x{0:X8} addr=0x{1:X} ({2}) thread={3}" -f $code, $addr, (Resolve $addr), $tid)
} else { Write-Host "NO EXCEPTION STREAM"; $tid = $null }

# --- threads (type 3 or 4?) ---
foreach ($tt in @(3, 4)) {
    if (-not $streams.ContainsKey($tt)) { continue }
    $r = $streams[$tt].rva
    $n = U4 $r
    $looksThreads = $true
    Write-Host ("stream[{0}] count={1} size={2}" -f $tt, $n, $streams[$tt].size)
}

# --- faulting thread stack: locate thread by tid in the plausible list ---
foreach ($tt in @(3, 4)) {
    if (-not $streams.ContainsKey($tt)) { continue }
    $r = $streams[$tt].rva
    $n = U4 $r
    for ($i = 0; $i -lt $n -and $i -lt 64; $i++) {
        $o = $r + 4 + $i * 48
        $t = U4 $o
        if ($tid -ne $null -and $t -ne $tid) { continue }
        $ss = U8 ($o + 24); $sz = U4 ($o + 32); $sr = U4 ($o + 36)
        Write-Host ("THREAD id={0} stack=0x{1:X} size={2} rva=0x{3:X} (stream {4})" -f $t, $ss, $sz, $sr, $tt)
        if ($sz -gt 0 -and $sz -lt 1000000) {
            $raw = Bytes $sr $sz
            $frames = 0
            for ($k = 0; $k + 4 -le $raw.Length -and $frames -lt 48; $k += 4) {
                $v = [uint64]([System.BitConverter]::ToUInt32($raw, $k)) 
                if ($v -gt 0x10000) {
                    $res = Resolve $v
                    if ($res -notmatch 'unmapped') {
                        Write-Host ("  esp+0x{0:X} 0x{1:X} {2}" -f $k, $v, $res)
                        $frames++
                    }
                }
            }
        }
    }
}
$br.Close(); $fs.Close()
