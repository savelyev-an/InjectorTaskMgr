// pestaff.cpp : works with remote memory
//

#include "stdafx.h"

//do not call this function with  suspended hProcess
//free() return value
HMODULE* GetRemoteModules(HANDLE hProcess, DWORD* pnModules)
{
	DWORD cbNeeded, cb = 1 * sizeof(HMODULE);
	HMODULE* phModules = nullptr;
	for (;;)
	{
		phModules = (HMODULE*)malloc(cb);
		BOOL res = EnumProcessModulesEx(
			hProcess,
			phModules,
			cb,
			&cbNeeded,
			LIST_MODULES_ALL);
		if (res == 0)
		{
			printf("EnumProcessModulesEx returns %d, cbNeeded is %d \n",
				GetLastError(), cbNeeded);
			free(phModules);
			return nullptr;
		}

		if (cb == cbNeeded)
		{
			printf("Success, cbNeeded is %d \n", cbNeeded);
			break;
		}

		free(phModules);
		cb = cbNeeded;
		continue;
	}

	*pnModules = cb / sizeof(HMODULE);
	return phModules;
}

//execute custom callback for each worker
void RemoteModuleWorker(HANDLE hProcess, HMODULE* phModules, DWORD nModules,
	ModuleCallback Worker, PVOID WorkerContext)
{
	for (int i = 0; i < nModules; i++)
	{
		ULONG_PTR module = (ULONG_PTR)(phModules[i]); \
			printf("module %d at %p \n", i, module);
		if (!Worker(hProcess, module, WorkerContext)) break;
	}
}

PIMAGE_NT_HEADERS GetLocalPeHeader(REMOTE_ARGS_DEFS, bool* pis64)
{
	PIMAGE_DOS_HEADER pLocalDosHeader = REMOTE(IMAGE_DOS_HEADER, pRemoteImageBase);
	ULONG_PTR pRemotePeHeader =
		RVA_TO_REMOTE_VA(PIMAGE_NT_HEADERS, pLocalDosHeader->e_lfanew);
	PIMAGE_NT_HEADERS pLocalPeHeader = REMOTE(IMAGE_NT_HEADERS, pRemotePeHeader);

	switch (pLocalPeHeader->FileHeader.Machine)
	{
	case IMAGE_FILE_MACHINE_I386:
		assert(sizeof(IMAGE_OPTIONAL_HEADER32) ==
			pLocalPeHeader->FileHeader.SizeOfOptionalHeader);
		{
			*pis64 = false;
			PIMAGE_NT_HEADERS32 pLocalPeHeader2 = REMOTE(IMAGE_NT_HEADERS32, pRemotePeHeader);
			free(pLocalPeHeader);
			return (PIMAGE_NT_HEADERS)pLocalPeHeader2;
		}
	case IMAGE_FILE_MACHINE_AMD64:
		assert(sizeof(IMAGE_OPTIONAL_HEADER64) ==
			pLocalPeHeader->FileHeader.SizeOfOptionalHeader);
		{
			*pis64 = true;
			PIMAGE_NT_HEADERS64 pLocalPeHeader2 = REMOTE(IMAGE_NT_HEADERS64, pRemotePeHeader);
			free(pLocalPeHeader);
			return (PIMAGE_NT_HEADERS)pLocalPeHeader2;
		}
	default:
		printf("Unsupported hardware platfrom, Machine is %d",
			pLocalPeHeader->FileHeader.Machine);
		free(pLocalPeHeader);
		return nullptr;
	}
}

