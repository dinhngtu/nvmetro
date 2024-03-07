#!/usr/bin/env pwsh
[CmdletBinding()]
param (
    [Parameter(Mandatory)][string]$BenchName,
    [Parameter(Mandatory)][string]$TargetHost,
    [Parameter()][string]$TargetUser = "root",
    [Parameter(Mandatory)][string]$TargetDevice,
    [Parameter()][int]$BreakTime = 30,
    [Parameter()][string]$OutputPath = "/root/nmbpf",
    [Parameter()][switch]$NoFormat
)

$BenchOutPath = "$OutputPath/kernbench-$BenchName"
New-Item -ItemType Directory -Path $BenchOutPath -Force

$session = New-PSSession -HostName $TargetHost -UserName $TargetUser
$cf = Invoke-Command -Session $session -ScriptBlock {
    if (-not $Using:NoFormat) {
        umount /mnt | Write-Information
        #nvme format $Using:TargetDevice | Write-Information
        "1" > /proc/sys/vm/drop_caches
        mkfs.ext4 -E "nodiscard,lazy_itable_init=0" -O ^has_journal -N 2097152 -F $Using:TargetDevice | Write-Information
        Start-Sleep -Seconds 3
    }
    mount -o noatime $Using:TargetDevice /mnt | Write-Information
    return New-Item -ItemType Directory -Path $Using:BenchOutPath -Force
}
if (-not $cf) {
    exit
}
#foreach ($wl in $Workloads) {
Invoke-Command -Session $session -ScriptBlock {
    New-Item -ItemType Directory -Path /mnt/linux -Force
    Push-Location /mnt/linux
    Copy-Item /root/linux-v5.10.66-c3.tar.lz -Destination /tmp/linux.tar.lz -Force
    sync
    Pop-Location
}
Get-Content /proc/stat > $BenchOutPath/cpu-extract-pre
Invoke-Command -Session $session -ScriptBlock {
    Push-Location /mnt/linux
    /usr/bin/time -f '%S\n%U\n%e' -o $Using:BenchOutPath/extract tarlz -xf /tmp/linux.tar.lz
    Pop-Location
}
Get-Content /proc/stat > $BenchOutPath/cpu-extract-post
Invoke-Command -Session $session -ScriptBlock {
    Push-Location /mnt/linux
    Copy-Item /root/linux-tinyconfig -Destination ./.config -Force
    sync
    Pop-Location
}
Get-Content /proc/stat > $BenchOutPath/cpu-compile-pre
Invoke-Command -Session $session -ScriptBlock {
    Push-Location /mnt/linux
    /usr/bin/time -f '%S\n%U\n%e' -o $Using:BenchOutPath/compile make -j4 | Out-Null
    Pop-Location
}
Get-Content /proc/stat > $BenchOutPath/cpu-compile-post
Invoke-Command -Session $session -ScriptBlock {
    umount /mnt
    "1" > /proc/sys/vm/drop_caches
    Start-Sleep -Seconds $Using:BreakTime
}
#}
Remove-PSSession $session
