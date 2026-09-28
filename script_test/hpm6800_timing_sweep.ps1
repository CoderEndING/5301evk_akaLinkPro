# Sweep the JTAG DMI timing knobs and report throughput + integrity per point.
#
# Each point is a full build + DFU flash + self-check + read/write benchmark, so
# a point costs ~2 minutes.
#
# Usage: powershell -File script_test\hpm6800_timing_sweep.ps1
$ErrorActionPreference = 'Continue'
$repo = Split-Path -Parent $PSScriptRoot
Set-Location $repo

# cap_high, nav_low, nav_high
$points = @(
    @(8, 8, 8),
    @(6, 6, 6),
    @(4, 4, 4),
    @(3, 4, 4)
)

foreach ($p in $points) {
    $cap = $p[0]; $nl = $p[1]; $nh = $p[2]
    Write-Output ""
    Write-Output "================ cap=$cap nav=$nl/$nh ================"
    $env:DMI_CAP_HIGH_NOP = "$cap"
    $env:DMI_NAV_LOW_NOP = "$nl"
    $env:DMI_NAV_HIGH_NOP = "$nh"

    python -u script_test\hpm6800_flash_probe.py 2>&1 | Select-String -Pattern 'attempt 1|probe is back|Traceback|RuntimeError' | ForEach-Object { "  $($_.Line.Trim())" }
    Start-Sleep -Seconds 2
    python -u script_test\hpm6800_probe.py set-mode 1 2>&1 | Out-Null
    python -u script_test\hpm6800_selfcheck.py 2>&1 | ForEach-Object { "  $_" }
    python -u script_test\hpm6800_riscv.py rbench 0x1200000 1024 50 2>&1 | Select-String 'rate|moved' | ForEach-Object { "  R $($_.Line.Trim())" }
    python -u script_test\hpm6800_riscv.py wbench 0x1200000 1024 50 2>&1 | Select-String 'rate|moved' | ForEach-Object { "  W $($_.Line.Trim())" }
}
Write-Output ""
Write-Output "sweep done"
