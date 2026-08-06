# 故障排除、限制与依赖

[返回 README](../../README.zh-CN.md) · [English](../en/troubleshooting.md)

## 故障排除

- 在编译前检查构建输出，确认已验证的固件记录数量以及 signature is valid。
- 确认 bin\dyndata.bin 和 bin\dyndata.sig 来自与驱动构建相同的密钥对。
- 确认 DynDataDirectory 是 REG_SZ 类型的本地绝对目录，而不是文件路径。
- 使用 DbgView 或内核调试器检查 VmLoader: 消息及报告的 NTSTATUS。
- Windows 更新后，如果内置配置不包含新的内核身份，请安装新生成的已签名动态数据文件对。

## 限制

- 驱动依赖未公开的 Windows 内核全局变量和固件提供程序结构。
- 即使两个 RVA 都可用，Windows 更新仍可能改变这些内部实现。
- 驱动只过滤选定的固件和 PnP 观察结果，不能移除所有虚拟化特征。
- 支持 x64 和 ARM64 的 Release、Debug 构建；ARM64 运行时行为仍依赖未公开的内核布局，必须在目标 Windows 版本上验证。
