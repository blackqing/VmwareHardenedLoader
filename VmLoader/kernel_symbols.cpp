extern "C" {
#include <kphdyndata.h>
#include <kphdynverify.h>

extern PLIST_ENTRY PsLoadedModuleList;
extern PERESOURCE PsLoadedModuleResource;
}

#include "kernel_symbols.h"

#include <ntimage.h>

#include "..\shared\symbol_config.h"
#include <vmloader_dyndata_public_key.h>

extern "C" NTSYSAPI NTSTATUS NTAPI RtlImageNtHeaderEx(
	_In_ ULONG Flags,
	_In_ PVOID Base,
	_In_ ULONG64 Size,
	_Out_ PIMAGE_NT_HEADERS* OutHeaders);

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, VmLoaderLoadKernelSymbols)
#endif

namespace {

constexpr ULONG kPoolTag = 'dDyV';
constexpr ULONG kMaximumDynDataLength = 8u * 1024u * 1024u;
constexpr ULONG kMaximumSignatureLength = 1024u;

struct VmLoaderKldrDataTableEntryPrefix {
	LIST_ENTRY InLoadOrderLinks;
	PVOID ExceptionTable;
	ULONG ExceptionTableSize;
	PVOID GpValue;
	PVOID NonPagedDebugInfo;
	PVOID DllBase;
	PVOID EntryPoint;
	ULONG SizeOfImage;
	UNICODE_STRING FullDllName;
	UNICODE_STRING BaseDllName;
};

struct VmLoaderKernelDescriptor {
	USHORT DynDataClass;
	UNICODE_STRING ImageName;
	ULONG FieldsLength;
};

struct VmLoaderKernelIdentity {
	USHORT DynDataClass;
	ULONG FieldsLength;
	PVOID ImageBase;
	PIMAGE_NT_HEADERS NtHeader;
	UNICODE_STRING ImageName;
};

const VmLoaderKernelDescriptor kKernelDescriptors[] = {
	{ KPH_DYN_CLASS_NTOSKRNL, RTL_CONSTANT_STRING(L"ntoskrnl.exe"),
		sizeof(KPH_DYN_NTOSKRNL_FIELDS) },
	{ KPH_DYN_CLASS_NTKRLA57, RTL_CONSTANT_STRING(L"ntkrla57.exe"),
		sizeof(KPH_DYN_NTKRLA57_FIELDS) },
};

void FreeBuffer(_In_opt_ PVOID Buffer) {
	if (Buffer) {
		ExFreePoolWithTag(Buffer, kPoolTag);
	}
}

PVOID AllocatePaged(_In_ SIZE_T Size) {
#pragma warning(push)
#pragma warning(disable : 4996)
	return ExAllocatePoolWithTag(PagedPool, Size, kPoolTag);
#pragma warning(pop)
}

PVOID AllocateNonPaged(_In_ SIZE_T Size) {
#pragma warning(push)
#pragma warning(disable : 4996)
	return ExAllocatePoolWithTag(NonPagedPoolNx, Size, kPoolTag);
#pragma warning(pop)
}

bool IsAsciiLetter(_In_ WCHAR Character) {
	return (Character >= L'A' && Character <= L'Z') ||
		(Character >= L'a' && Character <= L'z');
}

bool StartsWithInsensitive(
	_In_reads_(TextLength) const WCHAR* Text,
	_In_ ULONG TextLength,
	_In_z_ PCWSTR Prefix) {
	ULONG index = 0;
	while (Prefix[index] != L'\0') {
		if (index >= TextLength ||
			RtlUpcaseUnicodeChar(Text[index]) != RtlUpcaseUnicodeChar(Prefix[index])) {
			return false;
		}
		++index;
	}
	return true;
}

bool IsNetworkNtPath(
	_In_reads_(PathLength) const WCHAR* Path,
	_In_ ULONG PathLength) {
	return StartsWithInsensitive(Path, PathLength, L"\\??\\UNC") ||
		StartsWithInsensitive(Path, PathLength, L"\\GLOBAL??\\UNC") ||
		StartsWithInsensitive(Path, PathLength, L"\\??\\\\") ||
		StartsWithInsensitive(Path, PathLength, L"\\Device\\Mup") ||
		StartsWithInsensitive(Path, PathLength, L"\\Device\\LanmanRedirector") ||
		StartsWithInsensitive(Path, PathLength, L"\\Device\\WebDavRedirector") ||
		StartsWithInsensitive(Path, PathLength, L"\\Device\\Rdr") ||
		StartsWithInsensitive(Path, PathLength, L"\\??\\GLOBALROOT\\Device\\Mup") ||
		StartsWithInsensitive(Path, PathLength, L"\\??\\GLOBALROOT\\Device\\LanmanRedirector") ||
		StartsWithInsensitive(Path, PathLength, L"\\??\\GLOBALROOT\\Device\\WebDavRedirector") ||
		StartsWithInsensitive(Path, PathLength, L"\\??\\GLOBALROOT\\Device\\Rdr");
}

bool IsDriveRelativeNtPath(
	_In_reads_(PathLength) const WCHAR* Path,
	_In_ ULONG PathLength) {
	constexpr ULONG nt_prefix_length = RTL_NUMBER_OF(L"\\??\\") - 1;
	if (PathLength > nt_prefix_length + 1 &&
		StartsWithInsensitive(Path, PathLength, L"\\??\\") &&
		IsAsciiLetter(Path[nt_prefix_length]) && Path[nt_prefix_length + 1] == L':') {
		return PathLength <= nt_prefix_length + 2 || Path[nt_prefix_length + 2] != L'\\';
	}

	constexpr ULONG dos_devices_prefix_length = RTL_NUMBER_OF(L"\\DosDevices\\") - 1;
	if (PathLength > dos_devices_prefix_length + 1 &&
		StartsWithInsensitive(Path, PathLength, L"\\DosDevices\\") &&
		IsAsciiLetter(Path[dos_devices_prefix_length]) &&
		Path[dos_devices_prefix_length + 1] == L':') {
		return PathLength <= dos_devices_prefix_length + 2 ||
			Path[dos_devices_prefix_length + 2] != L'\\';
	}

	return false;
}

bool IsNtDriveRoot(
	_In_reads_(PathLength) const WCHAR* Path,
	_In_ ULONG PathLength) {
	if (PathLength == 7 && StartsWithInsensitive(Path, PathLength, L"\\??\\") &&
		IsAsciiLetter(Path[4]) && Path[5] == L':' && Path[6] == L'\\') {
		return true;
	}

	constexpr ULONG dos_devices_prefix_length = RTL_NUMBER_OF(L"\\DosDevices\\") - 1;
	return PathLength == dos_devices_prefix_length + 3 &&
		StartsWithInsensitive(Path, PathLength, L"\\DosDevices\\") &&
		IsAsciiLetter(Path[dos_devices_prefix_length]) &&
		Path[dos_devices_prefix_length + 1] == L':' &&
		Path[dos_devices_prefix_length + 2] == L'\\';
}

NTSTATUS OpenParametersKey(
	_In_ PUNICODE_STRING RegistryPath,
	_Out_ PHANDLE Key) {
	if (!RegistryPath || !RegistryPath->Buffer || RegistryPath->Length == 0 || !Key) {
		return STATUS_INVALID_PARAMETER;
	}

	constexpr USHORT suffix_length = sizeof(L"\\" VMLOADER_PARAMETERS_SUBKEY) - sizeof(WCHAR);
	const ULONG total_length = RegistryPath->Length + suffix_length + sizeof(WCHAR);
	if (total_length > MAXUSHORT) {
		return STATUS_NAME_TOO_LONG;
	}

	PWCHAR buffer = static_cast<PWCHAR>(AllocatePaged(total_length));
	if (!buffer) {
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	RtlCopyMemory(buffer, RegistryPath->Buffer, RegistryPath->Length);
	RtlCopyMemory(reinterpret_cast<PUCHAR>(buffer) + RegistryPath->Length,
		L"\\" VMLOADER_PARAMETERS_SUBKEY, suffix_length);
	buffer[(RegistryPath->Length + suffix_length) / sizeof(WCHAR)] = L'\0';

	UNICODE_STRING key_name;
	key_name.Buffer = buffer;
	key_name.Length = static_cast<USHORT>(RegistryPath->Length + suffix_length);
	key_name.MaximumLength = static_cast<USHORT>(total_length);

	OBJECT_ATTRIBUTES attributes;
	InitializeObjectAttributes(&attributes, &key_name, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
		nullptr, nullptr);
	const NTSTATUS status = ZwOpenKey(Key, KEY_QUERY_VALUE, &attributes);
	FreeBuffer(buffer);
	return status;
}

NTSTATUS QueryString(
	_In_ HANDLE Key,
	_In_z_ PCWSTR Name,
	_Out_ PUNICODE_STRING Value) {
	if (!Value) {
		return STATUS_INVALID_PARAMETER;
	}
	RtlZeroMemory(Value, sizeof(*Value));

	UNICODE_STRING value_name;
	RtlInitUnicodeString(&value_name, Name);

	ULONG required_length = 0;
	NTSTATUS status = ZwQueryValueKey(
		Key, &value_name, KeyValuePartialInformation, nullptr, 0, &required_length);
	if (status != STATUS_BUFFER_TOO_SMALL && status != STATUS_BUFFER_OVERFLOW) {
		return status;
	}
	if (required_length < sizeof(KEY_VALUE_PARTIAL_INFORMATION) ||
		required_length > MAXUSHORT) {
		return STATUS_INVALID_BUFFER_SIZE;
	}

	auto* information = static_cast<PKEY_VALUE_PARTIAL_INFORMATION>(
		AllocatePaged(required_length));
	if (!information) {
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	status = ZwQueryValueKey(Key, &value_name, KeyValuePartialInformation,
		information, required_length, &required_length);
	if (!NT_SUCCESS(status)) {
		FreeBuffer(information);
		return status;
	}

	const ULONG header_length = FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data);
	if (information->Type != REG_SZ || information->DataLength < sizeof(WCHAR) ||
		(information->DataLength % sizeof(WCHAR)) != 0 ||
		information->DataLength > required_length - header_length) {
		FreeBuffer(information);
		return STATUS_OBJECT_TYPE_MISMATCH;
	}

	ULONG string_length = information->DataLength;
	const auto* source = reinterpret_cast<const WCHAR*>(information->Data);
	if (source[string_length / sizeof(WCHAR) - 1] == L'\0') {
		string_length -= sizeof(WCHAR);
	}
	if (string_length == 0 || string_length > MAXUSHORT - sizeof(WCHAR)) {
		FreeBuffer(information);
		return STATUS_OBJECT_PATH_SYNTAX_BAD;
	}

	auto* buffer = static_cast<PWCHAR>(AllocatePaged(string_length + sizeof(WCHAR)));
	if (!buffer) {
		FreeBuffer(information);
		return STATUS_INSUFFICIENT_RESOURCES;
	}
	RtlCopyMemory(buffer, source, string_length);
	buffer[string_length / sizeof(WCHAR)] = L'\0';
	FreeBuffer(information);

	Value->Buffer = buffer;
	Value->Length = static_cast<USHORT>(string_length);
	Value->MaximumLength = static_cast<USHORT>(string_length + sizeof(WCHAR));
	return STATUS_SUCCESS;
}

NTSTATUS NormalizeDirectoryPath(
	_In_ PCUNICODE_STRING Input,
	_Out_ PUNICODE_STRING Output) {
	if (!Input || !Input->Buffer || Input->Length == 0 ||
		(Input->Length % sizeof(WCHAR)) != 0 || !Output) {
		return STATUS_INVALID_PARAMETER;
	}
	RtlZeroMemory(Output, sizeof(*Output));

	const ULONG input_length = Input->Length / sizeof(WCHAR);
	auto* normalized = static_cast<PWCHAR>(AllocatePaged((input_length + 1) * sizeof(WCHAR)));
	if (!normalized) {
		return STATUS_INSUFFICIENT_RESOURCES;
	}
	for (ULONG index = 0; index < input_length; ++index) {
		if (Input->Buffer[index] == L'\0') {
			FreeBuffer(normalized);
			return STATUS_OBJECT_PATH_SYNTAX_BAD;
		}
		normalized[index] = Input->Buffer[index] == L'/' ? L'\\' : Input->Buffer[index];
	}
	normalized[input_length] = L'\0';

	ULONG source_index = 0;
	bool add_dos_prefix = false;
	if (input_length >= 3 && IsAsciiLetter(normalized[0]) &&
		normalized[1] == L':' && normalized[2] == L'\\') {
		add_dos_prefix = true;
	}
	else if (input_length >= 4 && normalized[0] == L'\\' && normalized[1] == L'\\' &&
		(normalized[2] == L'?' || normalized[2] == L'.') && normalized[3] == L'\\') {
		source_index = 4;
		if (StartsWithInsensitive(normalized + source_index, input_length - source_index, L"UNC") ||
			StartsWithInsensitive(normalized + source_index, input_length - source_index,
				L"GLOBALROOT\\Device\\Mup")) {
			FreeBuffer(normalized);
			return STATUS_BAD_NETWORK_PATH;
		}
		if (input_length - source_index < 3 ||
			!IsAsciiLetter(normalized[source_index]) ||
			normalized[source_index + 1] != L':' ||
			normalized[source_index + 2] != L'\\') {
			FreeBuffer(normalized);
			return STATUS_OBJECT_PATH_SYNTAX_BAD;
		}
		add_dos_prefix = true;
	}
	else if (input_length >= 2 && normalized[0] == L'\\' && normalized[1] == L'\\') {
		FreeBuffer(normalized);
		return STATUS_BAD_NETWORK_PATH;
	}
	else if (normalized[0] != L'\\') {
		FreeBuffer(normalized);
		return STATUS_OBJECT_PATH_SYNTAX_BAD;
	}

	constexpr ULONG dos_prefix_length = RTL_NUMBER_OF(L"\\??\\") - 1;
	const ULONG output_capacity = input_length - source_index +
		(add_dos_prefix ? dos_prefix_length : 0) + 1;
	if (output_capacity * sizeof(WCHAR) > MAXUSHORT) {
		FreeBuffer(normalized);
		return STATUS_NAME_TOO_LONG;
	}

	auto* output_buffer = static_cast<PWCHAR>(AllocatePaged(output_capacity * sizeof(WCHAR)));
	if (!output_buffer) {
		FreeBuffer(normalized);
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	ULONG output_length = 0;
	if (add_dos_prefix) {
		RtlCopyMemory(output_buffer, L"\\??\\", dos_prefix_length * sizeof(WCHAR));
		output_length = dos_prefix_length;
	}
	RtlCopyMemory(output_buffer + output_length, normalized + source_index,
		(input_length - source_index) * sizeof(WCHAR));
	output_length += input_length - source_index;
	output_buffer[output_length] = L'\0';
	FreeBuffer(normalized);

	const bool invalid_syntax = output_length <= 1 ||
		IsDriveRelativeNtPath(output_buffer, output_length);
	const bool network_path = IsNetworkNtPath(output_buffer, output_length);
	if (invalid_syntax || network_path) {
		FreeBuffer(output_buffer);
		return invalid_syntax ? STATUS_OBJECT_PATH_SYNTAX_BAD : STATUS_BAD_NETWORK_PATH;
	}

	while (output_length > 1 && output_buffer[output_length - 1] == L'\\' &&
		!IsNtDriveRoot(output_buffer, output_length)) {
		output_buffer[--output_length] = L'\0';
	}

	Output->Buffer = output_buffer;
	Output->Length = static_cast<USHORT>(output_length * sizeof(WCHAR));
	Output->MaximumLength = static_cast<USHORT>(output_capacity * sizeof(WCHAR));
	return STATUS_SUCCESS;
}

NTSTATUS BuildFilePath(
	_In_ PCUNICODE_STRING Directory,
	_In_z_ PCWSTR FileName,
	_Out_ PUNICODE_STRING Path) {
	if (!Directory || !Directory->Buffer || Directory->Length == 0 || !Path) {
		return STATUS_INVALID_PARAMETER;
	}

	UNICODE_STRING file_name;
	RtlInitUnicodeString(&file_name, FileName);
	const bool add_separator = Directory->Buffer[Directory->Length / sizeof(WCHAR) - 1] != L'\\';
	const ULONG length = Directory->Length + (add_separator ? sizeof(WCHAR) : 0) + file_name.Length;
	if (length > MAXUSHORT - sizeof(WCHAR)) {
		return STATUS_NAME_TOO_LONG;
	}

	auto* buffer = static_cast<PWCHAR>(AllocatePaged(length + sizeof(WCHAR)));
	if (!buffer) {
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	ULONG offset = Directory->Length;
	RtlCopyMemory(buffer, Directory->Buffer, Directory->Length);
	if (add_separator) {
		buffer[offset / sizeof(WCHAR)] = L'\\';
		offset += sizeof(WCHAR);
	}
	RtlCopyMemory(reinterpret_cast<PUCHAR>(buffer) + offset, file_name.Buffer, file_name.Length);
	buffer[length / sizeof(WCHAR)] = L'\0';

	Path->Buffer = buffer;
	Path->Length = static_cast<USHORT>(length);
	Path->MaximumLength = static_cast<USHORT>(length + sizeof(WCHAR));
	return STATUS_SUCCESS;
}

NTSTATUS ReadFileWithLimit(
	_In_ PCUNICODE_STRING FileName,
	_In_ ULONG MaximumLength,
	_Outptr_result_bytebuffer_(*Length) PVOID* Buffer,
	_Out_ PULONG Length) {
	if (!FileName || !Buffer || !Length || MaximumLength == 0) {
		return STATUS_INVALID_PARAMETER;
	}
	*Buffer = nullptr;
	*Length = 0;

	OBJECT_ATTRIBUTES attributes;
	InitializeObjectAttributes(&attributes, const_cast<PUNICODE_STRING>(FileName),
		OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, nullptr, nullptr);

	HANDLE file = nullptr;
	IO_STATUS_BLOCK io_status = {};
	NTSTATUS status = ZwCreateFile(&file, GENERIC_READ | SYNCHRONIZE, &attributes,
		&io_status, nullptr, FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ, FILE_OPEN,
		FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT, nullptr, 0);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	FILE_STANDARD_INFORMATION information = {};
	status = ZwQueryInformationFile(file, &io_status, &information,
		sizeof(information), FileStandardInformation);
	if (!NT_SUCCESS(status)) {
		ZwClose(file);
		return status;
	}
	if (information.EndOfFile.QuadPart <= 0) {
		ZwClose(file);
		return STATUS_INVALID_BUFFER_SIZE;
	}
	if (information.EndOfFile.QuadPart > MaximumLength) {
		ZwClose(file);
		return STATUS_FILE_TOO_LARGE;
	}

	const ULONG file_length = static_cast<ULONG>(information.EndOfFile.QuadPart);
	PVOID buffer = AllocateNonPaged(file_length);
	if (!buffer) {
		ZwClose(file);
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	status = ZwReadFile(file, nullptr, nullptr, nullptr, &io_status,
		buffer, file_length, nullptr, nullptr);
	ZwClose(file);
	if (!NT_SUCCESS(status) || io_status.Information != file_length) {
		FreeBuffer(buffer);
		return NT_SUCCESS(status) ? STATUS_END_OF_FILE : status;
	}

	*Buffer = buffer;
	*Length = file_length;
	return STATUS_SUCCESS;
}

bool RvaInWritableDataSection(
	_In_ PIMAGE_NT_HEADERS NtHeader,
	_In_ ULONG Rva) {
	if (!Rva) {
		return false;
	}

	const auto* section = IMAGE_FIRST_SECTION(NtHeader);
	for (USHORT index = 0; index < NtHeader->FileHeader.NumberOfSections; ++index) {
		const ULONG section_start = section[index].VirtualAddress;
		const ULONG section_size = max(section[index].Misc.VirtualSize, section[index].SizeOfRawData);
		if (Rva < section_start || Rva - section_start >= section_size) {
			continue;
		}

		return (section[index].Characteristics & IMAGE_SCN_MEM_WRITE) != 0 &&
			(section[index].Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0;
	}

	return false;
}

NTSTATUS ResolveFromConfig(
	_In_reads_bytes_(ConfigLength) PKPH_DYN_CONFIG Config,
	_In_ ULONG ConfigLength,
	_In_ const VmLoaderKernelIdentity* Kernel,
	_Out_ VmLoaderKernelSymbols* Symbols) {
	PKPH_DYN_KERNEL_FIELDS fields = nullptr;
	NTSTATUS status = KphDynDataLookup(Config,
		ConfigLength,
		Kernel->DynDataClass,
		Kernel->NtHeader->FileHeader.Machine,
		Kernel->NtHeader->FileHeader.TimeDateStamp,
		Kernel->NtHeader->OptionalHeader.SizeOfImage,
		nullptr,
		Kernel->FieldsLength,
		reinterpret_cast<PVOID*>(&fields));
	if (!NT_SUCCESS(status)) {
		return status;
	}

	const ULONG resource_rva = fields->ExpFirmwareTableResource;
	const ULONG provider_list_rva = fields->ExpFirmwareTableProviderListHead;
	if (resource_rva == MAXULONG || provider_list_rva == MAXULONG) {
		return STATUS_SI_DYNDATA_UNSUPPORTED_KERNEL;
	}
	if (resource_rva == provider_list_rva ||
		!RvaInWritableDataSection(Kernel->NtHeader, resource_rva) ||
		!RvaInWritableDataSection(Kernel->NtHeader, provider_list_rva)) {
		return STATUS_INVALID_ADDRESS;
	}

	Symbols->FirmwareTableResource = static_cast<PUCHAR>(Kernel->ImageBase) + resource_rva;
	Symbols->FirmwareTableProviderListHead =
		static_cast<PUCHAR>(Kernel->ImageBase) + provider_list_rva;
	return STATUS_SUCCESS;
}

NTSTATUS ResolveExternalConfig(
	_In_ PUNICODE_STRING RegistryPath,
	_In_ const VmLoaderKernelIdentity* Kernel,
	_Out_ VmLoaderKernelSymbols* Symbols) {
	HANDLE key = nullptr;
	NTSTATUS status = OpenParametersKey(RegistryPath, &key);
	if (!NT_SUCCESS(status)) {
		DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
			"VmLoader: external dynamic data registry key unavailable: 0x%08X\n", status);
		return status;
	}

	UNICODE_STRING configured_directory = {};
	status = QueryString(key, VMLOADER_VALUE_DYNDATA_DIRECTORY, &configured_directory);
	ZwClose(key);
	if (!NT_SUCCESS(status)) {
		DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
			"VmLoader: query DynDataDirectory failed: 0x%08X\n", status);
		return status;
	}

	UNICODE_STRING directory = {};
	status = NormalizeDirectoryPath(&configured_directory, &directory);
	FreeBuffer(configured_directory.Buffer);
	if (!NT_SUCCESS(status)) {
		DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
			"VmLoader: DynDataDirectory rejected: 0x%08X\n", status);
		return status;
	}

	UNICODE_STRING config_path = {};
	UNICODE_STRING signature_path = {};
	PVOID config = nullptr;
	PVOID signature = nullptr;
	ULONG config_length = 0;
	ULONG signature_length = 0;

	status = BuildFilePath(&directory, L"dyndata.bin", &config_path);
	if (NT_SUCCESS(status)) {
		status = BuildFilePath(&directory, L"dyndata.sig", &signature_path);
	}
	FreeBuffer(directory.Buffer);
	if (!NT_SUCCESS(status)) {
		DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
			"VmLoader: external dynamic data path construction failed: 0x%08X\n", status);
		FreeBuffer(config_path.Buffer);
		return status;
	}

	status = ReadFileWithLimit(&config_path, kMaximumDynDataLength, &config, &config_length);
	FreeBuffer(config_path.Buffer);
	if (!NT_SUCCESS(status)) {
		DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
			"VmLoader: read external dyndata.bin failed: 0x%08X\n", status);
		FreeBuffer(signature_path.Buffer);
		return status;
	}

	status = ReadFileWithLimit(&signature_path, kMaximumSignatureLength,
		&signature, &signature_length);
	FreeBuffer(signature_path.Buffer);
	if (!NT_SUCCESS(status)) {
		DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
			"VmLoader: read external dyndata.sig failed: 0x%08X\n", status);
		FreeBuffer(config);
		return status;
	}

