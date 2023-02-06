$ErrorActionPreference = "Stop"

$scriptLocation = [System.IO.Path]::GetDirectoryName(
    $myInvocation.MyCommand.Definition)

$versionHeaderPath = "$scriptLocation/../include/version.h"
$driverInfBasePath = "$scriptLocation/../driver/wnbd.inf"
# The wnbd.inf file is updated automatically (e.g. including the version tag). In order
# to avoid unintended git changes, we'll make a copy.
$driverInfDestPath = "$scriptLocation/../vstudio/wnbd.inf"

function safe_exec() {
    # Powershell doesn't check the command exit code, we'll need to
    # do it ourselves. Also, in case of native commands, it treats stderr
    # output as an exception, which is why we'll have to capture it.
    cmd /c "$args 2>&1"
    if ($LASTEXITCODE) {
        throw "Command failed: $args"
    }
}

try {
    $gitShortDesc = safe_exec git describe --tags
    $gitLongDesc = safe_exec git describe --tags --long
    $gitTag = safe_exec git describe --tags --abbrev=0
    $gitBranch = safe_exec git branch --show-current
    $gitCommitCount = safe_exec git rev-list --count "$gitTag..$gitBranch"
    $isDev = (($gitLongDesc) -split "-")[-2] -ne "0"

    $gitTag -match "(?<major>\d+)\.(?<minor>\d+)\.(?<patch>\d+)"
    if ($Matches.Count -ne 4) {
        throw "Invalid version tag: $gitTag. Expecting a semantic version, such as '1.0.0-beta'."
    }

    $versionMajor = $Matches.major
    $versionMinor = $Matches.minor
    $versionPatch = $Matches.patch
    $versionStr = "$gitShortDesc"
    $versionStrMS = "$versionMajor.$versionMinor.$versionPatch.$gitCommitCount"
    $versionDetected = $true
}
catch {
    $errMsg = [string]$_.Exception.Message
    $warnMsg = "Could not detect WNBD version using the git tag. The following header " +
               "will have to be updated manually: "
    Write-Warning $warnMsg
    # Visual Studio is truncating long messages, we'll use a separate warning
    # for the actual path...
    Write-Warning $versionHeaderPath
    # Even though it's just a warning, we have to avoid using "Error:".
    # Visual Studio parses the message and will treat it as an error, saying that
    # this script returned -1, even if it didn't...
    Write-Warning "Original exception: $errMsg"

    $versionMajor = "X"
    $versionMinor = "Y"
    $versionPatch = "Z"
    $versionStr = "X.Y.Z"
    $versionStrMS = "X.Y.Z.0"
    $gitCommitCount = 0
}

# We might add some more info to the version string.
$versionStrMaxLen = 127
$versionStrLen = $versionStr.Length
if ($versionStrLen -gt 127) {
    throw "Version string too large. Length: $versionStrLen, maximum length: $versionStrMaxLen."
}

$versionHeader = @"
// Automatically generated using generate_version_h.ps1 at build time.

#pragma once

#define WNBD_VERSION_MAJOR ${versionMajor}
#define WNBD_VERSION_MINOR ${versionMinor}
#define WNBD_VERSION_PATCH ${versionPatch}
#define WNBD_COMMIT_COUNT ${gitCommitCount}

#define WNBD_VERSION_STR "${versionStr}"
#define WNBD_VERSION_STR_MS "${versionStrMS}"
"@

# If we can't detect the project version using the git tag, we're providing
# a template that the user can fill, which we won't overwrite.
if ($versionDetected -or (!(test-path $versionHeaderPath))) {
    echo $versionHeader | out-file -encoding utf8 -filepath $versionHeaderPath
    if (!($versionDetected)) {
        $err = @"
#error The WNBD version could not be automatically detected using the git tag. \
Please fill in the WNBD version and then remove this error.
"@
        echo $err | out-file -append -encoding utf8 -filepath $versionHeaderPath
    }
}

# update the driver inf version
cp $driverInfBasePath $driverInfDestPath
if ($versionDetected) {
    $infVersion = "${versionMajor}.${versionMinor}.${versionPatch}.${gitCommitCount}"
    safe_exec stampinf.exe -d "*" -a "amd64" -v "$infVersion" -f "$driverInfDestPath"
} else {
    safe_exec stampinf.exe -d "*" -a "amd64" -v "*" -f "$driverInfDestPath"
}
