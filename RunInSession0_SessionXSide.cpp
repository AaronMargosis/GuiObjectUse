// RunInSession0_SessionXSide.cpp
// 
// The portion of the RunInSession0_Framework that executes code in the current interactive desktop session.
// It configures a Windows service that runs an instance of this executable with specific parameters so that it can
// communicate back to this process.
//

#include <Windows.h>
#include <sddl.h>
#include <iostream>
#include <sstream>
#include <vector>

#include "DbgOut.h"
#include "SysErrorMessage.h"
#include "Utilities.h"
#include "HEX.h"
#include "RunInSession0_Framework_InternalDecls.h"

/// <summary>
/// Struct to be passed to threads redirecting data coming from session 0 process' stdout and stderr
/// </summary>
typedef struct _SourceDest_t {
    HANDLE hSource = nullptr, hDestination = nullptr;
} SourceDest_t;
/// <summary>
/// Forward declaration of thread function monitoring named pipes
/// </summary>
static DWORD WINAPI PipeMonitorThread(LPVOID lpvThreadParameter);

// ----------------------------------------------------------------------------------------------------
// ----------------------------------------------------------------------------------------------------

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
    DWORD dwMaxSeconds, // = 30,
    HANDLE hOutputDest, // = GetStdHandle(STD_OUTPUT_HANDLE),
    HANDLE hErrorDest   // = GetStdHandle(STD_ERROR_HANDLE)
)
{
    DbgOutArgcArgv(L"SessionXSide", argc, argv);

    // Variables declared up front so "goto" doesn't skip over initialization and trigger compiler warnings
    int retval = 0;

    SC_HANDLE
        hSCManager = nullptr,
        hService = nullptr;
    HANDLE
        // Don't start reading from pipe until session-0 side has connected
        hEventReadyToWrite = nullptr,
        // Event signaled when the session-0 side has completed its work
        hEventServiceDone = nullptr,
        // Named pipe for session-0 side's stdout
        hOutput = nullptr,
        // Named pipe for session-0 sides' stderr
        hError = nullptr,
        // Thread that reads from hOutput
        hThreadOutput = nullptr,
        // Thread that reads from hError
        hThreadError = nullptr;
    // Set security descriptor on named pipes
    PSECURITY_DESCRIPTOR 
        pSD = nullptr;

    SECURITY_ATTRIBUTES sa = { 0 };
    DWORD dwWait;
    BOOL ret;

    // Prevent arithmetic overflow converting seconds to milliseconds.
    // If wait greater than about 49 days, make it infinite.
    DWORD dwMaxMilliseconds = (dwMaxSeconds >= 4294967) ? INFINITE : (dwMaxSeconds * 1000);
    dbgOut.locked() << L"SessionXSide, dwMaxMilliseconds = " << dwMaxMilliseconds << L" (0x" << HEX(dwMaxMilliseconds) << L")" << std::endl;

    // Get a handle to the service control manager.
    // This is also the "is-admin" test, so do it early. This is the most likely function to fail, so do it early.
    hSCManager = OpenSCManagerW(
        NULL,                    // local computer
        NULL,                    // ServicesActive database 
        SC_MANAGER_ALL_ACCESS);  // full access rights 
    if (NULL == hSCManager)
    {
        DWORD dwLastErr = GetLastError();
        // Make sure this error message goes to stderr
        dbgOut.WriteToWCerr(true);
        if (ERROR_ACCESS_DENIED == dwLastErr)
            dbgOut.locked() << L"This program requires administrative rights." << std::endl;
        else
            dbgOut.locked() << L"Cannot open service control manager: " << SysErrorMessageWithCode(dwLastErr) << std::endl;
        return -1;
    }

    // Create names with unique strings for service, named pipes, and events
    std::wstring
        sServiceName = std::wstring(L"RunInSession0_") + CreateNewGuidString(),
        sNamedPipe_Output = std::wstring(L"\\\\.\\pipe\\Out_") + CreateNewGuidString(),
        sNamedPipe_Error = std::wstring(L"\\\\.\\pipe\\Err_") + CreateNewGuidString(),
        sEvent_ReadyToWrite = std::wstring(L"Global\\ReadyToWrite_") + CreateNewGuidString(),
        sEvent_ServiceDone = std::wstring(L"Global\\SvcDone_") + CreateNewGuidString();

    dbgOut.locked() << L"Service name: " << sServiceName << std::endl;
    dbgOut.locked() << L"Pipe names: " << sNamedPipe_Output << L", " << sNamedPipe_Error << std::endl;
    dbgOut.locked() << L"Events: " << sEvent_ReadyToWrite << L", " << sEvent_ServiceDone << std::endl;

    // Create named pipes and named events
    hEventReadyToWrite = CreateEventExW(NULL, sEvent_ReadyToWrite.c_str(), 0, EVENT_ALL_ACCESS);
    if (NULL == hEventReadyToWrite)
    {
        DWORD dwLastErr = GetLastError();
        dbgOut.locked() << L"Can't create event object: " << SysErrorMessageWithCode(dwLastErr) << std::endl;
    }
    hEventServiceDone = CreateEventExW(NULL, sEvent_ServiceDone.c_str(), 0, EVENT_ALL_ACCESS);
    if (NULL == hEventServiceDone)
    {
        DWORD dwLastErr = GetLastError();
        dbgOut.locked() << L"Can't create event object: " << SysErrorMessageWithCode(dwLastErr) << std::endl;
    }
    if (NULL == hEventReadyToWrite || NULL == hEventServiceDone)
    {
        retval = -3;
        goto SessionXCleanup;
    }

    // Security attributes for the named pipes: full control for BA and SY, no other access; not inheritable
    // Convert SDDL to SD.
    ret = ConvertStringSecurityDescriptorToSecurityDescriptorW(
        L"D:P(A;;FA;;;BA)(A;;FA;;;SY)",
        SDDL_REVISION_1,
        &pSD,
        NULL);
    if (!ret)
    {
        DWORD dwLastErr = GetLastError();
        dbgOut.locked() << L"ConvertStringSecurityDescriptorToSecurityDescriptorW failed; error " << SysErrorMessageWithCode(dwLastErr) << std::endl;
        retval = -4;
        goto SessionXCleanup;
    }
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = FALSE;
    sa.lpSecurityDescriptor = pSD;

    // Named pipe that session-0 side's stdout will be redirected to
    hOutput = CreateNamedPipeW(
        sNamedPipe_Output.c_str(),
        PIPE_ACCESS_INBOUND | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE,
        1,
        0,
        0,
        0,
        &sa);
    if (NULL == hOutput)
    {
        DWORD dwLastErr = GetLastError();
        dbgOut.locked() << L"Can't create named pipe object: " << SysErrorMessageWithCode(dwLastErr) << std::endl;
    }
    // Named pipe that session-0 side's stderr will be redirected to
    hError = CreateNamedPipeW(
        sNamedPipe_Error.c_str(),
        PIPE_ACCESS_INBOUND | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE,
        1,
        0,
        0,
        0,
        &sa);
    if (NULL == hError)
    {
        DWORD dwLastErr = GetLastError();
        dbgOut.locked() << L"Can't create named pipe object: " << SysErrorMessageWithCode(dwLastErr) << std::endl;
    }
    if (NULL == hOutput || NULL == hError)
    {
        retval = -5;
        goto SessionXCleanup;
    }

    // Scope the variables in this bit
    {
        // Construct the full command line for the service
        std::wstringstream strBinaryPathPlusParams;

        // Scope the lifetime of szUnquotedPath too
        {
            // Get the full path to the current executable
            TCHAR szUnquotedPath[MAX_PATH];
            if (!GetModuleFileNameW(NULL, szUnquotedPath, MAX_PATH))
            {
                DWORD dwLastErr = GetLastError();
                // Make sure this error message goes to stderr
                dbgOut.WriteToWCerr(true);
                dbgOut.locked() << L"GetModuleFileNameW failed: " << SysErrorMessageWithCode(dwLastErr) << std::endl;
                retval = -2;
                goto SessionXCleanup;
            }

            // Command line starts with a double-quoted copy of that path, in case it contains space characters.
            strBinaryPathPlusParams << L"\"" << szUnquotedPath << L"\"";
        }
        // Add the names of pipes and events to the service's command line
        strBinaryPathPlusParams << L" "
            << szSvcSwitch << L" "
            << sServiceName << L" "
            << sNamedPipe_Output << L" "
            << sNamedPipe_Error << L" "
            << sEvent_ReadyToWrite << L" "
            << sEvent_ServiceDone;
        // Create the service.
        hService = CreateServiceW(
            hSCManager,                // SCM database 
            sServiceName.c_str(),      // name of service 
            sServiceName.c_str(),      // service name to display 
            SERVICE_ALL_ACCESS,        // desired access 
            SERVICE_WIN32_OWN_PROCESS, // service type 
            SERVICE_DEMAND_START,      // start type 
            SERVICE_ERROR_NORMAL,      // error control type 
            strBinaryPathPlusParams.str().c_str(),     // path to service's binary (full command line)
            NULL,                      // no load ordering group 
            NULL,                      // no tag identifier 
            NULL,                      // No dependencies
            NULL,                      // LocalSystem account 
            NULL);                     // no password 

        if (NULL != hService)
        {
            dbgOut.locked() << L"Service successfully created: " << sServiceName << std::endl;
        }
        else
        {
            DWORD dwLastErr = GetLastError();
            // Make sure this error message goes to stderr
            dbgOut.WriteToWCerr(true);
            dbgOut.locked() << L"Cannot create service: " << SysErrorMessageWithCode(dwLastErr) << std::endl;
            return -6;
            goto SessionXCleanup;
        }
    }

    // Start the service (invoking ServiceMain)
    if (StartServiceW(hService, argc, (LPCWSTR*)argv))
    {
        dbgOut.locked() << L"Service started after installation" << std::endl;
    }
    else
    {
        DWORD dwLastErr = GetLastError();
        dbgOut.locked() << L"StartServiceW failed: " << SysErrorMessageWithCode(dwLastErr) << std::endl;
        return -7;
        goto SessionXCleanup;
    }

    // Wait for the session-0 service to indicate that it's connected to the named pipes for its stdout/stderr redirection
    //TODO: Why does the timeout not work on this call?
    dwWait = WaitForSingleObject(hEventReadyToWrite, 10000);
    switch (dwWait)
    {
    case WAIT_OBJECT_0:
        dbgOut.locked() << L"Session-0 side indicates it is ready to write." << std::endl;
        break;

    case WAIT_TIMEOUT:
        dbgOut.locked() << L"Timed out waiting for 'ready to write'" << std::endl;
        retval = -8;
        goto SessionXCleanup;

    default:
        dbgOut.locked() << L"Something waiting for 'ready to write': " << HEX(dwWait) << std::endl;
        retval = -9;
        goto SessionXCleanup;
    }

    // Scope for variables
    {
        // Start threads to read and redirect data from session-0's stdout and stderr.
        // Stack/automatic variables to pass to the threads: note that these variables must remain in scope, 
        // unaltered, and valid memory until the threads have exited.
        // Structs containing source and dest handles.
        SourceDest_t SourceDest_Output, SourceDest_Error;
        // For stdout
        SourceDest_Output.hSource = hOutput;
        SourceDest_Output.hDestination = hOutputDest;
        // For stderr
        SourceDest_Error.hSource = hError;
        SourceDest_Error.hDestination = hErrorDest;
        // Start the threads to monitor the pipes' data
        hThreadOutput = CreateThread(nullptr, 0, PipeMonitorThread, &SourceDest_Output, 0, nullptr);
        hThreadError = CreateThread(nullptr, 0, PipeMonitorThread, &SourceDest_Error, 0, nullptr);

        // Wait for signal that the service is done and that both stdout and stderr monitoring threads are done.
        const HANDLE handles[3] = { hEventServiceDone, hThreadOutput, hThreadError };
        const DWORD nHandles = sizeof(handles) / sizeof(handles[0]);
        // Configurable timeout (30 seconds by default)
        dwWait = WaitForMultipleObjects(nHandles, handles, TRUE, dwMaxMilliseconds);
        if (WAIT_OBJECT_0 <= dwWait && dwWait < WAIT_OBJECT_0 + nHandles)
        {
            dbgOut.locked() << L"Session 0 code done, and its output consumed" << std::endl;
            retval = 0;
        }
        else if (WAIT_TIMEOUT == dwWait)
        {
            //TODO: Can call WaitForSingleObject with timeout 0 for each handle to report where the failure is
            dbgOut.locked() << L"timed out waiting for end-of-service event signal and/or monitoring threads" << std::endl;
            retval = -10;
        }
        else
        {
            dbgOut.locked() << L"Something bad happened waiting for event: " << HEX(dwWait) << std::endl;
            retval = -11;
        }
    }

SessionXCleanup:

    dbgOut.locked() << L"Cleaning up" << std::endl;
    if (pSD)
    {
        dbgOut.locked() << L"LocalFree(pSD)" << std::endl;
        LocalFree(pSD);
    }

    // Close all object handles (not an error if the handle value is 0)
    if (hEventReadyToWrite) { dbgOut.locked() << L"CloseHandle hEventReadyToWrite" << std::endl; CloseHandle(hEventReadyToWrite); }
    if (hEventServiceDone) { dbgOut.locked() << L"CloseHandle hEventServiceDone" << std::endl; CloseHandle(hEventServiceDone); }
    if (hThreadOutput) 
    {
        // If something went wrong, use more aggressive cleanup
        if (0 != retval)
        {
            dbgOut.locked() << L"TerminateThread hThreadOutput" << std::endl;
#pragma warning(push)
#pragma warning (disable: 6258) // disable warning that "Using TerminateThread does not allow proper thread clean up."
            TerminateThread(hThreadOutput, 0);
#pragma warning(pop)
        }
        dbgOut.locked() << L"CloseHandle hThreadOutput" << std::endl; 
        CloseHandle(hThreadOutput); 
    }
    if (hThreadError)
    {
        // If something went wrong, use more aggressive cleanup
        if (0 != retval)
        {
            dbgOut.locked() << L"TerminateThread hThreadError" << std::endl;
#pragma warning(push)
#pragma warning (disable: 6258) // disable warning that "Using TerminateThread does not allow proper thread clean up."
            TerminateThread(hThreadError, 0);
#pragma warning(pop)
        }
        dbgOut.locked() << L"CloseHandle hThreadError" << std::endl;
        CloseHandle(hThreadError);
    }
    if (hOutput) { dbgOut.locked() << L"CloseHandle hOutput" << std::endl; CloseHandle(hOutput); }
    if (hError) { dbgOut.locked() << L"CloseHandle hError" << std::endl; CloseHandle(hError); }

    if (hService)
    {
        // If something went wrong, use more aggressive cleanup
        if (0 != retval)
        {
            // If the service's control handler is unresponsive, sending the service a SERVICE_CONTROL_STOP might 
            // not work and might even hang for 30 seconds or more.
            // Instead, get its PID and terminate the service process.
            dbgOut.locked() << L"Querying service to get its PID" << std::endl;
            SERVICE_STATUS_PROCESS ssp = { 0 };
            DWORD cbSSP = sizeof(ssp);
            if (QueryServiceStatusEx(hService, SC_STATUS_PROCESS_INFO, (LPBYTE)&ssp, cbSSP, &cbSSP))
            {
                dbgOut.locked() << L"Service's PID is " << ssp.dwProcessId << std::endl;
                if (0 != ssp.dwProcessId)
                {
                    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, ssp.dwProcessId);
                    if (NULL != hProcess)
                    {
                        dbgOut.locked() << L"Terminating process " << ssp.dwProcessId << std::endl;
                        if (TerminateProcess(hProcess, UINT(-32)))
                        {
                            dbgOut.locked() << "Process terminated" << std::endl;
                        }
                        else
                        {
                            DWORD dwLastErr = GetLastError();
                            dbgOut.locked() << L"Could not terminate process: " << SysErrorMessageWithCode(dwLastErr) << std::endl;
                        }
                    }
                    else
                    {
                        DWORD dwLastErr = GetLastError();
                        dbgOut.locked() << L"Could not access process to terminate it: " << SysErrorMessageWithCode(dwLastErr) << std::endl;
                    }
                }
            }
            else
            {
                DWORD dwLastErr = GetLastError();
                dbgOut.locked() << L"Could not query service: " << SysErrorMessageWithCode(dwLastErr) << std::endl;
            }
        }

        // Stop signal won't work if its service control handler is unresponsive
        //dbgOut.locked() << L"Sending STOP signal to service" << std::endl;
        //// Send a stop signal to the service if it's running (not a problem if not running)
        //SERVICE_STATUS serviceStatus = { 0 };
        //ControlService(hService, SERVICE_CONTROL_STOP, &serviceStatus);

        // Delete the service
        dbgOut.locked() << L"Deleting the service" << std::endl;
        if (DeleteService(hService))
        {
            dbgOut.locked() << L"Deleted service from SCM" << std::endl;
        }
        else
        {
            DWORD dwLastErr = GetLastError();
            dbgOut.locked() << L"Cannot delete service " << sServiceName << L": " << SysErrorMessageWithCode(dwLastErr) << std::endl;
        }
        CloseServiceHandle(hService);
        CloseServiceHandle(hSCManager);
    }

    return retval;
}

