// GuiObjectUse.cpp
// 
// This file contains the core code for the GuiObjectUse program, which is invoked by the RunInSession0_Framework.
//

#include <Windows.h>
#include <WtsApi32.h>
#pragma comment(lib, "Wtsapi32.lib")
#include <io.h>
#include <fcntl.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include "SysErrorMessage.h"
#include "CSid.h"
#include "FileOutput.h"
#include "Utilities.h"
#include "ServiceLookupByPID.h"
#include "NtInternal.h"
#include "RunInSession0_Framework.h"


const wchar_t* szUsageDescription =
L"    Lists processes in session 0 and the numbers of USER and GDI\n"
L"    resources they've used, as tab-delimited text with headers.\n"
L"    Requires administrative rights.\n"
L"    To inspect processes in the current session, use the -here\n"
L"    command line option (requires admin rights to inspect\n"
L"    processes running in other security contexts)."
;

const wchar_t* szParamsForFunction =
L"  -a : Show information about all processes, including processes\n"
L"       with no User/GDI objects and /or that cannot be opened.\n"
L"       By default, processes with no User or GDI objects or that\n"
L"       cannot be opened are not listed.\n"
;

// Forward declaration for the code to pass to the RunInSession0_Framework.
int GuiObjectUse(int argc, wchar_t** argv);


/// <summary>
/// Program entry point. Note that depending on the RunInSession0_Framework,
/// this process might be executing as a regular process or as a Windows service process.
/// The wmainCommandProcessor takes care of all that.
/// </summary>
int wmain(int argc, wchar_t** argv)
{
    //TODO: consider checking an environment variable for enabling dbgOut destinations.
    dbgOut.WriteToDebugStream(false);

    // Set output mode to UTF8.
    if (_setmode(_fileno(stdout), _O_U8TEXT) == -1 || _setmode(_fileno(stderr), _O_U8TEXT) == -1)
    {
        std::wcerr << L"Unable to set stdout and/or stderr modes to UTF8." << std::endl;
    }

    return wmainCommandProcessor(argc, argv, GuiObjectUse, szUsageDescription, szParamsForFunction);
}



//TODO: Output in order so that child processes are listed right after their parent.
//TODO: Offer an option to show the desktop sizes in the current session.

