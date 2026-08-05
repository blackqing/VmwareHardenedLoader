# VMwareHardenedLoader-ng

VMwareHardenedLoader-ng is a Windows kernel driver for VMware guest research environments. It filters VMware-related firmware strings and blocks selected VMware PnP registry enumeration.

The driver resolves its undocumented kernel globals from signed System Informer KPH dynamic data instead of PDBs, signature scanning, or registry-provided RVAs.

## Supported systems

- Windows 10 and Windows 11 x64 or ARM64 guests

## Build requirements

- Visual Studio 2022
- Windows Driver Kit 10
- .NET SDK 9 or newer for `CustomBuildTool`

The maintained build configurations are `Release|x64`, `Debug|x64`, `Release|ARM64`, and `Debug|ARM64`.

## Dynamic data

The build produces KPH dynamic configuration version 20. It extends the System Informer v19 kernel layout with these `ULONG` RVAs:

- `ExpFirmwareTableResource`
- `ExpFirmwareTableProviderListHead`

Each record is selected by an exact match on:

```text
Class + Machine + TimeDateStamp + SizeOfImage
```

A record that does not contain both firmware fields stores `ULONG_MAX` for the missing value and is treated as unsupported at runtime.

The driver validates both resolved RVAs before use. They must be non-zero, distinct, and located in writable, non-executable sections of the running `ntoskrnl.exe` image.

## Build instruction

Open a Visual Studio 2022 Developer Command Prompt with the WDK installed, then run:

```bat
msbuild VmLoader.sln /m /t:Rebuild /p:Configuration=Release /p:Platform=x64
```

For a debug build, use:

```bat
msbuild VmLoader.sln /m /t:Rebuild /p:Configuration=Debug /p:Platform=x64
```

Use `/p:Platform=ARM64` with either configuration to cross-build the ARM64 driver. The dynamic-data preparation tools still run as x64 host utilities and generate a shared data set containing both AMD64 and ARM64 records.

Before C/C++ compilation, the project automatically performs these steps:

1. Downloads the latest manifest from `https://github.com/HLND2T/kphtools/releases/latest/download/kphdyn.xml` into a temporary file.
2. Parses the XML with DTD processing disabled and verifies that usable firmware records exist.
3. Atomically replaces `thirdparty/systeminformer/kphlib/kphdyn.xml` only after validation succeeds.
4. Builds System Informer's `CustomBuildTool` and generates the v20 embedded source plus `ksidyn.bin`.
5. Signs the binary with RSA-PSS/SHA-512, verifies it again with the matching public key, and publishes it.
6. Generates the driver public-key header under the intermediate build directory.

The build intentionally does not use a cached manifest. A download, XML validation, generation, signing, or verification failure stops the build.

The build requires HTTPS access to GitHub and may require NuGet access when restoring `CustomBuildTool` packages.

### Signing key

On the first successful preparation, if both key files are absent, the build creates a VmLoader-specific 4096-bit RSA key pair here:

```text
thirdparty\systeminformer\tools\CustomSignTool\Resources\kph.key
thirdparty\systeminformer\tools\CustomSignTool\Resources\public.key
```

Both files are ignored by the System Informer submodule. The private key is never copied into the parent repository, intermediate public header, or packaged output.

Back up `kph.key` and `public.key` together. If exactly one file is present, the build fails instead of silently rotating the signing identity. Losing or replacing the key pair requires rebuilding the driver and re-signing all external dynamic data.

### Build outputs

Both x64 and ARM64 builds publish these runtime files under `bin`:

```text
bin\vmloader.sys
bin\dyndata.bin
bin\dyndata.sig
```

The build target's driver, dynamic-data files, and PDB are copied to the same directory as the install scripts. Building another architecture or configuration locally overwrites the previous generated artifacts in `bin`; the CI workflow builds each target in an isolated job and packages it immediately.

`vmloader.sys` remains unsigned. Sign it with a test or production code-signing certificate before loading it.

## Runtime loading

At `DriverEntry`, the driver first identifies the running `ntoskrnl.exe` or `ntkrla57.exe` image, its dynamic-data class, base, and PE metadata. It then tries dynamic data in this order:

1. Signed external `dyndata.bin` and `dyndata.sig` from `DynDataDirectory`.
2. The trusted v20 `KphDynConfig` compiled into the driver.

