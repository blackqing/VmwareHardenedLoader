#pragma once

#include <ntddk.h>

NTSTATUS VmLoaderInstallFirmwareHooks(
	_In_ PVOID FirmwareTableResource,
	_In_ PVOID FirmwareTableProviderListHead);

VOID VmLoaderRemoveFirmwareHooks();
