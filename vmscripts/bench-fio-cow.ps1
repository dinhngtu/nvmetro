#!/usr/bin/env pwsh
[CmdletBinding()]
param (
    [Parameter(Mandatory)][string]$BenchName,
    [Parameter(Mandatory)][string]$Scenario,
    [Parameter(Mandatory)][string]$TargetHost,
    [Parameter()][string]$TargetUser = "root",
    [Parameter(Mandatory)][string]$TargetDevice,
    [Parameter()][int]$BreakTime = 30,
    [Parameter()][string]$OutputPath = "/home/tu/nmbpf",
    [Parameter()][switch]$NoFormat,
    [Parameter()][switch]$Short,
    [Parameter()][switch]$Short1
)

$Scenarios = @{
    "bs512-trandread-iod1-j1"           = @("--bs=512", "--rw=randread", "--iodepth=1", "--numjobs=1", "--hipri");
    "bs512-trandread-iod4-j1"           = @("--bs=512", "--rw=randread", "--iodepth=4", "--numjobs=1", "--hipri");
    "bs512-trandread-iod32-j1"          = @("--bs=512", "--rw=randread", "--iodepth=32", "--numjobs=1", "--hipri");
    "bs512-trandread-iod128-j1"         = @("--bs=512", "--rw=randread", "--iodepth=128", "--numjobs=1", "--hipri");

    "bs512-trandwrite-iod1-j1"          = @("--bs=512", "--rw=randwrite", "--iodepth=1", "--numjobs=1", "--hipri");
    "bs512-trandwrite-iod4-j1"          = @("--bs=512", "--rw=randwrite", "--iodepth=4", "--numjobs=1", "--hipri");
    "bs512-trandwrite-iod32-j1"         = @("--bs=512", "--rw=randwrite", "--iodepth=32", "--numjobs=1", "--hipri");
    "bs512-trandwrite-iod128-j1"        = @("--bs=512", "--rw=randwrite", "--iodepth=128", "--numjobs=1", "--hipri");

    "bs512-trandrw-iod1-j1"             = @("--bs=512", "--rw=randrw", "--iodepth=1", "--numjobs=1", "--hipri");
    "bs512-trandrw-iod4-j1"             = @("--bs=512", "--rw=randrw", "--iodepth=4", "--numjobs=1", "--hipri");
    "bs512-trandrw-iod32-j1"            = @("--bs=512", "--rw=randrw", "--iodepth=32", "--numjobs=1", "--hipri");
    "bs512-trandrw-iod128-j1"           = @("--bs=512", "--rw=randrw", "--iodepth=128", "--numjobs=1", "--hipri");

    "bs16384-trandread-iod128-j1"       = @("--bs=16k", "--rw=randread", "--iodepth=128", "--numjobs=1", "--hipri");
    "bs16384-trandwrite-iod128-j1"      = @("--bs=16k", "--rw=randwrite", "--iodepth=128", "--numjobs=1", "--hipri");
    "bs16384-trandrw-iod128-j1"         = @("--bs=16k", "--rw=randrw", "--iodepth=128", "--numjobs=1", "--hipri");

    "bs512-trandread-iod128-j4"         = @("--bs=512", "--rw=randread", "--iodepth=128", "--numjobs=4", "--hipri");
    "bs512-trandwrite-iod128-j4"        = @("--bs=512", "--rw=randwrite", "--iodepth=128", "--numjobs=4", "--hipri");
    "bs512-trandrw-iod128-j4"           = @("--bs=512", "--rw=randrw", "--iodepth=128", "--numjobs=4", "--hipri");

    "bs16384-trandread-iod128-j4"       = @("--bs=16k", "--rw=randread", "--iodepth=128", "--numjobs=4", "--hipri");
    "bs16384-trandwrite-iod128-j4"      = @("--bs=16k", "--rw=randwrite", "--iodepth=128", "--numjobs=4", "--hipri");
    "bs16384-trandrw-iod128-j4"         = @("--bs=16k", "--rw=randrw", "--iodepth=128", "--numjobs=4", "--hipri");

    ####### new big block benches

    "bs16384-trandread-iod1-j1"         = @("--bs=16k", "--rw=randread", "--iodepth=1", "--numjobs=1", "--hipri");
    "bs16384-trandwrite-iod1-j1"        = @("--bs=16k", "--rw=randwrite", "--iodepth=1", "--numjobs=1", "--hipri");
    "bs16384-trandrw-iod1-j1"           = @("--bs=16k", "--rw=randrw", "--iodepth=1", "--numjobs=1", "--hipri");

    "bs16384-trandread-iod1-j4"         = @("--bs=16k", "--rw=randread", "--iodepth=1", "--numjobs=4", "--hipri");
    "bs16384-trandwrite-iod1-j4"        = @("--bs=16k", "--rw=randwrite", "--iodepth=1", "--numjobs=4", "--hipri");
    "bs16384-trandrw-iod1-j4"           = @("--bs=16k", "--rw=randrw", "--iodepth=1", "--numjobs=4", "--hipri");

    "bs131072-trandread-iod128-j1"      = @("--bs=128k", "--rw=randread", "--iodepth=128", "--numjobs=1");
    "bs131072-trandwrite-iod128-j1"     = @("--bs=128k", "--rw=randwrite", "--iodepth=128", "--numjobs=1");
    "bs131072-trandrw-iod128-j1"        = @("--bs=128k", "--rw=randrw", "--iodepth=128", "--numjobs=1");

    "bs131072-trandread-iod128-j4"      = @("--bs=128k", "--rw=randread", "--iodepth=128", "--numjobs=4");
    "bs131072-trandwrite-iod128-j4"     = @("--bs=128k", "--rw=randwrite", "--iodepth=128", "--numjobs=4");
    "bs131072-trandrw-iod128-j4"        = @("--bs=128k", "--rw=randrw", "--iodepth=128", "--numjobs=4");

    "bs131072-trandread-iod1-j1"        = @("--bs=128k", "--rw=randread", "--iodepth=1", "--numjobs=1");
    "bs131072-trandwrite-iod1-j1"       = @("--bs=128k", "--rw=randwrite", "--iodepth=1", "--numjobs=1");
    "bs131072-trandrw-iod1-j1"          = @("--bs=128k", "--rw=randrw", "--iodepth=1", "--numjobs=1");

    "bs131072-trandread-iod1-j4"        = @("--bs=128k", "--rw=randread", "--iodepth=1", "--numjobs=4");
    "bs131072-trandwrite-iod1-j4"       = @("--bs=128k", "--rw=randwrite", "--iodepth=1", "--numjobs=4");
    "bs131072-trandrw-iod1-j4"          = @("--bs=128k", "--rw=randrw", "--iodepth=1", "--numjobs=4");

    ####### latency benches

    "lat-bs512-trandread-iod1-j1"       = @("--bs=512", "--rw=randread", "--iodepth=1", "--numjobs=1", "--rate_iops=10000", "--hipri");
    "lat-bs512-trandread-iod128-j1"     = @("--bs=512", "--rw=randread", "--iodepth=128", "--numjobs=1", "--rate_iops=10000", "--hipri");
    "lat-bs512-trandwrite-iod1-j1"      = @("--bs=512", "--rw=randwrite", "--iodepth=1", "--numjobs=1", "--rate_iops=10000", "--hipri");
    "lat-bs512-trandwrite-iod128-j1"    = @("--bs=512", "--rw=randwrite", "--iodepth=128", "--numjobs=1", "--rate_iops=10000", "--hipri");

    "lat-bs16384-trandread-iod1-j1"     = @("--bs=16k", "--rw=randread", "--iodepth=1", "--numjobs=1", "--rate_iops=10000", "--hipri");
    "lat-bs16384-trandread-iod128-j1"   = @("--bs=16k", "--rw=randread", "--iodepth=128", "--numjobs=1", "--rate_iops=10000", "--hipri");
    "lat-bs16384-trandwrite-iod1-j1"    = @("--bs=16k", "--rw=randwrite", "--iodepth=1", "--numjobs=1", "--rate_iops=10000", "--hipri");
    "lat-bs16384-trandwrite-iod128-j1"  = @("--bs=16k", "--rw=randwrite", "--iodepth=128", "--numjobs=1", "--rate_iops=10000", "--hipri");

    "lat-bs131072-trandread-iod1-j1"    = @("--bs=128k", "--rw=randread", "--iodepth=1", "--numjobs=1", "--rate_iops=10000");
    "lat-bs131072-trandread-iod128-j1"  = @("--bs=128k", "--rw=randread", "--iodepth=128", "--numjobs=1", "--rate_iops=10000");
    "lat-bs131072-trandwrite-iod1-j1"   = @("--bs=128k", "--rw=randwrite", "--iodepth=1", "--numjobs=1", "--rate_iops=10000");
    "lat-bs131072-trandwrite-iod128-j1" = @("--bs=128k", "--rw=randwrite", "--iodepth=128", "--numjobs=1", "--rate_iops=10000");

    ####### 2nd round latency benches

    "lat-bs512-trandread-iod4-j1"       = @("--bs=512", "--rw=randread", "--iodepth=4", "--numjobs=1", "--rate_iops=10000", "--hipri");
    "lat-bs512-trandread-iod32-j1"      = @("--bs=512", "--rw=randread", "--iodepth=32", "--numjobs=1", "--rate_iops=10000", "--hipri");
    "lat-bs512-trandwrite-iod4-j1"      = @("--bs=512", "--rw=randwrite", "--iodepth=4", "--numjobs=1", "--rate_iops=10000", "--hipri");
    "lat-bs512-trandwrite-iod32-j1"     = @("--bs=512", "--rw=randwrite", "--iodepth=32", "--numjobs=1", "--rate_iops=10000", "--hipri");

    "lat-bs16384-trandread-iod4-j1"     = @("--bs=16k", "--rw=randread", "--iodepth=4", "--numjobs=1", "--rate_iops=10000", "--hipri");
    "lat-bs16384-trandread-iod32-j1"    = @("--bs=16k", "--rw=randread", "--iodepth=32", "--numjobs=1", "--rate_iops=10000", "--hipri");
    "lat-bs16384-trandwrite-iod4-j1"    = @("--bs=16k", "--rw=randwrite", "--iodepth=4", "--numjobs=1", "--rate_iops=10000", "--hipri");
    "lat-bs16384-trandwrite-iod32-j1"   = @("--bs=16k", "--rw=randwrite", "--iodepth=32", "--numjobs=1", "--rate_iops=10000", "--hipri");

    "lat-bs131072-trandread-iod4-j1"    = @("--bs=128k", "--rw=randread", "--iodepth=4", "--numjobs=1", "--rate_iops=10000");
    "lat-bs131072-trandread-iod32-j1"   = @("--bs=128k", "--rw=randread", "--iodepth=32", "--numjobs=1", "--rate_iops=10000");
    "lat-bs131072-trandwrite-iod4-j1"   = @("--bs=128k", "--rw=randwrite", "--iodepth=4", "--numjobs=1", "--rate_iops=10000");
    "lat-bs131072-trandwrite-iod32-j1"  = @("--bs=128k", "--rw=randwrite", "--iodepth=32", "--numjobs=1", "--rate_iops=10000");
}

