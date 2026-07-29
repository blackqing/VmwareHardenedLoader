#include "kernel_symbols.h"

#include <ntimage.h>

#include "..\shared\symbol_config.h"

extern "C" NTSYSAPI PVOID NTAPI RtlPcToFileHeader(
	_In_ PVOID PcValue,
	_Out_ PVOID* BaseOfImage);
extern "C" NTSYSAPI PIMAGE_NT_HEADERS NTAPI RtlImageNtHeader(_In_ PVOID Base);

namespace {

constexpr ULONG kPoolTag = 'rSyV';

NTSTATUS QueryDword(_In_ HANDLE Key, _In_ PCWSTR Name, _Out_ PULONG Value) {
	UNICODE_STRING value_name;
	RtlInitUnicodeString(&value_name, Name);

	UCHAR buffer[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(ULONG)] = {};
	ULONG result_length = 0;
	const NTSTATUS status = ZwQueryValueKey(
		Key, &value_name, KeyValuePartialInformation, buffer, sizeof(buffer), &result_length);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	const auto* value = reinterpret_cast<const KEY_VALUE_PARTIAL_INFORMATION*>(buffer);
	if (value->Type != REG_DWORD || value->DataLength != sizeof(ULONG)) {
		return STATUS_OBJECT_TYPE_MISMATCH;
	}

	*Value = *reinterpret_cast<const ULONG*>(value->Data);
	return STATUS_SUCCESS;
}

NTSTATUS OpenParametersKey(
	_In_ PUNICODE_STRING RegistryPath,
	_Out_ PHANDLE Key) {
	if (!RegistryPath || !RegistryPath->Buffer || RegistryPath->Length == 0) {
		return STATUS_INVALID_PARAMETER;
	}

	constexpr USHORT suffix_length = sizeof(L"\\Parameters") - sizeof(WCHAR);
	const ULONG total_length = RegistryPath->Length + suffix_length + sizeof(WCHAR);
	if (total_length > MAXUSHORT) {
		return STATUS_NAME_TOO_LONG;
	}

#pragma warning(push)
#pragma warning(disable : 4996)
	PWCHAR buffer = static_cast<PWCHAR>(ExAllocatePoolWithTag(PagedPool, total_length, kPoolTag));
#pragma warning(pop)
	if (!buffer) {
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	RtlCopyMemory(buffer, RegistryPath->Buffer, RegistryPath->Length);
	RtlCopyMemory(reinterpret_cast<PUCHAR>(buffer) + RegistryPath->Length,
		L"\\Parameters", suffix_length);
	buffer[(RegistryPath->Length + suffix_length) / sizeof(WCHAR)] = L'\0';

	UNICODE_STRING key_name;
	key_name.Buffer = buffer;
	key_name.Length = static_cast<USHORT>(RegistryPath->Length + suffix_length);
	key_name.MaximumLength = static_cast<USHORT>(total_length);

	OBJECT_ATTRIBUTES attributes;
	InitializeObjectAttributes(&attributes, &key_name, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
		nullptr, nullptr);
	const NTSTATUS status = ZwOpenKey(Key, KEY_QUERY_VALUE, &attributes);
	ExFreePoolWithTag(buffer, kPoolTag);
	return status;
}

bool RvaInWritableDataSection(
	_In_ PIMAGE_NT_HEADERS NtHeader,
	_In_ ULONG Rva) {
	if (!Rva) {
		return false;
	}

	const auto* section = IMAGE_FIRST_SECTION(NtHeader);
	for (USHORT i = 0; i < NtHeader->FileHeader.NumberOfSections; ++i) {
		const ULONG section_start = section[i].VirtualAddress;
		const ULONG section_size = max(section[i].Misc.VirtualSize, section[i].SizeOfRawData);
		if (Rva < section_start || Rva - section_start >= section_size) {
			continue;
		}

		const ULONG required_flags = IMAGE_SCN_MEM_WRITE;
		return (section[i].Characteristics & required_flags) == required_flags &&
			(section[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0;
	}

	return false;
}

} // namespace

NTSTATUS VmLoaderLoadKernelSymbols(
	_In_ PUNICODE_STRING RegistryPath,
	_Out_ VmLoaderKernelSymbols* Symbols) {
	PAGED_CODE();
	if (!Symbols) {
		return STATUS_INVALID_PARAMETER;
	}
	RtlZeroMemory(Symbols, sizeof(*Symbols));

	HANDLE key = nullptr;
	NTSTATUS status = OpenParametersKey(RegistryPath, &key);
	if (!NT_SUCCESS(status)) {
		DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
			"VmLoader: open symbol registry failed: 0x%08X\n", status);
		return status;
	}

	ULONG schema = 0;
	ULONG timestamp = 0;
	ULONG image_size = 0;
	ULONG checksum = 0;
	ULONG resource_rva = 0;
	ULONG provider_list_rva = 0;
	status = QueryDword(key, VMLOADER_VALUE_SCHEMA_VERSION, &schema);
	if (NT_SUCCESS(status)) status = QueryDword(key, VMLOADER_VALUE_KERNEL_TIMESTAMP, &timestamp);
	if (NT_SUCCESS(status)) status = QueryDword(key, VMLOADER_VALUE_KERNEL_IMAGE_SIZE, &image_size);
	if (NT_SUCCESS(status)) status = QueryDword(key, VMLOADER_VALUE_KERNEL_CHECKSUM, &checksum);
	if (NT_SUCCESS(status)) status = QueryDword(key, VMLOADER_VALUE_FIRMWARE_RESOURCE_RVA, &resource_rva);
	if (NT_SUCCESS(status)) status = QueryDword(key, VMLOADER_VALUE_FIRMWARE_PROVIDER_LIST_RVA, &provider_list_rva);
	ZwClose(key);
	if (!NT_SUCCESS(status)) {
		DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
			"VmLoader: incomplete symbol registry: 0x%08X\n", status);
		return status;
	}
	if (schema != VMLOADER_SYMBOL_SCHEMA_VERSION) {
		DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
			"VmLoader: unsupported symbol schema %lu\n", schema);
		return STATUS_REVISION_MISMATCH;
	}

