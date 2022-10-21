Windows Network Block Device (WNBD)
===================================

Build Status:
-------------

[![Build status](https://ci.appveyor.com/api/projects/status/2m73dxm2t7s7jlit/branch/main?svg=true)](https://ci.appveyor.com/project/aserdean/wnbd/branch/main)

What is WNBD?
-------------

The ``WNBD`` project provides virtual block devices through a Storport Miniport driver. It can
connect to a [Network Block Device (NBD)](https://nbd.sourceforge.io/) server, which exposes
device details and acts as an IO channel. As an alternative, it can dispatch IO commands to
an userspace process using a DeviceIoControl based interface.

The project also provides the ``libwnbd.dll`` library, which handles the userspace and driver
communication. It provides the following features:

* Creating WNBD devices (optionally connecting to a NBD server)
* Removing WNBD devices
* Listing WNBD devices
* Providing IO counters (driver as well as userspace counters)
* Processing IO requests (when not using NBD)

WNBD provides a low level API (the ``*Ioctl*`` functions), as well as a high level API that
includes the IO dispatching boilerplate. Please check the [public headers](include/) for
more details.

Submitting patches
------------------

WNBD is [licensed](LICENSE/) under LGPL v2.1.

Code contributions must include a valid "Signed-off-by" acknowledging
the license for the modified or contributed file.

We do not require assignment of copyright to contribute code; code is
contributed under the terms of the applicable license.

Please check [SubmittingPatches.rst](SubmittingPatches.rst/) for more details.

Folders
-------

* [Dockerfile](Dockerfile/) a Dockerfile providing the build prerequisites
* [driver](driver/) the driver sources
* [include](include/) public headers
* [ksocket_wsk](ksocket_wsk/) a WSK wrapper used to communicate with the Network Block Device server
* [wnbd-client](wnbd-client/) the WNBD CLI
* [libwnbd](libwbd/) ``libwnbd.dll`` - the WNBD userspace library
* [vstudio](vstudio/) the Visual Studio solution file and its projects

How to build
------------

### Prerequisites

Visual Studio 2019 build tools or GUI
([Community version](https://visualstudio.microsoft.com/thank-you-downloading-visual-studio/?sku=Community&rel=16)
or above)

[Windows Driver Kit 1909](https://docs.microsoft.com/en-us/windows-hardware/drivers/download-the-wdk)

As an alternative, you may use a [Docker Container](Dockerfile/Readme.md)
that provides the build prerequisites.

``wnbd-client`` uses Boost, other dependencies might be added as well in the future.
The WNBD dependencies are fetched using [this script](get_dependencies.ps1), which
is automatically invoked when building the solution.

### Building and packaging

The following snippet builds the WNBD project and copies the generated binaries to
``$outDir``. The "reinstall.ps1" script removes existing WNBD devices and drivers,
proceeding to install the driver placed in the same directory.

```PowerShell
git clone https://github.com/cloudbase/wnbd
msbuild wnbd\vstudio\wnbd.sln
# The following binaries can be archived and copied to a destination
# host. Use "reinstall.ps1" to install or reinstall the driver.
copy wnbd\vstudio\x64\Debug\driver\* $outDir
copy wnbd\vstudio\x64\Debug\wnbd-client.exe $outDir
copy wnbd\vstudio\x64\Debug\libwnbd.dll $outDir
copy wnbd\vstudio\reinstall.ps1 $outDir
```

You can also download the latest prebuilt packages from Appveyor via the links:

* [Debug](https://ci.appveyor.com/api/projects/aserdean/wnbd/artifacts/wnbd-Debug.zip?job=Configuration%3A+Debug)
* [Release](https://ci.appveyor.com/api/projects/aserdean/wnbd/artifacts/wnbd-Release.zip?job=Configuration%3A+Release)

When using the above Appveyor artifacts, make sure to also install the latest
Microsoft VC runtime. Note that VS 2015, 2017 and 2019 use the same redistributable
VC runtime package. You can skip this step when using the below MSI installer.

[This project](https://github.com/cloudbase/ceph-windows-installer) allows building
an MSI installer that bundles WNBD and the Ceph Windows clients.

How to install
--------------

### MSI installer

The Ceph MSI installer bundles a signed version of the WNBD driver. It can be
downloaded from here: https://cloudbase.it/ceph-for-windows/

If however you're interested in the manual install procedure, check the following
sections.

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

You can use the `wnbd-client.exe` command line tool to install and remove the driver.

To ***install*** the driver, issue the following from an elevated PowerShell prompt:
```Powershell
.\wnbd-client.exe install-driver .\wnbd.inf
```

To ***uninstall*** the driver, issue the following from an elevated PowerShell prompt:
```Powershell
.\wnbd-client.exe uninstall-driver
```
The `uninstall-driver` command will hard disconnect any existing disk mappings and WNBD
storage adapters and then remove any previous installations of the driver.

After installing the driver, you may want to copy ``wnbd-client.exe`` and ``libwnbd.dll``
to a directory that's part of the ``PATH`` environment variable.

Note that a host reboot may be required after installing or uninstalling the
WNBD driver.

Version
-------

The following command can be used to retrieve the wnbd-client, libwnbd and wnbd
driver version:

```PowerShell
wnbd-client.exe -v
```
```
wnbd-client.exe: 0.2.1-3-g3dc976b
libwnbd.dll: 0.2.1-3-g3dc976b
wnbd.sys: 0.2.1-3-g3dc976b
```

The driver version may also be retrieved by right clicking the ``wnbd.sys`` file
and going to the ``Details`` section of the ``Properties`` page.

Note that the driver version reported by the Windows Device Manager is actually
the build timestamp included in the ``DriverVer`` section of the ``wnbd.inf``
file (e.g. DriverVer = 02/19/2021,14.21.25.925).

Ceph integration
----------------

Mapping and umapping RBD images is straightforward, just use [rbd](https://docs.ceph.com/en/latest/man/8/rbd/), part of the [Ceph Windows port](https://github.com/ceph/ceph/pull/34859).

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
wnbd-client.exe map foo $nbdServerAddress --port 10809
Get-Disk
```
```
Number Friendly Name            Serial Number   HealthStatus         OperationalStatus      Total Size Partition
                                                                                                      Style
------ -------------            -------------   ------------         -----------------      ---------- ----------
0      Msft Virtual Disk                        Healthy              Online                     127 GB GPT
1      WNBD WNBD_DISK           foo             Healthy              Online                     256 MB RAW
```

### Listing mapped devices

```PowerShell
wnbd-client.exe list
```
```
Pid         DiskNumber  Nbd    Owner            InstanceName
3508        1           true   wnbd-client      foo
4024        2           false  ceph-rbd-wnbd    rbd/rbd_win_10g
```

### Unmapping the device

By default, a soft disconnect is attempted. If that fails or times out,
a hard disconnect is performed as fallback.

A soft disconnect will notify other storage drivers that the disk is
about to be removed. Such drivers can temporarily block the disk removal
until ready (e.g. wait for pending IO, flush caches, wait for open files, etc).

Using the ``--debug`` flag will tell what's blocking the soft device
removal (for example, a volume ID if there are open files).

A hard remove doesn't emit PnP notifications to other storage drivers,
requesting the WNBD driver to remove the disk immediately.

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

WNBD driver logging
-------------------

To help develop and debug the driver we use the following facilities
provided by the operating system:

- [DbgPrint](https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-dbgprintex)

- [WPP](https://docs.microsoft.com/en-us/windows-hardware/drivers/devtest/wpp-software-tracing)

- [ETW](https://docs.microsoft.com/en-us/windows-hardware/drivers/devtest/event-tracing-for-windows--etw-)

Note that the log level as well as the logging facilities are configurable. Please check the
[driver options](#driver-options) section for more details.

``libwnbd`` can use a logger provided by the library consumer application, defaulting to stderr logging.
So for example, if you're using ``libwnbd`` with Ceph, the according messages will go to the Ceph log.
Driver logs will still be separate, using one or more of the logging facilities described in this section.

### DbgPrint

To view and collect the log messages via `DbgPrint` you can use either a
[debugger](https://docs.microsoft.com/en-us/windows-hardware/drivers/debugger/)
or, if you do not want to go through the process of attaching the debugger, you
can simply use [DebugView](https://docs.microsoft.com/en-us/sysinternals/downloads/debugview).

### WPP

To collect messages using the `WPP` provider you can use the following snippet from an elevated command prompt:
```CMD
logman create trace "WNBD_tracing_session" -p {E35EAF83-0F07-418A-907C-141CD200F252} 0xffffffff 0xff -o c:\TraceFile.etl -rt
logman start "WNBD_tracing_session"
```
When you want to stop collecting you can issue:
```CMD
logman stop "WNBD_tracing_session"
```
To decode the generated ETL file you will need to have access to the debug symbols (`wnbd.pdb`) and use traceview
utility from the WDK binary folder.

### ETW

Error, warning and info ETW events will end up in the ``System`` section of the Windows Event Log.

Debug messages can be retrieved using tracing sessions, leveraging the `tracelog` and `tracerpt`
tools from the WDK binary folder.

To start a trace session one can use:
```CMD
tracelog -start WNBDEventdrv -guid #FFACC4E7-C115-4FE2-9D3C-80FAE73BAB91 -f WNBDEventdrv.etl
```

To stop:
```CMD
tracelog -stop WNBDEventdrv
```

To display the trace use:
```CMD
tracerpt WNBDEventdrv.etl
```

Driver options
--------------

Use the following command to retrieve the list of WNBD driver options:

```PowerShell
wnbd-client.exe list-opt
```
```
LogLevel           : 1 (Default: 1)
NewMappingsAllowed : true (Default: true)
EtwLoggingEnabled  : true (Default: true)
WppLoggingEnabled  : false (Default: false)
DbgPrintEnabled    : true (Default: true)
```

Use the following command to configure an option. If the setting should persist
across reboots, pass the optional ``--persistent`` argument.

The log level matches the DebugPrint filter levels:

* ``WNBD_LVL_ERROR`` (0)
* ``WNBD_LVL_WARN``  (1)
* ``WNBD_LVL_INFO``  (3)
* ``WNBD_LVL_DEBUG`` (4)

```PowerShell
wnbd-client.exe set-opt LogLevel 4 --persistent
```

To list only the persistent options, pass the ``--persistent`` parameter:
```PowerShell
wnbd-client.exe list-opt --persistent
```
```
LogLevel           : 4 (Default: 1)
```

To clear a custom setting and reapply the default value, use the following command:
```PowerShell
wnbd-client.exe reset-opt LogLevel
```

Passing the ``--persistent`` flag will remove the persistent setting as well.

Limitations
===========

### Hyper-V disk addressing

**Warning:** Hyper-V identifies passthrough VM disks by number instead of
SCSI ID, although the disk number can change across host reboots. This means
that the VMs can end up using incorrect disks after rebooting the host, which
is an important security concern. This issue also affects iSCSI and Fibre
Channel disks.

There are a few possible ways of avoiding this Hyper-V limitation:

* use an NTFS/ReFS partition to store VHDX image files instead of directly
  attaching the WNBD disk. This may slightly impact the IO performance.
* use the Hyper-V ``AutomaticStartAction`` setting to prevent the VMs from
  booting with the incorrect disks and have a script that updates VM disks
  attachments before powering them back on. The ``ElementName`` field of the
  [Msvm_StorageAllocationSettingData](https://docs.microsoft.com/en-us/windows/win32/hyperv_v2/msvm-storageallocationsettingdata)
  [WMI](https://docs.microsoft.com/en-us/windows/win32/wmisdk/wmi-start-page)
  class may be used to label VM disk attachments.
* use the Openstack Hyper-V driver, which automatically refreshes the VM disk
  attachments before powering them back on.

Troubleshooting
===============

### Privileges

Most ``wnbd-client`` commands require privileged rights. Make sure to use an elevated
PowerShell or CMD command prompt. That also applies when using other clients that leverage
the ``libwnbd`` library, such as Ceph clients.

### Crash dumps

Windows stores kernel crash dumps at ``%SystemRoot%\Memory.dmp``, which is tipically
``C:\Windows\Memory.dmp``.

Userspace crash dumps can be placed at a configurable location and enabled for all
applications or just predefined ones, as outlined
[here](https://docs.microsoft.com/en-us/windows/win32/wer/collecting-user-mode-dumps).

Whenever a Windows application crashes, an event will be submitted to the ``Application``
Windows Event Log, having Event ID 1000. The entry will also include the process id,
the faulting module name and path as well as the exception code.

Please note that in order to analyze crash dumps, the debug symbols (``.pdb`` files)
are required.

### Logging

Please see the [logging](#WNBD-driver-logging) and [driver options](#driver-options)
sections.

You can start by checking the ``System`` Windows Event Log for ``wnbd`` driver errors.
If needed, increase the ``wnbd`` log level and start a tracing session.

### IO counters

The following command can be used to retrieve per-disk IO counters that are gathered
at the driver level:

```PowerShell
wnbd-client.exe stats $mapping
```
```
Disk stats
TotalReceivedIORequests        : 70
TotalSubmittedIORequests       : 0
TotalReceivedIOReplies         : 70
UnsubmittedIORequests          : 0
PendingSubmittedIORequests     : 0
AbortedSubmittedIORequests     : 0
AbortedUnsubmittedIORequests   : 0
CompletedAbortedIORequests     : 0
OutstandingIOCount             : 0
```

The ``received`` requests are the ones coming from Storport, the driver upper layer.
The ``submitted`` requests are IO requests that have been forwarded to the storage backend
(e.g. NBD server). The ``replies`` counter shows replies coming from the storage backend.
Aborted requests are usually requests that have timed out, typically after 10 up to 30
seconds.

Those counters are very useful when debugging stuck IO, helping us understand the state of
the requests. The userspace ``libwnbd`` library also gathers IO counters, which can be
retrieved by the library consumers.

Worth mentioning that by default, Storport limits the number of pending IO requests to
1000 per adapter and 255 per LUN.
