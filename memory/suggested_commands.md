---
title: suggested_commands
type: note
permalink: VmwareHardenedLoader/suggested-commands
---

# Windows commands

- Initialize dependency submodule from repository root: `git submodule update --init --recursive`.
- Build from a VS 2022 Developer Command Prompt with WDK:
  - Release x64: `msbuild VmLoader.sln /m /t:Rebuild /p:Configuration=Release /p:Platform=x64`
  - Debug x64: `msbuild VmLoader.sln /m /t:Rebuild /p:Configuration=Debug /p:Platform=x64`
  - Release ARM64: `msbuild VmLoader.sln /m /t:Rebuild /p:Configuration=Release /p:Platform=ARM64`
  - Debug ARM64: `msbuild VmLoader.sln /m /t:Rebuild /p:Configuration=Debug /p:Platform=ARM64`
- The x64 and ARM64 MSBuild targets invoke `VmLoader/PrepareDynData.ps1` before C/C++ compilation; do not assume a cached manifest is acceptable. It downloads the latest KPH XML, validates it, builds the generator, signs/verifies generated data, and publishes artifacts.
- Optional external-data configuration (elevated Command Prompt): `reg add "HKLM\\SYSTEM\\CurrentControlSet\\Services\\vmloader\\Parameters" /v DynDataDirectory /t REG_SZ /d "C:\\VmLoader" /f`; directory must contain matching `dyndata.bin` and `dyndata.sig`.
- Test VM boot setting (elevated): `bcdedit /set testsigning on`; reboot, sign `bin\\vmloader.sys` with a trusted test certificate, and disable later with `bcdedit /set testsigning off`.
- Inspect runtime diagnostics with DbgView or a kernel debugger; driver messages use the `VmLoader:` prefix and report NTSTATUS values.
