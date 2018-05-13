/********************************************************************************************************
*  It Is a project "DLLIjection in TaskManager" (at tab "Processes" replace Column "Описание" with Affinity)
* ===================================
*  This is the file provided the sceleton class for Hooking: Hooker
*  data and methods of the class contains all needed for hooking
* ===================================
*  Other components:
*  ApiFunctions.h
*  dllmain.cpp
********************************************************************************************************/

#pragma once
#include "Windows.h"
#define SIZE_OF_TRAMPOLINE 6

class Hooker
{
	// Methods
public:
	
	/*
	* Only specific Hooker instances are available
	* _moduleName, _functionName  - data of original function
	* _pNewFunction               - pointer to putching function
	* The constractor just store the data to internal storage
	*/
	Hooker(_In_ LPCTSTR _moduleName, _In_ LPCSTR _functionName, LPVOID _In_ _pNewFunction);

	// Itializing the hook: do everything including making the trampoline
	void initHook();

	// Release Memory Chunck For ReWriting
	void UnLockMemory() ;

	//Put trampoline instead of calling original function
	void InsertTrampoline() ;

	// Restore original calling of Function
	void RestoreCalling() ;

	//Lock memory (restore original level of protection)
	void LockMemory() ;

	// Attributes
private:
	// Initialazation params
	_In_ LPCTSTR moduleName;
	_In_ LPCSTR  functionName;
	_In_ LPVOID  pNewFunction;
	
	//  jmp instruction
	BYTE	trampoline[SIZE_OF_TRAMPOLINE] = { 0 };
	
	// backup of the replaced bytes of oroginal function calling
	BYTE	originOpCode[SIZE_OF_TRAMPOLINE] = { 0 };
	
	
	//level of memory protection in place of calling original function
	DWORD	oldProtectMemory;

public:
	/*
	*  pointer to original function EntryPoint
	*  this data is public to provide ability to call original function in case of absense this function in headers and libs
	*/
	
	LPVOID	pOriginalCallingFunction;
};


Hooker::Hooker(_In_ LPCTSTR _moduleName, _In_ LPCSTR _functionName, LPVOID _In_ _pNewFunction) {
	moduleName   =	_moduleName;
	functionName =	_functionName;
	pNewFunction =	_pNewFunction;
}

void Hooker::initHook()
{

	pOriginalCallingFunction = GetProcAddress(GetModuleHandle(moduleName), functionName);			//get adress of the original function

	if (pOriginalCallingFunction == nullptr)															//in case of fail do not continue
	{
		wchar_t text2[100];
		wsprintf(text2, L"Original function: %s is null", functionName);
		MessageBox(nullptr, text2, L"MyDll.dll", MB_OK);
		return;
	}

	BYTE tempJmp[SIZE_OF_TRAMPOLINE] = { 0xE9, 0x90, 0x90, 0x90, 0x90, 0xC3 };					// 0xE9 = JMP 0x90 = NOP 0xC3 = RET
	memcpy(trampoline, tempJmp, SIZE_OF_TRAMPOLINE);											// store jmp instruction to 
	auto jmpSize = (DWORD(pNewFunction) - DWORD(pOriginalCallingFunction) - 5);					// calculate jump distance
	memcpy(&trampoline[1], &jmpSize, 4);														// fill the nop's with the jump distance (JMP,distance(4bytes),RET)

	UnLockMemory();
	memcpy(originOpCode, pOriginalCallingFunction, SIZE_OF_TRAMPOLINE);								// make backup
	InsertTrampoline();
	LockMemory();
	// just for debug
	// wchar_t text2[100];
	// wsprintf(text2, L"Initializing complite for %s in %ws \r\n functionCallingPointer is %p, MyFunctionPointer is %p", 
	// functionName, moduleName, pOriginalCallingFunction, pNewFunction);
	// MessageBox(nullptr, text2, L"MyDll.dll", MB_OK);
}

void Hooker::UnLockMemory() {
	VirtualProtect(LPVOID(pOriginalCallingFunction), SIZE_OF_TRAMPOLINE,								// assign read write protection
		PAGE_EXECUTE_READWRITE, &oldProtectMemory);
}

void Hooker::InsertTrampoline() {
	memcpy(pOriginalCallingFunction, trampoline, SIZE_OF_TRAMPOLINE);								// set jump instruction at the beginning of the original function
}

void Hooker::LockMemory() {
	VirtualProtect(LPVOID(pOriginalCallingFunction), SIZE_OF_TRAMPOLINE, oldProtectMemory, nullptr);	// reset protection
}

void Hooker::RestoreCalling() {
	memcpy(pOriginalCallingFunction, originOpCode, SIZE_OF_TRAMPOLINE);								// restore original function
}