	UNICODE_STRING routine_name;
	RtlInitUnicodeString(&routine_name, L"NtOpenFile");
	PVOID routine = MmGetSystemRoutineAddress(&routine_name);
	PVOID ntos_base = nullptr;
	if (!routine || !RtlPcToFileHeader(routine, &ntos_base) || !ntos_base) {
		DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
			"VmLoader: ntoskrnl base not found\n");
		return STATUS_NOT_FOUND;
	}

	PIMAGE_NT_HEADERS nt_header = RtlImageNtHeader(ntos_base);
	if (!nt_header || nt_header->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
		return STATUS_INVALID_IMAGE_FORMAT;
	}
	if (nt_header->FileHeader.TimeDateStamp != timestamp ||
		nt_header->OptionalHeader.SizeOfImage != image_size ||
		nt_header->OptionalHeader.CheckSum != checksum) {
		DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
			"VmLoader: symbol metadata does not match the running kernel\n");
		return STATUS_IMAGE_CHECKSUM_MISMATCH;
	}

	if (resource_rva == provider_list_rva ||
		!RvaInWritableDataSection(nt_header, resource_rva) ||
		!RvaInWritableDataSection(nt_header, provider_list_rva)) {
		DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
			"VmLoader: symbol RVAs are outside writable kernel data\n");
		return STATUS_INVALID_ADDRESS;
	}

	Symbols->FirmwareTableResource = static_cast<PUCHAR>(ntos_base) + resource_rva;
	Symbols->FirmwareTableProviderListHead = static_cast<PUCHAR>(ntos_base) + provider_list_rva;
	return STATUS_SUCCESS;
}
