# Runtime loading and external dynamic data

[Back to README](../../README.md) · [简体中文](../zh-CN/runtime.md)

## Runtime loading

At DriverEntry, the driver first identifies the running ntoskrnl.exe or ntkrla57.exe image, its dynamic-data class, base, and PE metadata. It then tries dynamic data in this order:

1. Signed external `dyndata.bin` and `dyndata.sig` from `DynDataDirectory`.
2. The trusted v20 `KphDynConfig` compiled into the driver.

External data is used only after its RSA-PSS/SHA-512 signature is verified by the public key compiled into the driver. The binary is limited to 8 MiB and the signature to 1024 bytes.

Missing registry configuration, missing files, invalid paths, signature failures, incompatible formats, unmatched kernels, missing firmware fields, or invalid RVAs are logged with an NTSTATUS, then the driver attempts the embedded configuration. If both sources fail, DriverEntry returns failure before any firmware or PnP hook is installed.

There is no runtime hot reload. Restart the driver to load changed external data.

## External dynamic data directory

Create this optional REG_SZ value under the driver service key:

~~~text
HKLM\SYSTEM\CurrentControlSet\Services\vmloader\Parameters
    DynDataDirectory    REG_SZ    C:\VmLoader
~~~

The configured directory must contain:

~~~text
dyndata.bin
dyndata.sig
~~~

Example:

~~~bat
reg add "HKLM\SYSTEM\CurrentControlSet\Services\vmloader\Parameters" ^
  /v DynDataDirectory /t REG_SZ /d "C:\VmLoader" /f
~~~

Accepted directory forms include a local DOS absolute path such as `C:\VmLoader` and a local NT absolute path such as `\??\C:\VmLoader` or `\Device\HarddiskVolume3\VmLoader`. Forward slashes are normalized.

Relative paths and network paths, including UNC and known NT redirector paths, are rejected. Environment variables are not expanded.

The external files must be signed with the same private key used when the driver public key was generated. Copying a `dyndata.bin/.sig` pair from a build with a different key causes signature rejection and embedded fallback.
