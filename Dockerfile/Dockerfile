# escape=`

ARG WIN_VER="ltsc2019"

FROM mcr.microsoft.com/windows/servercore:$WIN_VER

ADD https://aka.ms/vs/16/release/vs_buildtools.exe C:\TEMP\vs_buildtools.exe
ADD https://chocolatey.org/install.ps1 C:\TEMP\choco-install.ps1
ADD https://go.microsoft.com/fwlink/?linkid=2164149 C:\TEMP\wdksetup.exe

# Let's be explicit about the shell that we're going to use.
SHELL ["cmd", "/S", "/C"]

# Install Build Tools. A 3010 error signals that requested operation is
# successfull but changes will not be effective until the system is rebooted.
RUN C:\TEMP\vs_buildtools.exe --quiet --wait --norestart --nocache `
    --installPath C:\BuildTools `
    --add Microsoft.VisualStudio.Workload.VCTools `
    --add Microsoft.VisualStudio.Workload.MSBuildTools `
    --add Microsoft.VisualStudio.Component.VC.Runtimes.x86.x64.Spectre `
    --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    --add Microsoft.VisualStudio.Component.Windows10SDK.20348 `
    --add Microsoft.VisualStudio.Component.VC.14.24.x86.x64 `
    --add Microsoft.VisualStudio.Component.VC.14.24.x86.x64.Spectre `
 || IF "%ERRORLEVEL%"=="3010" EXIT 0

RUN powershell C:\TEMP\choco-install.ps1

RUN choco install git -y

# Install WDK excluding WDK.vsix.
RUN C:\TEMP\wdksetup.exe /q

# Install WDK.vsix in manual manner.
RUN copy "C:\Program Files (x86)\Windows Kits\10\Vsix\VS2019\WDK.vsix" C:\TEMP\wdkvsix.zip
RUN powershell Expand-Archive C:\TEMP\wdkvsix.zip -DestinationPath C:\TEMP\wdkvsix
RUN robocopy.exe /e "C:\temp\wdkvsix\$MSBuild\Microsoft\VC\v160" "C:\BuildTools\MSBuild\Microsoft\VC\v160" || EXIT 0

SHELL ["cmd"]

CMD [ "cmd","/k","c:\\BuildTools\\VC\\Auxiliary\\Build\\vcvarsall.bat", "x86_x64", "10.0.18362.0" ]