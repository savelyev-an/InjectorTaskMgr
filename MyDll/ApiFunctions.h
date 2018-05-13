/********************************************************************************************************
*  It Is a project "DLLIjection in TaskManager" (at tab "Processes" replace Column "Описание" with Affinity)
* ===================================
*  This file provides funcionality for hookers of the Taskmgr.exe (windows 8.1) size  on disk (1 241 088 bytes)
*  Each hooker is a separate namespace. Inside each namespace there are:
*  1) Hooker object (hooker)- it provide trampoline and replacing original function call
*  2) hookFunction - it provide the reengineered behavior
*  Also, each namespace can contain  assistive functions
* ===================================
*  Other components:
*  Hooker.h
*  dllmain.cpp
********************************************************************************************************/
#pragma once
#include <Windows.h>
#include <stdlib.h>
#include "Commctrl.h"
#include "Hooker.h"

typedef LONG NTSTATUS;							//  ====> #include <ntdef.h>
HANDLE hOut = NULL;								// 

extern "C"
// http://www.sql.ru/forum/214050/dll-i-konsol 
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

/*
*  The main funcionality of the project: send messages with the processors affinity
*/
namespace nsSendMessageW {
#ifndef JUST_CONTAINER
#define PROCCESS_WINDOW_DWSTYLE 1342177356		// mask for finding process Window
	HWND processesWindow = nullptr;					// store the windowHandle for renovation
	bool isAffinityColumnTitled = false;			// Title affinity column flag will be set "false" during checkColumns()
#define PID unsigned
#define MAX_PROCESSES_COUNT 1000
	PID Pids[MAX_PROCESSES_COUNT] = { 0 };			// store Pids for renovation the right row when affinity changed
#define NAME_COLUMN_TITLE L"Имя"
#define PID_COLUMN_TITLE L"ИД процесса"
#define DESCRIPTION_COLUMN_TITLE L"Описание"
#define AFFINITY_COLUMN_TITLE L"Process Affinity"
	int pidColumn = -1;							    // pid column
	int  AffinityColumn = -1;						// column for affinity writing

	LVITEM storedLvItem;							// LvItem for none sequencial writing
	bool isLvItemStored = false;					// IsLvItemStored
#endif

