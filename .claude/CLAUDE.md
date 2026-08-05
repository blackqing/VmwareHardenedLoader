# CLAUDE.md

This file provides guidance to AI tools when working with code in this repository.

## Build

Open a Visual Studio 2022 Developer Command Prompt with the WDK installed, then:

```bat
msbuild VmLoader.sln /m /t:Rebuild /p:Configuration=Release /p:Platform=x64
```

The post-build step copies `vmloader.sys` to `bin\`.

Only `Release|x64` has a post-build event and include path configuration in the vcxproj. Other platform/config combos exist in the solution but are not actively maintained for output.

## Architecture

This is a Windows kernel driver (WDM) that hides VMware presence from guest OS queries.

### Registration contract (`shared/symbol_config.h`)

Defines the registry key names and schema version shared between the driver and external tools. Symbols are written as DWORD RVAs under `HKLM\SYSTEM\CurrentControlSet\Services\vmloader\Parameters`. The driver validates a schema version, PE timestamp, image size, and checksum before trusting any RVA.

### Driver entry (`driver.cpp`)

`DriverEntry` does three things in order, each on failure preventing later steps:
1. `VmLoaderLoadKernelSymbols` — reads and validates symbol RVAs from registry
2. `VmLoaderInstallFirmwareHooks` — replaces firmware table handler function pointers
3. `VmLoaderInstallPnpHooks` — registers a `CmRegisterCallbackEx` at altitude 389999

`DriverUnload` tears down in reverse: PnP hooks, then firmware hooks.

### Kernel symbol validation (`kernel_symbols.cpp`)

Reads six DWORD values from the `Parameters` subkey, validates schema version, then resolves `ntoskrnl.exe` base via `MmGetSystemRoutineAddress("NtOpenFile")` → `RtlPcToFileHeader`. Confirms the running kernel's PE timestamp/image size/checksum match the registry values. Finally validates each RVA falls within a writable, non-executable section (`RvaInWritableDataSection`). Returns virtual addresses (`ntos_base + rva`) for `ExpFirmwareTableResource` and `ExpFirmwareTableProviderListHead`.

### Firmware hooking (`firmware_hook.cpp`)

Walks the `ExpFirmwareTableProviderListHead` linked list under `ExpFirmwareTableResource` ERESOURCE lock. Replaces three provider handlers:

- **'FIRM'** → `FilterFirm`: Replaces "VMware"→"System" and "Virtual"→"Generic" in raw SMBIOS table buffers.
- **'ACPI'** → `FilterAcpi`: Hides WAET table entirely (returns `STATUS_NOT_FOUND` for single-query, removes from enumeration), string-replaces "VMware"/"VMWARE"→"System"/"SYSTEM", and recomputes the ACPI checksum.
- **'RSMB'** → `FilterRsmb`: Same string replacement on the raw SMBIOS table.

String replacement (`ReplaceInPlace`) scans byte-by-byte with `RtlCompareMemory`, replaces in-place, and handles overlapping matches by advancing past each replacement.

### PnP device hiding (`pnp_hook.cpp`)

A registry callback (`CmRegisterCallbackEx`) that blocks user-mode enumeration of VMware hardware:

- **Post-enumerate**: After a successful `RegNtPostEnumerateKey` on `\Enum\PCI`, `\Enum\USB`, or `\Enum\HDAUDIO` branches, if the returned key name contains `VEN_15AD` (VMware PCI vendor) or `VID_0E0F` (VMware USB vendor), advances the enumeration index past it via `AdvancePastHidden` — re-enumerating with `ZwEnumerateKey` until a non-VMware key is found.
- **Pre-open**: Returns `STATUS_OBJECT_NAME_NOT_FOUND` for any `RegNtPreOpenKey`/`RegNtPreOpenKeyEx` whose complete path targets a VMware subkey under those same enum branches.
- Only filters user-mode callers (`ExGetPreviousMode() != UserMode` is a no-op).

## Key constraints

- The driver depends on undocumented Windows kernel internals (`ExpFirmwareTableResource`, `ExpFirmwareTableProviderListHead`, the `SYSTEM_FIRMWARE_TABLE_HANDLER` struct layout). These can change across Windows builds.
- The solution file references a removed SymbolResolver project (GUID `{0F53CB6D-859E-4B19-9499-35FC90F5C693}`) — its config sections still exist but the project file was deleted in the most recent commit.
- Only x64 Release builds are tested and supported. ARM/ARM64/Debug configs exist in the vcxproj but are not validated.
- The driver requires test-signing mode (`bcdedit /set testsigning on`).