// ----------------------------------------------------------------------------------------------------
// ----------------------------------------------------------------------------------------------------

/// <summary>
/// Read data from hPipe and write that data to hDestination
/// </summary>
static bool ReadPipeToDest(HANDLE hPipe, HANDLE hDestination)
{
    // Allocate buffer: read up to 1MB at a time
    const size_t cbBufSize = 1024 * 1024;
    std::vector<uint8_t> vBuffer(cbBufSize);
    uint8_t* buffer = vBuffer.data();

    bool bContinue = true;
    while (bContinue)
    {
        DWORD dwRead = 0;
        SecureZeroMemory(buffer, cbBufSize);
        // Read from the pipe until it's empty
        BOOL rfRet = ReadFile(hPipe, buffer, cbBufSize - 8, &dwRead, NULL);
        DWORD dwLastErr = GetLastError();
        if (rfRet)
        {
            // ReadFile succeeded; read dwRead bytes
            dbgOut.locked() << L"ReadPipeToFile: ReadFile read " << dwRead << L" bytes" << std::endl;
        }
        else if (ERROR_BROKEN_PIPE == dwLastErr)
        {
            // ReadFile failed with ERROR_BROKEN_PIPE: should be good now
            dbgOut.locked() << L"ReadPipeToFile: ERROR_BROKEN_PIPE - should be good now" << std::endl;
        }
        else if (ERROR_OPERATION_ABORTED == dwLastErr)
        {
            // ReadFile failed with ERROR_OPERATION_ABORTED: time must be up
            dbgOut.locked() << L"ReadPipeToFile: ERROR_OPERATION_ABORTED - time must be up" << std::endl;
        }
        else
            dbgOut.locked() << L"ReadFile error: " << SysErrorMessageWithCode(dwLastErr) << std::endl;
        bContinue = (rfRet && dwRead > 0);
        if (bContinue)
        {
            DWORD dwWritten = 0;
            BOOL ret = WriteFile(hDestination, buffer, dwRead, &dwWritten, NULL);
            if (ret)
            {
                if (dwRead != dwWritten)
                    dbgOut.locked() << L"WriteFile anomaly: read " << dwRead << L" bytes but wrote " << dwWritten << std::endl;
            }
            else
            {
                dwLastErr = GetLastError();
                dbgOut.locked() << L"WriteFile error: " << SysErrorMessageWithCode(dwLastErr) << std::endl;
            }
        }
    }
    return true;
}

/// <summary>
/// Thread function for monitoring a pipe, reading data from a source and writing it to a destination
/// </summary>
/// <param name="lpvThreadParameter">A SourceDest_t that contains a source and destination handle</param>
/// <returns>Always returns 0</returns>
static DWORD WINAPI PipeMonitorThread(LPVOID lpvThreadParameter)
{
    dbgOut.locked() << L"PipeMonitorThread starting" << std::endl;
    SourceDest_t* pSourceDest = (SourceDest_t*)lpvThreadParameter;
    if (nullptr != pSourceDest)
    {
        ReadPipeToDest(pSourceDest->hSource, pSourceDest->hDestination);
    }
    dbgOut.locked() << L"PipeMonitorThread exiting" << std::endl;
    return 0;
}
