# GuiObjectUse

Lists the USER and GUI object use for each process in session 0 or in other sessions.

Its primary use case is to find the root cause of session 0 desktop heap exhaustion.

Inspecting resource usage in session 0 requires administrative rights, but you don't
need to run the process in session 0 -- `GuiObjectUse.exe` takes care of that for you,
using the `RunInSession0_Framework` (see below). So, you can run the tool in your
interactive desktop session, and it will run code in session 0 and retrieve the
results automatically.

To inspect the USER/GDI resource usage of processes in the current session, use the
`-here` command-line option. Administrative rights are needed only to inspect processes
running in other security contexts.

The tool outputs tab-delimited text with headers.

Usage:
```
    GuiObjectUse.exe [-here] [additional params]
    GuiObjectUse.exe [-t timeout] [-o outfile] [additional params]

  -here : run the code in the current session rather than in session 0
  -t    : max time in seconds for the session-0 service code to complete (default 30 seconds)
  -o    : redirect stdout from the session-0 code to named file

additional params (these must come last):
  -a : Show information about all processes, including processes
       with no User/GDI objects and /or that cannot be opened.
       By default, processes with no User or GDI objects or that
       cannot be opened are not listed.
```

There are two versions:
* `GuiObjectUse.exe` is a 64-bit (x64) Windows executable.
* `GuiObjectUse32.exe` is a 32-bit (x86) Windows executable.


For developers:<br>
`RunInSession0_Framework` is a framework to enable a self-contained program running with 
admin rights in an interactive desktop session to execute target code that is in the same
executable as System in session 0 and to capture its output, without involving 
Sysinternals PsExec.

Sample outputs here.
