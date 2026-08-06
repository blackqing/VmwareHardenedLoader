# 疑难解答、局限性

[返回 README](../../README.zh-CN.md) · [English](../en/troubleshooting.md)

## 疑难解答

- 在编译前检查构建输出，确认已验证的固件记录数量以及 signature is valid。
- 确认 `bin\dyndata.bin` 和 `bin\dyndata.sig` 来自与驱动构建相同的密钥对。
- 确认 `DynDataDirectory` 是 `REG_SZ` 类型的本地绝对目录，而不是文件路径。
- 使用 DbgView 或内核调试器检查 VmLoader: 消息及报告的 NTSTATUS。（目前只有Debug构建的版本会输出DbgPrint）
- Windows 更新后，如果内置配置不包含新的内核身份，请安装新生成的已签名动态数据文件对。

## 局限性

- 驱动依赖未公开的 Windows 内核全局变量和结构。如果微软大规模修改这些未公开的结构，那么本驱动可能立即失效。
- 驱动只过滤选定的固件和 PnP 观察结果，不能移除所有虚拟化特征。
- 虽然支持 ARM64 ，但未在真实的 Windows ARM64 版本上验证。
- 某些运行于内核模式的软件保护工具（俗称“壳”）可能会直接以特征码搜索的方式在内核内存中定位 `vmloader.sys` (比如，搜索字面量字符串： `"VMWARE"` `"VEN_15AD"`) 