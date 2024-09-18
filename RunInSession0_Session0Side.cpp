// RunInSession0_Session0Side.cpp
// 
// The portion of the RunInSession0_Framework that executes code as a service in session 0 as System.
//

#include <Windows.h>
#include <sddl.h>
#include <iostream>
#include <sstream>

#include "DbgOut.h"
#include "SysErrorMessage.h"
#include "Utilities.h"
#include "HEX.h"
#include "RunInSession0_Framework_InternalDecls.h"

/// <summary>
/// Static (module-wide) pointer to the code to execute
/// </summary>
static pfn_CodeToRunInSession0_t st_CodeToRunInSession0 = nullptr;

/// <summary>
/// Service status handle, set from ServiceMain, used by NotifySCM
/// </summary>
static SERVICE_STATUS_HANDLE hServiceStatus = nullptr;

// Forward declarations
// Standard entry points expected by the SCM:
static void WINAPI ServiceMain(DWORD dwArgc, wchar_t** lpszArgv);
static void WINAPI ServiceControlHandler(DWORD);

// Forward declarations of functions to report status to the SCM:
// Reports existing status to SCM with no changes
static BOOL NotifySCM();
// Reports new status to SCM
static BOOL NotifySCM(DWORD dwNewState);
// Reports new status to SCM with Win32 exit code
static BOOL NotifySCM(DWORD dwNewState, DWORD dwWin32ExitCode);

// ----------------------------------------------------------------------------------------------------
// ----------------------------------------------------------------------------------------------------
/// <summary>
/// For executing the code in session 0, with this process started by the Service Control Manager.
/// pfn_CodeToRunInSession0 is the pointer to the code to execute.
/// The other parameters are the names of the service and of objects the service will need to communicate 
/// back to the session X process.
/// </summary>
/// <returns>0 if successful, non-zero otherwise</returns>

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
    const wchar_t* szEvent_ServiceDone)
{
    dbgOut.locked()
        << L"ServiceExeSide("
        << (void*)pfn_CodeToRunInSession0 << L", "
        << szServiceName << L", "
        << szNamedPipe_Output << L", "
        << szNamedPipe_Error << L", "
        << szEvent_ReadyToWrite << L", "
        << szEvent_ServiceDone << L")" << std::endl;

    // Set the module-wide pointer to the code to execute
    st_CodeToRunInSession0 = pfn_CodeToRunInSession0;

    // This function's return value
    DWORD dwRetval = 0;

    // Redirect stdout and stderr to the named pipes and connect to them
    FILE* pFile1 = nullptr, * pFile2 = nullptr;
    errno_t eout = _wfreopen_s(&pFile1, szNamedPipe_Output, L"w", stdout);
    errno_t eerr = _wfreopen_s(&pFile2, szNamedPipe_Error, L"w", stderr);
    dbgOut.locked() << L"_wfreopen_s stdout result " << eout << L"; " << (void*)pFile1 << std::endl;
    dbgOut.locked() << L"_wfreopen_s stderr result " << eerr << L"; " << (void*)pFile2 << std::endl;

    // Tell the session-X side that this side has connected to the named pipes
    HANDLE hEventReadyToWrite = OpenEventW(EVENT_MODIFY_STATE, FALSE, szEvent_ReadyToWrite);
    if (NULL != hEventReadyToWrite)
    {
        dbgOut.locked() << L"Signaling ready to write" << std::endl;
        if (!SetEvent(hEventReadyToWrite))
        {
            DWORD dwLastErr = GetLastError();
            dbgOut.locked() << L"SetEvent (ready to write) failed: " << SysErrorMessageWithCode(dwLastErr) << std::endl;
        }
        else
        {
            dbgOut.locked() << L"Signaled ready to write" << std::endl;
        }
        CloseHandle(hEventReadyToWrite);
    }
    else
    {
        DWORD dwLastErr = GetLastError();
        dbgOut.locked() << L"Can't open event object to signal ready to write: " << SysErrorMessageWithCode(dwLastErr) << std::endl;
    }

    // Start the service code
    SERVICE_TABLE_ENTRYW DispatchTable[] =
    {
        { const_cast<wchar_t*>(szServiceName), ServiceMain},
        { NULL, NULL }
    };
    // If called successfully, the StartServiceCtrlDispatcherW API will not return until the service has stopped, 
    // at which point the process can exit.
    if (!StartServiceCtrlDispatcherW(DispatchTable))
    {
        dwRetval = GetLastError();
        if (ERROR_FAILED_SERVICE_CONTROLLER_CONNECT == dwRetval)
        {
            dbgOut
                << L"Error: could not connect to the service controller." << std::endl
                << L"This executable is designed to be executed as a service, and started by the Service Control Manager, not from a command line." << std::endl;
        }
        else
        {
            dbgOut.locked() << L"StartServiceCtrlDispatcherW failed: " << SysErrorMessageWithCode(dwRetval) << std::endl;
        }
    }

    dbgOut.locked() << L"Signaling that the service side is done" << std::endl;
    HANDLE hEventServiceDone = OpenEventW(EVENT_MODIFY_STATE, FALSE, szEvent_ServiceDone);
    if (NULL != hEventServiceDone)
    {
        SetEvent(hEventServiceDone);
        CloseHandle(hEventServiceDone);
    }
    else
    {
        DWORD dwLastErr = GetLastError();
        dbgOut.locked() << L"Can't open event object to signal service done: " << SysErrorMessageWithCode(dwLastErr) << std::endl;
    }

    return dwRetval;
}

