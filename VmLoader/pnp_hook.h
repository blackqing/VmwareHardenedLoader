#pragma once

#include <ntddk.h>

NTSTATUS VmLoaderInstallPnpHooks(_In_ PDRIVER_OBJECT DriverObject);

VOID VmLoaderRemovePnpHooks();
