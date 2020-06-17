$scriptLocation = [System.IO.Path]::GetDirectoryName(
    $myInvocation.MyCommand.Definition)

$devconBin = "$scriptLocation\devcon.exe"
$wnbdInf = "$scriptLocation\wnbd.inf"
$wnbdCat = "$scriptLocation\wnbd.cat"
$wnbdSys = "$scriptLocation\wnbd.sys"

$status = Test-Path -Path $devconBin -PathType leaf

if ($status -eq $false) {
    Write-Host "Devcon utility not found in $scriptLocation"
}

$status = Test-Path -Path $wnbdInf -PathType leaf

if ($status -eq $false) {
    Write-Host "wnbd.inf not found in $scriptLocation"
}

$status = Test-Path -Path $wnbdCat -PathType leaf

if ($status -eq $false) {
    Write-Host "wnbd.cat not found in $scriptLocation"
}

$status = Test-Path -Path $wnbdSys -PathType leaf

if ($status -eq $false) {
    Write-Host "wnbd.sys not found in $scriptLocation"
}

& $devconBin remove "root\wnbd"

pnputil.exe /enum-drivers | sls -Context 5 wnbd | findstr Published | `
    % {$_ -match "(oem\d+.inf)"; pnputil.exe /delete-driver $matches[0] /force }

& $devconBin install $wnbdInf root\wnbd
