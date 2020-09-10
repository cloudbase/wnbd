About
=====

This guide describes building WNBD using a Docker Windows container,
leveraging the Docker file provided by WNBD.

The resulting container provides all the build requirements (e.g.
Visual Studio), without bloating the host.

This simple example uses an interactive shell, feel free to automate
the process, maybe copying the resulting binaries to a different
location.

Building the image
------------------

```PowerShell
docker build . -t wnbd_build
```

Run container using the resulting image
---------------------------------------

The following command will enter an interactive container shell.
This shell provides the prerequisites for building WNBD.

```PowerShell
docker run -it wnbd_build
```

Clone and build WNBD via VS 2019 command prompt
-----------------------------------------------

Run the following commands in the container interactive shell.

```PowerShell
git clone https://github.com/cloudbase/wnbd
msbuild wnbd\vstudio\wnbd.sln
copy wnbd\vstudio\x64\Debug\driver\* .
copy wnbd\vstudio\x64\Debug\wnbd-client.exe .
copy wnbd\vstudio\x64\Debug\wnbd.dll .
```
