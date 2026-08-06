# Driver behavior, test signing, and VMware configuration

[Back to README](../../README.md) · [简体中文](../zh-CN/behavior.md)

## Driver behavior

DriverEntry performs these operations in order:

1. Loads and validates KPH dynamic data for the running kernel.
2. Replaces firmware table provider handlers.
3. Registers the PnP registry callback.

Failure at any stage prevents later stages from running. Unload removes PnP hooks first, then firmware hooks.

Firmware filtering:

- FIRM: replaces VMware with System and Virtual with Generic in SMBIOS buffers.
- ACPI: hides WAET, replaces VMware strings, and recomputes the ACPI checksum.
- RSMB: replaces VMware strings in raw SMBIOS data.

PnP filtering applies only to user-mode callers and hides matching VMware PCI/USB device keys under the supported Enum branches.

## Test signing

For test VMs, enable test-signing mode from an elevated Command Prompt:

~~~bat
bcdedit /set testsigning on
~~~

Restart Windows after changing the boot setting. Sign `bin\vmloader.sys` with a trusted test certificate (`install.bat` and `install.ps1` will do it for you) before loading either x64 or ARM64 builds.

Disable test-signing mode after testing with:

~~~bat
bcdedit /set testsigning off
~~~

## VMware configuration

Power off the VM and back up its .vmx file before editing it.

Common settings to minimize VMware-specific indicators include:

~~~ini
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
~~~

~~~ini
scsi0:0.productID = "Some generic product name"
scsi0:0.vendorID = "Some generic vendor name"
~~~

~~~ini
ethernetN.addressType = "static"
ethernetN.address = "AA:BB:CC:DD:EE:FF"  # Avoid "00:05:69" / "00:0c:29" / "00:1C:14" / "00:50:56"
ethernetN.checkMACAddress = "false"
~~~

Do not install VMware Tools in the test guest if the objective is to minimize VMware-specific indicators.
