// Injector.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#define SIC_MARK


UCHAR g_shellcode_x64[] =
{
	/*0x00:*/ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   //pLoadLibrary pointer, RUNTIME
	/*0x08:*/ 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,   // 8x nops to fix disassembly of VS
	/*0x10:*/ 0x48, 0x83, 0xEC, 0x28,							//sub         rsp,28h
	/*0x14:*/ 0x48, 0x8D, 0x0D, 0x1D, 0x00, 0x00, 0x00,			//lea         rcx,[RIP+(38h-1Bh)]
	/*0x1B:*/ 0xFF, 0x15, 0xDF, 0xFF, 0xFF, 0xFF,				//call        qword ptr[RIP-(21h-0)]
	/*0x21:*/ 0x33, 0xC9,										//xor         ecx, ecx
	/*0x23:*/ 0x83, 0xCA, 0xFF,									//or          edx, 0FFFFFFFFh
	/*0x26:*/ 0x48, 0x85, 0xC0,									//test        rax, rax
	/*0x29:*/ 0x0F, 0x44, 0xCA,									//cmove       ecx, edx
	/*0x2C:*/ 0x8B, 0xC1,										//mov         eax, ecx
	/*0x2E:*/ 0x48, 0x83, 0xC4, 0x28,							//add         rsp, 28h
	/*0x32:*/ 0xC3,												//ret
	/*0x33:*/ 0x90, 0x90, 0x90, 0x90, 0x90						// 5x nop for alignment
	/*0x38:*/										            // String: "MyDll.dll"
};

UCHAR g_shellcode_x86[] =
{
	/*0x00:*/ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	//pLoadLibrary pointer, RUNTIME
	/*0x08:*/ 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	//pString pointer, RUNTIME
	/*0x10:*/ 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,	//8x nops to fix disassembly of VS
	/*0x18:*/ 0x68, 0xF0, 0xFF, 0xFF, 0xFF, 					//push        offset string L"kernel32.dll" (0D82108h)
	/*0x1D:*/ 0xFF, 0x15, 0xDD, 0xFF, 0xFF, 0xFF,				//call        dword ptr[g_pLoadLibraryW(0D833BCh)]
	/*0x23:*/ 0xF7, 0xD8,										//neg         eax
	/*0x25:*/ 0x1B, 0xC0,										//sbb         eax, eax
	/*0x27:*/ 0xF7, 0xD8,										//neg         eax
	/*0x29:*/ 0x48,												//dec         eax
	/*0x2A:*/ 0xC3,												//ret
	/*0x2B:*/ 0x90, 0x90, 0x90, 0x90, 0x90						//5x nop for alignment
	/*0x30:*/													//String: "MyDll.dll"
};