	status = KphDynVerifySignature(VmLoaderDynDataPublicKey,
		VMLOADER_DYNDATA_PUBLIC_KEY_LENGTH,
		static_cast<const UCHAR*>(config),
		config_length,
		static_cast<const UCHAR*>(signature),
		signature_length);
	FreeBuffer(signature);
	if (!NT_SUCCESS(status)) {
		DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
			"VmLoader: external dynamic data signature rejected: 0x%08X\n", status);
		FreeBuffer(config);
		return status;
	}

	status = ResolveFromConfig(static_cast<PKPH_DYN_CONFIG>(config),
		config_length, Kernel, Symbols);
	FreeBuffer(config);
	if (!NT_SUCCESS(status)) {
		DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
			"VmLoader: external dynamic data lookup rejected: 0x%08X\n", status);
	}
	return status;
}

NTSTATUS FindKernelIdentity(
	_Out_ VmLoaderKernelIdentity* Kernel) {
	RtlZeroMemory(Kernel, sizeof(*Kernel));
	NTSTATUS status = STATUS_NOT_FOUND;

	KeEnterCriticalRegion();
	if (!ExAcquireResourceSharedLite(PsLoadedModuleResource, TRUE)) {
		KeLeaveCriticalRegion();
		return STATUS_RESOURCE_NOT_OWNED;
	}

	for (ULONG descriptor_index = 0;
		descriptor_index < RTL_NUMBER_OF(kKernelDescriptors);
		++descriptor_index) {
		const auto* descriptor = &kKernelDescriptors[descriptor_index];
		for (PLIST_ENTRY link = PsLoadedModuleList->Flink;
			link != PsLoadedModuleList;
			link = link->Flink) {
			auto* entry = CONTAINING_RECORD(
				link, VmLoaderKldrDataTableEntryPrefix, InLoadOrderLinks);
			if (!RtlEqualUnicodeString(&entry->BaseDllName, &descriptor->ImageName, TRUE)) {
				continue;
			}

			__try {
				PIMAGE_NT_HEADERS nt_header = nullptr;
				status = RtlImageNtHeaderEx(
					0, entry->DllBase, entry->SizeOfImage, &nt_header);
				if (NT_SUCCESS(status)) {
					if (!RTL_CONTAINS_FIELD(&nt_header->OptionalHeader,
						nt_header->FileHeader.SizeOfOptionalHeader, SizeOfImage) ||
						nt_header->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
						status = STATUS_INVALID_IMAGE_FORMAT;
					}
					else {
						Kernel->DynDataClass = descriptor->DynDataClass;
						Kernel->FieldsLength = descriptor->FieldsLength;
						Kernel->ImageBase = entry->DllBase;
						Kernel->NtHeader = nt_header;
						Kernel->ImageName = descriptor->ImageName;
					}
				}
			}
			__except (EXCEPTION_EXECUTE_HANDLER) {
				status = GetExceptionCode();
			}

			break;
		}

		if (NT_SUCCESS(status)) {
			break;
		}
	}

	ExReleaseResourceLite(PsLoadedModuleResource);
	KeLeaveCriticalRegion();
	return status;
}

} // namespace

