#!/usr/bin/env pwsh
[CmdletBinding()]
param (
    [Parameter(Mandatory)][string]$TargetHost,
    [Parameter()][string]$TargetUser = "root",
    [Parameter(Mandatory)][string]$TargetDevice
)

$session = New-PSSession -HostName $TargetHost -UserName $TargetUser
$exists = Invoke-Command -HostName $TargetHost -UserName $TargetUser -ScriptBlock { Test-Path -PathType Leaf $Using:TargetDevice }
if (!$exists) {
    throw "Block device doesn't exist"
}
Invoke-Command -Session $session -ScriptBlock {
    /bin/time "${env:HOME}/writerand" -b $Using:TargetDevice -z $(blockdev --getsize64 $Using:TargetDevice) -B 65536 -k "${env:HOME}/keyfile" -P 0.5 -j 6
}
Remove-PSSession $session
