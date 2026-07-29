# VMwareHardenedLoader-ng

VMwareHardenedLoader-ng is a Windows kernel driver and user-mode symbol resolver for VMware guest research environments. The driver filters VMware-related strings from firmware table query results.

This version replaces kernel signature scanning and Capstone disassembly with exact PDB symbol resolution.

## Supported systems

- Windows 10 and Windows 11 x64 guests
- Visual Studio 2022
- Windows Driver Kit 10

Older Windows versions are not tested by the current build configuration.

## How it works

`vmloader_resolver.exe` performs these steps before the driver starts:

1. Finds the running `ntoskrnl.exe` image.
2. Reads its RSDS PDB identity.
3. Downloads the matching PDB from the Microsoft symbol server.
4. Resolves `ExpFirmwareTableResource` and `ExpFirmwareTableProviderListHead`.
5. Confirms that both symbols point to writable, non-executable kernel data.
6. Writes their RVAs and kernel identity to the driver service registry key.

The driver reads this configuration from:

```text
HKLM\SYSTEM\CurrentControlSet\Services\vmloader\Parameters
```

Before it uses an RVA, the driver checks the schema version, PE timestamp, image size, checksum, and section permissions against the running kernel. It rejects stale or invalid configuration.

## Project structure

| Path | Purpose |
| --- | --- |
| `SymbolResolver/main.cpp` | Downloads the matching PDB, resolves symbols, and writes the registry configuration. |
| `VmLoader/kernel_symbols.cpp` | Reads and validates symbol RVAs in kernel mode. |
| `VmLoader/firmware_hook.cpp` | Installs firmware provider hooks and filters returned strings. |
| `VmLoader/driver.cpp` | Manages driver startup and unload. |
| `shared/symbol_config.h` | Defines the registry contract shared by both programs. |

## Build

Open a Visual Studio 2022 Developer Command Prompt with the WDK installed, then run:

```bat
msbuild VmLoader.sln /m /t:Rebuild /p:Configuration=Release /p:Platform=x64
```

The build copies these files to `bin`:

```text
bin\vmloader.sys
bin\vmloader_resolver.exe
```

The build leaves `vmloader.sys` unsigned. Sign it with a test or production certificate before loading it.

### Test certificate

Create and export a test certificate on the host in an elevated PowerShell:

```powershell
$cert = New-SelfSignedCertificate `
  -Type CodeSigningCert `
  -Subject "CN=VmLoader Test" `
  -CertStoreLocation "Cert:\LocalMachine\My"

Export-Certificate -Cert $cert -FilePath C:\VmLoaderTest.cer
```

Sign the driver on the host. Replace the path if the WDK is installed elsewhere:

```powershell
$signtool = Get-ChildItem "C:\Program Files (x86)\Windows Kits\10\bin\*\x64\signtool.exe" |
  Sort-Object FullName -Descending |
  Select-Object -First 1 -ExpandProperty FullName
$driverPath = (Resolve-Path ".\bin\vmloader.sys").Path

& $signtool sign /v /fd SHA256 /ph /s My /sm /n "VmLoader Test" `
  $driverPath
```

Copy the signed `vmloader.sys`, `vmloader_resolver.exe`, `install.bat`, and `C:\VmLoaderTest.cer` to the guest.

On the guest, open an elevated Command Prompt and install the certificate before starting the driver:

```bat
certutil -addstore -f Root C:\VmLoaderTest.cer
certutil -addstore -f TrustedPublisher C:\VmLoaderTest.cer
```

For test signing in a disposable VM, open an elevated Command Prompt and run:

```bat
bcdedit /set testsigning on
```

Restart Windows after changing the boot setting. Test mode must be enabled before Windows loads the unsigned driver. Disable it after testing with:

```bat
bcdedit /set testsigning off
```

## Resolver checks

Run the local metadata check without downloading a PDB or changing the registry:

```bat
bin\vmloader_resolver.exe --self-test
```

Download the matching PDB and resolve all required symbols without changing the registry:

```bat
bin\vmloader_resolver.exe --dry-run
```

