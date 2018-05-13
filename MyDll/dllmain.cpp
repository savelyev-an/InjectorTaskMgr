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
		nsNtSetInformationProcess	::hooker.initHook();
		nsCreateWindowExW			::hooker.initHook();

		// file.open("d:\\logDLLInhection.txt"); //debug
		//	MessageBox(nullptr, L"hooker init in dllmain finished", L"MyDll.dll", MB_OK); // debug
		CreateConsole();
		PrintOnConsole(L"DLLINjection Debug Window\n");
		//DestroyConsole();


		break;
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	case DLL_PROCESS_DETACH:
	break;
	}
	return TRUE;
}

