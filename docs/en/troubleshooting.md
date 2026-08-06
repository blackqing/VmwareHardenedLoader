# Troubleshooting, limitations, and dependencies

[Back to README](../../README.md) · [简体中文](../zh-CN/troubleshooting.md)

## Troubleshooting

- Check build output for the validated firmware-record counts and signature is valid before compilation.
- Confirm that `bin\dyndata.bin` and `bin\dyndata.sig` came from the same key pair as the driver build.
- Check `DynDataDirectory` is a `REG_SZ` local absolute directory, not a file path.
- Use DbgView or a kernel debugger to inspect VmLoader: messages and the reported NTSTATUS. (DbgPrint is available only with Debug build)
- After a Windows update, get a newly generated signed dynamic-data pair if the embedded configuration does not contain the new kernel identity.

## Limitations

- The driver depends on undocumented Windows kernel globals and firmware provider structures. Failure comes when Microsoft pushes massive updates to the related undocumented structures.
- The driver filters selected firmware and PnP observations only; it does not remove all virtualization indicators.
- ARM64 build has not been validated on the real world Windows-ARM64.
- Some kernel-mode software protection utilities may still find `vmloader.sys` by searching patterns (i.e. literal string `"VMWARE"` `"VEN_15AD"`) in kernel memory.