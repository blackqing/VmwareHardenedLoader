# CLAUDE.md

This file provides guidance to coding agents working in this repository.

## Serena memories (progressive disclosure)

1. Activate this repository in Serena at agent startup, then use `list_memories` to discover the available memories by name. Do not load every memory by default.
2. Read `mem:core` first. It is the graph root for the project purpose, source map, driver lifecycle, dynamic-data trust model, and runtime constraints.
3. Follow only the references needed for the current task:
   - `mem:tech_stack` — toolchain, dependency submodule, generated artifacts, and supported configurations.
   - `mem:suggested_commands` — build, preparation, registry, test-signing, and diagnostic commands; read before executing project commands.
   - `mem:conventions` — kernel coding conventions, hook lifecycle invariants, input validation, and safety constraints; read before changing code.
   - `mem:task_completion` — build/runtime verification and handoff requirements; read before declaring implementation work complete.
   - `mem:memory_maintenance` — memory graph structure, retention criteria, and maintenance actions; read when creating or updating memories.
4. If memories are missing, stale, or insufficient, inspect the relevant source files directly. Persist only stable, non-obvious project knowledge, and keep memory references accurate with Serena's `write_memory`, `edit_memory`, or `delete_memory` operations.

## Repository context (prefer memories)

The high-level architecture and invariants are maintained in `mem:core`; this file intentionally keeps only the navigation points needed for discovery:

- Project purpose, source map, driver lifecycle, and dynamic-data security model: `mem:core`.
- Toolchain, System Informer dependency, and generated outputs: `mem:tech_stack`.
- Build and runtime commands: `mem:suggested_commands`.
- Code and safety rules for kernel hooks and symbol handling: `mem:conventions`.
- Completion verification and handoff gate: `mem:task_completion`.

## Source-file entry points when memories are insufficient

- `README.md` — user-facing overview, build prerequisites, dynamic-data preparation, runtime loading, and limitations.
- `VmLoader/driver.cpp` — `DriverEntry` orchestration and reverse-order unload.
- `VmLoader/kernel_symbols.cpp/.h` — running-kernel discovery, signed/embedded KPH dynamic-data lookup, and PE/RVA validation.
- `VmLoader/firmware_hook.cpp/.h` — firmware provider handler replacement for FIRM, ACPI, and RSMB.
- `VmLoader/pnp_hook.cpp/.h` — user-mode registry callback for VMware PCI, USB, and HDAUDIO enumeration.
- `shared/symbol_config.h` — service parameter names shared by the driver and external tools.
- `VmLoader/PrepareDynData.ps1` — KPH manifest validation, dynamic-data generation/signing/verification, and artifact publication.
- `VmLoader/VmLoader.vcxproj` and `VmLoader.sln` — maintained build entry points.
- `thirdparty/systeminformer/` — System Informer git submodule providing KPH libraries and build/signing tools; avoid editing vendor or generated files unless the task explicitly requires it.

## Important rules

- Treat undocumented Windows kernel globals and firmware-provider layouts as build- and version-sensitive. Keep changes local and preserve rollback behavior.
- Treat kernel symbol data, PE metadata, signatures, file paths, and resolved RVAs as untrusted input; preserve the validation rules documented in `mem:core` and `mem:conventions`.
- The maintained targets are x64 Debug and Release. ARM64 configurations exist but are not validated for output.
- Test-signing mode is required for local driver loading; never commit private signing keys or generated secrets.
- Use the verification gate in `mem:task_completion` for implementation work. If required build, network, WDK, or disposable VM prerequisites are unavailable, report that limitation explicitly rather than claiming full validation.
