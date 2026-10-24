' EXPECT: run-ok
' WINDOWS: ok
' FIXED (crash-fixes-2026-10-23): W Err.Raise with a skipped Source argument raises 13 instead of the given number
' Batman66 1.1.0 JSON parser: Err.Raise 8732 ,, "Expecting property name".
On Error Resume Next
Err.Raise 5, , "desc"
Dim n : n = Err.Number : Dim d : d = Err.Description
On Error GoTo 0
If n <> 5 Or d <> "desc" Then Err.Raise 6, "case", "got " & n & " " & d