Run the resolver without an option to write the validated configuration. The `vmloader` service key must already exist.

```bat
bin\vmloader_resolver.exe
```

The first symbol resolution requires HTTPS access to:

```text
https://msdl.microsoft.com/download/symbols
```

Downloaded PDB files are cached under `%ProgramData%\VmLoader\Symbols`.

## VMware configuration

Power off the VM and back up its `.vmx` file before editing it. Add these settings to the VMX file:

```ini
hypervisor.cpuid.v0 = "FALSE"
board-id.reflectHost = "TRUE"
hw.model.reflectHost = "TRUE"
serialNumber.reflectHost = "TRUE"
smbios.reflectHost = "TRUE"
SMBIOS.noOEMStrings = "TRUE"
isolation.tools.getPtrLocation.disable = "TRUE"
isolation.tools.setPtrLocation.disable = "TRUE"
isolation.tools.setVersion.disable = "TRUE"
isolation.tools.getVersion.disable = "TRUE"
monitor_control.disable_directexec = "TRUE"
monitor_control.disable_chksimd = "TRUE"
monitor_control.disable_ntreloc = "TRUE"
monitor_control.disable_selfmod = "TRUE"
monitor_control.disable_reloc = "TRUE"
monitor_control.disable_btinout = "TRUE"
monitor_control.disable_btmemspace = "TRUE"
monitor_control.disable_btpriv = "TRUE"
monitor_control.disable_btseg = "TRUE"
monitor_control.restrict_backdoor = "TRUE"
```

If the system disk uses the first SCSI slot, also change its reported identity:

```ini
scsi0:0.productID = "Generic SSD"
scsi0:0.vendorID = "Generic"
```

Set a non-VMware MAC address in the VMX file. For example:

```ini
ethernet0.address = "00:11:56:20:D2:E8"
```

Do not install VMware Tools in the test guest. They can restore VMware-specific indicators.

## Install

Warning: `install.bat` modifies the driver service, deletes `HKLM\HARDWARE\ACPI\DSDT\PTLTD_`, and forces a restart after installation. Use it only in a test VM with a recoverable snapshot.

1. Build both projects as `Release|x64`.
2. Sign `bin\vmloader.sys` and export the test certificate.
3. Copy the signed driver, resolver, installer, and certificate to the guest.
4. Import the certificate into `Root` and `TrustedPublisher` on the guest.
5. Enable test signing and restart the guest.
6. Run `bin\install.bat` as administrator in the guest.

The installer stops an existing `vmloader` service, copies the driver to `C:\vmloader.sys`, creates or updates the service, resolves the current kernel symbols, and starts the driver. It does not start the driver when symbol resolution fails.

## Uninstall

Run this command as administrator:

```bat
bin\uninstall.bat
```

The script stops and deletes the service, removes the symbol registry configuration, and deletes `C:\vmloader.sys`.

## After a Windows update

A Windows update can replace `ntoskrnl.exe`. The driver rejects registry values for the previous kernel build. Run `vmloader_resolver.exe` again before starting the driver, or rerun `install.bat` after signing the current driver build.

## Troubleshooting

- Run `vmloader_resolver.exe --self-test` to check kernel image and PDB metadata parsing.
- Run `vmloader_resolver.exe --dry-run` to test network access and symbol availability.
- Check that the Microsoft symbol server is reachable through the guest network and proxy.
- Use DbgView to read `VmLoader:` kernel messages when the service fails to start.
- Confirm that Windows accepts the driver signature or that test-signing mode is enabled.

## Limitations

- The driver depends on private Windows symbols and an undocumented firmware provider structure.
- A future Windows build can change the private structure even when both symbols resolve.
- The driver filters firmware table results only. It does not change VMware graphics, device, network, or guest-tool indicators.
- Only x64 builds are supported.

## License

Released under the MIT License. See [LICENSE](LICENSE).

Some utility concepts in the original project came from [HyperPlatform](https://github.com/tandasat/HyperPlatform). The repository also contains the historical [Capstone](https://github.com/capstone-engine/capstone) source tree.
