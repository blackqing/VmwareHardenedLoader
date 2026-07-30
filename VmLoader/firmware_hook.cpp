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

void ReplaceInPlace(
	_Inout_ PVOID Buffer,
	_In_ ULONG BufferLength,
	_In_reads_bytes_(PatternLength) const char* Pattern,
	_In_reads_bytes_(PatternLength) const char* Replacement,
	_In_ SIZE_T PatternLength) {
	if (!Buffer || !Pattern || !Replacement || PatternLength == 0) {
		return;
	}
	PUCHAR cursor = static_cast<PUCHAR>(Buffer);
	SIZE_T remaining = BufferLength;
	while (remaining >= PatternLength) {
		PUCHAR match = nullptr;
		const SIZE_T searchLen = remaining - PatternLength + 1;
		for (SIZE_T i = 0; i < searchLen; ++i) {
			if (RtlCompareMemory(cursor + i, Pattern, PatternLength) == PatternLength) {
				match = cursor + i;
				break;
			}
		}
		if (!match) {
			return;
		}
		RtlCopyMemory(match, Replacement, PatternLength);
		const SIZE_T consumed = static_cast<SIZE_T>((match - cursor) + PatternLength);
		cursor = match + PatternLength;
		remaining -= consumed;
	}
}


void RemoveEnumerationEntry(
	_Inout_ PSYSTEM_FIRMWARE_TABLE_INFORMATION Info,
	_In_reads_bytes_(4) const char* Signature) {
	if (!Info || !Info->TableBuffer || !Signature) {
		return;
	}
	ULONG length = Info->TableBufferLength;
	if (length < 4 || (length & 3u) != 0) {
		return;
	}
	PUCHAR bytes = static_cast<PUCHAR>(static_cast<PVOID>(Info->TableBuffer));
	const UCHAR reversed[4] = {
		static_cast<UCHAR>(Signature[3]),
		static_cast<UCHAR>(Signature[2]),
		static_cast<UCHAR>(Signature[1]),
		static_cast<UCHAR>(Signature[0]),
	};
	ULONG dst = 0;
	for (ULONG src = 0; src < length; src += 4) {
		if (RtlCompareMemory(bytes + src, Signature, 4) == 4 ||
			RtlCompareMemory(bytes + src, reversed, 4) == 4) {
			continue;
		}
		if (dst != src) {
			bytes[dst + 0] = bytes[src + 0];
			bytes[dst + 1] = bytes[src + 1];
			bytes[dst + 2] = bytes[src + 2];
			bytes[dst + 3] = bytes[src + 3];
		}
		dst += 4;
	}
	if (dst < length) {
		RtlZeroMemory(bytes + dst, length - dst);
		Info->TableBufferLength = dst;
	}
}

BOOLEAN TableIdMatches(_In_ ULONG TableID, _In_reads_bytes_(4) const char* Signature) {
	const UCHAR reversed[4] = {
		static_cast<UCHAR>(Signature[3]),
		static_cast<UCHAR>(Signature[2]),
		static_cast<UCHAR>(Signature[1]),
		static_cast<UCHAR>(Signature[0]),
	};
	return (RtlCompareMemory(&TableID, Signature, 4) == 4 ||
		RtlCompareMemory(&TableID, reversed, 4) == 4);
}

// Standard ACPI table header layout:
//   [ 0.. 3] Signature
//   [ 4.. 7] Length (whole table)
//   [ 8]     Revision
//   [ 9]     Checksum       <- sum of all Length bytes must equal 0 (mod 256)
//   [10..15] OEMID
//   [16..23] OEMTableID
//   [24..27] OEMRevision
//   [28..31] AslCompilerID
//   [32..35] AslCompilerRevision

void RecomputeAcpiChecksum(_Inout_ PVOID Buffer, _In_ ULONG BufferLength) {
	if (!Buffer || BufferLength < 36) {
		return;
	}
	PUCHAR bytes = static_cast<PUCHAR>(Buffer);
	ULONG length = 0;
	RtlCopyMemory(&length, bytes + 4, sizeof(length));
	if (length < 36 || length > BufferLength) {
		return;
	}
	bytes[9] = 0;
	UCHAR sum = 0;
	for (ULONG i = 0; i < length; ++i) {
		sum = static_cast<UCHAR>(sum + bytes[i]);
	}
	bytes[9] = static_cast<UCHAR>(0u - sum);
}

NTSTATUS __cdecl FilterFirm(PSYSTEM_FIRMWARE_TABLE_INFORMATION info) {
	const NTSTATUS status = g_original_firm_handler(info);
	if (NT_SUCCESS(status) && info && info->Action == 1) {
		ReplaceInPlace(info->TableBuffer, info->TableBufferLength, "VMware", "System", 6);
		ReplaceInPlace(info->TableBuffer, info->TableBufferLength, "Virtual", "Generic", 7);
	}
	return status;
}

NTSTATUS __cdecl FilterAcpi(PSYSTEM_FIRMWARE_TABLE_INFORMATION info) {

	if (info && info->Action == 1 && TableIdMatches(info->TableID, "WAET")) {
		info->TableBufferLength = 0;
		return STATUS_NOT_FOUND;
	}

	const NTSTATUS status = g_original_acpi_handler(info);
	if (NT_SUCCESS(status) && info) {
		if (info->Action == 0) {
			RemoveEnumerationEntry(info, "WAET");
		} else if (info->Action == 1) {
			ReplaceInPlace(info->TableBuffer, info->TableBufferLength, "VMware", "System", 6);
			ReplaceInPlace(info->TableBuffer, info->TableBufferLength, "VMWARE", "SYSTEM", 6);
			RecomputeAcpiChecksum(info->TableBuffer, info->TableBufferLength);
		}
	}
	return status;
}

NTSTATUS __cdecl FilterRsmb(PSYSTEM_FIRMWARE_TABLE_INFORMATION info) {
	const NTSTATUS status = g_original_rsmb_handler(info);
	if (NT_SUCCESS(status) && info && info->Action == 1) {
		ReplaceInPlace(info->TableBuffer, info->TableBufferLength, "VMware", "System", 6);
		ReplaceInPlace(info->TableBuffer, info->TableBufferLength, "VMWARE", "SYSTEM", 6);
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
