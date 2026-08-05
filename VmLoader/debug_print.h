#pragma once

#include <ntddk.h>

#ifdef _DEBUG
#define VMLOADER_DBG_PRINT(...) DbgPrintEx(__VA_ARGS__)
#else
#define VMLOADER_DBG_PRINT(...) ((void)0)
#endif
