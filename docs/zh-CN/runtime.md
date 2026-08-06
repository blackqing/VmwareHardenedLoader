# 运行时加载与外部动态数据

[返回 README](../../README.zh-CN.md) · [English](../en/runtime.md)

## 运行时加载

在 DriverEntry 中，驱动首先识别正在运行的 ntoskrnl.exe 或 ntkrla57.exe 映像、动态数据类别、基址和 PE 元数据，然后按以下顺序尝试动态数据：

1. DynDataDirectory 中已签名的外部 dyndata.bin 和 dyndata.sig。
2. 编译进驱动的受信任 v20 KphDynConfig。

只有在使用驱动内置公钥验证 RSA-PSS/SHA-512 签名后，才会使用外部数据。二进制文件限制为 8 MiB，签名限制为 1024 字节。

注册表配置缺失、文件缺失、路径无效、签名失败、格式不兼容、内核不匹配、固件字段缺失或 RVA 无效时，驱动会记录对应的 NTSTATUS，然后尝试内置配置。如果两个数据源都失败，DriverEntry 会在安装任何固件或 PnP hook 前返回失败。

运行时不支持热加载。修改外部数据后必须重启驱动。

## 外部动态数据目录

在驱动服务键下创建可选的 REG_SZ 值：

~~~text
HKLM\SYSTEM\CurrentControlSet\Services\vmloader\Parameters
    DynDataDirectory    REG_SZ    C:\VmLoader
~~~

配置的目录必须包含：

~~~text
dyndata.bin
dyndata.sig
~~~

示例：

~~~bat
reg add "HKLM\SYSTEM\CurrentControlSet\Services\vmloader\Parameters" ^
  /v DynDataDirectory /t REG_SZ /d "C:\VmLoader" /f
~~~

接受的目录形式包括 C:\VmLoader 这样的本地 DOS 绝对路径，以及 \??\C:\VmLoader 或 \Device\HarddiskVolume3\VmLoader 这样的本地 NT 绝对路径。正斜杠会被规范化。

相对路径和网络路径（包括 UNC 及已知的 NT redirector 路径）会被拒绝。不会展开环境变量。

外部文件必须使用生成驱动公钥时对应的同一私钥签名。从其他密钥对的构建中复制 dyndata.bin/.sig 会导致签名拒绝并回退到内置配置。