/// <summary>
/// Standard entry point for a Windows service
/// </summary>
/// <param name="dwArgc"></param>
/// <param name="lpszArgv"></param>
/// <returns></returns>
static void WINAPI ServiceMain(DWORD dwArgc, wchar_t** lpszArgv)
{
    DbgOutArgcArgv(L"ServiceMain", dwArgc, lpszArgv);
    
    //TODO: Make sure the following early return is commented out when not testing this failure condition.
    // The early return is for testing purposes to break the service intentionally.
    //if (dwArgc < 100)
    //    return;

    const wchar_t* szServiceName = lpszArgv[0];

    // Register the service's control handling function
    hServiceStatus = RegisterServiceCtrlHandlerW(
        szServiceName,
        ServiceControlHandler);
    if (!hServiceStatus)
    {
        DWORD dwLastErr = GetLastError();
        dbgOut.locked() << L"RegisterServiceCtrlHandlerW failed: " << SysErrorMessageWithCode(dwLastErr) << std::endl;
        return;
    }

    // Report initial status to the SCM
    NotifySCM(SERVICE_START_PENDING);
    NotifySCM(SERVICE_RUNNING);

    DWORD dwExitCode = 0;
    if (st_CodeToRunInSession0)
    {
        // Run the specified app-specific code here in session 0 as System.
        dwExitCode = (st_CodeToRunInSession0)(dwArgc - 1, &lpszArgv[1]);
        dbgOut.locked() << L"ServiceMain: requested code completed." << std::endl;
    }
    else
    {
        dbgOut.locked() << L"No code requested to run!" << std::endl;
    }

    NotifySCM(SERVICE_STOPPED, dwExitCode);
}


/// <summary>
/// ServiceControlHandler: handle control codes sent to the service by the SCM.
/// </summary>
static void WINAPI ServiceControlHandler(DWORD dwControlCode)
{
    dbgOut.locked() << L"ServiceControlHandler, code " << dwControlCode << std::endl;

    switch (dwControlCode)
    {
        // Respond to stop or shutdown notifications
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
        NotifySCM(SERVICE_STOP_PENDING);
        // Signal the service to stop.
        //SetEvent(hServiceStopEvent);
        break;

        // Answer when asked
    case SERVICE_CONTROL_INTERROGATE:
        NotifySCM();
        break;

    default:
        break;
    }
}


/// <summary>
/// Common implementation function for the overloaded NotifySCM functions
/// </summary>
/// <param name="bKeepCurrentState">Input: whether to continue reporting previous state or to set a new state</param>
/// <param name="dwNewState">Input: new state (unless bKeepCurrentState is false)</param>
/// <param name="dwWin32ExitCode">Input: Win32 exit code</param>
/// <returns>TRUE on success, FALSE otherwise</returns>
static BOOL NotifySCM_Impl(bool bKeepCurrentState, DWORD dwNewState, DWORD dwWin32ExitCode)
{
    dbgOut.locked() << L"NotifySCM_Impl " << bKeepCurrentState << L", " << dwNewState << L", " << dwWin32ExitCode << std::endl;

    // dwCurrentState is a static value in case it needs to be remembered from previous calls
    static DWORD dwCurrentState;
    // bInPendingState used to determine whether the checkpoint number needs to be incremented during a pending state.
    static bool bInPendingState = false;
    static DWORD dwCheckpoint = 0;

    //	Wait hint is 3 seconds for pending operations; 0 for other operations
    DWORD dwWaitHint = 3000;

    switch (dwNewState)
    {
    case SERVICE_STOPPED:
    case SERVICE_RUNNING:
    case SERVICE_PAUSED:
        dwCheckpoint = 0;
        dwWaitHint = 0;
        bInPendingState = false;
        break;

    case SERVICE_START_PENDING:
    case SERVICE_STOP_PENDING:
    case SERVICE_CONTINUE_PENDING:
    case SERVICE_PAUSE_PENDING:
        // Pending operation; reset checkpoint value if starting a new pending state
        if (!bInPendingState || (!bKeepCurrentState && dwNewState != dwCurrentState))
        {
            dwCheckpoint = 1;
            bInPendingState = true;
        }
        else
        {
            ++dwCheckpoint;
        }
        break;

    default:
        // This should never happen.
        return FALSE;
    }

    if (!bKeepCurrentState)
    {
        dwCurrentState = dwNewState;
    }

    // fill in the SERVICE_STATUS structure
    SERVICE_STATUS ServiceStatus = { 0 };
    ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    ServiceStatus.dwCurrentState = dwCurrentState;
    ServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    ServiceStatus.dwWin32ExitCode = dwWin32ExitCode;
    ServiceStatus.dwServiceSpecificExitCode = 0;
    ServiceStatus.dwCheckPoint = dwCheckpoint;
    ServiceStatus.dwWaitHint = dwWaitHint;

    // send status to SCM
    return SetServiceStatus(hServiceStatus, &ServiceStatus);
}

/// <summary>
/// Reports existing status to SCM with no changes
/// </summary>
static BOOL NotifySCM()
{
    return NotifySCM_Impl(true, 0, 0);
}

/// <summary>
/// Reports new status to SCM
/// </summary>

static BOOL NotifySCM(DWORD dwNewState)
{
    return NotifySCM_Impl(false, dwNewState, 0);
}

/// <summary>
/// Reports new status to SCM with Win32 exit code
/// </summary>
static BOOL NotifySCM(DWORD dwNewState, DWORD dwWin32ExitCode)
{
    return NotifySCM_Impl(false, dwNewState, dwWin32ExitCode);
}

