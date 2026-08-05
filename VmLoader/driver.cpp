#include <ntddk.h>

#include "firmware_hook.h"
#include "kernel_symbols.h"
#include "pnp_hook.h"
#include "registry_cleanup.h"

namespace {

VOID DriverUnload(_In_ PDRIVER_OBJECT DriverObject) {
	UNREFERENCED_PARAMETER(DriverObject);
	PAGED_CODE();
	VmLoaderRemovePnpHooks();
	VmLoaderRemoveFirmwareHooks();
}

} // namespace

extern "C" NTSTATUS DriverEntry(
	_In_ PDRIVER_OBJECT DriverObject,
	_In_ PUNICODE_STRING RegistryPath) {
	PAGED_CODE();

	VmLoaderKernelSymbols symbols = {};
	NTSTATUS status = VmLoaderLoadKernelSymbols(RegistryPath, &symbols);
	if (!NT_SUCCESS(status)) {
		DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
			"VmLoader: symbol configuration rejected: 0x%08X\n", status);
		return status;
	}

	status = VmLoaderInstallFirmwareHooks(
		symbols.FirmwareTableResource, symbols.FirmwareTableProviderListHead);
	if (!NT_SUCCESS(status)) {
		DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
			"VmLoader: firmware hook installation failed: 0x%08X\n", status);
		return status;
	}

	status = VmLoaderInstallPnpHooks(DriverObject);
	if (!NT_SUCCESS(status)) {
		DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
			"VmLoader: pnp hook installation failed: 0x%08X\n", status);
		VmLoaderRemoveFirmwareHooks();
		return status;
	}

	status = VmLoaderCleanupRegistryEntries();
	if (!NT_SUCCESS(status)) {
		DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
			"VmLoader: reg entries cleanup failed: 0x%08X\n", status);
		VmLoaderRemoveFirmwareHooks();
		VmLoaderRemovePnpHooks();
		return status;
	}

	DriverObject->DriverUnload = DriverUnload;
	return STATUS_SUCCESS;
}
