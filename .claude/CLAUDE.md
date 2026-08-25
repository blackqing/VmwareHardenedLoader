# CLAUDE.md

This file provides guidance to coding agents working in this repository.

## Basic Memory knowledge base (progressive disclosure)

1. Use `search_notes` first to check existing notes in `memory/` (do not read all by default).
2. Read `core` first. It is the graph root for the project purpose, source map, driver lifecycle, dynamic-data trust model, and runtime constraints.
3. Follow only the references needed for the current task:
   - `tech_stack` — toolchain, dependency submodule, generated artifacts, and supported configurations.
   - `suggested_commands` — build, preparation, registry, test-signing, and diagnostic commands; read before executing project commands.
   - `conventions` — kernel coding conventions, hook lifecycle invariants, input validation, and safety constraints; read before changing code.
   - `task_completion` — build/runtime verification and handoff requirements; read before declaring implementation work complete.
   - `memory_maintenance` — memory graph structure, retention criteria, and maintenance actions; read when creating or updating notes.
4. If notes are missing, stale, or insufficient, inspect the relevant source files directly. Persist only stable, non-obvious project knowledge, and keep note references accurate with `write_note`, `edit_note`, or `delete_note`.

## Repository context (prefer notes)

The high-level architecture and invariants are maintained in `core`; this file intentionally keeps only the navigation points needed for discovery:

- Project purpose, source map, driver lifecycle, and dynamic-data security model: `core`.
- Toolchain, System Informer dependency, and generated outputs: `tech_stack`.
- Build and runtime commands: `suggested_commands`.
- Code and safety rules for kernel hooks and symbol handling: `conventions`.
- Completion verification and handoff gate: `task_completion`.

## Source-file entry points when notes are insufficient

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
- Treat kernel symbol data, PE metadata, signatures, file paths, and resolved RVAs as untrusted input; preserve the validation rules documented in `core` and `conventions`.
- The maintained targets are x64 Debug and Release. ARM64 configurations exist but are not validated for output.
- Test-signing mode is required for local driver loading; never commit private signing keys or generated secrets.
- Use the verification gate in `task_completion` for implementation work. If required build, network, WDK, or disposable VM prerequisites are unavailable, report that limitation explicitly rather than claiming full validation.

## Progressive disclosure key points

- Read Basic Memory notes first, then locate single files/symbols; avoid reading the whole repository at once.
- For symbol/binary-related directories, prioritize on-demand targeted lookup and avoid full scans.

## Explore SKILLs

- project-level SKILLs should be explored from `.claude/skills` even when we are using Codex.
