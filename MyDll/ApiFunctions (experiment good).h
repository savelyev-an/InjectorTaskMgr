#define _CRT_SECURE_NO_WARNINGS

/********************************************************************************************************
*  It Is a project "DLLIjection in TaskManager" (at tab "Processes" replace Column "Описание" with Affinity)
*  Actually it replaces Column # 6 or the last column, if the amount of the column is less than 6
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

#define EXPERIMENT

#ifndef EXPERIMENT 
#define PID unsigned

typedef LONG NTSTATUS;							//  ====> #include <ntdef.h>

int prevColumn = -1;							// 
bool isNewColumnNamed = false;					// Insert column only one time
HWND processesWindow = nullptr;					// store the windowHandle for renovation
#define MAX_PROCESSES_COUNT 1000
PID Pids[MAX_PROCESSES_COUNT] = { 0 };			// store Pids for renovation the right row when affinity changed
LVITEM storedLvItem;							// LvItem for none sequencial writing
bool isLvItemStored = false;					// IsLvItemStored

#define PID_COLUMN 1							// pid column
#define PROCCESS_WINDOW_DWSTYLE 1342177356		// mask for finding process Window
int  AffinityColumn = 6;						// column for affinity writing
int maxColumn = 0;								// counter for max_column
bool countComplete = false;						// flag for finished column counting 
bool needWriteFirstRowAffinity = false;			// flag for finished column counting and first row writing

// #include <fstream> debug
// std::ofstream file; debug
#else  // #ifndef EXPERIMENT 
#define PID unsigned

typedef LONG NTSTATUS;							//  ====> #include <ntdef.h>

int prevColumn = -1;							// 
bool isNewColumnNamed = false;					// Insert column only one time
HWND processesWindow = nullptr;					// store the windowHandle for renovation
#define MAX_PROCESSES_COUNT 1000
PID Pids[MAX_PROCESSES_COUNT] = { 0 };			// store Pids for renovation the right row when affinity changed
LVITEM storedLvItem;							// LvItem for none sequencial writing
bool isLvItemStored = false;					// IsLvItemStored

#define PROCCESS_WINDOW_DWSTYLE 1342177356		// mask for finding process Window

#define PID_COLUMN_TITLE L"ИД процесса"
#define DESCRIPTION_COLUMN_TITLE L"Описание"
#define AFFINITY_COLUMN_TITLE L"Processor Affinity"
int pidColumn = -1;							    // pid column
int  AffinityColumn = -1;						// column for affinity writing
int maxColumn = 0;								// counter for max_column
bool countComplete = false;						// flag for finished column counting 
bool needWriteFirstRowAffinity = false;			// flag for finished column counting and first row writing

 #include <fstream> // debug
 std::ofstream file; //debug

int counter = 0;
#endif // #ifndef EXPERIMENT 

#ifndef EXPERIMENT 
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
	* write the first row affinity
	*/
	static void writeFirstRowAffinity();

	/*
	* Call original function SendMessageW
	*/
	static LRESULT WINAPI callOrigSendMessageW(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

	LRESULT WINAPI hookFunction(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
	{
		// file << (int)hwnd << " ; " << msg << " ; " << wparam << " ; " << lparam << std::endl; debug
		if (hwnd == processesWindow) {
			/* debug
			if (msg == LVM_DELETECOLUMN) {
				file.close();
			}
			*/
			if (msg == LVM_INSERTITEMW || msg == LVM_SETITEMW)//Intercepts LVM_INSERTITEM and LVM_SETITEM messages
			{

				LVITEMW* lvitemw = (LVITEMW*)lparam;
				int	row = lvitemw->iItem;
				int column = lvitemw->iSubItem;
				LPWSTR text = ((LVITEMW*)lparam)->pszText;
				
				if (!countComplete) {
					if (maxColumn <= column) {
						maxColumn = column;
					}
					else {
						countComplete = true;
						needWriteFirstRowAffinity = true;
						if (AffinityColumn > maxColumn)
							AffinityColumn = maxColumn;
					}
				}


				if (column == PID_COLUMN) {
					// прочитаем и сохраним ПИД
					Pids[row] = _wtoi(text);
				}
				else if (countComplete && column == AffinityColumn) {  //change
					// Check the sequential double message to affinity 
					if (isNewColumnNamed && (prevColumn == column && row == 2)) {
						isNewColumnNamed = false;
					}

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


		if ( !isNewColumnNamed && countComplete ) {  // Change &&&& counter>0
			// this code executed in the begining and if we had 2 sequential messages to Affinity Column 
			isNewColumnNamed = true;
			setNameAffinityColumn();
		}
		
		if (needWriteFirstRowAffinity && isLvItemStored) {
			needWriteFirstRowAffinity = false;
			writeFirstRowAffinity();
		}
		
		LRESULT result = callOrigSendMessageW(hwnd, msg, wparam, lparam);//Calls the real SendMessage function.

		return result;
	}

	// this function should be executed in area of original SendMessageW
	static void setNameAffinityColumn() {
		LVCOLUMNW lvColumn;
		lvColumn.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT;
		lvColumn.fmt = LVCFMT_LEFT;
		lvColumn.cx = 75;
		lvColumn.iSubItem = AffinityColumn;
		lvColumn.pszText = (LPWSTR)L"Processor Affinity";
		callOrigSendMessageW(processesWindow, LVM_SETCOLUMNW, AffinityColumn, (LPARAM)&lvColumn);  // может удалять или реплйсить ????
		return;
	}

	// this function should be executed in area of original SendMessageW
	static void writeFirstRowAffinity() {
		LVITEM lvItem = storedLvItem;
		lvItem.iItem = 0;
		lvItem.iSubItem = AffinityColumn;
		int const bufLength = 30;
		wchar_t buffer[bufLength];
		getProcessAffinityByPID(Pids[0], buffer, bufLength);
		lvItem.pszText = buffer;
		callOrigSendMessageW(processesWindow, LVM_SETITEMW, AffinityColumn, (LPARAM)&lvItem);
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
}

#else  // #ifndef EXPERIMENT 
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
	* write the first row affinity
	*/
	static void writeFirstRowAffinity();

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
			counter++;

			if (counter == 11) {
				checkColumns();
			}

			if (msg == LVM_DELETECOLUMN || msg == LVM_INSERTCOLUMN) {
				checkColumns();
			}

			if (msg == LVM_INSERTITEMW || msg == LVM_SETITEMW)//Intercepts LVM_INSERTITEM and LVM_SETITEM messages
			{

				LVITEMW* lvitemw = (LVITEMW*)lparam;
				int	row = lvitemw->iItem;
				int column = lvitemw->iSubItem;
				LPWSTR text = ((LVITEMW*)lparam)->pszText;

				if (!countComplete) {
					if (maxColumn <= column) {
						maxColumn = column;
					}
					else {
						countComplete = true;
						needWriteFirstRowAffinity = true;
						if (AffinityColumn > maxColumn)
							AffinityColumn = maxColumn;
					}
				}


				if (column == pidColumn) {
					// прочитаем и сохраним ПИД
					Pids[row] = _wtoi(text);
				}
				else if (countComplete && column == AffinityColumn) {  //change
																	   // Check the sequential double message to affinity 
					if (isNewColumnNamed && (prevColumn == column && row == 2)) {
						isNewColumnNamed = false;
					}

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

			if (!isNewColumnNamed && countComplete) {  // Change &&&& counter>0
													   // this code executed in the begining and if we had 2 sequential messages to Affinity Column 
				isNewColumnNamed = true;
				setNameAffinityColumn();
			}

			if (needWriteFirstRowAffinity && isLvItemStored) {
				needWriteFirstRowAffinity = false;
				writeFirstRowAffinity();
			}

		}

		LRESULT result = callOrigSendMessageW(hwnd, msg, wparam, lparam);//Calls the real SendMessage function.

		return result;
	}

	// this function should be executed in area of original SendMessageW
	static void setNameAffinityColumn() {
		LVCOLUMNW lvColumn;
		lvColumn.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT;
		lvColumn.fmt = LVCFMT_LEFT;
		lvColumn.cx = 75;
		lvColumn.iSubItem = AffinityColumn;
		lvColumn.pszText = (LPWSTR)AFFINITY_COLUMN_TITLE;
		callOrigSendMessageW(processesWindow, LVM_SETCOLUMNW, AffinityColumn, (LPARAM)&lvColumn);  // может удалять или реплйсить ????
		return;
	}

	// this function should be executed in area of original SendMessageW
	static void writeFirstRowAffinity() {
		LVITEM lvItem = storedLvItem;
		lvItem.iItem = 0;
		lvItem.iSubItem = AffinityColumn;
		int const bufLength = 30;
		wchar_t buffer[bufLength];
		getProcessAffinityByPID(Pids[0], buffer, bufLength);
		lvItem.pszText = buffer;
		callOrigSendMessageW(processesWindow, LVM_SETITEMW, AffinityColumn, (LPARAM)&lvItem);
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

		int TOTAL = -1;
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
				TOTAL = i;
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
#endif // #ifndef EXPERIMENT 

#ifndef EXPERIMENT
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

		if (processesWindow == nullptr && isProcessWindow)
			processesWindow = result;
		return result;
	}
}
#else // #ifndef EXPERIMENT 
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
#endif // #ifndef EXPERIMENT 


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


