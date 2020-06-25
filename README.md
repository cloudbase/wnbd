Windows Network Block Device (WNBD)
===================================

Build Status:
-------------
[![Build status](https://ci.appveyor.com/api/projects/status/2m73dxm2t7s7jlit/branch/master?svg=true)](https://ci.appveyor.com/project/aserdean/wnbd/branch/master)


What is WNBD?
-------------

WNBD is a client side implementation of [Network Block Device](https://nbd.sourceforge.io/)

Prerequisites
-------------

Visual Studio 2019 build tools or GUI ([Community version](https://visualstudio.microsoft.com/thank-you-downloading-visual-studio/?sku=Community&rel=16)  or above)

[Windows Driver Kit 1909](https://docs.microsoft.com/en-us/windows-hardware/drivers/download-the-wdk)

Folders
-------

* <a href="Dockerfile/">Dockerfile</a> contains a Dockerfile to create a Docker image that contains needed prerequisites

* <a href="driver/">driver</a> contains the driver sources

* <a href="lib/ksocket_wsk/">ksocket_wsk</a> contains the WSK implementation needed to communicate with the Network Block Device server

* <a href="userspace/userspace/">userspace</a> contains a simple console application `wnbd-client` useful for testing

* <a href="vstudio/">vstudio</a> contains the Visual Studio solution file and its projects

How to build
------------

```
> git clone https://github.com/cloudbase/wnbd
> msbuild wnbd\vstudio\wnbd.sln
> copy wnbd\vstudio\x64\Debug\driver\* .
> copy wnbd\vstudio\x64\Debug\wnbd-client.exe .
```

* You can download the latest prebuilt packages from Appveyor via the links:
  * [Debug](https://ci.appveyor.com/api/projects/aserdean/wnbd/artifacts/wnbd-Debug.zip?job=Configuration%3A+Debug)
  * [Release](https://ci.appveyor.com/api/projects/aserdean/wnbd/artifacts/wnbd-Release.zip?job=Configuration%3A+Release)

How to install
--------------

* **Prerequisites**.

  After building, the driver is automatically test signed. To install the built driver you must make sure that your target machine is Test Signed enabled first.
  To enable Test Signing on your target machine, please issue the following from an elevated command prompt:
  ```
  > bcdedit.exe /set testsigning yes
  ```
  Please note that you can enable driver signing only if Secure Boot is not enabled on the target machine.
  To check if your target machine does not have Secure Boot enabled issue `Confirm-SecureBootUEFI` from an elevated powershell prompt
  ```
  PS C:\WINDOWS\system32> Confirm-SecureBootUEFI
  False
  ```
  **A reboot is required after changing `bcdedit` settings**

* **Installation/removal**.

  We require [devcon.exe](https://cloudbase.it/downloads/devcon.exe) utility to install and uninstall the driver.

  * To **install** the driver issue the following from an elevated command prompt:
    ```
    > .\devcon.exe install .\wnbd.inf root\wnbd
    ```
  (The command above assumes that the utility `devcon.exe` and the driver files `wnbd.inf`, `wnbd.cat`, `wnbd.sys` are in the current directory)

  * To **uninstall** the driver issue the following from an elevated command prompt:
    ```
    .\devcon.exe remove "root\wnbd"
    ```
  (The command above assumes that the utility `devcon.exe` is in the current directory)

  For convenience, we included <a href="vstudio/reinstall.ps1">reinstall.ps1</a> which uninstalls (ignoring the error) and installs the driver again.

Ceph integration
----------------

Mapping an umapping RDB images is straighforward, just use [rbd-nbd](https://docs.ceph.com/docs/master/man/8/rbd-nbd/), part of the [Ceph Windows port](https://github.com/ceph/ceph/pull/34859).

    rbd-nbd map img1
    rbd-nbd unmap img1    

Testing with NBD (Network Block Device)
---------------------------------------

Please note that the following is not needed for Ceph and is mostly intended to be used in development scenarios.

We assume you are familiar with <a href="https://github.com/NetworkBlockDevice/nbd#using-nbd">using NBD</a>.

  * `wnbd-client` syntax
  ```
  PS C:\workspace> .\wnbd-client.exe
  Syntax:
  wnbd-client map  <InstanceName> <HostName> <PortName> <ExportName> <DoNotNegotiate>
  wnbd-client unmap <InstanceName>
  wnbd-client list
  ```

  * NBD server configuration:
  ```
  root@ubuntu-Virtual-Machine:/home/ubuntu# cat /etc/nbd-server/config
  [generic]
  # If you want to run everything as root rather than the nbd user, you
  # may either say "root" in the two following lines, or remove them
  # altogether. Do not remove the [generic] section, however.
          port = 9000
          user = nbd
          group = nbd
          includedir = /etc/nbd-server/conf.d

  # What follows are export definitions. You may create as much of them as
  # you want, but the section header has to be unique.

  [foo]
      exportname = /blaz/bla.img
      port = 9000
      copyonwrite = true
  root@ubuntu-Virtual-Machine:/home/ubuntu# ifconfig eth0 | grep 172
          inet 172.17.160.251  netmask 255.255.255.240  broadcast 172.17.160.255
  ```

  * Mapping an export:
  ```
  PS C:\workspace> .\wnbd-client.exe map test2 172.17.160.251 9000 foo
  InstanceName=test2
  HostName=172.17.160.251
  PortName=9000
  ExportName=foo
  MustNegociate=1
  PS C:\workspace> Get-Disk

  Number Friendly Name            Serial Number   HealthStatus         OperationalStatus      Total Size Partition
                                                                                                        Style
  ------ -------------            -------------   ------------         -----------------      ---------- ----------
  0      Msft Virtual Disk                        Healthy              Online                     127 GB GPT
  1      WNBD Dis WNBD_DISK_ID    test2           Healthy              Online                     256 MB RAW
  ```

  * Listing the mapped device
  ```
  PS C:\workspace> .\wnbd-client.exe list
  Status: 0
  InstanceName    Pid     DiskNumber
  test2           6712            1
  ```

  * Unmapping the device
  ```
  PS C:\workspace> .\wnbd-client.exe unmap test2
  PS C:\workspace> Get-Disk

  Number Friendly Name             Serial Number    HealthStatus         OperationalStatus      Total Size Partition
                                                                                                          Style
  ------ -------------             -------------    ------------         -----------------      ---------- ----------
  0      Msft Virtual Disk                          Healthy              Online                     127 GB GPT

```

What other documentation is available?
--------------------------------------

Build via docker

- [BUILD.Docker.md]

[BUILD.Docker.md]:Dockerfile/Readme.md
