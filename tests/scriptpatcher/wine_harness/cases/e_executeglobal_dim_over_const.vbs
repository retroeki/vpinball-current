' EXPECT: run-ok
' WINDOWS: ok
' KNOWN-BUG: E exec_global_code skips a Dim whose name matches an existing global Const, so the next assignment fails (vbscript.c)
' FROM: Space Jam: table Const Bsize vs core.vbs Dim BSize (42 reports, 2026-10-23)
' Windows: the ExecuteGlobal Dim creates its own variable; the rest of the code runs.
Option Explicit
Const Bsize = 26
Sub LoadVBSFiles
    On Error Resume Next
    ExecuteGlobal "Dim BSize : BSize = 25 : Dim LoadedMarker : LoadedMarker = 1"
End Sub
LoadVBSFiles
If Eval("LoadedMarker") <> 1 Then Err.Raise 1000
If Bsize <> 26 Then Err.Raise 1001