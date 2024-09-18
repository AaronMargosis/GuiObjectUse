#pragma once

#include <string>

/// <summary>
/// Creates a GUID and returns it in string form.
/// </summary>
/// <returns>GUID in string format</returns>
std::wstring CreateNewGuidString();

// --------------------------------------------------------------------------------------------------------------
/// <summary>
/// Indicates whether the current process is running in session 0.
/// </summary>
/// <param name="bInSession0">If function is successful, bInSession0 is set to true if current process is in session 0; false otherwise.</param>
/// <returns>true if function is able to determine which session the current process is running in</returns>
bool InSession0(bool& bInSession0);

/// <summary>
/// Get the WTS session ID of the current process.
/// </summary>
/// <param name="dwSessionID">Output: current process' WTS session ID</param>
/// <param name="sErrorInfo">Output: error information if something goes wrong</param>
/// <returns>true if successful, false otherwise</returns>
bool GetCurrentTSSessionID(DWORD& dwSessionID, std::wstring& sErrorInfo);

// --------------------------------------------------------------------------------------------------------------
/// <summary>
/// Gets the PPID -- the PID of the parent process of the input child process.
/// </summary>
/// <param name="hProcess">Input: Handle to the child process</param>
/// <param name="sErrorInfo">Output: error info on failure</param>
/// <returns>The PID of the parent process; returns 0 if the PPID cannot be retrieved.</returns>
ULONG_PTR GetParentPID(HANDLE hProcess, std::wstring& sErrorInfo);

