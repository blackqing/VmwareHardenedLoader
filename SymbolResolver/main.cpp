#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>
#include <shlobj.h>
#include <winhttp.h>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "..\shared\symbol_config.h"

namespace {

constexpr ULONG kSystemModuleInformation = 11;
constexpr LONG kStatusInfoLengthMismatch = static_cast<LONG>(0xC0000004L);
constexpr DWORD kCodeViewRsdsSignature = 0x53445352;
constexpr wchar_t kServiceRegistryPath[] =
	L"SYSTEM\\CurrentControlSet\\Services\\vmloader";

struct RTL_PROCESS_MODULE_INFORMATION_LOCAL {
	HANDLE Section;
	PVOID MappedBase;
	PVOID ImageBase;
	ULONG ImageSize;
	ULONG Flags;
	USHORT LoadOrderIndex;
	USHORT InitOrderIndex;
	USHORT LoadCount;
	USHORT OffsetToFileName;
	UCHAR FullPathName[256];
};

struct RTL_PROCESS_MODULES_LOCAL {
	ULONG NumberOfModules;
	RTL_PROCESS_MODULE_INFORMATION_LOCAL Modules[1];
};

struct KernelModule {
	std::wstring ImagePath;
	DWORD ImageSize;
};

struct ImageSection {
	DWORD Rva;
	DWORD Size;
	DWORD Characteristics;
};

struct PeIdentity {
	DWORD TimeDateStamp;
	DWORD SizeOfImage;
	DWORD CheckSum;
	GUID PdbGuid;
	DWORD PdbAge;
	std::wstring PdbName;
	std::vector<ImageSection> Sections;
};

struct ResolvedSymbols {
	DWORD FirmwareTableResourceRva;
	DWORD FirmwareTableProviderListHeadRva;
};

class ScopedHandle {
public:
	explicit ScopedHandle(HANDLE handle = INVALID_HANDLE_VALUE) : handle_(handle) {}
	~ScopedHandle() {
		if (handle_ != INVALID_HANDLE_VALUE && handle_ != nullptr) {
			CloseHandle(handle_);
		}
	}
	ScopedHandle(const ScopedHandle&) = delete;
	ScopedHandle& operator=(const ScopedHandle&) = delete;
	HANDLE get() const { return handle_; }
	bool valid() const { return handle_ != INVALID_HANDLE_VALUE && handle_ != nullptr; }
	void close() {
		if (valid()) {
			CloseHandle(handle_);
			handle_ = INVALID_HANDLE_VALUE;
		}
	}

private:
	HANDLE handle_;
};

class ScopedInternet {
public:
	explicit ScopedInternet(HINTERNET handle = nullptr) : handle_(handle) {}
	~ScopedInternet() {
		if (handle_) {
			WinHttpCloseHandle(handle_);
		}
	}
	ScopedInternet(const ScopedInternet&) = delete;
	ScopedInternet& operator=(const ScopedInternet&) = delete;
	HINTERNET get() const { return handle_; }
	bool valid() const { return handle_ != nullptr; }

private:
	HINTERNET handle_;
};

std::wstring Win32ErrorMessage(DWORD error) {
	PWSTR message = nullptr;
	const DWORD length = FormatMessageW(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
			FORMAT_MESSAGE_IGNORE_INSERTS,
		nullptr, error, 0, reinterpret_cast<PWSTR>(&message), 0, nullptr);
	std::wstring result = length && message ? std::wstring(message, length) : L"unknown error";
	if (message) {
		LocalFree(message);
	}
	while (!result.empty() && (result.back() == L'\r' || result.back() == L'\n')) {
		result.pop_back();
	}
	return result;
}

std::wstring AnsiToWide(const char* text, size_t length) {
	if (!text || length == 0) {
		return {};
	}
	const int required = MultiByteToWideChar(
		CP_ACP, MB_ERR_INVALID_CHARS, text, static_cast<int>(length), nullptr, 0);
	if (required <= 0) {
		return {};
	}
	std::wstring result(static_cast<size_t>(required), L'\0');
	if (!MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, text,
		static_cast<int>(length), &result[0], required)) {
		return {};
	}
	return result;
}

