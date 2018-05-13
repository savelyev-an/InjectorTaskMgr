/********************************************************************************************************
*  It is a project "DLLIjection in TaskManager" (at tab "Processes" replace Column "Описание" with Affinity)
* ===================================
*  This is the file provides injection functionality into the TaskManager
* ===================================
*  Other components:
*  Hooker.h
*  ApiFunctions.h
********************************************************************************************************/
#include "stdafx.h"
#include "apifunctions.h"
#include "Hooker.h"
#include <iostream>
using namespace std;

HANDLE hOut = NULL;

extern "C"
{
	__declspec(dllexport) void CreateConsole()
	{
		if (hOut == NULL)
		{
			AllocConsole();
			hOut = GetStdHandle(STD_OUTPUT_HANDLE);
		}

	}

	__declspec(dllexport) void DestroyConsole()
	{
		if (hOut != NULL)
		{
			FreeConsole();
			hOut = NULL;
		}

	}

	__declspec(dllexport) void PrintOnConsole(wchar_t *pTxt)
	{
		DWORD NumberOfCharsWritten;
		WriteConsole(hOut, pTxt, lstrlen(pTxt), &NumberOfCharsWritten, NULL);
	}
}

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
					 )
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		
		// MessageBox(nullptr, L"before hookers init in dllmain", L"MyDll.dll", MB_OK); // debug

		nsSendMessageW				::hooker.initHook();
		nsCreateWindowExW			::hooker.initHook();
		nsNtSetInformationProcess	::hooker.initHook();
		// file.open("d:\\logDLLInhection.txt"); //debug
		//	MessageBox(nullptr, L"hooker init in dllmain finished", L"MyDll.dll", MB_OK); // debug
		CreateConsole();
		PrintOnConsole(L"TEST");
		MessageBox(NULL, L"Console test", L"Console test", MB_OK);
		DestroyConsole();

		cout <<endl<<endl << "BinGo!" << std::endl;

		break;
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	case DLL_PROCESS_DETACH:
	break;
	}
	return TRUE;
}

