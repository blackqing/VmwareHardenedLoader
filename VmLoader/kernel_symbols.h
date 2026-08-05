#pragma once

#include <ntddk.h>

struct VmLoaderKernelSymbols {
	PVOID FirmwareTableResource;
	PVOID FirmwareTableProviderListHead;
};

extern "C" NTSTATUS VmLoaderLoadKernelSymbols(
	_In_ PUNICODE_STRING RegistryPath,
	_Out_ VmLoaderKernelSymbols* Symbols);