bool QueryKernelModule(KernelModule* module, std::wstring* error) {
	const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
	if (!ntdll) {
		*error = L"GetModuleHandleW(ntdll.dll) failed";
		return false;
	}

	using NtQuerySystemInformation = LONG(NTAPI*)(ULONG, PVOID, ULONG, PULONG);
	const auto query = reinterpret_cast<NtQuerySystemInformation>(
		GetProcAddress(ntdll, "NtQuerySystemInformation"));
	if (!query) {
		*error = L"NtQuerySystemInformation is unavailable";
		return false;
	}

	ULONG size = 0;
	LONG status = query(kSystemModuleInformation, nullptr, 0, &size);
	if (status != kStatusInfoLengthMismatch || size < sizeof(RTL_PROCESS_MODULES_LOCAL)) {
		*error = L"NtQuerySystemInformation size query failed";
		return false;
	}

	std::vector<BYTE> buffer(size);
	for (int attempt = 0; attempt < 3; ++attempt) {
		status = query(kSystemModuleInformation, buffer.data(),
			static_cast<ULONG>(buffer.size()), &size);
		if (status >= 0) {
			break;
		}
		if (status != kStatusInfoLengthMismatch) {
			*error = L"NtQuerySystemInformation failed with NTSTATUS 0x";
			wchar_t code[16] = {};
			swprintf_s(code, L"%08X", static_cast<ULONG>(status));
			*error += code;
			return false;
		}
		buffer.resize(size);
	}
	if (status < 0) {
		*error = L"NtQuerySystemInformation did not stabilize";
		return false;
	}

	const auto* modules = reinterpret_cast<const RTL_PROCESS_MODULES_LOCAL*>(buffer.data());
	if (modules->NumberOfModules == 0) {
		*error = L"the running kernel module was not returned";
		return false;
	}

	const auto& kernel = modules->Modules[0];
	const size_t full_length = strnlen_s(
		reinterpret_cast<const char*>(kernel.FullPathName), sizeof(kernel.FullPathName));
	if (kernel.OffsetToFileName >= full_length) {
		*error = L"the running kernel filename is invalid";
		return false;
	}
	const char* filename = reinterpret_cast<const char*>(kernel.FullPathName) +
		kernel.OffsetToFileName;
	const std::wstring wide_filename = AnsiToWide(filename, full_length - kernel.OffsetToFileName);
	if (wide_filename.empty()) {
		*error = L"the running kernel filename could not be converted";
		return false;
	}

	wchar_t system_directory[MAX_PATH] = {};
	const UINT directory_length = GetSystemDirectoryW(system_directory, MAX_PATH);
	if (!directory_length || directory_length >= MAX_PATH) {
		*error = L"GetSystemDirectoryW failed";
		return false;
	}

	module->ImagePath.assign(system_directory, directory_length);
	module->ImagePath += L"\\";
	module->ImagePath += wide_filename;
	module->ImageSize = kernel.ImageSize;
	return true;
}

bool RangeInside(size_t offset, size_t length, size_t total) {
	return offset <= total && length <= total - offset;
}

bool RvaToFileOffset(
	DWORD rva,
	DWORD size_of_headers,
	const IMAGE_SECTION_HEADER* sections,
	WORD section_count,
	size_t file_size,
	size_t* offset) {
	if (rva < size_of_headers) {
		if (rva >= file_size) {
			return false;
		}
		*offset = rva;
		return true;
	}

	for (WORD i = 0; i < section_count; ++i) {
		const DWORD section_size =
			std::max(sections[i].Misc.VirtualSize, sections[i].SizeOfRawData);
		if (rva < sections[i].VirtualAddress ||
			rva - sections[i].VirtualAddress >= section_size) {
			continue;
		}
		const DWORD delta = rva - sections[i].VirtualAddress;
		if (delta >= sections[i].SizeOfRawData) {
			return false;
		}
		const size_t raw_offset = static_cast<size_t>(sections[i].PointerToRawData) + delta;
		if (raw_offset >= file_size) {
			return false;
		}
		*offset = raw_offset;
		return true;
	}
	return false;
}

