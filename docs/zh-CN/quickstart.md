# 快速开始

本文介绍如何在测试虚拟机中使用 `bin` 目录下的安装、测试签名和卸载脚本。脚本可以从仓库根目录或任意工作目录运行；下面的命令假设仓库位于 `D:\VmwareHardenedLoader`。

[返回 README](../../README.zh-CN.md) · [English](../en/quickstart.md)

## 前置条件

- 已按[构建指南](build.md)完成构建。
- 确认 `bin\VmLoader.sys` 存在。正常构建还会生成 `bin\dyndata.bin` 和 `bin\dyndata.sig`。
- 使用管理员权限运行命令提示符。虽然脚本会自动申请 UAC 提权，但直接管理员权限的cmd运行更容易看到完整错误信息。
- 测试签名仅适用于隔离的测试虚拟机，不要用于生产系统。

## 1. 测试签名驱动

在仓库根目录执行：

~~~bat
bin\test_signing.bat
~~~

脚本会：

1. 在本机计算机证书存储中查找或创建 `CN=VmLoader Test Signing` 代码签名证书。
2. 将证书加入 `Root` 和 `TrustedPublisher` 信任存储。
3. 使用 Windows SDK 的 `signtool.exe` 签名并验证 `bin\VmLoader.sys`。

如果提示找不到 `signtool.exe`，请安装 Windows 10/11 SDK，并从 Visual Studio Developer PowerShell/Command Prompt 运行脚本。

## 2. 启用 Windows 测试签名模式

驱动加载前，在管理员命令提示符中执行：

~~~bat
bcdedit /set testsigning on
~~~

Windows 重启后才会应用该设置。测试完成后请按本文最后的步骤关闭测试签名模式。

## 3. 安装并加载驱动

Windows 重启后，在仓库根目录执行：

~~~bat
bin\install.bat
~~~

脚本默认会：

- 将 `bin\VmLoader.sys` 复制到 `%SystemRoot%\System32\drivers\VmLoader.sys`；
- 创建或更新名为 `vmloader` 的 kernel service，并配置为系统启动；
- 将 `HKLM\SYSTEM\CurrentControlSet\Services\vmloader\Parameters\DynDataDirectory` 指向 `bin` 目录；
- 启动驱动服务。

保持 `dyndata.bin` 和 `dyndata.sig` 与脚本位于同一个 `bin` 目录。驱动会优先验证并使用这两个外部动态数据文件，验证失败时回退到驱动内置数据。

可用以下命令检查服务状态：

~~~bat
sc query vmloader
~~~

看到 `STATE` 为 `RUNNING` 表示服务已加载。重新构建或替换驱动文件后，再次运行 `bin\install.bat` 即可停止旧服务、更新文件并重新加载。

## 4. 卸载驱动

在管理员命令提示符中执行：

~~~bat
bin\uninstall.bat
~~~

脚本会停止驱动（最多等待 15 秒）、删除 `vmloader` 服务，并删除 `%SystemRoot%\System32\drivers\VmLoader.sys`。它不会删除测试签名证书，也不会关闭 Windows 测试签名模式。

测试完成后关闭测试签名并重启：

~~~bat
bcdedit /set testsigning off
~~~

卸载脚本可以重复运行；如果服务和驱动文件已经不存在，它会报告已卸载。