Windows Network Block Device (WNBD)
===================================

Build Status:
-------------

[![Build status](https://ci.appveyor.com/api/projects/status/2m73dxm2t7s7jlit/branch/master?svg=true)](https://ci.appveyor.com/project/aserdean/wnbd/branch/master)


What is WNBD?
-------------

The ``WNBD`` project provides virtual block devices through a Storport Miniport driver. It can
connect to a [Network Block Device (NBD)](https://nbd.sourceforge.io/) server, which exposes
device details and acts as an IO channel. As an alternative, it can dispatch IO commands to
an userspace process using a DeviceIoControl based interface.

The project also provides the ``wnbd.dll`` library, which handles the userspace and driver
communication. It provides the following features:

* Creating WNBD devices (optionally connecting to a NBD server)
* Removing WNBD devices
* Listing WNBD devices
* Providing IO counters (driver as well as userspace counters)
* Processing IO requests (when not using NBD)

WNBD provides a low level API (the ``*Ioctl*`` functions), as well as a high level API that
includes the IO dispatching boilerplate. Please check the [include](include\).
public headers for more details.

Submitting patches
------------------

WNBD is [licensed](LICENSE/) under LGPL v2.1.

Code contributions must include a valid "Signed-off-by" acknowledging
the license for the modified or contributed file.

We do not require assignment of copyright to contribute code; code is
contributed under the terms of the applicable license.

Please check [SubmittingPatches.rst](SubmittingPatches.rst/) for more details.

Prerequisites
-------------

Visual Studio 2019 build tools or GUI ([Community version](https://visualstudio.microsoft.com/thank-you-downloading-visual-studio/?sku=Community&rel=16)  or above)

[Windows Driver Kit 1909](https://docs.microsoft.com/en-us/windows-hardware/drivers/download-the-wdk)

As an alternative, you may use a [Docker Container](Dockerfile/Readme.md) that provides the build prerequisites.

Folders
-------

* [Dockerfile](Dockerfile/) a Dockerfile providing the build prerequisites
* [driver](driver/) the driver sources
* [include](include/) public headers
* [ksocket_wsk](ksocket_wsk/) a WSK wrapper used to communicate with the Network Block Device server
* [wnbd-client](wnbd-client/) the WNBD CLI
* [libwnbd](libwbd/) ``wnbd.dll`` - the WNBD userspace library
* [vstudio](vstudio/) the Visual Studio solution file and its projects

How to build
------------

```PowerShell
git clone https://github.com/cloudbase/wnbd
msbuild wnbd\vstudio\wnbd.sln
copy wnbd\vstudio\x64\Debug\driver\* .
copy wnbd\vstudio\x64\Debug\wnbd-client.exe .
copy wnbd\vstudio\x64\Debug\libwnbd.dll .
```

You can download the latest prebuilt packages from Appveyor via the links:

* [Debug](https://ci.appveyor.com/api/projects/aserdean/wnbd/artifacts/wnbd-Debug.zip?job=Configuration%3A+Debug)
* [Release](https://ci.appveyor.com/api/projects/aserdean/wnbd/artifacts/wnbd-Release.zip?job=Configuration%3A+Release)

How to install
--------------

### Prerequisites

By default, the driver will be "test signed" as part of the build process. In order to install it,
make sure that your target machine allows "test signed" drivers.
To enable test signing mode on your target machine, please issue the following from an elevated
command prompt:

```PowerShell
bcdedit.exe /set testsigning yes
```

Please note that test signed drivers cannot be used when Secure Boot is enabled on the target
machine. To check the Secure Boot configuration, issue `Confirm-SecureBootUEFI` from an elevated
PowerShell prompt

```PowerShell
Confirm-SecureBootUEFI
```

**A reboot is required after changing `bcdedit` settings**

Those steps are not required when using a certified driver.

### Install / Uninstall

We require the [devcon.exe](https://cloudbase.it/downloads/devcon.exe) utility in order to
install and uninstall the driver.

To **install** the driver, issue the following from an elevated command prompt:

```PowerShell
.\devcon.exe install .\wnbd.inf root\wnbd
```

(The command above assumes that the utility `devcon.exe` and the driver files `wnbd.inf`, `wnbd.cat`, `wnbd.sys` are in the current directory)

After installing the driver, copy ``libwnbd.dll`` and ``wnbd-client.exe``,
also adding the destination folder to the environment PATH variable.

To **uninstall** the driver, issue the following from an elevated PowerShell prompt:

```PowerShell
.\devcon.exe remove "root\wnbd"
pnputil.exe /enum-drivers | sls -Context 5 wnbd | findstr Published | `
    % {$_ -match "(oem\d+.inf)"; pnputil.exe /delete-driver $matches[0] /force }
```

(The command above assumes that the utility `devcon.exe` is in the current directory)

For convenience, we included [reinstall.ps1](vstudio/reinstall.ps1), which installs/reinstalls the driver.

[This project](https://github.com/cloudbase/ceph-windows-installer) allows building MSI installers that bundle WNBD and the Ceph Windows clients.

Ceph integration
----------------

Mapping and umapping RBD images is straightforward, just use [rbd](https://docs.ceph.com/docs/master/man/8/rbd/), part of the [Ceph Windows port](https://github.com/ceph/ceph/pull/34859).

```PowerShell
rbd device map $imageName
rbd device unmap $imageName
```

Mapping NBD devices
-------------------

The following samples describe configuring a Linux NBD server and connecting to it using WNBD.
Please check [this page](https://github.com/NetworkBlockDevice/nbd#using-nbd) for more details
about using NBD.

Use ``wnbd-client help [<command-name>]`` to get the full list of commands as well as the
available options.

### NBD server configuration

```bash
cat /etc/nbd-server/config
```

```ini
[generic]
# If you want to run everything as root rather than the nbd user, you
# may either say "root" in the two following lines, or remove them
# altogether. Do not remove the [bgeneric] section, however.
port = 10809
user = nbd
group = nbd
includedir = /etc/nbd-server/conf.d

# What follows are export definitions. You may create as much of them as
# you want, but the section header has to be unique.
[foo]
exportname = /image/path.img
port = 10809
copyonwrite = true
```


### Mapping an NBD export

```PowerShell
# feel free to use a different name for the mapping
wnbd-client.exe map test2 $nbdServerAddress 10809 foo
Get-Disk
```
```
Number Friendly Name            Serial Number   HealthStatus         OperationalStatus      Total Size Partition
                                                                                                      Style
------ -------------            -------------   ------------         -----------------      ---------- ----------
0      Msft Virtual Disk                        Healthy              Online                     127 GB GPT
1      WNBD Dis WNBD_DISK_ID    test2           Healthy              Online                     256 MB RAW
```

### Listing mapped devices

```PowerShell
wnbd-client.exe list
```
```
Pid         DiskNumber  Nbd    Owner            InstanceName
3508        1           true   wnbd-client      test2
4024        2           false  ceph-rbd-wnbd    rbd/rbd_win_10g
```

### Unmapping the device

```PowerShell
wnbd-client.exe unmap test2
Get-Disk
```
```
Number Friendly Name             Serial Number    HealthStatus         OperationalStatus      Total Size Partition
                                                                                                            Style
    ------ -------------             -------------    ------------         -----------------      ---------- ----------
    0      Msft Virtual Disk                          Healthy              Online                     127 GB GPT
```