bool ReadPeIdentity(
	const std::wstring& image_path,
	PeIdentity* identity,
	std::wstring* error) {
	ScopedHandle file(CreateFileW(image_path.c_str(), GENERIC_READ,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL, nullptr));
	if (!file.valid()) {
		*error = L"open kernel image failed: " + Win32ErrorMessage(GetLastError());
		return false;
	}

	LARGE_INTEGER file_size = {};
	if (!GetFileSizeEx(file.get(), &file_size) || file_size.QuadPart <= 0 ||
		static_cast<ULONGLONG>(file_size.QuadPart) > std::numeric_limits<size_t>::max()) {
		*error = L"invalid kernel image size";
		return false;
	}

	ScopedHandle mapping(CreateFileMappingW(file.get(), nullptr, PAGE_READONLY, 0, 0, nullptr));
	if (!mapping.valid()) {
		*error = L"map kernel image failed: " + Win32ErrorMessage(GetLastError());
		return false;
	}

	const auto* image = static_cast<const BYTE*>(MapViewOfFile(mapping.get(), FILE_MAP_READ, 0, 0, 0));
	if (!image) {
		*error = L"view kernel image failed: " + Win32ErrorMessage(GetLastError());
		return false;
	}
	const size_t image_length = static_cast<size_t>(file_size.QuadPart);

	bool success = false;
	do {
		if (!RangeInside(0, sizeof(IMAGE_DOS_HEADER), image_length)) {
			*error = L"kernel image has no DOS header";
			break;
		}
		const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
		if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0) {
			*error = L"kernel image has an invalid DOS header";
			break;
		}

		const size_t nt_offset = static_cast<size_t>(dos->e_lfanew);
		if (!RangeInside(nt_offset, sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER), image_length) ||
			*reinterpret_cast<const DWORD*>(image + nt_offset) != IMAGE_NT_SIGNATURE) {
			*error = L"kernel image has an invalid NT header";
			break;
		}
		const auto* file_header = reinterpret_cast<const IMAGE_FILE_HEADER*>(
			image + nt_offset + sizeof(DWORD));
		const size_t optional_offset = nt_offset + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER);
		if (file_header->SizeOfOptionalHeader < sizeof(IMAGE_OPTIONAL_HEADER64) ||
			!RangeInside(optional_offset, file_header->SizeOfOptionalHeader, image_length)) {
			*error = L"kernel image has an invalid optional header";
			break;
		}
		const auto* optional = reinterpret_cast<const IMAGE_OPTIONAL_HEADER64*>(image + optional_offset);
		if (optional->Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
			optional->NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_DEBUG) {
			*error = L"kernel image is not a supported x64 image";
			break;
		}

		const size_t section_offset = optional_offset + file_header->SizeOfOptionalHeader;
		const size_t section_bytes =
			static_cast<size_t>(file_header->NumberOfSections) * sizeof(IMAGE_SECTION_HEADER);
		if (!RangeInside(section_offset, section_bytes, image_length)) {
			*error = L"kernel image section table is invalid";
			break;
		}
		const auto* sections = reinterpret_cast<const IMAGE_SECTION_HEADER*>(image + section_offset);

		identity->TimeDateStamp = file_header->TimeDateStamp;
		identity->SizeOfImage = optional->SizeOfImage;
		identity->CheckSum = optional->CheckSum;
		identity->Sections.clear();
		identity->Sections.reserve(file_header->NumberOfSections);
		for (WORD i = 0; i < file_header->NumberOfSections; ++i) {
			identity->Sections.push_back({sections[i].VirtualAddress,
				std::max(sections[i].Misc.VirtualSize, sections[i].SizeOfRawData),
				sections[i].Characteristics});
		}

		const IMAGE_DATA_DIRECTORY debug_directory =
			optional->DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
		size_t debug_offset = 0;
		if (!debug_directory.VirtualAddress ||
			!RvaToFileOffset(debug_directory.VirtualAddress, optional->SizeOfHeaders,
				sections, file_header->NumberOfSections, image_length, &debug_offset) ||
			!RangeInside(debug_offset, debug_directory.Size, image_length)) {
			*error = L"kernel image has no readable debug directory";
			break;
		}

		const size_t debug_count = debug_directory.Size / sizeof(IMAGE_DEBUG_DIRECTORY);
		const auto* debug_entries =
			reinterpret_cast<const IMAGE_DEBUG_DIRECTORY*>(image + debug_offset);
		bool found_codeview = false;
		for (size_t i = 0; i < debug_count; ++i) {
			if (debug_entries[i].Type != IMAGE_DEBUG_TYPE_CODEVIEW ||
				debug_entries[i].SizeOfData < sizeof(DWORD) + sizeof(GUID) + sizeof(DWORD) + 1 ||
				!RangeInside(debug_entries[i].PointerToRawData,
					debug_entries[i].SizeOfData, image_length)) {
				continue;
			}

			const BYTE* codeview = image + debug_entries[i].PointerToRawData;
			if (*reinterpret_cast<const DWORD*>(codeview) != kCodeViewRsdsSignature) {
				continue;
			}
			identity->PdbGuid = *reinterpret_cast<const GUID*>(codeview + sizeof(DWORD));
			identity->PdbAge = *reinterpret_cast<const DWORD*>(
				codeview + sizeof(DWORD) + sizeof(GUID));
			const char* pdb_path = reinterpret_cast<const char*>(
				codeview + sizeof(DWORD) + sizeof(GUID) + sizeof(DWORD));
			const size_t pdb_capacity = debug_entries[i].SizeOfData -
				(sizeof(DWORD) + sizeof(GUID) + sizeof(DWORD));
			const size_t pdb_length = strnlen_s(pdb_path, pdb_capacity);
			if (pdb_length == 0 || pdb_length == pdb_capacity) {
				continue;
			}
			const char* basename = pdb_path;
			for (size_t index = 0; index < pdb_length; ++index) {
				if (pdb_path[index] == '\\' || pdb_path[index] == '/') {
					basename = pdb_path + index + 1;
				}
			}
			identity->PdbName = AnsiToWide(
				basename, pdb_length - static_cast<size_t>(basename - pdb_path));
			found_codeview = !identity->PdbName.empty();
			if (found_codeview) {
				break;
			}
		}
		if (!found_codeview) {
			*error = L"kernel image has no RSDS PDB identity";
			break;
		}
		success = true;
	} while (false);

	UnmapViewOfFile(image);
	return success;
}

