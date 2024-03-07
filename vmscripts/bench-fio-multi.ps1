#!/usr/bin/env pwsh
[CmdletBinding()]
param (
    [Parameter(Mandatory)][string]$BenchName,
    [Parameter(Mandatory)][string]$Scenario,
    [Parameter(Mandatory)][string[]]$TargetHosts,
    [Parameter()][string]$TargetUser = "root",
    [Parameter(Mandatory)][string]$TargetDevice,
    [Parameter()][int]$BreakTime = 30,
    [Parameter()][string]$OutputPath = "${Env:HOME}/nmbpf",
    [Parameter()][switch]$NoFormat
)

$Scenarios = @{
    "bs512-trandread-iod1-j1"       = @("--bs=512", "--rw=randread", "--iodepth=1", "--numjobs=1");
    "bs512-trandread-iod4-j1"       = @("--bs=512", "--rw=randread", "--iodepth=4", "--numjobs=1");
    "bs512-trandread-iod32-j1"      = @("--bs=512", "--rw=randread", "--iodepth=32", "--numjobs=1");
    "bs512-trandread-iod128-j1"     = @("--bs=512", "--rw=randread", "--iodepth=128", "--numjobs=1");

    "bs512-trandwrite-iod1-j1"      = @("--bs=512", "--rw=randwrite", "--iodepth=1", "--numjobs=1");
    "bs512-trandwrite-iod4-j1"      = @("--bs=512", "--rw=randwrite", "--iodepth=4", "--numjobs=1");
    "bs512-trandwrite-iod32-j1"     = @("--bs=512", "--rw=randwrite", "--iodepth=32", "--numjobs=1");
    "bs512-trandwrite-iod128-j1"    = @("--bs=512", "--rw=randwrite", "--iodepth=128", "--numjobs=1");

    "bs512-trandrw-iod1-j1"         = @("--bs=512", "--rw=randrw", "--iodepth=1", "--numjobs=1");
    "bs512-trandrw-iod4-j1"         = @("--bs=512", "--rw=randrw", "--iodepth=4", "--numjobs=1");
    "bs512-trandrw-iod32-j1"        = @("--bs=512", "--rw=randrw", "--iodepth=32", "--numjobs=1");
    "bs512-trandrw-iod128-j1"       = @("--bs=512", "--rw=randrw", "--iodepth=128", "--numjobs=1");

    "bs16384-tread-iod128-j1"       = @("--bs=16k", "--rw=read", "--iodepth=128", "--numjobs=1");
    "bs16384-twrite-iod128-j1"      = @("--bs=16k", "--rw=write", "--iodepth=128", "--numjobs=1");
    "bs16384-treadwrite-iod128-j1"  = @("--bs=16k", "--rw=readwrite", "--iodepth=128", "--numjobs=1");

    "bs512-trandread-iod128-j4"     = @("--bs=512", "--rw=randread", "--iodepth=128", "--numjobs=4");
    "bs512-trandwrite-iod128-j4"    = @("--bs=512", "--rw=randwrite", "--iodepth=128", "--numjobs=4");
    "bs512-trandrw-iod128-j4"       = @("--bs=512", "--rw=randrw", "--iodepth=128", "--numjobs=4");

    "bs16384-tread-iod128-j4"       = @("--bs=16k", "--rw=read", "--iodepth=128", "--numjobs=4");
    "bs16384-twrite-iod128-j4"      = @("--bs=16k", "--rw=write", "--iodepth=128", "--numjobs=4");
    "bs16384-treadwrite-iod128-j4"  = @("--bs=16k", "--rw=readwrite", "--iodepth=128", "--numjobs=4");

    ####### new big block benches

    "bs16384-tread-iod1-j1"         = @("--bs=16k", "--rw=read", "--iodepth=1", "--numjobs=1");
    "bs16384-twrite-iod1-j1"        = @("--bs=16k", "--rw=write", "--iodepth=1", "--numjobs=1");
    "bs16384-treadwrite-iod1-j1"    = @("--bs=16k", "--rw=readwrite", "--iodepth=1", "--numjobs=1");

    "bs16384-tread-iod1-j4"         = @("--bs=16k", "--rw=read", "--iodepth=1", "--numjobs=4");
    "bs16384-twrite-iod1-j4"        = @("--bs=16k", "--rw=write", "--iodepth=1", "--numjobs=4");
    "bs16384-treadwrite-iod1-j4"    = @("--bs=16k", "--rw=readwrite", "--iodepth=1", "--numjobs=4");

    "bs131072-tread-iod128-j1"      = @("--bs=128k", "--rw=read", "--iodepth=128", "--numjobs=1");
    "bs131072-twrite-iod128-j1"     = @("--bs=128k", "--rw=write", "--iodepth=128", "--numjobs=1");
    "bs131072-treadwrite-iod128-j1" = @("--bs=128k", "--rw=readwrite", "--iodepth=128", "--numjobs=1");

    "bs131072-tread-iod128-j4"      = @("--bs=128k", "--rw=read", "--iodepth=128", "--numjobs=4");
    "bs131072-twrite-iod128-j4"     = @("--bs=128k", "--rw=write", "--iodepth=128", "--numjobs=4");
    "bs131072-treadwrite-iod128-j4" = @("--bs=128k", "--rw=readwrite", "--iodepth=128", "--numjobs=4");

    "bs131072-tread-iod1-j1"        = @("--bs=128k", "--rw=read", "--iodepth=1", "--numjobs=1");
    "bs131072-twrite-iod1-j1"       = @("--bs=128k", "--rw=write", "--iodepth=1", "--numjobs=1");
    "bs131072-treadwrite-iod1-j1"   = @("--bs=128k", "--rw=readwrite", "--iodepth=1", "--numjobs=1");

    "bs131072-tread-iod1-j4"        = @("--bs=128k", "--rw=read", "--iodepth=1", "--numjobs=4");
    "bs131072-twrite-iod1-j4"       = @("--bs=128k", "--rw=write", "--iodepth=1", "--numjobs=4");
    "bs131072-treadwrite-iod1-j4"   = @("--bs=128k", "--rw=readwrite", "--iodepth=1", "--numjobs=4");
}

$BenchOutPath = "$OutputPath/fio-$BenchName"
New-Item -ItemType Directory -Path $BenchOutPath -Force

$session = New-PSSession -HostName $TargetHosts -UserName $TargetUser
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
    $mac = (Get-Content -Head 1 /sys/class/net/enp0s1/address).Replace(":", "")
    sudo fio @scenconfig `
        --output-format=json `
        --size=100g `
        --ioengine=io_uring `
        --hipri `
        --direct=1 `
        --exitall `
        --name mdev `
        --time_based `
        --runtime=10 `
        --filename=$Using:TargetDevice `
        > ${Using:BenchOutPath}/${scen}_${mac}
}
Get-Content /proc/stat > $BenchOutPath/cpu-${scen}-post
Start-Sleep -Seconds $BreakTime
#}
Remove-PSSession $session
