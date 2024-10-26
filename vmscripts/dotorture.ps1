#!/usr/bin/env pwsh
[CmdletBinding()]
param(
    [Parameter(Mandatory)]$TargetHost,
    [Parameter(Mandatory)]$TargetDevice,
    [Parameter(Mandatory)]$BenchPrefix,
    [Parameter(Mandatory)][ValidateSet("Passthrough", "Mdev", "Repl", "Spdk", "None")][string]$FormatMode,
    [Parameter()][int]$BreakTime = 60,
    [Parameter()][switch]$NoCpuFreq,
    [Parameter()][switch]$NoFio,
    [Parameter()][switch]$NoKernbench,
    [Parameter()][switch]$NoYcsb,
    [Parameter()][switch]$NoYcsb1,
    [Parameter()][switch]$NoYcsb4
)

Set-StrictMode -Version Latest

$attempts = @("1", "2", "3")
#$attempts = @("1")

$fioscens = @(
    "bs512-trandread-iod128-j4",
    "bs512-trandwrite-iod128-j4",
    "bs512-trandrw-iod128-j4",

    "bs16384-tread-iod128-j4",
    "bs16384-twrite-iod128-j4",
    "bs16384-treadwrite-iod128-j4",

    "bs131072-tread-iod128-j4",
    "bs131072-twrite-iod128-j4",
    "bs131072-treadwrite-iod128-j4"
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

function Out-Bell {
    param([int]$Count = 1)
    for ($i = 0; $i -lt $Count; $i++) {
        [Console]::Beep()
        Start-Sleep -Milliseconds 1100
    }
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
    Out-Bell
    Start-Sleep -Seconds $BreakTime
}

if (-not $NoKernbench) {
    foreach ($i in $attempts) {
        Format-Device -TargetHost $TargetHost -TargetDevice $TargetDevice -FormatMode $FormatMode
        ./bench-kernbench.ps1 -TargetHost $TargetHost -BenchName $BenchPrefix-$i -TargetDevice $TargetDevice
    }
    Out-Bell
    Start-Sleep -Seconds $BreakTime
}

if (-not $NoYcsb) {
    if (-not $NoYcsb1) {
        foreach ($i in $attempts) {
            foreach ($wl in @('a', 'b', 'c', 'd', 'e', 'f')) {
                Format-Device -TargetHost $TargetHost -TargetDevice $TargetDevice -FormatMode $FormatMode
                ./bench-ycsb.ps1 -TargetHost $TargetHost -BenchName $BenchPrefix-j1-$i -Workload $wl -NumJobs 1 -TargetDevice $TargetDevice
            }
        }
        Out-Bell
        Start-Sleep -Seconds $BreakTime
    }

    if (-not $NoYcsb4) {
        foreach ($i in $attempts) {
            foreach ($wl in @('a', 'b', 'c', 'd', 'e', 'f')) {
                Format-Device -TargetHost $TargetHost -TargetDevice $TargetDevice -FormatMode $FormatMode
                ./bench-ycsb.ps1 -TargetHost $TargetHost -BenchName $BenchPrefix-j4-$i -Workload $wl -NumJobs 4 -TargetDevice $TargetDevice
            }
        }
        Out-Bell
    }
}

Out-Bell -Count 5
