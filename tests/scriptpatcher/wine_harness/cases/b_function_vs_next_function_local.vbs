' EXPECT: run-ok
' WINDOWS: ok
' KNOWN-BUG: B create_function checks a name against the previously compiled function's locals (compile.c)
' FROM: Pink Floyd 3.28c line 4234 (65 reports, 2026-10-23)
Option Explicit
Function PI()
    PI = 4*Atn(1)
End Function
Function dSin(degrees)
    Dim Pi : Pi = 3.14159
    dSin = Sin(degrees * Pi / 180)
End Function
If Abs(PI() - 3.14159) > 0.001 Then Err.Raise 1000