BOOL InjectDll(HANDLE hProcess, LPCTSTR lpFileName, PVOID pfnLoadLibrary, bool is64)
{
	BOOL ret = FALSE;
	PVOID lpShellcode_remote = NULL;
	HANDLE hRemoteThread = NULL;
	UCHAR* g_shellcode = g_shellcode_x64;
	ULONG size_g_shellcode = sizeof(g_shellcode_x64);
	if (!is64) {
		g_shellcode = g_shellcode_x86;
		size_g_shellcode = sizeof(g_shellcode_x86);
	}

	for (;;)
	{
		//allocate remote storage
		DWORD lpFileName_size = (wcslen(lpFileName) + 1) * sizeof(wchar_t);
		DWORD lpShellcode_size = size_g_shellcode + lpFileName_size;
		lpShellcode_remote = VirtualAllocEx(hProcess, NULL,
			lpShellcode_size, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
		if (NULL == lpShellcode_remote)
		{
			printf("VirtualAllocEx returns NULL \n");
			break;
		}

		//fill remote storage with actual shellcode
		SIZE_T bytesWritten;
		BOOL res = WriteProcessMemory(hProcess, lpShellcode_remote,
			g_shellcode, size_g_shellcode, &bytesWritten);
		if (FALSE == res)
		{
			printf("WriteProcessMemory failed with %d \n", GetLastError());
			break;
		}

		//fill remote storage with string
		res = WriteProcessMemory(hProcess, RVA_TO_VA(PVOID, lpShellcode_remote, size_g_shellcode),
			lpFileName, lpFileName_size, &bytesWritten);
		if (FALSE == res)
		{
			printf("WriteProcessMemory failed with %d \n", GetLastError());
			break;
		}

		//adjust pfnLoadLibrary
		DWORD PatchedPointerRVA = 0x00;
		ULONG_PTR PatchedPointerValue = (ULONG_PTR)pfnLoadLibrary;
		WriteRemoteDataType<ULONG_PTR>(hProcess,
			RVA_TO_VA(ULONG_PTR, lpShellcode_remote, PatchedPointerRVA),
			&PatchedPointerValue);

		DWORD tid;
		//in case of problems try MyLoadLibrary if this is actually current process
		hRemoteThread = CreateRemoteThread(hProcess,
			NULL, 0, (LPTHREAD_START_ROUTINE)
			RVA_TO_VA(ULONG_PTR, lpShellcode_remote, 0x10),
			lpShellcode_remote,
			0, &tid);
		if (NULL == hRemoteThread)
		{
			printf("CreateRemoteThread failed with %d \n", GetLastError());
			break;
		}

		//wait for MyDll initialization
		WaitForSingleObject(hRemoteThread, INFINITE);

		DWORD ExitCode = 0xDEADFACE;
		GetExitCodeThread(hRemoteThread, &ExitCode);
		printf("GetExitCodeThread returns %x", ExitCode);

		ret = TRUE;
		break;
	}

	if (!ret)
	{
		if (lpShellcode_remote) SIC_MARK;
		//TODO call VirtualFree(...)
	}

	if (hRemoteThread) CloseHandle(hRemoteThread);
	return ret;
}

ULONG_PTR FindEntryPoint2(REMOTE_ARGS_DEFS, bool* pis64)
{
	
	PIMAGE_NT_HEADERS pLocalPeHeader = GetLocalPeHeader(REMOTE_ARGS_CALL, pis64);   
	ULONG_PTR pRemoteEntryPoint;

	if (*pis64)
	{
		PIMAGE_NT_HEADERS64 pLocalPeHeader2 = (PIMAGE_NT_HEADERS64)pLocalPeHeader;
		pRemoteEntryPoint = RVA_TO_REMOTE_VA(
			PVOID,
			pLocalPeHeader2->OptionalHeader.AddressOfEntryPoint);
	}
	else
	{
		PIMAGE_NT_HEADERS32 pLocalPeHeader2 = (PIMAGE_NT_HEADERS32)pLocalPeHeader;
		pRemoteEntryPoint = RVA_TO_REMOTE_VA(
			PVOID,
			pLocalPeHeader2->OptionalHeader.AddressOfEntryPoint);
	}
	free(pLocalPeHeader);
	return pRemoteEntryPoint;
}

//returns entry point in remote process
ULONG_PTR GetEntryPoint(HANDLE hProcess, bool* pis64) 
{
	PROCESS_BASIC_INFORMATION pbi;
	memset(&pbi, 0, sizeof(pbi));
	DWORD retlen = 0;

	NTSTATUS Status = ZwQueryInformationProcess(
		hProcess,
		0,
		&pbi,
		sizeof(pbi),
		&retlen);

	PEB* pLocalPeb = REMOTE(PEB, (ULONG_PTR)pbi.PebBaseAddress);
	printf("from PEB: %p and %p \n", pLocalPeb->Reserved3[0], pLocalPeb->Reserved3[1]);

	ULONG_PTR PebRemoteImageBase = (ULONG_PTR)pLocalPeb->Reserved3[1]; //TODO x64 PoC only
	ULONG_PTR pRemoteEntryPoint = FindEntryPoint2(hProcess, PebRemoteImageBase, pis64);
	return pRemoteEntryPoint;
}

//returns address of LoadLibraryA in remote process
ULONG_PTR GetRemoteLoadLibraryA(REMOTE_ARGS_DEFS)
{
	//TODO move code from main
	return 0;
}


int _tmain(int argc, _TCHAR* argv[])
{
	STARTUPINFO info = { sizeof(info) };
	PROCESS_INFORMATION processInfo;
	::CreateProcess(L"C:\\Windows\\system32\\taskmgr.exe", NULL,
		NULL, NULL, TRUE, CREATE_SUSPENDED, NULL, NULL, &info, &processInfo);

	HANDLE hProcess = processInfo.hProcess;
	bool is64 = true;
	ULONG_PTR pRemoteEntryPoint = GetEntryPoint(hProcess, &is64);

	WORD OrigWord;
	WORD PatchedWord = 0xFEEB;
	ReadRemoteDataType<WORD>(hProcess, pRemoteEntryPoint, &OrigWord);
	WriteRemoteDataType<WORD>(hProcess, pRemoteEntryPoint, &PatchedWord);

	printf("notepad.exe entry point is at %p \n", pRemoteEntryPoint);

	ResumeThread(processInfo.hThread); //resume patched process;
	Sleep(1000);

	DWORD nModules;
	HMODULE* phModules = GetRemoteModules(processInfo.hProcess, &nModules);

	EXPORT_CONTEXT MyContext;
	MyContext.ModuleName = "KERNEL32.dll";
	MyContext.FunctionName = "LoadLibraryW";
	MyContext.RemoteFunctionAddress = 0;
	RemoteModuleWorker(processInfo.hProcess, phModules, nModules, FindExport, &MyContext);
	printf("kernel32!LoadLibraryW is at %p \n", MyContext.RemoteFunctionAddress);
	
	wchar_t* pDllPath = L"D:\\Magistr\\Spring\\REverse Engineering\\ProjectStudyInjection\\InjectorTaskMgr\\x64\\Debug\\MyDll.dll"; //x64
	
	InjectDll(hProcess,
		pDllPath,
		(PVOID)MyContext.RemoteFunctionAddress,
		is64);
	Sleep(1000);

	NTSTATUS Status = ZwSuspendProcess(hProcess);
	WriteRemoteDataType<WORD>(hProcess, pRemoteEntryPoint, &OrigWord);

	Status = ZwResumeProcess(hProcess);

	Sleep(5000);

	return 0;
}