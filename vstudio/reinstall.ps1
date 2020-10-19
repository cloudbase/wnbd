$scriptLocation = [System.IO.Path]::GetDirectoryName(
    $myInvocation.MyCommand.Definition)

$devconBin = "$scriptLocation\devcon.exe"
$wnbdInf = "$scriptLocation\wnbd.inf"
$wnbdCat = "$scriptLocation\wnbd.cat"
$wnbdSys = "$scriptLocation\wnbd.sys"

$requiredFiles = @($devconBin, $wnbdInf, $wnbdCat, $wnbdSys)
foreach ($path in $requiredFiles) {
    if (!(Test-Path -Path $path -PathType leaf)) {
        Write-Warning "Could not find file: $path"
    }
}

& $devconBin remove "root\wnbd"

pnputil.exe /enum-drivers | sls -Context 5 wnbd | findstr Published | `
    % {$_ -match "(oem\d+.inf)"; pnputil.exe /delete-driver $matches[0] /force }

& $devconBin install $wnbdInf root\wnbd
