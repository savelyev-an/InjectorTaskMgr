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

#include <Windows.h>
#include <stdlib.h>
#include "Commctrl.h"
#include "Hooker.h"

typedef LONG NTSTATUS;							//  ====> #include <ntdef.h>

int prevColumn = -1;							// 
bool isAffinityColumnTitled = false;			// Title affinity column flag will be set "false" during checkColumns()
HWND processesWindow = nullptr;					// store the windowHandle for renovation
#define PID unsigned
#define MAX_PROCESSES_COUNT 1000
PID Pids[MAX_PROCESSES_COUNT] = { 0 };			// store Pids for renovation the right row when affinity changed
LVITEM storedLvItem;							// LvItem for none sequencial writing
bool isLvItemStored = false;					// IsLvItemStored

#define PROCCESS_WINDOW_DWSTYLE 1342177356		// mask for finding process Window

#define PID_COLUMN_TITLE L"ИД процесса"
#define DESCRIPTION_COLUMN_TITLE L"Описание"
#define AFFINITY_COLUMN_TITLE L"Process Affinity"
int totalColumn = -1;							// used as flag for checking columns
int pidColumn = -1;							    // pid column
int  AffinityColumn = -1;						// column for affinity writing
bool needWriteFirstRowAffinity = false;			// flag for finished column counting and first row writing
bool ListViewIsReady = false;
#define LVM_SOMETHING 11						// I don't find this constant

// #include <fstream> // debug
// std::ofstream file; //debug

int counter = 0;

/*
*  The main funcionality of the project: send messages with the processors affinity
*/
namespace nsSendMessageW {

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
	static void setNameAffinityColumn();

	/*
	* Call original function SendMessageW
	*/
	static LRESULT WINAPI callOrigSendMessageW(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

	/*
	*  find aim columns
	*/
	static void checkColumns();

	LRESULT WINAPI hookFunction(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
	{
		// file << (int)hwnd << " ; " << msg << " ; " << wparam << " ; " << lparam << std::endl; debug
		if (hwnd == processesWindow) {

			if (totalColumn == -1 && (msg == LVM_INSERTITEMW || msg == LVM_SETITEMW)) { // first message come, ListViewIsReady
				ListViewIsReady = true;
			}

			if (ListViewIsReady && totalColumn == -1) {
				ListViewIsReady = false;
				checkColumns();
			}

			if (msg == LVM_DELETECOLUMN || msg == LVM_INSERTCOLUMN) {
				checkColumns();
			}

			if (!isAffinityColumnTitled && totalColumn != -1) {
				isAffinityColumnTitled = true;
				setNameAffinityColumn();
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
				prevColumn = column;
			};

		}

		LRESULT result = callOrigSendMessageW(hwnd, msg, wparam, lparam);//Calls the real SendMessage function.

		return result;
	}

	static void setNameAffinityColumn() {
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

	static void checkColumns() {
		HWND hWndHdr = (HWND)SendMessage(processesWindow, LVM_GETHEADER, 0, 0);
		int columnCount = (int)SendMessage(hWndHdr, HDM_GETITEMCOUNT, 0, 0L); //items of ListView header are columns
		int itemCount = SendMessage(processesWindow, LVM_GETITEMCOUNT, 0, 0);
		for (int i = 1; i<20; i++) {
			LVCOLUMN lvc;
			lvc.mask = LVCF_TEXT;
			lvc.pszText = new wchar_t[255];
			lvc.cchTextMax = 255;
			wcscpy_s(lvc.pszText, 30, L"");

			SendMessage(processesWindow, LVM_GETCOLUMN, i, (LPARAM)&lvc);

			if (wcscmp(lvc.pszText, PID_COLUMN_TITLE ) == 0) {
				pidColumn = i;
			}

			if (wcscmp(lvc.pszText, DESCRIPTION_COLUMN_TITLE)			== 0||
				wcscmp(lvc.pszText, AFFINITY_COLUMN_TITLE)	== 0 )
				AffinityColumn = i;

			if (wcscmp(lvc.pszText, L"") == 0) {
				totalColumn = i;
				break;
			}
		}

		//file.open("d:\\logDLLInhection.txt"); //debug
		//file << AffinityColumn << std::endl;
		//file << pidColumn << std::endl;
		//file << TOTAL << std::endl;
		//file.close();
	}

}

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
			isProcessWindow = true;
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
					LVITEM lvItem = storedLvItem;
					lvItem.iItem = row;
					lvItem.iSubItem = AffinityColumn;
					lvItem.pszText = (LPWSTR)L"this String will be replaced by nsSendMessageW::hookFunction";
					SendMessageW(processesWindow, LVM_SETITEMW, AffinityColumn, (LPARAM)&lvItem);
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
			if (Pids[i] == pid) {
				result = i;
				break;
			}
		}
		return result;
	}
};


