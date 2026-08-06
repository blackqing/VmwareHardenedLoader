# VMwareHardenedLoader-ng

[English README](README.md)

VMwareHardenedLoader-ng 是用于 VMware 客户机研究环境的 Windows 内核驱动。它会过滤选定的 VMware 固件字符串，并阻止选定的 VMware PnP 注册表枚举。

驱动使用已签名的 System Informer KPH 动态数据解析未公开的内核全局变量，不依赖 PDB、特征码扫描或注册表提供的 RVA。

## 快速开始

先阅读[构建指南](docs/zh-CN/build.md)，然后在安装 WDK 的 Visual Studio 2022 Developer Command Prompt 中构建：

~~~bat
msbuild VmLoader.sln /m /t:Rebuild /p:Configuration=Release /p:Platform=x64
~~~

构建过程会下载并验证 KPH manifest，生成并签名 v20 动态数据，并发布安装脚本所需的运行时工件。配置外部动态数据或加载驱动前，请阅读[运行时加载说明](docs/zh-CN/runtime.md)。

## 支持的系统

- Windows 10 和 Windows 11 x64 或 ARM64 客户机

维护的配置包括 Release|x64、Debug|x64、Release|ARM64 和 Debug|ARM64。ARM64 运行时行为仍依赖未公开的内核布局，必须在目标 Windows 版本上验证。

## 文档

- [构建要求与动态数据](docs/zh-CN/build.md)
- [运行时加载与外部动态数据](docs/zh-CN/runtime.md)
- [驱动行为、测试签名与 VMware 配置](docs/zh-CN/behavior.md)
- [故障排除、限制与依赖](docs/zh-CN/troubleshooting.md)

## 许可证

本项目采用 MIT License。详见 [LICENSE](LICENSE)。

## 依赖

[System Informer](https://github.com/hzqst/systeminformer/) 以子模块形式包含，用于提供 KPH 动态数据以及 CustomBuildTool、CustomSignTool 工具。

[kphtools](https://github.com/HLND2T/kphtools) 提供符号源，以及 `nt!ExpFirmwareTableResource` 和 `nt!ExpFirmwareTableProviderListHead` 的 RVA。
