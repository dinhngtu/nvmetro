#!/usr/bin/env pwsh
[CmdletBinding()]
param (
    [Parameter(Mandatory)][string]$BenchName,
    [Parameter(Mandatory)][ValidateSet("a", "b", "c", "d", "e", "f", "test")][string]$Workload,
    [Parameter(Mandatory)][string]$TargetHost,
    [Parameter()][string]$TargetUser = "root",
    [Parameter(Mandatory)][string]$TargetDevice,
    [Parameter()][int]$BreakTime = 30,
    [Parameter()][string]$OutputPath = "/root/nmbpf",
    [Parameter()][switch]$NoFormat,
    [Parameter()][int]$NumJobs = 1,
    [Parameter()][switch]$HighPriority
)

$ycsb = "/root/YCSB"

$BenchOutPath = "$OutputPath/ycsb-$BenchName"
New-Item -ItemType Directory -Path $BenchOutPath -Force

$session = New-PSSession -HostName $TargetHost -UserName $TargetUser
$cf = Invoke-Command -Session $session -ScriptBlock {
    if (-not $Using:NoFormat) {
        umount /mnt | Write-Information
        #nvme format $Using:TargetDevice | Write-Information
        "1" > /proc/sys/vm/drop_caches
        mkfs.ext4 -E "nodiscard,lazy_itable_init=0" -O ^has_journal -N 4096 -F $Using:TargetDevice | Write-Information
        Start-Sleep -Seconds 3
    }
    mount -o noatime $Using:TargetDevice /mnt | Write-Information
    return New-Item -ItemType Directory -Path $Using:BenchOutPath -Force
}
if (-not $cf) {
    exit
}
Get-Content /proc/stat > $BenchOutPath/cpu-$Workload-load-pre
Invoke-Command -Session $session -ScriptBlock {
    $ycsb = $Using:ycsb
    Push-Location $ycsb
    $NumJobs = $Using:NumJobs
    1..$NumJobs | ForEach-Object {
        # ForEach-Object -Parallel doesn't like usings so we pass our arguments like this
        [PSCustomObject]@{
            ThreadNr = $_;
            Workload = ${Using:Workload};
            BenchOutPath = ${Using:BenchOutPath};
            HighPriority = ${Using:HighPriority};
        }
    } | ForEach-Object -Parallel {
        $tfile = "$($_.BenchOutPath)/$($_.Workload)-load-$($_.ThreadNr)"
        $dbdir = "/mnt/ycsb-rocksdb-$($_.Workload)-$($_.ThreadNr)"
        if ($_.HighPriority) {
            ionice -c1 -n0 bin/ycsb load rocksdb -s -P workloads/workload$($_.Workload) -p rocksdb.dir=$dbdir > $tfile
        } else {
            bin/ycsb load rocksdb -s -P workloads/workload$($_.Workload) -p rocksdb.dir=$dbdir > $tfile
        }
    }
    Pop-Location
}
Get-Content /proc/stat > $BenchOutPath/cpu-$Workload-load-post
Get-Content /proc/stat > $BenchOutPath/cpu-$Workload-run-pre
Invoke-Command -Session $session -ScriptBlock {
    $ycsb = $Using:ycsb
    Push-Location $ycsb
    $NumJobs = $Using:NumJobs
    1..$NumJobs | ForEach-Object {
        [PSCustomObject]@{
            ThreadNr = $_;
            Workload = ${Using:Workload};
            BenchOutPath = ${Using:BenchOutPath};
            HighPriority = ${Using:HighPriority};
        }
    } | ForEach-Object -Parallel {
        $tfile = "$($_.BenchOutPath)/$($_.Workload)-run-$($_.ThreadNr)"
        $dbdir = "/mnt/ycsb-rocksdb-$($_.Workload)-$($_.ThreadNr)"
        if ($_.HighPriority) {
            ionice -c1 -n0 bin/ycsb run rocksdb -s -P workloads/workload$($_.Workload) -p rocksdb.dir=$dbdir > $tfile
        } else {
            bin/ycsb run rocksdb -s -P workloads/workload$($_.Workload) -p rocksdb.dir=$dbdir > $tfile
        }
    }
    Pop-Location
}
Get-Content /proc/stat > $BenchOutPath/cpu-$Workload-run-post
Invoke-Command -Session $session -ScriptBlock {
    umount /mnt
    Start-Sleep -Seconds $Using:BreakTime
}
Remove-PSSession $session
