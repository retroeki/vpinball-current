' EXPECT: run-ok
' WINDOWS: ok
' KNOWN-BUG: C lex.c treats the dot after Then as member access (previous char is an identifier char)
' FROM: BarbWire/Tiki Bob/Break/Fallout/Kiss iDig/Tenacious D (62 reports, 2026-10-23)
Class Box
  Public Value
End Class
Dim b : Set b = New Box
Dim x : x = 1
With b
  If x = 1 Then.Value = 5
End With
If b.Value <> 5 Then Err.Raise 1000