External data is used only after its RSA-PSS/SHA-512 signature is verified by the public key compiled into the driver. The binary is limited to 8 MiB and the signature to 1024 bytes.

Missing registry configuration, missing files, invalid paths, signature failures, incompatible formats, unmatched kernels, missing firmware fields, or invalid RVAs are logged with an `NTSTATUS`, then the driver attempts the embedded configuration. If both sources fail, `DriverEntry` returns failure before any firmware or PnP hook is installed.

There is no runtime hot reload. Restart the driver to load changed external data.

## External dynamic data directory

Create this optional `REG_SZ` value under the driver service key:

```text
HKLM\SYSTEM\CurrentControlSet\Services\vmloader\Parameters
    DynDataDirectory    REG_SZ    C:\VmLoader
```

The configured directory must contain:

```text
dyndata.bin
dyndata.sig
```

Example:

```bat
reg add "HKLM\SYSTEM\CurrentControlSet\Services\vmloader\Parameters" ^
  /v DynDataDirectory /t REG_SZ /d "C:\VmLoader" /f
```

Accepted directory forms include a local DOS absolute path such as `C:\VmLoader` and a local NT absolute path such as `\??\C:\VmLoader` or `\Device\HarddiskVolume3\VmLoader`. Forward slashes are normalized.

Relative paths and network paths, including UNC and known NT redirector paths, are rejected. Environment variables are not expanded.

The external files must be signed with the same private key used when the driver public key was generated. Copying a `dyndata.bin/.sig` pair from a build with a different key causes signature rejection and embedded fallback.

## Driver behavior

`DriverEntry` performs these operations in order:

1. Loads and validates KPH dynamic data for the running kernel.
2. Replaces firmware table provider handlers.
3. Registers the PnP registry callback.

Failure at any stage prevents later stages from running. Unload removes PnP hooks first, then firmware hooks.

Firmware filtering:

- `FIRM`: replaces `VMware` with `System` and `Virtual` with `Generic` in SMBIOS buffers.
- `ACPI`: hides WAET, replaces VMware strings, and recomputes the ACPI checksum.
- `RSMB`: replaces VMware strings in raw SMBIOS data.

PnP filtering applies only to user-mode callers and hides matching VMware PCI/USB device keys under the supported Enum branches.

## Test signing

For disposable test VMs, enable test-signing mode from an elevated Command Prompt:

```bat
bcdedit /set testsigning on
```

Restart Windows after changing the boot setting. Sign `bin\vmloader.sys` with a trusted test certificate before loading either x64 or ARM64 builds. Disable test-signing mode after testing with:

```bat
bcdedit /set testsigning off
```

## VMware configuration

Power off the VM and back up its `.vmx` file before editing it. Common research settings include:

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
monitor_control.restrict_backdoor = "TRUE"
```

Do not install VMware Tools in the test guest if the objective is to minimize VMware-specific indicators.

## Troubleshooting

- Check build output for the validated firmware-record counts and `signature is valid` before compilation.
- Confirm that `bin\dyndata.bin` and `bin\dyndata.sig` came from the same key pair as the driver build.
- Check `DynDataDirectory` is a `REG_SZ` local absolute directory, not a file path.
- Use DbgView or a kernel debugger to inspect `VmLoader:` messages and the reported `NTSTATUS`.
- After a Windows update, install a newly generated signed dynamic-data pair if the embedded configuration does not contain the new kernel identity.

## Limitations

- The driver depends on undocumented Windows kernel globals and firmware provider structures.
- A Windows update can change those internals even when both RVAs are available.
- The driver filters selected firmware and PnP observations only; it does not remove all virtualization indicators.
- x64 and ARM64 Release and Debug builds are supported; ARM64 runtime behavior still depends on undocumented kernel layouts and must be validated on the target Windows build.

## Dependencies

[System Informer](https://github.com/hzqst/systeminformer/) as submodule for KPH dynamic data and the `CustomBuildTool` and `CustomSignTool` utilities.

[kphtools](https://github.com/HLND2T/kphtools) as symbol source that provides RVA for `nt!ExpFirmwareTableResource` && `nt!ExpFirmwareTableProviderListHead`.

## License

Released under the MIT License. See [LICENSE](LICENSE).