	/*
	* Hooking function
	*/
	LRESULT WINAPI hookFunction(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

	Hooker hooker = Hooker(L"User32.dll", "SendMessageW", hookFunction);

	/*
	* safe list of processor affinity
	*/
	static bool getProcessAffinityByPID(IN PID processID, _Out_ wchar_t* buffer, IN int bufLength);

	/*
	* inserting (replacing) new column
	*/
	static void titleAffinityColumn();

	/*
	* Call original function SendMessageW
	*/
	static LRESULT WINAPI callOrigSendMessageW(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

	/*
	*  find aim columns
	*/
	static void checkColumns(HWND hwnd);

	LRESULT WINAPI hookFunction(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
	{
		// check the window
		if ((processesWindow == nullptr) && (msg == LVM_INSERTITEMW || msg == LVM_SETITEMW)) { // first message come, ListViewIsReady
			checkColumns(hwnd);
		}

		if (hwnd == processesWindow) {

			if (!isAffinityColumnTitled && processesWindow == hwnd) {
				isAffinityColumnTitled = true;
				titleAffinityColumn();
			}

			if (msg == LVM_INSERTITEMW || msg == LVM_SETITEMW)//Intercepts LVM_INSERTITEM and LVM_SETITEM messages
			{
				LVITEMW* lvitemw = (LVITEMW*)lparam;
				int	row = lvitemw->iItem;
				int column = lvitemw->iSubItem;
				LPWSTR text = ((LVITEMW*)lparam)->pszText;

				if (column == pidColumn) {
					// прочитаем и сохраним ПИД
					Pids[row] = _wtoi(text);
				}
				else if (column == AffinityColumn) {  //change countComplete && 

					// save params for nsNtSetInformationProcess
					if (!isLvItemStored) {
						storedLvItem = *lvitemw;
						isLvItemStored = true;
					}

					//place the affinity
					int const bufLength = 30;
					wchar_t buffer[bufLength];
					getProcessAffinityByPID(Pids[row], buffer, bufLength);
					((LVITEMW*)lparam)->pszText = buffer;
				}
			};
		}

		LRESULT result = callOrigSendMessageW(hwnd, msg, wparam, lparam);//Calls the real SendMessage function.
		
		if (hwnd == processesWindow && (msg == LVM_DELETECOLUMN || msg == LVM_INSERTCOLUMN)) {
			checkColumns(processesWindow);
		}

		return result;
	}

	static void titleAffinityColumn() {
		LVCOLUMNW lvColumn;
		lvColumn.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT ;
		lvColumn.fmt = LVCFMT_LEFT;
		lvColumn.cx = 75;
		lvColumn.iSubItem = AffinityColumn;
		lvColumn.pszText = (LPWSTR)AFFINITY_COLUMN_TITLE;
		callOrigSendMessageW(processesWindow, LVM_SETCOLUMNW, AffinityColumn, (LPARAM)&lvColumn); 
		return;
	}

	static bool getProcessAffinityByPID(IN PID processID, _Out_ wchar_t* buffer, IN int bufLength) {
		HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION |
			PROCESS_VM_READ,
			FALSE, processID);
		bool result = false;
		wsprintf(buffer, L"\0");
		if (hProcess) {
			DWORD_PTR process_mask = 0;
			DWORD_PTR system_mask = 0;
			unsigned count = 0;
			unsigned cursor = 0;
			wchar_t bufTemp[6];
			bool notFirstProc = false;
			bool status_ok = GetProcessAffinityMask(hProcess, &process_mask, &system_mask);
			if (status_ok) {
				result = true;
				for (int i = 0; i<32; i++) {
					count++;
					if (process_mask & 1) {
						// add one processor
						if (notFirstProc) {
							wsprintf(bufTemp, L",%d\0", count);
						}
						else {
							wsprintf(bufTemp, L"%d\0", count);
							notFirstProc = true;
						}
						//flush to buffer
						wcscat_s(buffer, bufLength, bufTemp);
					}
					process_mask >>= 1;
				}
			}
		}
		if (!result)
			wsprintf(buffer, L"NO DATA\0");
		return result;
	}

	static LRESULT WINAPI callOrigSendMessageW(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
		hooker.UnLockMemory();
		hooker.RestoreCalling();
		LRESULT result = SendMessageW(hwnd, msg, wparam, lparam);//Calls the real SendMessage function.
		hooker.InsertTrampoline();
		hooker.LockMemory();
		return result;
	}

	static void checkColumns(HWND hWnd) {
		wchar_t buffer[200];
		wsprintf(buffer, L"\n checkColumns started \n");
		PrintOnConsole(buffer);

		// если у нас не заполнено processWindow должны попоробовать взять //проверяем наличие Name и PID
		HWND hWndHdr = (HWND)SendMessage(hWnd, LVM_GETHEADER, 0, 0);
		int columnCount = (int)SendMessage(hWndHdr, HDM_GETITEMCOUNT, 0, 0L); //items of ListView header are columns
		// int itemCount = SendMessage(hWnd, LVM_GETITEMCOUNT, 0, 0); count all items
		int nameColumn = -1;
		AffinityColumn = -1;

		for (int i = 0; i<columnCount; i++) {
			LVCOLUMN lvc;
			lvc.mask = LVCF_TEXT ;
			lvc.pszText = new wchar_t[255];
			lvc.cchTextMax = 255;
			wcscpy_s(lvc.pszText, 30, L"");
			lvc.iSubItem = 0;
			SendMessage(hWnd, LVM_GETCOLUMN, i, (LPARAM)&lvc);
			PrintOnConsole(lvc.pszText);
			wsprintf(buffer, L" i= %d iSubItem=%d \n", i, lvc.iSubItem);
			PrintOnConsole(buffer);

			if (wcscmp(lvc.pszText, NAME_COLUMN_TITLE) == 0) {
				nameColumn = i;
			}

			if (wcscmp(lvc.pszText, PID_COLUMN_TITLE ) == 0) {
				pidColumn = i;
			}

			if (wcscmp(lvc.pszText, DESCRIPTION_COLUMN_TITLE)	== 0||
				wcscmp(lvc.pszText, AFFINITY_COLUMN_TITLE)		== 0 )
				AffinityColumn = i;

			if (wcscmp(lvc.pszText, L"") == 0) {
				break;
			}
		}

		if (processesWindow == nullptr && nameColumn != -1 && pidColumn != -1) {
			processesWindow = hWnd;
		}

		if (AffinityColumn != -1) {
			isAffinityColumnTitled = false;
		}

		wsprintf(buffer, L"checkColumns done \n columnCount=%d   \n", columnCount);
		PrintOnConsole(buffer);
		wsprintf(buffer, L"nameCount=%d pidColumn=%d  affinityColumn=%d \n", nameColumn, pidColumn, AffinityColumn);
		PrintOnConsole(buffer);
	}
}


/*
* Provide the rewriting for the action of the changing affinity
*/
namespace nsNtSetInformationProcess {
	/*
	* NtSetInformationProcess is an undocumented function from ntdll.dll
	* signature is taken from:
	* http://hex.pp.ua/nt/NtSetInformationProcess.php
	* with hook is used only for check was data Affinity for some process changed, to provide rewrite the data;
	*/

	// Forward declaration
	NTSTATUS hookFunction( // This fuction is called only when i try to close the app
		IN HANDLE						ProcessHandle,
		IN PROCESS_INFORMATION_CLASS	ProcessInformationClass,
		IN PVOID						ProcessInformation,
		IN ULONG						ProcessInformationLength);

	Hooker hooker = Hooker(L"ntdll.dll", "NtSetInformationProcess", hookFunction);

	static int findRowByPID(PID pid);

	NTSTATUS hookFunction( // This fuction is called only when i try to close the app
		IN HANDLE						ProcessHandle,
		IN PROCESS_INFORMATION_CLASS	ProcessInformationClass,
		IN PVOID						ProcessInformation,
		IN ULONG						ProcessInformationLength
	) {
		DWORD_PTR processMask_1 = 0;
		DWORD_PTR processMask_2 = 0;
		DWORD_PTR system_mask = 0;
		GetProcessAffinityMask(ProcessHandle, &processMask_1, &system_mask);
		hooker.UnLockMemory();
		hooker.RestoreCalling();
		// Here we should execute the <NtSetInformationProcess>
		// we haven't it in compile time, but we have the Runtime pointer to this function :)
		// sample get from : https://gist.github.com/OsandaMalith/8e6fcfedcbfcb5da4003
		typedef
			NTSTATUS(NTAPI* NtSeInfoProcessFUNCTYPE)(IN HANDLE ProcessHandle, IN ULONG ProcessInformationClass, IN PVOID ProcessInformation, IN ULONG ProcessInformationLength);

		// deifne the function
		NtSeInfoProcessFUNCTYPE NtSetInformationProcess = (NtSeInfoProcessFUNCTYPE)hooker.pOriginalCallingFunction;

		// use function NtSetInformationProcess:
		NTSTATUS result = (*NtSetInformationProcess)(ProcessHandle, ProcessInformationClass, ProcessInformation, ProcessInformationLength);

		hooker.InsertTrampoline();
		hooker.LockMemory();

		GetProcessAffinityMask(ProcessHandle, &processMask_2, &system_mask);

			if (processMask_1 != processMask_2) {

				PID pid = GetProcessId(ProcessHandle);
				// запустим сообщение в нужную ячейку, affinity будет получено уже внутри nsSendMessage
				// определим строку
				int row = findRowByPID(pid);
				// отправим данные 
				if (row != -1) {
					LVITEM lvItem = nsSendMessageW::storedLvItem;
					lvItem.iItem = row;
					lvItem.iSubItem = nsSendMessageW::AffinityColumn;
					lvItem.pszText = (LPWSTR)L"this String will be replaced by nsSendMessageW::hookFunction";
					SendMessageW(processesWindow, LVM_SETITEMW, nsSendMessageW::AffinityColumn, (LPARAM)&lvItem);
					// debug
					//wchar_t affinitySring[30];
					//nsSendMessageW::getProcessAffinityByPID(pid, affinitySring, 30);
					//wchar_t text2[150];
					//wsprintf(text2, L"NtSetInformationProcess in progress!!  pid =%d, r\n\ affinityString =%ws", pid, affinitySring);
					//MessageBox(nullptr, text2, L"MyDll.dll", MB_OK);
				}
			}
		return result;
	};

	static int findRowByPID(PID pid) {
		int result = -1; // this is an error message
		for (int i = 0; i < MAX_PROCESSES_COUNT; i++) {
			if (nsSendMessageW::Pids[i] == pid) {
				result = i;
				break;
			}
		}
		return result;
	}
};

/*
*  prohibit sorting at the processes window
*  and findig the processes window
*/
namespace nsCreateWindowExW {
	// Forward declaration
	HWND WINAPI hookFunction(
		_In_     DWORD     dwExStyle,
		_In_opt_ LPCTSTR   lpClassName,
		_In_opt_ LPCTSTR   lpWindowName,
		_In_     DWORD     dwStyle,
		_In_     int       x,
		_In_     int       y,
		_In_     int       nWidth,
		_In_     int       nHeight,
		_In_opt_ HWND      hWndParent,
		_In_opt_ HMENU     hMenu,
		_In_opt_ HINSTANCE hInstance,
		_In_opt_ LPVOID    lpParam
	);

	Hooker hooker = Hooker(L"User32.dll", "CreateWindowExW", hookFunction);

	HWND WINAPI hookFunction(
		_In_     DWORD     dwExStyle,
		_In_opt_ LPCTSTR   lpClassName,
		_In_opt_ LPCTSTR   lpWindowName,
		_In_     DWORD     dwStyle,
		_In_     int       x,
		_In_     int       y,
		_In_     int       nWidth,
		_In_     int       nHeight,
		_In_opt_ HWND      hWndParent,
		_In_opt_ HMENU     hMenu,
		_In_opt_ HINSTANCE hInstance,
		_In_opt_ LPVOID    lpParam
	) {
		/*
		file <<"Window creation: " << " ; " << (int)result << " ; " << dwExStyle << " ; " << lpClassName << " ; " << lpWindowName
		<< " ; " << dwStyle << " ; " << hWndParent  << " ; " << hMenu << " ; " << hInstance << " ; " << lpParam <<std::endl;
		*/
		bool isProcessWindow = false;
		// find the processWindow
		if (dwStyle == PROCCESS_WINDOW_DWSTYLE) {
			dwStyle = dwStyle | LVS_NOSORTHEADER;  // prohibit sorting
		}

		hooker.UnLockMemory();
		hooker.RestoreCalling();
		HWND result = CreateWindowExW(
			dwExStyle,
			lpClassName,
			lpWindowName,
			dwStyle,
			x,
			y,
			nWidth,
			nHeight,
			hWndParent,
			hMenu,
			hInstance,
			lpParam);
		hooker.InsertTrampoline();
		hooker.LockMemory();

		if (processesWindow == nullptr && isProcessWindow) {
			processesWindow = result;
		}
		return result;
	}
}