' EXPECT: run-ok
' WINDOWS: ok
' FIXED (crash-fixes-2026-10-23): A parser.y SubFirstArg joined Sub (a) OP rest as OP((a), rest), so
' o.Kick (5 - 1)/2 + 1, 21.5 passed (5-1)/(2+1) (Flying Turns). The first argument now
' follows operator precedence, so the patcher no longer needs to add brackets.
Class C
  Public v
  Sub Kick(a, b) : v = a & "|" & b : End Sub
  Sub One(a) : v = a : End Sub
End Class
Dim o, r : Set o = New C
Sub Two(a, b) : r = a & "|" & b : End Sub
Sub Check(got, want) : If CStr(got) <> want Then Err.Raise 5, "case", "got " & got & ", want " & want : End If : End Sub
o.Kick (5 - 1)/2 + 1, 21.5 : Check o.v, "3|21.5"
Two (5 - 1)/2 + 1, 21.5 : Check r, "3|21.5"
o.Kick (6)*2 + 1, 1 : Check o.v, "13|1"
o.Kick (10) - 2 - 3, 0 : Check o.v, "5|0"
o.Kick (2) ^ 3 * 2 - 1, 0 : Check o.v, "15|0"
o.Kick (7) Mod 4 + 1, 0 : Check o.v, "4|0"
o.Kick (1) & 2 + 3, 0 : Check o.v, "15|0"
o.One (6)*2 + 1 : Check o.v, "13"
o.One (3) + 4 * 2 : Check o.v, "11"
o.One (1) < 2 And 3 > 4 : Check o.v, "False"
