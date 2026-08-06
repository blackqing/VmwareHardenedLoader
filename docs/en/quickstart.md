# Quick start

This guide explains how to use the install, test-signing, and uninstall scripts in `bin` on a test virtual machine. The scripts can be run from the repository root or any working directory; the examples below assume the repository is at `D:\VmwareHardenedLoader`.

[Back to README](../../README.md) · [简体中文](../zh-CN/quickstart.md)

## Prerequisites

- Complete a build by following the [build guide](build.md), or download latest pre-built binaries from `https://github.com/hzqst/VmwareHardenedLoader/releases`
- Ensure `bin\VmLoader.sys` exists. A normal build also produces `bin\dyndata.bin` and `bin\dyndata.sig`.
- Use an elevated Command Prompt. The scripts ask for UAC elevation when needed, but an elevated prompt makes errors easier to see.
- Use test signing only in an isolated test VM, never on a production system.

## 1. Test-sign the driver

From the repository root, run:

~~~bat
bin\test_signing.bat
~~~

The script:

1. Finds or creates the `CN=VmLoader Test Signing` code-signing certificate in the local-machine certificate store.
2. Adds the certificate to the `Root` and `TrustedPublisher` trust stores.
3. Signs and verifies `bin\VmLoader.sys` with the Windows SDK `signtool.exe`.

If `signtool.exe` cannot be found, install the Windows 10/11 SDK or run the script from a Visual Studio Developer PowerShell/Command Prompt.

## 2. Enable Windows test-signing mode

Before loading the driver, run the following in an elevated Command Prompt:

~~~bat
bcdedit /set testsigning on
~~~

Windows applies this setting after the restart. Disable it after testing as described at the end of this guide.

## 3. Install and load the driver

After Windows restarts, run this from the repository root:

~~~bat
bin\install.bat
~~~

By default, the script:

- Copies `bin\VmLoader.sys` to `%SystemRoot%\System32\drivers\VmLoader.sys`;
- Creates or updates the `vmloader` kernel service and configures it for system start;
- Sets `HKLM\SYSTEM\CurrentControlSet\Services\vmloader\Parameters\DynDataDirectory` to the `bin` directory;
- Starts the driver service.

Keep `dyndata.bin` and `dyndata.sig` in the same `bin` directory as the scripts. The driver verifies and prefers these external dynamic-data files, then falls back to its embedded data if validation fails.

Check the service state with:

~~~bat
sc query vmloader
~~~

`STATE` equal to `RUNNING` indicates that the service is loaded. After rebuilding or replacing the driver, run `bin\install.bat` again to stop the old service, update the file, and load it again.

## 4. Uninstall the driver

From an elevated Command Prompt, run:

~~~bat
bin\uninstall.bat
~~~

The script stops the driver (waiting up to 15 seconds), deletes the `vmloader` service, and removes `%SystemRoot%\System32\drivers\VmLoader.sys`. It does not remove the test-signing certificate or disable Windows test-signing mode.

After testing, disable test signing and restart:

~~~bat
bcdedit /set testsigning off
~~~

The uninstall script is safe to run repeatedly; if the service and driver file are already absent, it reports that the driver is already uninstalled.
