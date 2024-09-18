// RunInSession0_wmainCommandProcessor.cpp
// 
// The portion of the RunInSession0_Framework that processes wmain argc/argv arguments.
//


#include <iostream>
#include "DbgOut.h"
#include "SysErrorMessage.h"
#include "Utilities.h"
#include "StringUtils.h"
#include "RunInSession0_Framework_InternalDecls.h"

/// <summary>
/// Output usage and error information to stderr and exit the process.
/// </summary>
/// <param name="argv0">The process' argv[0] -- the full path to the program's executable.</param>
/// <param name="szUsageDescription">App-specific description of the program</param>
/// <param name="szParamsForFunction">App-specific parameters for the program</param>
/// <param name="szError">Error information to report</param>
static void Usage(const wchar_t* argv0, const wchar_t* szUsageDescription, const wchar_t* szParamsForFunction, const wchar_t* szError)
{
    std::wstring sExe = GetFileNameFromFilePath(argv0);
    if (szError)
        std::wcerr << szError << std::endl;
    if (szUsageDescription)
    {
        std::wcerr
            << std::endl
            << sExe << L":" << std::endl
            << szUsageDescription << std::endl
            ;
    }
    // The -here, -t, and -o parameters are handled by the RunInSession0_Framework.
    // App-specific parameters must come after those parameters.
    std::wcerr
        << std::endl
        << L"Usage:" << std::endl
        << std::endl
        << L"    " << sExe << L" [-here] [additional params]" << std::endl
        << L"    " << sExe << L" [-t timeout] [-o outfile] [additional params]" << std::endl
        << std::endl
        << L"  -here : run the code in the current session rather than in session 0" << std::endl
        << L"  -t    : max time in seconds for the session-0 service code to complete (default 30 seconds)" << std::endl
        << L"  -o    : redirect stdout from the session-0 code to named file" << std::endl
        << std::endl
        << L"additional params (these must come last):" << std::endl
        << (szParamsForFunction ? szParamsForFunction : L"(none)") << std::endl
        << std::endl;
    exit(-1);
}