$BenchType = "fio"
$FioSize = "100g"
if ($Short) {
    $BenchType = "fioshort"
    $FioSize = "10g"
}
if ($Short1) {
    $BenchType = "fioshort1"
    $FioSize = "1g"
}
$BenchOutPath = "$OutputPath/$BenchType-$BenchName"
New-Item -ItemType Directory -Path $BenchOutPath -Force

$session = New-PSSession -HostName $TargetHost -UserName $TargetUser
$cf = Invoke-Command -Session $session -ScriptBlock {
    if (-not $Using:NoFormat) {
        umount /mnt | Write-Information
        #nvme format $Using:TargetDevice | Write-Information
        "1" > /proc/sys/vm/drop_caches
        Start-Sleep -Seconds 3
    }
    return New-Item -ItemType Directory -Path $Using:BenchOutPath -Force
}
if (-not $cf) {
    exit
}
#foreach ($scen in $Scenarios.Keys) {
Get-Content /proc/stat > $BenchOutPath/cpu-${scen}-pre
Invoke-Command -Session $session -ScriptBlock {
    $scen = $Using:Scenario
    $Scenarios = $Using:Scenarios
    $scenconfig = $Scenarios[$scen]
    $FioSize = $Using:FioSize
    sudo fio `
        --output-format=json `
        --size=$FioSize `
        --ioengine=io_uring `
        --direct=1 `
        --exitall `
        --name mdev `
        --time_based `
        --runtime=10 `
        --filename=$Using:TargetDevice `
        @scenconfig `
        > ${Using:BenchOutPath}/${scen}
}
Get-Content /proc/stat > $BenchOutPath/cpu-${scen}-post
Start-Sleep -Seconds $BreakTime
#}
Remove-PSSession $session
