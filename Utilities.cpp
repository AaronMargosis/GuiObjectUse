// Utility functions

// Need to define WIN32_NO_STATUS temporarily when including both Windows.h and ntstatus.h
#define WIN32_NO_STATUS
#include <Windows.h>
#undef WIN32_NO_STATUS
#include <ntstatus.h>
#include <rpc.h>
#pragma comment(lib, "Rpcrt4.lib")
#include "NtInternal.h"
#include "SysErrorMessage.h"
#include "StringUtils.h"
#include "Utilities.h"

// --------------------------------------------------------------------------------------------------------------
/// <summary>
/// Creates a GUID and returns it in string form.
/// </summary>
/// <returns>GUID in string format</returns>
std::wstring CreateNewGuidString()
{
	std::wstring retval;
	GUID guid;
	RPC_WSTR pWstr = NULL;
	if (RPC_S_OK == UuidCreate(&guid) && RPC_S_OK == UuidToStringW(&guid, &pWstr))
	{
		retval = (const wchar_t*)pWstr;
		RpcStringFreeW(&pWstr);
	}
	return retval;
}

// --------------------------------------------------------------------------------------------------------------
/// <summary>
/// Indicates whether the current process is running in session 0.
/// </summary>
/// <param name="bInSession0">If function is successful, bInSession0 is set to true if current process is in session 0; false otherwise.</param>
/// <returns>true if function is able to determine which session the current process is running in</returns>
bool InSession0(bool& bInSession0)
{
	DWORD dwSessionID = 0;
	if (ProcessIdToSessionId(GetCurrentProcessId(), &dwSessionID))
	{
		bInSession0 = (0 == dwSessionID);
		return true;
	}
	return false;
}

// --------------------------------------------------------------------------------------------------------------
/// <summary>
/// Get the WTS session ID of the current process.
/// </summary>
/// <param name="dwSessionID">Output: current process' WTS session ID</param>
/// <param name="sErrorInfo">Output: error information if something goes wrong</param>
/// <returns>true if successful, false otherwise</returns>
bool GetCurrentTSSessionID(DWORD& dwSessionID, std::wstring& sErrorInfo)
{
	sErrorInfo.clear();
	if (ProcessIdToSessionId(GetCurrentProcessId(), &dwSessionID))
	{
		return true;
	}
	else
	{
		sErrorInfo = SysErrorMessageWithCode();
		return false;
	}

	//bool retval = false;
	//HANDLE hToken = NULL;
	//if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken))
	//{
	//	sErrorInfo = SysErrorMessageWithCode();
	//}
	//else
	//{
	//	DWORD dwRetLen = 0;
	//	if (!GetTokenInformation(hToken, TokenSessionId, &dwSessionID, sizeof(dwSessionID), &dwRetLen))
	//	{
	//		sErrorInfo = SysErrorMessageWithCode();
	//	}
	//	else
	//	{
	//		retval = true;
	//	}
	//	CloseHandle(hToken);
	//}
	//return retval;
}

// --------------------------------------------------------------------------------------------------------------

/// <summary>
/// Gets the PPID -- the PID of the parent process of the input child process.
/// </summary>
/// <param name="hProcess">Input: Handle to the child process</param>
/// <param name="sErrorInfo">Output: error info on failure</param>
/// <returns>The PID of the parent process; returns 0 if the PPID cannot be retrieved.</returns>
ULONG_PTR GetParentPID(HANDLE hProcess, std::wstring& sErrorInfo)
{
	sErrorInfo.clear();
	// Acquire pointers to ntdll interfaces
	HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
	if (nullptr == ntdll)
		return 0;
	pfn_NtQueryInformationProcess_t NtQueryInformationProcess = (pfn_NtQueryInformationProcess_t)GetProcAddress(ntdll, "NtQueryInformationProcess");
	if (nullptr == NtQueryInformationProcess)
		return 0;

	PROCESS_EXTENDED_BASIC_INFORMATION processExtBasicInfo = { 0 };
	processExtBasicInfo.Size = sizeof(processExtBasicInfo);
	ULONG infoLen = ULONG(sizeof(processExtBasicInfo));
	NTSTATUS ntStat = NtQueryInformationProcess(hProcess, ProcessBasicInformation, &processExtBasicInfo, infoLen, &infoLen);
	if (STATUS_SUCCESS == ntStat)
	{
		return processExtBasicInfo.BasicInfo.InheritedFromUniqueProcessId;
	}
	else
	{
		sErrorInfo = SysErrorMessage(ntStat, true);
		return 0;
	}
}