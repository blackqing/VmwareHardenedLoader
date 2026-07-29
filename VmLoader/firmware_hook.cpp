#include "firmware_hook.h"

namespace {

using FirmwareTableHandler = NTSTATUS(__cdecl*)(PSYSTEM_FIRMWARE_TABLE_INFORMATION);

struct SYSTEM_FIRMWARE_TABLE_HANDLER_NODE {
	SYSTEM_FIRMWARE_TABLE_HANDLER SystemFWHandler;
	LIST_ENTRY FirmwareTableProviderList;
};
using PSYSTEM_FIRMWARE_TABLE_HANDLER_NODE = SYSTEM_FIRMWARE_TABLE_HANDLER_NODE*;

FirmwareTableHandler g_original_firm_handler = nullptr;
FirmwareTableHandler g_original_acpi_handler = nullptr;
FirmwareTableHandler g_original_rsmb_handler = nullptr;
PERESOURCE g_firmware_table_resource = nullptr;
PLIST_ENTRY g_firmware_table_provider_list_head = nullptr;
BOOLEAN g_hooks_installed = FALSE;

void RemoveSignatures(
	_Inout_ PVOID Buffer,
	_In_ ULONG BufferLength,
	_In_reads_bytes_(SignatureLength) const char* Signature,
	_In_ SIZE_T SignatureLength) {
	if (!Buffer || !Signature || SignatureLength == 0) {
		return;
	}
	PUCHAR cursor = static_cast<PUCHAR>(Buffer);
	SIZE_T remaining = BufferLength;
	while (remaining >= SignatureLength) {
		PUCHAR match = nullptr;
		for (SIZE_T i = 0; i <= remaining - SignatureLength; ++i) {
			if (RtlCompareMemory(cursor + i, Signature, SignatureLength) == SignatureLength) {
				match = cursor + i;
				break;
			}
		}
		if (!match) {
			return;
		}
		RtlFillMemory(match, SignatureLength, '7');
		remaining -= static_cast<SIZE_T>(match + SignatureLength - cursor);
		cursor = match + SignatureLength;
	}
}

NTSTATUS __cdecl FilterFirm(PSYSTEM_FIRMWARE_TABLE_INFORMATION info) {
	const NTSTATUS status = g_original_firm_handler(info);
	if (NT_SUCCESS(status) && info && info->Action == 1) {
		RemoveSignatures(info->TableBuffer, info->TableBufferLength, "VMware", 6);
		RemoveSignatures(info->TableBuffer, info->TableBufferLength, "Virtual", 7);
	}
	return status;
}

NTSTATUS __cdecl FilterAcpi(PSYSTEM_FIRMWARE_TABLE_INFORMATION info) {
	const NTSTATUS status = g_original_acpi_handler(info);
	if (NT_SUCCESS(status) && info && info->Action == 1) {
		RemoveSignatures(info->TableBuffer, info->TableBufferLength, "VMware", 6);
		RemoveSignatures(info->TableBuffer, info->TableBufferLength, "VMWARE", 6);
	}
	return status;
}

NTSTATUS __cdecl FilterRsmb(PSYSTEM_FIRMWARE_TABLE_INFORMATION info) {
	const NTSTATUS status = g_original_rsmb_handler(info);
	if (NT_SUCCESS(status) && info && info->Action == 1) {
		RemoveSignatures(info->TableBuffer, info->TableBufferLength, "VMware", 6);
		RemoveSignatures(info->TableBuffer, info->TableBufferLength, "VMWARE", 6);
	}
	return status;
}

void HookProvider(
	_In_ PSYSTEM_FIRMWARE_TABLE_HANDLER_NODE Node,
	_In_ ULONG Signature,
	_In_ FirmwareTableHandler Replacement,
	_Out_ FirmwareTableHandler* Original) {
	if (Node->SystemFWHandler.ProviderSignature == Signature &&
		Node->SystemFWHandler.FirmwareTableHandler && !*Original) {
		*Original = Node->SystemFWHandler.FirmwareTableHandler;
		Node->SystemFWHandler.FirmwareTableHandler = Replacement;
	}
}

} // namespace

NTSTATUS VmLoaderInstallFirmwareHooks(
	_In_ PVOID FirmwareTableResource,
	_In_ PVOID FirmwareTableProviderListHead) {
	PAGED_CODE();
	if (!FirmwareTableResource || !FirmwareTableProviderListHead) {
		return STATUS_INVALID_PARAMETER;
	}
	if (g_hooks_installed) {
		return STATUS_ALREADY_REGISTERED;
	}

	g_firmware_table_resource = static_cast<PERESOURCE>(FirmwareTableResource);
	g_firmware_table_provider_list_head = static_cast<PLIST_ENTRY>(FirmwareTableProviderListHead);
	ExAcquireResourceExclusiveLite(g_firmware_table_resource, TRUE);

	ULONG visited = 0;
	for (PLIST_ENTRY entry = g_firmware_table_provider_list_head->Flink;
		entry && entry != g_firmware_table_provider_list_head && visited++ < 64;
		entry = entry->Flink) {
		auto* node = CONTAINING_RECORD(entry, SYSTEM_FIRMWARE_TABLE_HANDLER_NODE, FirmwareTableProviderList);
		HookProvider(node, 'FIRM', FilterFirm, &g_original_firm_handler);
		HookProvider(node, 'ACPI', FilterAcpi, &g_original_acpi_handler);
		HookProvider(node, 'RSMB', FilterRsmb, &g_original_rsmb_handler);
	}

	ExReleaseResourceLite(g_firmware_table_resource);
	if (!g_original_firm_handler && !g_original_acpi_handler && !g_original_rsmb_handler) {
		g_firmware_table_resource = nullptr;
		g_firmware_table_provider_list_head = nullptr;
		return STATUS_NOT_FOUND;
	}

	g_hooks_installed = TRUE;
	return STATUS_SUCCESS;
}

VOID VmLoaderRemoveFirmwareHooks() {
	PAGED_CODE();
	if (!g_hooks_installed || !g_firmware_table_resource || !g_firmware_table_provider_list_head) {
		return;
	}

	ExAcquireResourceExclusiveLite(g_firmware_table_resource, TRUE);
	ULONG visited = 0;
	for (PLIST_ENTRY entry = g_firmware_table_provider_list_head->Flink;
		entry && entry != g_firmware_table_provider_list_head && visited++ < 64;
		entry = entry->Flink) {
		auto* node = CONTAINING_RECORD(entry, SYSTEM_FIRMWARE_TABLE_HANDLER_NODE, FirmwareTableProviderList);
		if (node->SystemFWHandler.ProviderSignature == 'FIRM' && g_original_firm_handler) {
			node->SystemFWHandler.FirmwareTableHandler = g_original_firm_handler;
		}
		if (node->SystemFWHandler.ProviderSignature == 'ACPI' && g_original_acpi_handler) {
			node->SystemFWHandler.FirmwareTableHandler = g_original_acpi_handler;
		}
		if (node->SystemFWHandler.ProviderSignature == 'RSMB' && g_original_rsmb_handler) {
			node->SystemFWHandler.FirmwareTableHandler = g_original_rsmb_handler;
		}
	}
	ExReleaseResourceLite(g_firmware_table_resource);

	g_original_firm_handler = nullptr;
	g_original_acpi_handler = nullptr;
	g_original_rsmb_handler = nullptr;
	g_firmware_table_resource = nullptr;
	g_firmware_table_provider_list_head = nullptr;
	g_hooks_installed = FALSE;
}
