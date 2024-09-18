#pragma once

// RunInSession0_Framework_InternalDecls.h
// 
// Internal declarations for the RunInSession0_Framework.
//

// Include the public declarations first.
#include "RunInSession0_Framework.h"


/// <summary>
/// Code to execute in session X (> 0) to prepare the execution of code in session 0, start the session
/// 0 code, capture its output, then clean up.
/// </summary>
/// <param name="argc">Number of optional app-specific arguments to pass to the desired code</param>
/// <param name="argv">Optional app-specific arguments to pass to the desired code</param>
/// <param name="dwMaxSeconds">Maximum number of seconds the code should need to run in session 0 (default 30)</param>
/// <param name="hOutputDest">Where to redirect session 0's stdout (default: this process' stdout)</param>
/// <param name="hErrorDest">Where to redirect session 0's stderr (default: this process' stderr)</param>
/// <returns>0 if successful, non-zero otherwise</returns>
int SessionXSide(
    int argc,
    wchar_t** argv,
    DWORD dwMaxSeconds = 30, 
    HANDLE hOutputDest = GetStdHandle(STD_OUTPUT_HANDLE),
    HANDLE hErrorDest = GetStdHandle(STD_ERROR_HANDLE)
    );

/// <summary>
/// The Session-X side sets up this executable as a service with a specific number of 
/// command line parameters and with at least one unique "key" value.
/// </summary>
static const int nRequiredServiceExeParams = 7;
static const wchar_t* szSvcSwitch = L"-svcparams_4e4450eda4cd";

/// <summary>
/// Determines whether the command line params are intended for the session-0 service instance.
/// </summary>
/// <param name="argc">wmain's argc</param>
/// <param name="argv">wmain's argv</param>
/// <returns>true if the command line params appear to be for the service instance for Session0Side</returns>
inline bool AreServiceExeParams(int argc, wchar_t** argv)
{
    return (nRequiredServiceExeParams == argc && 0 == wcscmp(szSvcSwitch, argv[1]));
}

/// <summary>
/// For executing the code in session 0, with this process started by the Service Control Manager.
/// </summary>
/// <param name="pfn_CodeToRunInSession0">Pointer to the function to execute</param>
/// <param name="szServiceName">The name of this service</param>
/// <param name="szNamedPipe_Output">Handle of the pipe to redirect this process' stdout to</param>
/// <param name="szNamedPipe_Error">Handle of the pipe to redirect this process' stderr to</param>
/// <param name="szEvent_ReadyToWrite">Event to signal when this process has connected to the two pipes</param>
/// <param name="szEvent_ServiceDone">Event to signal when this process has completed its work</param>
/// <returns>0 on success; error code from StartServiceCtrlDispatcherW otherwise.</returns>
int ServiceExeSide(
    pfn_CodeToRunInSession0_t pfn_CodeToRunInSession0,
    const wchar_t* szServiceName,
    const wchar_t* szNamedPipe_Output,
    const wchar_t* szNamedPipe_Error,
    const wchar_t* szEvent_ReadyToWrite,
    const wchar_t* szEvent_ServiceDone
);