std::wstring BuildPdbKey(const GUID& guid, DWORD age) {
	wchar_t key[64] = {};
	swprintf_s(key,
		L"%08lX%04hX%04hX%02hhX%02hhX%02hhX%02hhX%02hhX%02hhX%02hhX%02hhX%lX",
		guid.Data1, guid.Data2, guid.Data3,
		guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
		guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7], age);
	return key;
}

bool EnsureDirectory(const std::wstring& path, std::wstring* error) {
	if (CreateDirectoryW(path.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS) {
		return true;
	}
	*error = L"create directory failed for " + path + L": " +
		Win32ErrorMessage(GetLastError());
	return false;
}

bool ExistingNonEmptyFile(const std::wstring& path) {
	WIN32_FILE_ATTRIBUTE_DATA data = {};
	return GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data) &&
		(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 &&
		(data.nFileSizeHigh != 0 || data.nFileSizeLow != 0);
}

bool DownloadPdb(
	const PeIdentity& identity,
	std::wstring* pdb_directory,
	std::wstring* error) {
	wchar_t program_data[MAX_PATH] = {};
	if (FAILED(SHGetFolderPathW(nullptr, CSIDL_COMMON_APPDATA | CSIDL_FLAG_CREATE,
		nullptr, SHGFP_TYPE_CURRENT, program_data))) {
		*error = L"locate ProgramData failed";
		return false;
	}

	const std::wstring product_root = std::wstring(program_data) + L"\\VmLoader";
	const std::wstring cache_root = product_root + L"\\Symbols";
	const std::wstring pdb_root = cache_root + L"\\" + identity.PdbName;
	const std::wstring key = BuildPdbKey(identity.PdbGuid, identity.PdbAge);
	*pdb_directory = pdb_root + L"\\" + key;
	if (!EnsureDirectory(product_root, error) || !EnsureDirectory(cache_root, error) ||
		!EnsureDirectory(pdb_root, error) ||
		!EnsureDirectory(*pdb_directory, error)) {
		return false;
	}

	const std::wstring destination = *pdb_directory + L"\\" + identity.PdbName;
	if (ExistingNonEmptyFile(destination)) {
		return true;
	}

	const std::wstring request_path = L"/download/symbols/" + identity.PdbName +
		L"/" + key + L"/" + identity.PdbName;
	ScopedInternet session(WinHttpOpen(L"VmLoader Symbol Resolver/1.0",
		WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
		WINHTTP_NO_PROXY_BYPASS, 0));
	if (!session.valid()) {
		*error = L"WinHttpOpen failed: " + Win32ErrorMessage(GetLastError());
		return false;
	}
	ScopedInternet connection(WinHttpConnect(
		session.get(), L"msdl.microsoft.com", INTERNET_DEFAULT_HTTPS_PORT, 0));
	if (!connection.valid()) {
		*error = L"WinHttpConnect failed: " + Win32ErrorMessage(GetLastError());
		return false;
	}
	ScopedInternet request(WinHttpOpenRequest(connection.get(), L"GET", request_path.c_str(),
		nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
	if (!request.valid() || !WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS,
		0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(request.get(), nullptr)) {
		*error = L"kernel PDB download request failed: " + Win32ErrorMessage(GetLastError());
		return false;
	}

	DWORD status_code = 0;
	DWORD status_size = sizeof(status_code);
	if (!WinHttpQueryHeaders(request.get(),
		WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
		WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &status_size, WINHTTP_NO_HEADER_INDEX) ||
		status_code != HTTP_STATUS_OK) {
		*error = L"Microsoft symbol server returned HTTP " + std::to_wstring(status_code);
		return false;
	}

	const std::wstring temporary = destination + L".tmp." +
		std::to_wstring(GetCurrentProcessId());
	ScopedHandle output(CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
		CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
	if (!output.valid()) {
		*error = L"create PDB cache file failed: " + Win32ErrorMessage(GetLastError());
		return false;
	}

	bool downloaded = true;
	for (;;) {
		DWORD available = 0;
		if (!WinHttpQueryDataAvailable(request.get(), &available)) {
			*error = L"PDB download read failed: " + Win32ErrorMessage(GetLastError());
			downloaded = false;
			break;
		}
		if (!available) {
			break;
		}
		std::vector<BYTE> chunk(available);
		DWORD read = 0;
		if (!WinHttpReadData(request.get(), chunk.data(), available, &read) || read == 0) {
			*error = L"PDB download returned incomplete data";
			downloaded = false;
			break;
		}
		DWORD written = 0;
		if (!WriteFile(output.get(), chunk.data(), read, &written, nullptr) || written != read) {
			*error = L"write PDB cache failed: " + Win32ErrorMessage(GetLastError());
			downloaded = false;
			break;
		}
	}
	if (!downloaded || !FlushFileBuffers(output.get())) {
		output.close();
		DeleteFileW(temporary.c_str());
		return false;
	}
	output.close();

	if (!MoveFileExW(temporary.c_str(), destination.c_str(),
		MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
		*error = L"commit PDB cache failed: " + Win32ErrorMessage(GetLastError());
		DeleteFileW(temporary.c_str());
		return false;
	}
	return true;
}

bool IsWritableDataRva(const PeIdentity& identity, DWORD rva) {
	for (const auto& section : identity.Sections) {
		if (rva < section.Rva || rva - section.Rva >= section.Size) {
			continue;
		}
		return (section.Characteristics & IMAGE_SCN_MEM_WRITE) != 0 &&
			(section.Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0;
	}
	return false;
}

bool ResolveSymbolRva(
	HANDLE process,
	DWORD64 module_base,
	PCWSTR symbol_name,
	DWORD* rva,
	std::wstring* error) {
	std::vector<BYTE> buffer(sizeof(SYMBOL_INFOW) + MAX_SYM_NAME * sizeof(WCHAR));
	auto* symbol = reinterpret_cast<PSYMBOL_INFOW>(buffer.data());
	symbol->SizeOfStruct = sizeof(SYMBOL_INFOW);
	symbol->MaxNameLen = MAX_SYM_NAME;
	const std::wstring qualified_name = L"nt!" + std::wstring(symbol_name);
	if (!SymFromNameW(process, qualified_name.c_str(), symbol)) {
		*error = L"resolve " + qualified_name + L" failed: " +
			Win32ErrorMessage(GetLastError());
		return false;
	}
	if (symbol->Address < module_base ||
		symbol->Address - module_base > std::numeric_limits<DWORD>::max()) {
		*error = L"resolved symbol is outside the loaded kernel image";
		return false;
	}
	*rva = static_cast<DWORD>(symbol->Address - module_base);
	return true;
}

bool ResolveSymbols(
	const KernelModule& kernel,
	const PeIdentity& identity,
	const std::wstring& pdb_directory,
	ResolvedSymbols* resolved,
	std::wstring* error) {
	HANDLE process = GetCurrentProcess();
	SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME | SYMOPT_FAIL_CRITICAL_ERRORS);
	if (!SymInitializeW(process, pdb_directory.c_str(), FALSE)) {
		*error = L"SymInitializeW failed: " + Win32ErrorMessage(GetLastError());
		return false;
	}

	const DWORD64 module_base = SymLoadModuleExW(process, nullptr, kernel.ImagePath.c_str(),
		L"nt", 0, 0, nullptr, 0);
	if (!module_base) {
		*error = L"SymLoadModuleExW failed: " + Win32ErrorMessage(GetLastError());
		SymCleanup(process);
		return false;
	}

	bool success = ResolveSymbolRva(process, module_base, VMLOADER_SYMBOL_RESOURCE,
		&resolved->FirmwareTableResourceRva, error) &&
		ResolveSymbolRva(process, module_base, VMLOADER_SYMBOL_PROVIDER_LIST,
			&resolved->FirmwareTableProviderListHeadRva, error);
	SymUnloadModule64(process, module_base);
	SymCleanup(process);
	if (!success) {
		return false;
	}
	if (resolved->FirmwareTableResourceRva == resolved->FirmwareTableProviderListHeadRva ||
		!IsWritableDataRva(identity, resolved->FirmwareTableResourceRva) ||
		!IsWritableDataRva(identity, resolved->FirmwareTableProviderListHeadRva)) {
		*error = L"resolved symbols are not distinct writable kernel data";
		return false;
	}
	return true;
}

bool SetDword(HKEY key, PCWSTR name, DWORD value, std::wstring* error) {
	const LONG status = RegSetValueExW(key, name, 0, REG_DWORD,
		reinterpret_cast<const BYTE*>(&value), sizeof(value));
	if (status == ERROR_SUCCESS) {
		return true;
	}
	*error = L"write registry value " + std::wstring(name) + L" failed: " +
		Win32ErrorMessage(static_cast<DWORD>(status));
	return false;
}

bool WriteRegistryConfiguration(
	const PeIdentity& identity,
	const ResolvedSymbols& resolved,
	std::wstring* error) {
	HKEY service = nullptr;
	LONG status = RegOpenKeyExW(HKEY_LOCAL_MACHINE, kServiceRegistryPath, 0,
		KEY_CREATE_SUB_KEY | KEY_WOW64_64KEY, &service);
	if (status != ERROR_SUCCESS) {
		*error = L"open vmloader service registry key failed: " +
			Win32ErrorMessage(static_cast<DWORD>(status));
		return false;
	}

	HKEY parameters = nullptr;
	status = RegCreateKeyExW(service, VMLOADER_PARAMETERS_SUBKEY, 0, nullptr,
		REG_OPTION_NON_VOLATILE, KEY_SET_VALUE | KEY_WOW64_64KEY, nullptr,
		&parameters, nullptr);
	RegCloseKey(service);
	if (status != ERROR_SUCCESS) {
		*error = L"create symbol Parameters registry key failed: " +
			Win32ErrorMessage(static_cast<DWORD>(status));
		return false;
	}

	RegDeleteValueW(parameters, VMLOADER_VALUE_SCHEMA_VERSION);
	const bool success =
		SetDword(parameters, VMLOADER_VALUE_KERNEL_TIMESTAMP, identity.TimeDateStamp, error) &&
		SetDword(parameters, VMLOADER_VALUE_KERNEL_IMAGE_SIZE, identity.SizeOfImage, error) &&
		SetDword(parameters, VMLOADER_VALUE_KERNEL_CHECKSUM, identity.CheckSum, error) &&
		SetDword(parameters, VMLOADER_VALUE_FIRMWARE_RESOURCE_RVA,
			resolved.FirmwareTableResourceRva, error) &&
		SetDword(parameters, VMLOADER_VALUE_FIRMWARE_PROVIDER_LIST_RVA,
			resolved.FirmwareTableProviderListHeadRva, error) &&
		SetDword(parameters, VMLOADER_VALUE_SCHEMA_VERSION,
			VMLOADER_SYMBOL_SCHEMA_VERSION, error);
	if (success) {
		RegFlushKey(parameters);
	}
	RegCloseKey(parameters);
	return success;
}

bool RunSelfTest(std::wstring* error) {
	KernelModule kernel = {};
	PeIdentity identity = {};
	if (!QueryKernelModule(&kernel, error) ||
		!ReadPeIdentity(kernel.ImagePath, &identity, error)) {
		return false;
	}
	if (kernel.ImageSize != identity.SizeOfImage || identity.PdbName.empty() ||
		BuildPdbKey(identity.PdbGuid, identity.PdbAge).empty()) {
		*error = L"kernel module and PE/PDB metadata are inconsistent";
		return false;
	}
	return true;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
	bool dry_run = false;
	bool self_test = false;
	for (int i = 1; i < argc; ++i) {
		if (wcscmp(argv[i], L"--dry-run") == 0) {
			dry_run = true;
		} else if (wcscmp(argv[i], L"--self-test") == 0) {
			self_test = true;
		} else {
			std::wcerr << L"usage: vmloader_resolver.exe [--dry-run|--self-test]\n";
			return 2;
		}
	}

	std::wstring error;
	if (self_test) {
		if (!RunSelfTest(&error)) {
			std::wcerr << L"self-test failed: " << error << L"\n";
			return 1;
		}
		std::wcout << L"self-test passed\n";
		return 0;
	}

	KernelModule kernel = {};
	PeIdentity identity = {};
	if (!QueryKernelModule(&kernel, &error) ||
		!ReadPeIdentity(kernel.ImagePath, &identity, &error)) {
		std::wcerr << L"resolver failed: " << error << L"\n";
		return 1;
	}
	if (kernel.ImageSize != identity.SizeOfImage) {
		std::wcerr << L"resolver failed: the on-disk kernel does not match the running image\n";
		return 1;
	}

	std::wstring pdb_directory;
	if (!DownloadPdb(identity, &pdb_directory, &error)) {
		std::wcerr << L"resolver failed: " << error << L"\n";
		return 1;
	}

	ResolvedSymbols resolved = {};
	if (!ResolveSymbols(kernel, identity, pdb_directory, &resolved, &error)) {
		std::wcerr << L"resolver failed: " << error << L"\n";
		return 1;
	}

	std::wcout << L"kernel: " << kernel.ImagePath << L"\n"
		<< VMLOADER_SYMBOL_RESOURCE << L": RVA 0x" << std::hex
		<< resolved.FirmwareTableResourceRva << L"\n"
		<< VMLOADER_SYMBOL_PROVIDER_LIST << L": RVA 0x"
		<< resolved.FirmwareTableProviderListHeadRva << std::dec << L"\n";

	if (dry_run) {
		std::wcout << L"dry-run: registry unchanged\n";
		return 0;
	}
	if (!WriteRegistryConfiguration(identity, resolved, &error)) {
		std::wcerr << L"resolver failed: " << error << L"\n";
		return 1;
	}
	std::wcout << L"registry configuration updated\n";
	return 0;
}
