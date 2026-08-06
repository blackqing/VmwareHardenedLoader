# VMwareHardenedLoader-ng

[简体中文](README.zh-CN.md)

VMwareHardenedLoader-ng is a Windows kernel driver for VMware guest research environments. It filters selected VMware-related firmware strings and blocks selected VMware PnP registry enumeration.

The driver resolves undocumented kernel globals from signed System Informer KPH dynamic data instead of PDBs, signature scanning, or registry-provided RVAs.

## Quick start

Read the [build guide](docs/en/build.md), then build from a Visual Studio 2022 Developer Command Prompt with the WDK installed:

~~~bat
msbuild VmLoader.sln /m /t:Rebuild /p:Configuration=Release /p:Platform=x64
~~~

The build downloads and validates the KPH manifest, generates and signs v20 dynamic data, and publishes the runtime artifacts needed by the install scripts. See [runtime loading](docs/en/runtime.md) before configuring external dynamic data or loading the driver.

## Supported systems

- Windows 10 and Windows 11 x64 or ARM64 guests

The maintained configurations are Release|x64, Debug|x64, Release|ARM64, and Debug|ARM64. ARM64 runtime behavior still depends on undocumented kernel layouts and must be validated on the target Windows build.

## Documentation

- [Build requirements and dynamic data](docs/en/build.md)
- [Runtime loading and external dynamic data](docs/en/runtime.md)
- [Driver behavior, test signing, and VMware configuration](docs/en/behavior.md)
- [Troubleshooting, limitations, and dependencies](docs/en/troubleshooting.md)

## Security and compatibility notes

External dyndata.bin and dyndata.sig files are accepted only after signature verification with the public key compiled into the driver, exact running-kernel identity matching, required-field checks, and PE/RVA validation. The embedded v20 configuration is the fallback.

The driver depends on undocumented Windows kernel globals and firmware-provider structures. Windows updates can change those internals, and the driver filters selected observations only; it does not remove all virtualization indicators.

## License

Released under the MIT License. See [LICENSE](LICENSE).

## Dependencies

[System Informer](https://github.com/hzqst/systeminformer/) is included as a submodule for KPH dynamic data and the CustomBuildTool and CustomSignTool utilities.

[kphtools](https://github.com/HLND2T/kphtools) provides the symbol source and RVAs for nt!ExpFirmwareTableResource and nt!ExpFirmwareTableProviderListHead.
