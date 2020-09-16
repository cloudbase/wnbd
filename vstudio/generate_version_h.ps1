$ErrorActionPreference = "Stop"

$scriptLocation = [System.IO.Path]::GetDirectoryName(
    $myInvocation.MyCommand.Definition)

$versionHeaderPath = "$scriptLocation/../include/version.h"

$gitShortDesc = git describe --tags
$gitLongDesc = git describe --tags --long
$gitTag = git describe --tags --abbrev=0

$isDev = (($gitLongDesc) -split "-")[-2] -ne "0"

$gitTag -match "(?<major>\d+)\.(?<minor>\d+)\.(?<patch>\d+)"
if ($Matches.Count -ne 4) {
    throw "Invalid version tag: $gitTag. Expecting a semantic version, such as '1.0.0-beta'."
}

$versionMajor = $Matches.major
$versionMinor = $Matches.minor
$versionPatch = $Matches.patch
$versionStr = "$gitShortDesc"

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

#define WNBD_VERSION_STR "${versionStr}"
"@

echo $versionHeader | out-file -encoding utf8 -filepath $versionHeaderPath