/// <summary>
/// Processes wmain's command line parameters to run the target function.
/// </summary>
/// <param name="argc">wmain's argc</param>
/// <param name="argv">wmain's argv</param>
/// <param name="pfn_CodeToRunInSession0">Target function to execute</param>
/// <param name="szUsageDescription">Descriptive text for Usage output</param>
/// <param name="szParamsForFunction">Usage text specific to the target function</param>
/// <returns>Current process' exit code</returns>
int wmainCommandProcessor(
    int argc,
    wchar_t** argv,
    pfn_CodeToRunInSession0_t pfn_CodeToRunInSession0,
    const wchar_t* szUsageDescription,
    const wchar_t* szParamsForFunction
)
{
    dbgOut.locked() << L"RunInSession0_Framework starting process " << GetCurrentProcessId() << std::endl;
    DbgOutArgcArgv(L"wmainCommandProcessor", argc, argv);

    // Determine whether this process is already in session 0.
    bool bInSession0 = false;
    if (!InSession0(bInSession0))
    {
        dbgOut.locked() << L"Unable to determine which session the current process is in" << std::endl;
        return -1;
    }

    // If in session 0 and with expected service parameters, then this program was started by the 
    // Service Control Manager. Call the function that sets up communications with the session X 
    // process and executes the requested code.
    if (bInSession0 && AreServiceExeParams(argc, argv))
    {
        return ServiceExeSide(
            pfn_CodeToRunInSession0, // Function to execute in session 0
            argv[2], // service name
            argv[3], // szNamedPipe_Output
            argv[4], // szNamedPipe_Error
            argv[5], // szEvent_ReadyToWrite
            argv[6]  // szEvent_ServiceDone
        );
    }

    // Not running as a service. Handle other command line parameters.
    bool bStayInThisSession = false, bTimeoutOverride = false;
    const wchar_t* szRedirectToFile = nullptr;
    DWORD dwMaxSeconds = 30;
    int ixArg = 1, argcExtra = 0;
    while (ixArg < argc)
    {
        // Requests for command-line usage:
        if (
            0 == wcscmp(L"/?", argv[ixArg]) ||
            0 == wcscmp(L"-?", argv[ixArg]) ||
            0 == wcscmp(L"-help", argv[ixArg]) ||
            0 == wcscmp(L"/help", argv[ixArg])
            )
        {
            Usage(argv[0], szUsageDescription, szParamsForFunction, nullptr);
        }
        else if (0 == wcscmp(L"-here", argv[ixArg]))
        {
            bStayInThisSession = true;
        }
        else if (0 == wcscmp(L"-t", argv[ixArg]))
        {
            if (++ixArg >= argc)
                Usage(argv[0], szUsageDescription, szParamsForFunction, L"Missing arg for -t");
            if (1 != swscanf_s(argv[ixArg], L"%lu", &dwMaxSeconds) || 0 == dwMaxSeconds)
                Usage(argv[0], szUsageDescription, szParamsForFunction, L"Invalid arg for -t");
            bTimeoutOverride = true;
        }
        else if (0 == wcscmp(L"-o", argv[ixArg]))
        {
            if (++ixArg >= argc)
                Usage(argv[0], szUsageDescription, szParamsForFunction, L"Missing arg for -o");
            szRedirectToFile = argv[ixArg];
        }
        else
        {
            // argcExtra -- app-specific arguments to be processed by app-specific code.
            argcExtra = argc - ixArg;
            break;
        }
        ++ixArg;
    }

    // Check for setting of mutually-exlusive options
    if (bStayInThisSession && (nullptr != szRedirectToFile || bTimeoutOverride))
    {
        Usage(argv[0], szUsageDescription, szParamsForFunction, L"Invalid combination of options");
    }

    dbgOut.locked() << L"bStayInThisSession = " << bStayInThisSession << std::endl;
    dbgOut.locked() << L"dwMaxSeconds = " << dwMaxSeconds << std::endl;
    dbgOut.locked() << L"szRedirectToFile = " << (szRedirectToFile ? szRedirectToFile : L"nullptr") << std::endl;
    dbgOut.locked() << L"Remaining params: " << argcExtra << std::endl;
    for (ixArg = 0; ixArg < argcExtra; ++ixArg)
    {
        dbgOut.locked() << L" Arg " << ixArg << L": " << argv[(argc - argcExtra) + ixArg] << std::endl;
    }

    // If -here specified, just run the target code.
    if (bStayInThisSession)
    {
        return pfn_CodeToRunInSession0(argcExtra, &argv[argc - argcExtra]);
    }
    else
    {
        // If -o specified, set up the output file for stdout redirection.
        HANDLE hRedirFile = nullptr;
        if (szRedirectToFile)
        {
            hRedirFile = CreateFileW(
                szRedirectToFile,
                FILE_ALL_ACCESS,
                0,
                nullptr,
                CREATE_ALWAYS,
                0,
                nullptr);
            if (INVALID_HANDLE_VALUE == hRedirFile)
            {
                DWORD dwLastErr = GetLastError();
                // Make sure this error message gets written to stderr
                dbgOut.WriteToWCerr(true);
                dbgOut.locked() << L"Cannot open " << szRedirectToFile << L": " << SysErrorMessageWithCode(dwLastErr) << std::endl;
                Usage(argv[0], szUsageDescription, szParamsForFunction, nullptr);
            }
        }

        // Run the "session X" code to prepare the execution of code in session 0 and then run that code.
        // Note that if we are already in session 0 here but not running as a service, we're still starting 
        // a service and communicating with it. It's too much work to support timeout and redirection otherwise.
        int retval = SessionXSide(
            argcExtra,
            &argv[argc - argcExtra],
            dwMaxSeconds,
            (hRedirFile ? hRedirFile : GetStdHandle(STD_OUTPUT_HANDLE)),
            GetStdHandle(STD_ERROR_HANDLE));

        if (hRedirFile)
        {
            // Close the redirected-output file.
            CloseHandle(hRedirFile);
        }
        return retval;
    }
}

