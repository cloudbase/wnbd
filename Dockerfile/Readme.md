Build image
-----------

```
PS C:\Users\User\test> docker build .
```

Output:

```
PS C:\Users\User\test> docker build .
Sending build context to Docker daemon  3.584kB
Step 1/17 : ARG WIN_VER="ltsc2019"
Step 2/17 : FROM mcr.microsoft.com/windows/servercore:$WIN_VER
 ---> 81094f2483ae
Step 3/17 : ADD https://aka.ms/vs/16/release/vs_buildtools.exe C:\TEMP\vs_buildtools.exe
Downloading [==================================================>]  1.383MB/1.383MB

 ---> b00d6808893d
Step 4/17 : ADD https://chocolatey.org/install.ps1 C:\TEMP\choco-install.ps1
Downloading  22.66kB

 ---> 4a88a49fa4a9
Step 5/17 : ADD https://go.microsoft.com/fwlink/?linkid=2085767 C:\TEMP\wdksetup.exe
Downloading [==================================================>]  1.321MB/1.321MB
 ---> 8da9902668bb
Step 6/17 : SHELL ["cmd", "/S", "/C"]
 ---> Running in c5257e5ea395
Removing intermediate container c5257e5ea395
 ---> 2997dcbcdf7a
Step 7/17 : RUN C:\TEMP\vs_buildtools.exe --quiet --wait --norestart --nocache     --installPath C:\BuildTools     --add Microsoft.VisualStudio.Workload.VCTools     --add Microsoft.VisualStudio.Workload.MSBuildTools     --add Microsoft.VisualStudio.Component.VC.Runtimes.x86.x64.Spectre     --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64     --add Microsoft.VisualStudio.Component.Windows10SDK.18362     --add Microsoft.VisualStudio.Component.VC.14.24.x86.x64     --add Microsoft.VisualStudio.Component.VC.14.24.x86.x64.Spectre  || IF "%ERRORLEVEL%"=="3010" EXIT 0
 ---> Running in 3ae3b0bc1145
Preparing: C:\Users\ContainerAdministrator\AppData\Local\Temp\40932d4d0d2b8b72a8\vs_bootstrapper_d15\HelpFile\1028\help.html...
Preparing: C:\Users\ContainerAdministrator\AppData\Local\Temp\40932d4d0d2b8b72a8\vs_bootstrapper_d15\HelpFile\2052\help.html...
Preparing: C:\Users\ContainerAdministrator\AppData\Local\Temp\40932d4d0d2b8b72a8\vs_bootstrapper_d15\HelpFile\1055\help.html...
Preparing: C:\Users\ContainerAdministrator\AppData\Local\Temp\40932d4d0d2b8b72a8\vs_bootstrapper_d15\HelpFile\1046\help.html...
Preparing: C:\Users\ContainerAdministrator\AppData\Local\Temp\40932d4d0d2b8b72a8\vs_bootstrapper_d15\HelpFile\1042\help.html...
Preparing: C:\Users\ContainerAdministrator\AppData\Local\Temp\40932d4d0d2b8b72a8\vs_bootstrapper_d15\HelpFile\1029\help.html...
Preparing: C:\Users\ContainerAdministrator\AppData\Local\Temp\40932d4d0d2b8b72a8\vs_bootstrapper_d15\HelpFile\1036\help.html...
Preparing: C:\Users\ContainerAdministrator\AppData\Local\Temp\40932d4d0d2b8b72a8\vs_bootstrapper_d15\HelpFile\3082\help.html...
Preparing: C:\Users\ContainerAdministrator\AppData\Local\Temp\40932d4d0d2b8b72a8\vs_bootstrapper_d15\HelpFile\1040\help.html...
Preparing: C:\Users\ContainerAdministrator\AppData\Local\Temp\40932d4d0d2b8b72a8\vs_bootstrapper_d15\HelpFile\1031\help.html...
Preparing: C:\Users\ContainerAdministrator\AppData\Local\Temp\40932d4d0d2b8b72a8\vs_bootstrapper_d15\HelpFile\1045\help.html...
Preparing: C:\Users\ContainerAdministrator\AppData\Local\Temp\40932d4d0d2b8b72a8\vs_bootstrapper_d15\HelpFile\1041\help.html...
Preparing: C:\Users\ContainerAdministrator\AppData\Local\Temp\40932d4d0d2b8b72a8\vs_bootstrapper_d15\HelpFile\1049\help.html...
Preparing: C:\Users\ContainerAdministrator\AppData\Local\Temp\40932d4d0d2b8b72a8\vs_bootstrapper_d15\HelpFile\1033\help.html...
Preparing: C:\Users\ContainerAdministrator\AppData\Local\Temp\40932d4d0d2b8b72a8\vs_bootstrapper_d15\vs_setup_bootstrapper.exe...
Preparing: C:\Users\ContainerAdministrator\AppData\Local\Temp\40932d4d0d2b8b72a8\vs_bootstrapper_d15\Microsoft.Diagnostics.Tracing.EventSource.dll...
Preparing: C:\Users\ContainerAdministrator\AppData\Local\Temp\40932d4d0d2b8b72a8\vs_bootstrapper_d15\Microsoft.VisualStudio.RemoteControl.dll...
Preparing: C:\Users\ContainerAdministrator\AppData\Local\Temp\40932d4d0d2b8b72a8\vs_bootstrapper_d15\Microsoft.VisualStudio.Setup.Common.dll...
Preparing: C:\Users\ContainerAdministrator\AppData\Local\Temp\40932d4d0d2b8b72a8\vs_bootstrapper_d15\Microsoft.VisualStudio.Setup.dll...
Preparing: C:\Users\ContainerAdministrator\AppData\Local\Temp\40932d4d0d2b8b72a8\vs_bootstrapper_d15\Microsoft.VisualStudio.Setup.Download.dll...
Preparing: C:\Users\ContainerAdministrator\AppData\Local\Temp\40932d4d0d2b8b72a8\vs_bootstrapper_d15\Microsoft.VisualStudio.Telemetry.dll...
Preparing: C:\Users\ContainerAdministrator\AppData\Local\Temp\40932d4d0d2b8b72a8\vs_bootstrapper_d15\Microsoft.VisualStudio.Utilities.Internal.dll...
Preparing: C:\Users\ContainerAdministrator\AppData\Local\Temp\40932d4d0d2b8b72a8\vs_bootstrapper_d15\Newtonsoft.Json.dll...
Preparing: C:\Users\ContainerAdministrator\AppData\Local\Temp\40932d4d0d2b8b72a8\vs_bootstrapper_d15\zh-Hans\vs_setup_bootstrapper.resources.dll...
Preparing: C:\Users\ContainerAdministrator\AppData\Local\Temp\40932d4d0d2b8b72a8\vs_bootstrapper_d15\tr\vs_setup_bootstrapper.resources.dll...
Preparing: C:\Users\ContainerAdministrator\AppData\Local\Temp\40932d4d0d2b8b72a8\vs_bootstrapper_d15\es\vs_setup_bootstrapper.resources.dll...
Preparing: C:\Users\ContainerAdministrator\AppData\Local\Temp\40932d4d0d2b8b72a8\vs_bootstrapper_d15\it\vs_setup_bootstrapper.resources.dll...
Preparing: C:\Users\ContainerAdministrator\AppData\Local\Temp\40932d4d0d2b8b72a8\vs_bootstrapper_d15\pl\vs_setup_bootstrapper.resources.dll...
Preparing: C:\Users\ContainerAdministrator\AppData\Local\Temp\40932d4d0d2b8b72a8\vs_bootstrapper_d15\pt-BR\vs_setup_bootstrapper.resources.dll...
Preparing: C:\Users\ContainerAdministrator\AppData\Local\Temp\40932d4d0d2b8b72a8\vs_bootstrapper_d15\de\vs_setup_bootstrapper.resources.dll...
Preparing: C:\Users\ContainerAdministrator\AppData\Local\Temp\40932d4d0d2b8b72a8\vs_bootstrapper_d15\fr\vs_setup_bootstrapper.resources.dll...
Preparing: C:\Users\ContainerAdministrator\AppData\Local\Temp\40932d4d0d2b8b72a8\vs_bootstrapper_d15\ko\vs_setup_bootstrapper.resources.dll...
Preparing: C:\Users\ContainerAdministrator\AppData\Local\Temp\40932d4d0d2b8b72a8\vs_bootstrapper_d15\ja\vs_setup_bootstrapper.resources.dll...
Preparing: C:\Users\ContainerAdministrator\AppData\Local\Temp\40932d4d0d2b8b72a8\vs_bootstrapper_d15\zh-Hant\vs_setup_bootstrapper.resources.dll...
Preparing: C:\Users\ContainerAdministrator\AppData\Local\Temp\40932d4d0d2b8b72a8\vs_bootstrapper_d15\cs\vs_setup_bootstrapper.resources.dll...
Preparing: C:\Users\ContainerAdministrator\AppData\Local\Temp\40932d4d0d2b8b72a8\vs_bootstrapper_d15\ru\vs_setup_bootstrapper.resources.dll...
Preparing: C:\Users\ContainerAdministrator\AppData\Local\Temp\40932d4d0d2b8b72a8\vs_bootstrapper_d15\vs_setup_bootstrapper.config...
Preparing: C:\Users\ContainerAdministrator\AppData\Local\Temp\40932d4d0d2b8b72a8\vs_bootstrapper_d15\vs_setup_bootstrapper.exe.config...
Preparing: C:\Users\ContainerAdministrator\AppData\Local\Temp\40932d4d0d2b8b72a8\vs_bootstrapper_d15\detection.json...
Preparing: C:\Users\ContainerAdministrator\AppData\Local\Temp\40932d4d0d2b8b72a8\vs_bootstrapper_d15\vs_setup_bootstrapper.json...
Removing intermediate container 3ae3b0bc1145
 ---> 03b119a6f929
Step 8/17 : RUN powershell C:\TEMP\choco-install.ps1
 ---> Running in f6ca643d96dc
Getting latest version of the Chocolatey package for download.
Getting Chocolatey from https://chocolatey.org/api/v2/package/chocolatey/0.10.15.
Downloading 7-Zip commandline tool prior to extraction.
Extracting C:\Users\ContainerAdministrator\AppData\Local\Temp\chocolatey\chocInstall\chocolatey.zip to C:\Users\ContainerAdministrator\AppData\Local\Temp\chocolatey\chocInstall...
Installing chocolatey on this machine
Creating ChocolateyInstall as an environment variable (targeting 'Machine')
  Setting ChocolateyInstall to 'C:\ProgramData\chocolatey'
WARNING: It's very likely you will need to close and reopen your shell
  before you can use choco.
Restricting write permissions to Administrators
We are setting up the Chocolatey package repository.
The packages themselves go to 'C:\ProgramData\chocolatey\lib'
  (i.e. C:\ProgramData\chocolatey\lib\yourPackageName).
A shim file for the command line goes to 'C:\ProgramData\chocolatey\bin'
  and points to an executable in 'C:\ProgramData\chocolatey\lib\yourPackageName'.

Creating Chocolatey folders if they do not already exist.

WARNING: You can safely ignore errors related to missing log files when
  upgrading from a version of Chocolatey less than 0.9.9.
  'Batch file could not be found' is also safe to ignore.
  'The system cannot find the file specified' - also safe.
chocolatey.nupkg file not installed in lib.
 Attempting to locate it from bootstrapper.
PATH environment variable does not have C:\ProgramData\chocolatey\bin in it. Adding...
WARNING: Not setting tab completion: Profile file does not exist at
'C:\Users\ContainerAdministrator\Documents\WindowsPowerShell\Microsoft.PowerShe
ll_profile.ps1'.
Chocolatey (choco.exe) is now ready.
You can call choco from anywhere, command line or powershell by typing choco.
Run choco /? for a list of functions.
You may need to shut down and restart powershell and/or consoles
 first prior to using choco.
Ensuring chocolatey commands are on the path
Ensuring chocolatey.nupkg is in the lib folder
Removing intermediate container f6ca643d96dc
 ---> 1c59ee19166f
Step 9/17 : RUN choco install git -y
 ---> Running in 651c8ff4be38
Chocolatey v0.10.15
Installing the following packages:
git
By installing you accept licenses for the packages.
Progress: Downloading git.install 2.25.1... 100%
Progress: Downloading chocolatey-core.extension 1.3.5.1... 100%
Progress: Downloading git 2.25.1... 100%

chocolatey-core.extension v1.3.5.1 [Approved]
chocolatey-core.extension package files install completed. Performing other installation steps.
 Installed/updated chocolatey-core extensions.
 The install of chocolatey-core.extension was successful.
  Software installed to 'C:\ProgramData\chocolatey\extensions\chocolatey-core'

git.install v2.25.1 [Approved]
git.install package files install completed. Performing other installation steps.
Using Git LFS
Installing 64-bit git.install...
git.install has been installed.
git.install installed to 'C:\Program Files\Git'
  git.install can be automatically uninstalled.
Environment Vars (like PATH) have changed. Close/reopen your shell to
 see the changes (or in powershell/cmd.exe just type `refreshenv`).
 The install of git.install was successful.
  Software installed to 'C:\Program Files\Git\'

git v2.25.1 [Approved]
git package files install completed. Performing other installation steps.
 The install of git was successful.
  Software install location not explicitly set, could be in package or
  default install location if installer.

Chocolatey installed 3/3 packages.
 See the log for details (C:\ProgramData\chocolatey\logs\chocolatey.log).
Removing intermediate container 651c8ff4be38
 ---> 87ccd41eb517
Step 10/17 : RUN C:\TEMP\wdksetup.exe /q
 ---> Running in 43f76f5f5602
Removing intermediate container 43f76f5f5602
 ---> 7314afbe9d41
Step 11/17 : RUN copy "C:\Program Files (x86)\Windows Kits\10\Vsix\VS2019\WDK.vsix" C:\TEMP\wdkvsix.zip
 ---> Running in 3ff2683f0729
        1 file(s) copied.
Removing intermediate container 3ff2683f0729
 ---> 42ed65317d18
Step 12/17 : RUN powershell Expand-Archive C:\TEMP\wdkvsix.zip -DestinationPath C:\TEMP\wdkvsix
 ---> Running in 32179addb8dd
Removing intermediate container 32179addb8dd
 ---> b698f631f226
Step 13/17 : RUN robocopy.exe /e "C:\temp\wdkvsix\$MSBuild\Microsoft\VC\v160" "C:\BuildTools\MSBuild\Microsoft\VC\v160" || EXIT 0
 ---> Running in bb4fa058f796

-------------------------------------------------------------------------------
   ROBOCOPY     ::     Robust File Copy for Windows
-------------------------------------------------------------------------------

  Started : Saturday, March 14, 2020 8:04:28 PM
   Source : C:\temp\wdkvsix\$MSBuild\Microsoft\VC\v160\
     Dest : C:\BuildTools\MSBuild\Microsoft\VC\v160\

    Files : *.*

  Options : *.* /S /E /DCOPY:DA /COPY:DAT /R:1000000 /W:30

------------------------------------------------------------------------------

                           1    C:\temp\wdkvsix\$MSBuild\Microsoft\VC\v160\
        *EXTRA Dir        -1    C:\BuildTools\MSBuild\Microsoft\VC\v160\1033\
        *EXTRA Dir        -1    C:\BuildTools\MSBuild\Microsoft\VC\v160\BuildCustomizations\
          *EXTRA File               1635        fxcop.xml
          *EXTRA File             415312        Microsoft.Build.CPPTasks.Common.dll
          *EXTRA File              11812        Microsoft.BuildSteps.targets
          *EXTRA File              11764        Microsoft.Cl.Common.props
          *EXTRA File               1761        Microsoft.CodeAnalysis.Extensions.props
          *EXTRA File               2774        Microsoft.CodeAnalysis.Extensions.targets
          *EXTRA File                851        Microsoft.CodeAnalysis.props
          *EXTRA File               2946        Microsoft.Cpp.Analysis.props
          *EXTRA File               2547        Microsoft.Cpp.Analysis.targets
          *EXTRA File              14079        Microsoft.Cpp.AppContainerApplication.props
          *EXTRA File                753        Microsoft.Cpp.Application.props
          *EXTRA File               1034        Microsoft.Cpp.BuildBsc.props
          *EXTRA File               3438        Microsoft.Cpp.Clang.props
          *EXTRA File              17221        Microsoft.Cpp.Clang.targets
          *EXTRA File               1422        Microsoft.Cpp.ClangTidy.props
          *EXTRA File              13530        Microsoft.Cpp.ClangTidy.targets
          *EXTRA File              33860        Microsoft.Cpp.Common.props
          *EXTRA File               1699        Microsoft.Cpp.CoreWin.props
          *EXTRA File               6892        Microsoft.Cpp.Current.targets
          *EXTRA File              18287        Microsoft.Cpp.Default.props
          *EXTRA File              28371        Microsoft.Cpp.DesignTime.targets
          *EXTRA File               1077        Microsoft.Cpp.EnableASAN.props
          *EXTRA File               1336        Microsoft.Cpp.InvalidPlatform.targets
          *EXTRA File               1405        Microsoft.Cpp.ManagedExtensions.props
          *EXTRA File               1534        Microsoft.Cpp.ManagedExtensionsNetCore.props
          *EXTRA File               1431        Microsoft.Cpp.managedExtensionsOldSyntax.props
          *EXTRA File               1399        Microsoft.Cpp.ManagedExtensionsPure.props
          *EXTRA File               1419        Microsoft.Cpp.ManagedExtensionsSafe.props
          *EXTRA File               1378        Microsoft.Cpp.mfcDynamic.props
          *EXTRA File               1225        Microsoft.Cpp.mfcStatic.props
          *EXTRA File               1867        Microsoft.Cpp.MissingToolset.targets
          *EXTRA File               1275        Microsoft.Cpp.MSVC.Toolset.ARM.props
          *EXTRA File               1291        Microsoft.Cpp.MSVC.Toolset.ARM64.props
          *EXTRA File               2780        Microsoft.Cpp.MSVC.Toolset.Common.props
          *EXTRA File               1427        Microsoft.Cpp.MSVC.Toolset.Win32.props
          *EXTRA File               1402        Microsoft.Cpp.MSVC.Toolset.x64.props
          *EXTRA File                956        Microsoft.Cpp.MultiByteCharSupport.props
          *EXTRA File               1211        Microsoft.Cpp.pginstrument.props
          *EXTRA File               1206        Microsoft.Cpp.pgoptimize.props
          *EXTRA File               1205        Microsoft.Cpp.pgupdate.props
          *EXTRA File               1016        Microsoft.Cpp.Platform.props
          *EXTRA File               1157        Microsoft.Cpp.Platform.targets
          *EXTRA File               4746        Microsoft.Cpp.props
          *EXTRA File               1426        Microsoft.Cpp.Redirect.10.props
          *EXTRA File              19328        Microsoft.Cpp.Redirect.10.targets
          *EXTRA File               7805        Microsoft.Cpp.Redirect.11.props
          *EXTRA File               5387        Microsoft.Cpp.Redirect.11.targets
          *EXTRA File               2020        Microsoft.Cpp.Redirect.12.props
          *EXTRA File                975        Microsoft.Cpp.Redirect.12.targets
          *EXTRA File               2198        Microsoft.Cpp.Redirect.14.props
          *EXTRA File               6334        Microsoft.Cpp.Redirect.14.targets
          *EXTRA File               4023        Microsoft.Cpp.Redirect.15.props
          *EXTRA File               2068        Microsoft.Cpp.Redirect.15.targets
          *EXTRA File               1352        Microsoft.Cpp.Redirect.props
          *EXTRA File               1409        Microsoft.Cpp.Redirect.targets
          *EXTRA File                992        Microsoft.Cpp.StaticAnalysis.props
          *EXTRA File               1961        Microsoft.Cpp.targets
          *EXTRA File               4076        Microsoft.Cpp.ToolsetLocation.props
          *EXTRA File               1100        Microsoft.Cpp.ToolsetLocation.targets
          *EXTRA File               1150        Microsoft.Cpp.unicodesupport.props
          *EXTRA File               2154        Microsoft.Cpp.UnitTest.props
          *EXTRA File               1926        Microsoft.Cpp.Unity.props
          *EXTRA File              12327        Microsoft.Cpp.Unity.targets
          *EXTRA File               1110        Microsoft.Cpp.UpgradeFromVC60.props
          *EXTRA File               1110        Microsoft.Cpp.UpgradeFromVC70.props
          *EXTRA File               1110        Microsoft.Cpp.UpgradeFromVC71.props
          *EXTRA File              16922        Microsoft.Cpp.VCTools.Content.props
          *EXTRA File               6063        Microsoft.Cpp.VCTools.props
          *EXTRA File               1624        Microsoft.Cpp.WholeProgramOptimization.props
          *EXTRA File               1051        Microsoft.Cpp.WinDLL.props
          *EXTRA File              14034        Microsoft.Cpp.WindowsSDK.props
          *EXTRA File               3565        Microsoft.Cpp.WindowsSDK.targets
          *EXTRA File             129412        Microsoft.CppBuild.targets
          *EXTRA File               3976        Microsoft.CppClean.targets
          *EXTRA File             143927        Microsoft.CppCommon.targets
          *EXTRA File               7201        Microsoft.Link.Common.props
          *EXTRA File               1221        Microsoft.Makefile.props
          *EXTRA File              10891        Microsoft.MakeFile.targets
          *EXTRA File               6246        Microsoft.Metagen.targets
100%        New File                1517        Microsoft.Cpp.WDK.props
                           0    C:\temp\wdkvsix\$MSBuild\Microsoft\VC\v160\Platforms\
                           0    C:\temp\wdkvsix\$MSBuild\Microsoft\VC\v160\Platforms\ARM\
          *EXTRA File               1800        Platform.Common.props
          *EXTRA File               1278        Platform.Default.props
          *EXTRA File                922        Platform.props
          *EXTRA File                807        Platform.targets
          New Dir          2    C:\temp\wdkvsix\$MSBuild\Microsoft\VC\v160\Platforms\ARM\ImportAfter\
100%        New File                 521        Microsoft.Cpp.WDK.props
100%        New File                 525        Microsoft.Cpp.WDK.targets
          New Dir          1    C:\temp\wdkvsix\$MSBuild\Microsoft\VC\v160\Platforms\ARM\ImportBefore\
100%        New File                 597        Microsoft.Cpp.WDK.props
          New Dir          1    C:\temp\wdkvsix\$MSBuild\Microsoft\VC\v160\Platforms\ARM\ImportBefore\Default\
100%        New File                1517        Microsoft.Cpp.WDK.props
          New Dir          0    C:\temp\wdkvsix\$MSBuild\Microsoft\VC\v160\Platforms\ARM\ImportBefore\Platforms\
          New Dir          0    C:\temp\wdkvsix\$MSBuild\Microsoft\VC\v160\Platforms\ARM\ImportBefore\Platforms\ARM\
          New Dir          0    C:\temp\wdkvsix\$MSBuild\Microsoft\VC\v160\Platforms\ARM\ImportBefore\Platforms\ARM\PlatformToolsets\
          New Dir          1    C:\temp\wdkvsix\$MSBuild\Microsoft\VC\v160\Platforms\ARM\ImportBefore\Platforms\ARM\PlatformToolsets\WindowsApplicationForDrivers10.0\
100%        New File                3944        Toolset.props
                           0    C:\temp\wdkvsix\$MSBuild\Microsoft\VC\v160\Platforms\ARM\PlatformToolsets\
        *EXTRA Dir        -1    C:\BuildTools\MSBuild\Microsoft\VC\v160\Platforms\ARM\PlatformToolsets\v142\
          New Dir          1    C:\temp\wdkvsix\$MSBuild\Microsoft\VC\v160\Platforms\ARM\PlatformToolsets\WindowsApplicationForDrivers10.0\
100%        New File                1865        Toolset.targets
          New Dir          2    C:\temp\wdkvsix\$MSBuild\Microsoft\VC\v160\Platforms\ARM\PlatformToolsets\WindowsKernelModeDriver10.0\
100%        New File                3545        Toolset.props
100%        New File                1840        Toolset.targets
          New Dir          2    C:\temp\wdkvsix\$MSBuild\Microsoft\VC\v160\Platforms\ARM\PlatformToolsets\WindowsUserModeDriver10.0\
100%        New File                3533        Toolset.props
100%        New File                1830        Toolset.targets
          New Dir          1    C:\temp\wdkvsix\$MSBuild\Microsoft\VC\v160\Platforms\ARM\PlatformUpgrade\
100%        New File                3511        Microsoft.Cpp.WDK.PlatformUpgrade.props
          New Dir          0    C:\temp\wdkvsix\$MSBuild\Microsoft\VC\v160\Platforms\ARM64\
          New Dir          2    C:\temp\wdkvsix\$MSBuild\Microsoft\VC\v160\Platforms\ARM64\ImportAfter\
100%        New File                 521        Microsoft.Cpp.WDK.props
100%        New File                 525        Microsoft.Cpp.WDK.targets
          New Dir          1    C:\temp\wdkvsix\$MSBuild\Microsoft\VC\v160\Platforms\ARM64\ImportBefore\
100%        New File                 597        Microsoft.Cpp.WDK.props
          New Dir          0    C:\temp\wdkvsix\$MSBuild\Microsoft\VC\v160\Platforms\ARM64\PlatformToolsets\
          New Dir          2    C:\temp\wdkvsix\$MSBuild\Microsoft\VC\v160\Platforms\ARM64\PlatformToolsets\WindowsApplicationForDrivers10.0\
100%        New File                3944        Toolset.props
100%        New File                1865        Toolset.targets
          New Dir          2    C:\temp\wdkvsix\$MSBuild\Microsoft\VC\v160\Platforms\ARM64\PlatformToolsets\WindowsKernelModeDriver10.0\
100%        New File                3545        Toolset.props
100%        New File                1840        Toolset.targets
          New Dir          2    C:\temp\wdkvsix\$MSBuild\Microsoft\VC\v160\Platforms\ARM64\PlatformToolsets\WindowsUserModeDriver10.0\
100%        New File                3533        Toolset.props
100%        New File                1830        Toolset.targets
          New Dir          1    C:\temp\wdkvsix\$MSBuild\Microsoft\VC\v160\Platforms\ARM64\PlatformUpgrade\
100%        New File                3511        Microsoft.Cpp.WDK.PlatformUpgrade.props
                           0    C:\temp\wdkvsix\$MSBuild\Microsoft\VC\v160\Platforms\Win32\
          *EXTRA File               1870        Platform.Common.props
          *EXTRA File               1997        Platform.Default.props
          *EXTRA File                799        Platform.props
          *EXTRA File                807        Platform.targets
          New Dir          2    C:\temp\wdkvsix\$MSBuild\Microsoft\VC\v160\Platforms\Win32\ImportAfter\
100%        New File                 521        Microsoft.Cpp.WDK.props
100%        New File                 525        Microsoft.Cpp.WDK.targets
          New Dir          1    C:\temp\wdkvsix\$MSBuild\Microsoft\VC\v160\Platforms\Win32\ImportBefore\
100%        New File                 597        Microsoft.Cpp.WDK.props
          New Dir          1    C:\temp\wdkvsix\$MSBuild\Microsoft\VC\v160\Platforms\Win32\ImportBefore\Default\
100%        New File                1517        Microsoft.Cpp.WDK.props
                           0    C:\temp\wdkvsix\$MSBuild\Microsoft\VC\v160\Platforms\Win32\PlatformToolsets\
        *EXTRA Dir        -1    C:\BuildTools\MSBuild\Microsoft\VC\v160\Platforms\Win32\PlatformToolsets\v142\
          New Dir          2    C:\temp\wdkvsix\$MSBuild\Microsoft\VC\v160\Platforms\Win32\PlatformToolsets\WindowsApplicationForDrivers10.0\
100%        New File                3944        Toolset.props
100%        New File                1865        Toolset.targets
          New Dir          2    C:\temp\wdkvsix\$MSBuild\Microsoft\VC\v160\Platforms\Win32\PlatformToolsets\WindowsKernelModeDriver10.0\
100%        New File                3545        Toolset.props
100%        New File                1840        Toolset.targets
          New Dir          2    C:\temp\wdkvsix\$MSBuild\Microsoft\VC\v160\Platforms\Win32\PlatformToolsets\WindowsUserModeDriver10.0\
100%        New File                3533        Toolset.props
100%        New File                1830        Toolset.targets
          New Dir          1    C:\temp\wdkvsix\$MSBuild\Microsoft\VC\v160\Platforms\Win32\PlatformUpgrade\
100%        New File                3511        Microsoft.Cpp.WDK.PlatformUpgrade.props
                           0    C:\temp\wdkvsix\$MSBuild\Microsoft\VC\v160\Platforms\x64\
          *EXTRA File               1504        Platform.Common.props
          *EXTRA File               2012        Platform.Default.props
          *EXTRA File                799        Platform.props
          *EXTRA File               1449        Platform.targets
          New Dir          2    C:\temp\wdkvsix\$MSBuild\Microsoft\VC\v160\Platforms\x64\ImportAfter\
100%        New File                 521        Microsoft.Cpp.WDK.props
100%        New File                 525        Microsoft.Cpp.WDK.targets
          New Dir          1    C:\temp\wdkvsix\$MSBuild\Microsoft\VC\v160\Platforms\x64\ImportBefore\
100%        New File                 597        Microsoft.Cpp.WDK.props
          New Dir          1    C:\temp\wdkvsix\$MSBuild\Microsoft\VC\v160\Platforms\x64\ImportBefore\Default\
100%        New File                1517        Microsoft.Cpp.WDK.props
                           0    C:\temp\wdkvsix\$MSBuild\Microsoft\VC\v160\Platforms\x64\PlatformToolsets\
        *EXTRA Dir        -1    C:\BuildTools\MSBuild\Microsoft\VC\v160\Platforms\x64\PlatformToolsets\v142\
          New Dir          2    C:\temp\wdkvsix\$MSBuild\Microsoft\VC\v160\Platforms\x64\PlatformToolsets\WindowsApplicationForDrivers10.0\
100%        New File                3944        Toolset.props
100%        New File                1865        Toolset.targets
          New Dir          2    C:\temp\wdkvsix\$MSBuild\Microsoft\VC\v160\Platforms\x64\PlatformToolsets\WindowsKernelModeDriver10.0\
100%        New File                3545        Toolset.props
100%        New File                1840        Toolset.targets
          New Dir          2    C:\temp\wdkvsix\$MSBuild\Microsoft\VC\v160\Platforms\x64\PlatformToolsets\WindowsUserModeDriver10.0\
100%        New File                3533        Toolset.props
100%        New File                1830        Toolset.targets
          New Dir          1    C:\temp\wdkvsix\$MSBuild\Microsoft\VC\v160\Platforms\x64\PlatformUpgrade\
100%        New File                3511        Microsoft.Cpp.WDK.PlatformUpgrade.props
          New Dir          1    C:\temp\wdkvsix\$MSBuild\Microsoft\VC\v160\WDKConversion\
100%        New File                1564        PreConfiguration.props

------------------------------------------------------------------------------

               Total    Copied   Skipped  Mismatch    FAILED    Extras
    Dirs :        42        34         8         0         0         5
   Files :        45        45         0         0         0        91
   Bytes :    92.2 k    92.2 k         0         0         0    1.02 m
   Times :   0:00:02   0:00:00                       0:00:00   0:00:01


   Speed :              139757 Bytes/sec.
   Speed :               7.996 MegaBytes/min.
   Ended : Saturday, March 14, 2020 8:04:30 PM

Removing intermediate container bb4fa058f796
 ---> f47688a45d1b
Step 14/17 : ADD https://download.microsoft.com/download/3/2/2/3224B87F-CFA0-4E70-BDA3-3DE650EFEBA5/vcredist_x64.exe C:\TEMP\vc_2010_x64.exe
Downloading [==================================================>]  5.719MB/5.719MB
 ---> 1ff241d360c4
Step 15/17 : RUN C:\TEMP\vc_2010_x64.exe /quiet /install
 ---> Running in 3d3416968804
Removing intermediate container 3d3416968804
 ---> 6bfa37c9663e
Step 16/17 : SHELL ["cmd"]
 ---> Running in 27ccfe45e5a4
Removing intermediate container 27ccfe45e5a4
 ---> 3f5014dc1e11
Step 17/17 : CMD [ "cmd","/k","c:\\BuildTools\\VC\\Auxiliary\\Build\\vcvarsall.bat", "x86_x64", "10.0.18362.0" ]
 ---> Running in c951177cb065
Removing intermediate container c951177cb065
 ---> 84125ddf167d
Successfully built 84125ddf167d

```

Run container based on that image:
----------------------------------

```
PS C:\Users\User\test> docker run -it 84125ddf167d
```

Clone and build WNBD via VS 2019 command prompt
--------------------------------------------------

```
> git clone https://github.com/cloudbase/wnbd
> msbuild wnbd\vstudio\wnbd.sln
> copy wnbd\vstudio\x64\Debug\driver\* .
> copy wnbd\vstudio\x64\Debug\wnbd-client.exe .
```
