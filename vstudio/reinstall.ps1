$scriptLocation = [System.IO.Path]::GetDirectoryName(
    $myInvocation.MyCommand.Definition)

$wnbdBin = "$scriptLocation\wnbd-client.exe"
$wnbdInf = "$scriptLocation\wnbd.inf"
$wnbdCat = "$scriptLocation\wnbd.cat"
$wnbdSys = "$scriptLocation\wnbd.sys"

$requiredFiles = @($wnbdBin, $wnbdInf, $wnbdCat, $wnbdSys)
foreach ($path in $requiredFiles) {
    if (!(Test-Path -Path $path -PathType leaf)) {
        Write-Warning "Could not find file: $path"
    }
}

& $wnbdBin uninstall-driver

& $wnbdBin install-driver $wnbdInf
