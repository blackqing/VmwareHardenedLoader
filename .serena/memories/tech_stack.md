# Toolchain and dependencies

- Language/runtime: C++ WDM kernel driver; Windows 10+ guest target, documented support focused on Windows 10/11 x64.
- IDE/build: Visual Studio 2022 with Windows Driver Kit 10; MSBuild solution `VmLoader.sln`; maintained configurations `Debug|x64` and `Release|x64`.
- Dynamic-data preparation: Windows PowerShell script plus .NET SDK 9+ `dotnet msbuild` for System Informer's `CustomBuildTool`; HTTPS access to GitHub and possible NuGet restore are required.
- Third-party dependency: `thirdparty/systeminformer` git submodule from `https://github.com/hzqst/systeminformer/`; its kphlib provides KPH dynamic-data headers/sources and signing tools.
- Generated/runtime artifacts: intermediate public-key header; `bin/vmloader.sys`, `bin/dyndata.bin`, and `bin/dyndata.sig`.
- No test framework or automated unit-test target is present in the root solution; verification is build plus targeted runtime/kernel-debug checks.
