#!/usr/bin/env pwsh
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string[]]$TargetHosts,
    [Parameter(Mandatory)]$TargetDevice,
    [Parameter(Mandatory)]$BenchPrefix,
    [Parameter(Mandatory)][ValidateSet("Discard", "None")][string]$FormatMode,
    [Parameter()][int]$BreakTime = 60,
    [Parameter()][switch]$NoFio
)

Set-StrictMode -Version Latest

$fioscens = @(
    "bs512-trandread-iod1-j1",
    "bs512-trandwrite-iod1-j1",
    "bs512-trandrw-iod1-j1",
    "bs512-trandread-iod4-j1",
    "bs512-trandwrite-iod4-j1",
    "bs512-trandrw-iod4-j1",
    "bs512-trandread-iod32-j1",
    "bs512-trandwrite-iod32-j1",
    "bs512-trandrw-iod32-j1",

    "bs512-trandread-iod128-j1",
    "bs512-trandwrite-iod128-j1",
    "bs512-trandrw-iod128-j1",

    "bs16384-trandread-iod1-j1",
    "bs16384-trandwrite-iod1-j1",
    "bs16384-trandrw-iod1-j1",
    "bs16384-trandread-iod4-j1",
    "bs16384-trandwrite-iod4-j1",
    "bs16384-trandrw-iod4-j1",
    "bs16384-trandread-iod32-j1",
    "bs16384-trandwrite-iod32-j1",
    "bs16384-trandrw-iod32-j1",
    "bs16384-trandread-iod128-j1",
    "bs16384-trandwrite-iod128-j1",
    "bs16384-trandrw-iod128-j1"

    "bs131072-trandread-iod1-j1",
    "bs131072-trandwrite-iod1-j1",
    "bs131072-trandrw-iod1-j1",
    "bs131072-trandread-iod4-j1",
    "bs131072-trandwrite-iod4-j1",
    "bs131072-trandrw-iod4-j1",
    "bs131072-trandread-iod32-j1",
    "bs131072-trandwrite-iod32-j1",
    "bs131072-trandrw-iod32-j1",
    "bs131072-trandread-iod128-j1",
    "bs131072-trandwrite-iod128-j1",
    "bs131072-trandrw-iod128-j1"
)

function Format-Device {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string[]]$TargetHosts,
        [Parameter(Mandatory)]$TargetDevice,
        [Parameter(Mandatory)][ValidateSet("Discard", "None")][string]$FormatMode
    )
    switch ($FormatMode) {
        "Discard" {
            icm -HostName $TargetHosts -UserName root -ScriptBlock { sync; "1" > /proc/sys/vm/drop_caches }
            1..4 | % { sudo blkdiscard "${TargetDevice}p$_" }
        }
        "None" {
            icm -HostName $TargetHosts -UserName root -ScriptBlock { sync; "1" > /proc/sys/vm/drop_caches }
        }
    }
    Start-Sleep -Seconds 30
}

$TargetHosts | % {
    $exists = icm -HostName $_ -UserName root -ScriptBlock { Test-Path -PathType Leaf $Using:TargetDevice }
    if (!$exists) {
        throw "Block device doesn't exist"
    }
}

if (-not $NoFio) {
    foreach ($i in @('1', '2', '3')) {
    #foreach ($i in @('1')) {
        foreach ($scen in $fioscens) {
            Write-Host "Formatting"
            Format-Device -TargetHosts $TargetHosts -TargetDevice $TargetDevice -FormatMode $FormatMode
            ./bench-fio-multi.ps1 -TargetHosts $TargetHosts -BenchName $BenchPrefix-$i -Scenario $scen -TargetDevice $TargetDevice
        }
    }
    tput bel
    Start-Sleep -Seconds $BreakTime
}
