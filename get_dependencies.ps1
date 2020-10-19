Param(
  [switch]$Clean
)

$scriptLocation = [System.IO.Path]::GetDirectoryName(
    $myInvocation.MyCommand.Definition)
$ErrorActionPreference = "Stop"

$nugetUrl = "https://dist.nuget.org/win-x86-commandline/v4.5.1/nuget.exe"

$depsDir = "$scriptLocation\vstudio\deps"
$nugetPath = "$depsDir\nuget.exe"
$depsConfig = "$scriptLocation\packages.config"
$completeFlag = "$depsDir\complete"

function safe_exec($cmd) {
    cmd /c "$cmd 2>&1"
    $exitCode = $LASTEXITCODE
    if ($exitCode) {
        throw "Command failed: $cmd. Exit code: $exitCode"
    }
}

if ($Clean -and (test-path $depsDir)) {
    rm -recurse -force $depsDir
    Write-Host "Cleaning up dependencies: $depsDir"
}

mkdir -force $depsDir
if (!(test-path $nugetPath)) {
    Write-Host "Fetching nuget from $nugetUrl."
    Invoke-WebRequest $nugetUrl -OutFile $nugetPath
}

if (!(test-path $completeFlag)) {
    Write-Host "Retrieving dependencies."
    safe_exec "$nugetPath install $depsConfig -OutputDirectory `"$depsDir`""
    sc $completeFlag "Finished retrieving dependencies."
}
else {
    write-host "Nuget dependencies already fetched."
}