/// <summary>
/// Lists processes in session 0 and the numbers of USER and GDI
/// resources they've used, as tab-delimited text with headers.
/// </summary>
/// <param name="argc">Number of optional parameters</param>
/// <param name="argv">Optional parameters</param>
/// <returns>0 if successful, negative value otherwise</returns>
int GuiObjectUse(int argc, wchar_t** argv)
{
    DbgOutArgcArgv(L"GuiObjectUse", argc, argv);

    // Whether to output information about all processes or just those with non-zero results.
    bool bShowAll = false;

    // Process command-line arguments
    int ixArg = 0;
    while (ixArg < argc)
    {
        if (0 == wcscmp(L"-a", argv[ixArg]))
            bShowAll = true;
        else
        {
            std::wcerr << L"Unrecognized command line option: " << argv[ixArg] << std::endl;
            return -1;
        }
        ++ixArg;
    }

    // Determine this process' WTS session ID.
    std::wstring sErrorInfo;
    DWORD dwSessionID;
    if (!GetCurrentTSSessionID(dwSessionID, sErrorInfo))
    {
        std::wcerr << L"Unable to retrieve current TS session ID: " << sErrorInfo << std::endl;
        return -1;
    }

    DWORD dwUserObjects, dwUserObjectsPeak, dwGdiObjects, dwGdiObjectsPeak;
    DWORD dwTotalUserObjects = 0, dwTotalUserObjectsPeak = 0, dwTotalGdiObjects = 0, dwTotalGdiObjectsPeak = 0;
    DWORD dwLevel = 0;

    // Get information about all processes in the session.
    WTS_PROCESS_INFOW* pProcessesInfo = nullptr;
    DWORD dwProcessCount = 0;
#pragma warning(push)
#pragma warning (disable: 6387) // disable false positive about invalid parameter
    BOOL ret = WTSEnumerateProcessesExW(WTS_CURRENT_SERVER_HANDLE, &dwLevel, dwSessionID, (LPWSTR*)&pProcessesInfo, &dwProcessCount);
#pragma warning(pop)
    if (!ret)
    {
        DWORD dwLastErr = GetLastError();
        std::wcerr << L"WTSEnumerateProcessesExW with session " << dwSessionID << L" failed: " << SysErrorMessageWithCode(dwLastErr) << std::endl;
        return -2;
    }

    const wchar_t* const szTab = L"\t";

    // Output tab-delimited headers to stdout. (If running as a service, stdout will be redirected.) 
    std::wcout 
        << L"Session" << szTab
        << L"PID" << szTab
        << L"Process name" << szTab
        << L"PPID" << szTab
        << L"Services" << szTab
        << L"User SID" << szTab
        << L"User name" << szTab
        << L"USER objects" << szTab
        << L"USER objects peak" << szTab
        << L"GDI objects" << szTab
        << L"GDI objects peak" << std::endl;
    // Iterate through all of the processes in this session.
    for (size_t ix = 0; ix < dwProcessCount; ++ix)
    {
        WTS_PROCESS_INFOW& wtsCurrProcess = pProcessesInfo[ix];
        // Always skip PID 0: not a real process.
        if (0 != wtsCurrProcess.ProcessId)
        {
            // Identify any services running in that process
            const ServiceList_t* pServiceList = nullptr;
            std::wstring sServices;
            if (LookupServicesByPID((ULONG_PTR)wtsCurrProcess.ProcessId, &pServiceList))
            {
                std::wstringstream strServices;
                for (
                    ServiceList_t::const_iterator iterSvc = pServiceList->begin();
                    iterSvc != pServiceList->end();
                    iterSvc++
                    )
                {
                    strServices << iterSvc->sServiceName << L" ";
                }
                sServices = strServices.str();
            }

            // Get the SID of the account executing the process.
            CSid sid(wtsCurrProcess.pUserSid);

            HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, wtsCurrProcess.ProcessId);
            if (hProcess)
            {
                // Get information about the process' User and GDI objects.
                dwUserObjects = GetGuiResources(hProcess, GR_USEROBJECTS);
                dwUserObjectsPeak = GetGuiResources(hProcess, GR_USEROBJECTS_PEAK);
                dwGdiObjects = GetGuiResources(hProcess, GR_GDIOBJECTS);
                dwGdiObjectsPeak = GetGuiResources(hProcess, GR_GDIOBJECTS_PEAK);
                dwTotalUserObjects += dwUserObjects;
                dwTotalUserObjectsPeak += dwUserObjectsPeak;
                dwTotalGdiObjects += dwGdiObjects;
                dwTotalGdiObjectsPeak += dwGdiObjectsPeak;
                // Get the PID of the process' parent process
                ULONG_PTR ppid = GetParentPID(hProcess, sErrorInfo);
                std::wstringstream strPPID;
                if (0 != ppid)
                    strPPID << ppid;
                else
                    strPPID << sErrorInfo;
                CloseHandle(hProcess);

                // Report info about the process if any of the numbers are non-zero, or the "show all" option is selected.
                if (bShowAll || dwUserObjects > 0 || dwGdiObjects > 0 || dwUserObjectsPeak > 0 || dwGdiObjectsPeak > 0)
                {
                    std::wcout
                        << wtsCurrProcess.SessionId << szTab
                        << wtsCurrProcess.ProcessId << szTab
                        << wtsCurrProcess.pProcessName << szTab
                        << strPPID.str() << szTab
                        << sServices << szTab
                        << sid.toSidString() << szTab
                        << sid.toDomainAndUsername() << szTab
                        << dwUserObjects << szTab
                        << dwUserObjectsPeak << szTab
                        << dwGdiObjects << szTab
                        << dwGdiObjectsPeak << std::endl;
                }
            }
            else
            {
                // Report processes that we couldn't get information about only if "show all" is selected.
                if (bShowAll)
                {
                    DWORD dwLastErr = GetLastError();
                    std::wcout
                        << wtsCurrProcess.SessionId << szTab
                        << wtsCurrProcess.ProcessId << szTab
                        << wtsCurrProcess.pProcessName << szTab
                        << szTab
                        << sServices << szTab
                        << sid.toSidString() << szTab
                        << sid.toDomainAndUsername() << szTab
                        << L"Error " << dwLastErr << szTab
                        << SysErrorMessage(dwLastErr) << szTab
                        << L"Error " << dwLastErr << szTab
                        << SysErrorMessage(dwLastErr) << std::endl;
                }
            }
        }
    }

    WTSFreeMemoryExW(WTSTypeProcessInfoLevel0, pProcessesInfo, dwProcessCount);

    // Total from the enumerated processes
    std::wcout
        << dwSessionID << szTab
        << L"TOTAL" << szTab
        << L"[enumerated processes]" << szTab
        << L"" << szTab
        << L"" << szTab
        << L"" << szTab
        << L"" << szTab
        << dwTotalUserObjects << szTab
        << dwTotalUserObjectsPeak << szTab
        << dwTotalGdiObjects << szTab
        << dwTotalGdiObjectsPeak << std::endl;

    // Session-wide usage (hProcess = GR_GLOBAL)
    dwUserObjects = GetGuiResources(GR_GLOBAL, GR_USEROBJECTS);
    dwUserObjectsPeak = GetGuiResources(GR_GLOBAL, GR_USEROBJECTS_PEAK);
    dwGdiObjects = GetGuiResources(GR_GLOBAL, GR_GDIOBJECTS);
    dwGdiObjectsPeak = GetGuiResources(GR_GLOBAL, GR_GDIOBJECTS_PEAK);
    std::wcout
        << dwSessionID << szTab
        << L"GR_GLOBAL" << szTab
        << L"[Session-wide usage]" << szTab
        << L"" << szTab
        << L"" << szTab
        << L"" << szTab
        << L"" << szTab
        << dwUserObjects << szTab
        << dwUserObjectsPeak << szTab
        << dwGdiObjects << szTab
        << dwGdiObjectsPeak << std::endl;

    return 0;
}
