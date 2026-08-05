# Completion and verification gate

- For driver/source changes, run the relevant x64 rebuild from a VS 2022 Developer Command Prompt with WDK; prefer Release for final validation and Debug when diagnostics matter.
- A successful preparation phase must show validated firmware-record counts, generated KPH v20 layout, and successful signature verification before compilation proceeds.
- Confirm published artifacts when build succeeds: `bin\\vmloader.sys`, `bin\\dyndata.bin`, and `bin\\dyndata.sig`; the driver remains unsigned by the project and needs test/production signing before loading.
- There is no root automated test suite. For runtime-sensitive changes, inspect `VmLoader:` DbgView/kernel-debugger output and exercise the affected firmware/PnP query path in a disposable test VM.
- If build prerequisites, network/NuGet access, WDK, or a usable Windows kernel test VM are unavailable, report that limitation explicitly; do not claim the change is fully validated.
- Before handoff, review the diff for unrelated generated files/secrets. Keep VmLoader-specific RSA key files under the ignored System Informer Resources directory and never commit the private key.
