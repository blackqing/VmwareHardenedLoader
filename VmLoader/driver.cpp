#include <ntddk.h>

#include "firmware_hook.h"
#include "kernel_symbols.h"

namespace {

VOID DriverUnload(_In_ PDRIVER_OBJECT DriverObject) {
	UNREFERENCED_PARAMETER(DriverObject);
	PAGED_CODE();
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

	DriverObject->DriverUnload = DriverUnload;
	return STATUS_SUCCESS;
}