bool FindExport(REMOTE_ARGS_DEFS, PVOID Context)
{
	PEXPORT_CONTEXT ExportContext = (PEXPORT_CONTEXT)Context;
	ExportContext->RemoteFunctionAddress = 0;
	bool bFound = false; //have I found export in this module?
	bool bIterateMore = true; //should we iterate next module?

	bool is64;
	PIMAGE_NT_HEADERS pLocalPeHeader = GetLocalPeHeader(REMOTE_ARGS_CALL, &is64);
	ULONG_PTR pRemoteImageExportDescriptor;

	if (is64)
	{
		PIMAGE_NT_HEADERS64 pLocalPeHeader2 = (PIMAGE_NT_HEADERS64)pLocalPeHeader;
		pRemoteImageExportDescriptor = RVA_TO_REMOTE_VA(
			PIMAGE_EXPORT_DIRECTORY,
			pLocalPeHeader2->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress);
	}
	else
	{
		PIMAGE_NT_HEADERS32 pLocalPeHeader2 = (PIMAGE_NT_HEADERS32)pLocalPeHeader;
		pRemoteImageExportDescriptor = RVA_TO_REMOTE_VA(
			PIMAGE_EXPORT_DIRECTORY,
			pLocalPeHeader2->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress);
	}
	free(pLocalPeHeader);

	PIMAGE_EXPORT_DIRECTORY pLocalImageExportDescriptor =
		REMOTE(IMAGE_EXPORT_DIRECTORY, pRemoteImageExportDescriptor);

	char* pLocalDllName = nullptr;
	DWORD* pLocalNames = nullptr;
	DWORD* pLocalAddrs = nullptr;

	for (;;)
	{
		if (!pLocalImageExportDescriptor) break; //no export table, iterate next module

		ULONG_PTR pRemoteDllName = RVA_TO_REMOTE_VA(
			char*,
			pLocalImageExportDescriptor->Name);
		pLocalDllName = REMOTE_ARRAY_ZEROENDED_NOLEN(char, pRemoteDllName);

		printf("dllName is %s \n", pLocalDllName);

		if (0 != strcmp(ExportContext->ModuleName, pLocalDllName)) break; //not our dll, iterate next module
		bIterateMore = false; //we've found our dll no need to iterate more modules

		ULONG_PTR pRemoteNames =
			RVA_TO_REMOTE_VA(DWORD*, pLocalImageExportDescriptor->AddressOfNames);
		ULONG_PTR pRemoteAddrs =
			RVA_TO_REMOTE_VA(DWORD*, pLocalImageExportDescriptor->AddressOfFunctions);

		pLocalNames = REMOTE_ARRAY_FIXED(DWORD, pRemoteNames, pLocalImageExportDescriptor->NumberOfNames);
		pLocalAddrs = REMOTE_ARRAY_FIXED(DWORD, pRemoteAddrs, pLocalImageExportDescriptor->NumberOfFunctions);

		if (pLocalImageExportDescriptor->NumberOfNames != pLocalImageExportDescriptor->NumberOfFunctions)
		{
			printf("FindExport: ERROR: VERY STRANGE mismatch NumberOfNames vs NumberOfFunctions \n");
			//TODO printf args ...
			break;
		}

		for (int i = 0; i < pLocalImageExportDescriptor->NumberOfNames; i++)
		{
			ULONG_PTR pRemoteString = RVA_TO_REMOTE_VA(char*, pLocalNames[i]);
			char* pLocalString = REMOTE_ARRAY_ZEROENDED_NOLEN(char, pRemoteString);

			//printf("Function: %s at %p \n", pLocalString, pLocalAddrs[i]);

			if (0 == strcmp(ExportContext->FunctionName, pLocalString))
			{
				bFound = true; //stop iterating, we found it
				ExportContext->RemoteFunctionAddress = pRemoteImageBase + pLocalAddrs[i];
				free(pLocalString);
				break;
			}
			free(pLocalString);
		}

		break;
	}

	if ((!bIterateMore)&(!bFound))
	{
		printf("FindExport: ERROR: VERY STRANGE function was not found \n");
	}

	if (pLocalNames) free(pLocalNames);
	if (pLocalAddrs) free(pLocalAddrs);
	if (pLocalImageExportDescriptor) free(pLocalImageExportDescriptor);
	if (pLocalDllName) free(pLocalDllName);
	return bIterateMore;
}