NTSTATUS VmLoaderLoadKernelSymbols(
	_In_ PUNICODE_STRING RegistryPath,
	_Out_ VmLoaderKernelSymbols* Symbols) {
	PAGED_CODE();
	if (!RegistryPath || !Symbols) {
		return STATUS_INVALID_PARAMETER;
	}
	RtlZeroMemory(Symbols, sizeof(*Symbols));

	VmLoaderKernelIdentity kernel = {};
	NTSTATUS status = FindKernelIdentity(&kernel);
	if (!NT_SUCCESS(status)) {
		DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
			"VmLoader: running kernel identity unavailable: 0x%08X\n", status);
		return status;
	}
	DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL,
		"VmLoader: running kernel %wZ uses dynamic data class %hu\n",
		&kernel.ImageName, kernel.DynDataClass);

	status = ResolveExternalConfig(RegistryPath, &kernel, Symbols);
	if (NT_SUCCESS(status)) {
		DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL,
			"VmLoader: using signed external dynamic data\n");
		return STATUS_SUCCESS;
	}

	DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
		"VmLoader: external dynamic data failed (0x%08X), trying embedded data\n", status);
	RtlZeroMemory(Symbols, sizeof(*Symbols));
	status = ResolveFromConfig(reinterpret_cast<PKPH_DYN_CONFIG>(const_cast<BYTE*>(KphDynConfig)),
		KphDynConfigLength, &kernel, Symbols);
	if (!NT_SUCCESS(status)) {
		DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
			"VmLoader: embedded dynamic data rejected: 0x%08X\n", status);
		RtlZeroMemory(Symbols, sizeof(*Symbols));
		return status;
	}

	DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL,
		"VmLoader: using embedded dynamic data\n");
	return STATUS_SUCCESS;
}
