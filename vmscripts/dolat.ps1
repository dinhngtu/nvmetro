#!/usr/bin/env pwsh
[CmdletBinding()]
param(
    [Parameter(Mandatory)]$TargetHost,
    [Parameter(Mandatory)]$TargetDevice,
    [Parameter(Mandatory)]$BenchPrefix,
    [Parameter(Mandatory)][ValidateSet("Passthrough", "Mdev", "Repl", "Spdk", "None")][string]$FormatMode,
    [Parameter()][int]$BreakTime = 300,
    [Parameter()][switch]$NoCpuFreq,
    [Parameter()][switch]$NoFio
)

Set-StrictMode -Version Latest

#$attempts = @("1", "2", "3")
$attempts = @("1")

$fioscens = @(
    "lat-bs512-trandread-iod1-j1",
    "lat-bs512-trandread-iod128-j1",
    "lat-bs512-trandwrite-iod1-j1",
    "lat-bs512-trandwrite-iod128-j1",
    "lat-bs16384-trandread-iod1-j1",
    "lat-bs16384-trandread-iod128-j1",
    "lat-bs16384-trandwrite-iod1-j1",
    "lat-bs16384-trandwrite-iod128-j1",
    "lat-bs131072-trandread-iod1-j1",
    "lat-bs131072-trandread-iod128-j1",
    "lat-bs131072-trandwrite-iod1-j1",
    "lat-bs131072-trandwrite-iod128-j1",
    "lat-bs512-trandread-iod4-j1",
    "lat-bs512-trandread-iod32-j1",
    "lat-bs512-trandwrite-iod4-j1",
    "lat-bs512-trandwrite-iod32-j1",
    "lat-bs16384-trandread-iod4-j1",
    "lat-bs16384-trandread-iod32-j1",
    "lat-bs16384-trandwrite-iod4-j1",
    "lat-bs16384-trandwrite-iod32-j1",
    "lat-bs131072-trandread-iod4-j1",
    "lat-bs131072-trandread-iod32-j1",
    "lat-bs131072-trandwrite-iod4-j1",
    "lat-bs131072-trandwrite-iod32-j1"
)

function Format-Device {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]$TargetHost,
        [Parameter(Mandatory)]$TargetDevice,
        [Parameter(Mandatory)][ValidateSet("Passthrough", "Mdev", "Repl", "Spdk", "None")][string]$FormatMode
    )
    switch ($FormatMode) {
        "Passthrough" {
            icm -HostName $TargetHost -UserName root -ScriptBlock {
                sync
                "1" > /proc/sys/vm/drop_caches
                nvme format $Using:TargetDevice
            }
        }
        "Mdev" {
            icm -HostName $TargetHost -UserName root -ScriptBlock { sync; "1" > /proc/sys/vm/drop_caches }
            sudo bash ./format.sh
        }
        "Spdk" {
            icm -HostName $TargetHost -UserName root -ScriptBlock { sync; "1" > /proc/sys/vm/drop_caches }
            sudo bash ./format-spdk.sh
        }
        "Repl" {
            icm -HostName $TargetHost -UserName root -ScriptBlock { sync; "1" > /proc/sys/vm/drop_caches }
            bash ./repl-format.sh
        }
        "None" {
            icm -HostName $TargetHost -UserName root -ScriptBlock { sync; "1" > /proc/sys/vm/drop_caches }
        }
    }
    Start-Sleep -Seconds 30
}

$exists = icm -HostName $TargetHost -UserName root -ScriptBlock { Test-Path -PathType Leaf $Using:TargetDevice }
if (!$exists) {
    throw "Block device doesn't exist"
}

if (-not $NoCpuFreq) {
    icm -HostName $TargetHost -UserName root -ScriptBlock {
        if ([Environment]::MachineName -eq "nvme-sgx") {
            Set-Content -Path /sys/devices/system/cpu/intel_pstate/no_turbo -Value 1
            Set-Content -Path /sys/devices/system/cpu/cpu[0-9]*/cpufreq/scaling_governor -Value "performance"
        }
    }
}

if (-not $NoFio) {
    foreach ($i in $attempts) {
        foreach ($scen in $fioscens) {
            Format-Device -TargetHost $TargetHost -TargetDevice $TargetDevice -FormatMode $FormatMode
            ./bench-fio.ps1 -TargetHost $TargetHost -BenchName $BenchPrefix-$i -Scenario $scen -TargetDevice $TargetDevice
        }
    }
    tput bel
    Start-Sleep -Seconds $BreakTime
}
