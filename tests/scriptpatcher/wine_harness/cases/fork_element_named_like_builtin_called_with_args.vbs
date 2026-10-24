' EXPECT: run-ok
' WINDOWS: run-error
' ITEMS: Left=Wall,Trim=Wall
' Deliberate fork divergence (crash-fixes-2026-10-23): an element named like a built-in
' shadows it (Trim.visible), but a call WITH arguments still gets the built-in. Windows
' raises 450 on Left("abc", 1); core.vbs line 91 calls Left(UserDirectory, ...) in
' tables with a wall called Left (Sharp Shooter II, Munsters).
Dim x : x = Left("abc", 1)
If x <> "a" Then Err.Raise 5, "case", "x=" & x
x = Trim("  a ")
If x <> "a" Then Err.Raise 5, "case", "x=" & x
If Not IsObject(Trim) Then Err.Raise 5, "case", "Trim is not the element"
