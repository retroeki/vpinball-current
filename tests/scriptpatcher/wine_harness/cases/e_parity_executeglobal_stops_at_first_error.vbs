' EXPECT: run-ok
' WINDOWS: ok
' Guard: On Error Resume Next in the caller does NOT carry into ExecuteGlobal'd code on Windows;
' the executed code stops at its first error and the caller sees Err. Do not fix E that way.
Const Bsize = 26
On Error Resume Next
ExecuteGlobal "Dim M2 : M2 = 0 : BSize = 25 : M2 = 2"
Dim e : e = Err.Number
On Error Goto 0
If e <> 501 Then Err.Raise 1000
If Eval("M2") <> 0 Then Err.Raise 1001