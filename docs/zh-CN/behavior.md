# 驱动行为、测试签名与 VMware 配置

[返回 README](../../README.zh-CN.md) · [English](../en/behavior.md)

## 驱动行为

DriverEntry 按以下顺序执行：

1. 加载并验证当前内核的 KPH 动态数据。
2. 替换固件表提供程序处理函数。
3. 注册 PnP 注册表回调。

任一步骤失败都会阻止后续步骤运行。卸载时先移除 PnP hook，再移除固件 hook。

固件过滤：

- FIRM：在 SMBIOS 缓冲区中将 VMware 替换为 System，将 Virtual 替换为 Generic。
- ACPI：隐藏 WAET，替换 VMware 字符串，并重新计算 ACPI 校验和。
- RSMB：在原始 SMBIOS 数据中替换 VMware 字符串。

PnP 过滤只适用于用户态调用者，并会在受支持的 Enum 分支下隐藏匹配的 VMware PCI/USB 设备键。

## 测试签名

对于虚拟机测试环境，请在提升权限的命令提示符中启用测试签名模式：

~~~bat
bcdedit /set testsigning on
~~~

修改启动设置后重启 Windows。加载 x64 或 ARM64 构建前，使用受信任的测试证书为 `bin\vmloader.sys` 签名 (`install.bat` 和 `install.ps1` 会自动替你签名)。

测试结束后使用以下命令关闭测试签名：

~~~bat
bcdedit /set testsigning off
~~~

## VMware 配置

编辑前请关闭虚拟机并备份 .vmx 文件。常见的研究配置包括：

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

如果目标是尽量减少 VMware 特征，请不要在测试客户机中安装 VMware Tools。
