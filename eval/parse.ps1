#!/usr/bin/env pwsh
[CmdletBinding()]param(
    [Parameter()]$root = "~/nmbpf",
    [Parameter()][switch]$NoFio,
    [Parameter()][switch]$NoFioMulti,
    [Parameter()][switch]$NoFioLat,
    [Parameter()][switch]$NoFioLatLat,
    [Parameter()][switch]$NoRest
)

if (!$NoFio) {
    Import-TemplatedData -RootPath $root -TemplatePath ./fio.txt -Recurse -AddFilePath -Verbose:$VerbosePreference | ForEach-Object -Parallel {
        $data = Get-Content "$Using:root/$($_._file)" | ConvertFrom-Json -Depth 9999
        $_ | Add-Member -MemberType NoteProperty -Name read_ios -Value $data.disk_util[0].read_ios -PassThru | Add-Member -MemberType NoteProperty -Name write_ios -Value $data.disk_util[0].write_ios -PassThru
    } | Select-Object -ExcludeProperty _file | Export-Csv fio.csv
}

if (!$NoFioMulti) {
    Import-TemplatedData -RootPath $root -TemplatePath ./fiomulti.txt -Recurse -AddFilePath -Verbose:$VerbosePreference | ForEach-Object -Parallel {
        $data = Get-Content "$Using:root/$($_._file)" | ConvertFrom-Json -Depth 9999
        $_ | Add-Member -MemberType NoteProperty -Name read_ios -Value $data.disk_util[0].read_ios -PassThru | Add-Member -MemberType NoteProperty -Name write_ios -Value $data.disk_util[0].write_ios -PassThru
    } | Select-Object -ExcludeProperty _file | Export-Csv fiomulti.csv
}

if (!$NoFioLat) {
    Import-TemplatedData -RootPath $root -TemplatePath ./fiolat.txt -Recurse -AddFilePath -Verbose:$VerbosePreference | ForEach-Object -Parallel {
        $data = Get-Content "$Using:root/$($_._file)" | ConvertFrom-Json -Depth 9999
        $_ |
        Add-Member -MemberType NoteProperty -Name read50 -Value $data.jobs[0].read.clat_ns.percentile."50.000000" -PassThru |
        Add-Member -MemberType NoteProperty -Name read90 -Value $data.jobs[0].read.clat_ns.percentile."90.000000" -PassThru |
        Add-Member -MemberType NoteProperty -Name read99 -Value $data.jobs[0].read.clat_ns.percentile."99.000000" -PassThru |
        Add-Member -MemberType NoteProperty -Name read999 -Value $data.jobs[0].read.clat_ns.percentile."99.900000" -PassThru |
        Add-Member -MemberType NoteProperty -Name write50 -Value $data.jobs[0].write.clat_ns.percentile."50.000000" -PassThru |
        Add-Member -MemberType NoteProperty -Name write90 -Value $data.jobs[0].write.clat_ns.percentile."90.000000" -PassThru |
        Add-Member -MemberType NoteProperty -Name write99 -Value $data.jobs[0].write.clat_ns.percentile."99.000000" -PassThru |
        Add-Member -MemberType NoteProperty -Name write999 -Value $data.jobs[0].write.clat_ns.percentile."99.900000" -PassThru
    } | Select-Object -ExcludeProperty _file | Export-Csv fiolat.csv
}

if (!$NoFioLatLat) {
    Import-TemplatedData -RootPath $root -TemplatePath ./fiolatlat.txt -Recurse -AddFilePath -Verbose:$VerbosePreference | ForEach-Object -Parallel {
        $data = Get-Content "$Using:root/$($_._file)" | ConvertFrom-Json -Depth 9999
        $_ |
        Add-Member -MemberType NoteProperty -Name read50 -Value $data.jobs[0].read.clat_ns.percentile."50.000000" -PassThru |
        Add-Member -MemberType NoteProperty -Name read90 -Value $data.jobs[0].read.clat_ns.percentile."90.000000" -PassThru |
        Add-Member -MemberType NoteProperty -Name read99 -Value $data.jobs[0].read.clat_ns.percentile."99.000000" -PassThru |
        Add-Member -MemberType NoteProperty -Name read999 -Value $data.jobs[0].read.clat_ns.percentile."99.900000" -PassThru |
        Add-Member -MemberType NoteProperty -Name write50 -Value $data.jobs[0].write.clat_ns.percentile."50.000000" -PassThru |
        Add-Member -MemberType NoteProperty -Name write90 -Value $data.jobs[0].write.clat_ns.percentile."90.000000" -PassThru |
        Add-Member -MemberType NoteProperty -Name write99 -Value $data.jobs[0].write.clat_ns.percentile."99.000000" -PassThru |
        Add-Member -MemberType NoteProperty -Name write999 -Value $data.jobs[0].write.clat_ns.percentile."99.900000" -PassThru
    } | Select-Object -ExcludeProperty _file | Export-Csv fiolatlat.csv
}

if (!$NoRest) {
    #Import-TemplatedData -RootPath $root -TemplatePath ./kernbench.txt -Recurse | Export-Csv kernbench.csv
    Import-TemplatedData -RootPath $root -TemplatePath ./ycsb.txt -Recurse | Export-Csv ycsb.csv
    Import-TemplatedData -RootPath $root -TemplatePath ./cpu-fio.txt -Recurse | Export-Csv cpu-fio.csv
    #Import-TemplatedData -RootPath $root -TemplatePath ./cpu-kernbench.txt -Recurse | Export-Csv cpu-kernbench.csv
    Import-TemplatedData -RootPath $root -TemplatePath ./cpu-ycsb.txt -Recurse | Export-Csv cpu-ycsb.csv
}
