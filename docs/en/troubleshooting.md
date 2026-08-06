# Troubleshooting, limitations, and dependencies

[Back to README](../../README.md) · [简体中文](../zh-CN/troubleshooting.md)

## Troubleshooting

- Check build output for the validated firmware-record counts and signature is valid before compilation.
- Confirm that bin\dyndata.bin and bin\dyndata.sig came from the same key pair as the driver build.
- Check DynDataDirectory is a REG_SZ local absolute directory, not a file path.
- Use DbgView or a kernel debugger to inspect VmLoader: messages and the reported NTSTATUS.
- After a Windows update, install a newly generated signed dynamic-data pair if the embedded configuration does not contain the new kernel identity.

## Limitations

- The driver depends on undocumented Windows kernel globals and firmware provider structures.
- A Windows update can change those internals even when both RVAs are available.
- The driver filters selected firmware and PnP observations only; it does not remove all virtualization indicators.
- x64 and ARM64 Release and Debug builds are supported; ARM64 runtime behavior still depends on undocumented kernel layouts and must be validated on the target Windows build.
