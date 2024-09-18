#pragma once

// RunInSession0_Framework.h
// 
// Public declarations for the RunInSession0_Framework.
//
// RunInSession0_Framework is a framework to enable a self-contained program running with 
// admin rights in an interactive desktop session to execute target code that is in the same
// executable as System in session 0 and to capture its output, without involving 
// Sysinternals PsExec.
// 
// Components of the framework include:
//     RunInSession0_Framework.h (this header file)
//     RunInSession0_Framework_InternalDecls.h
//     RunInSession0_Session0Side.cpp
//     RunInSession0_SessionXSide.cpp
//     RunInSession0_wmainCommandProcessor.cpp.
//
// 

//TODO: provide a way to validate all command-line parameters, including app-specific ones, from the initial process.
//TODO: make it possible for app-specific command-line parameters not to have to appear after all framework-specific command-line parameters.
//TODO: offer a command-line option and/or environment variables to enable dbgOut destinations.


/// <summary>
/// The code to execute should be a function that takes standard argc/argv parameters and returns int.
/// It can write to stdout and stderr.
/// </summary>
typedef int (*pfn_CodeToRunInSession0_t)(int, wchar_t**);


/// <summary>
/// wmainCommandProcessor processes wmain's command line parameters to run the target function.
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
);


/// <summary>
/// For debugging purposes
/// </summary>
#include "DbgOut.h"
inline void DbgOutArgcArgv(const wchar_t* szLabel, int argc, wchar_t** argv)
{
    dbgOut.locked() << szLabel << L": argc = " << argc << std::endl;
    for (int ixArg = 0; ixArg < argc; ++ixArg)
    {
        dbgOut.locked() << L"  Arg " << ixArg << L": " << argv[ixArg] << std::endl;
    }
}