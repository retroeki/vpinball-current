' EXPECT: run-ok
' WINDOWS: ok
' KNOWN-BUG: V Wine lexes ".0" after a member name as another member access, not a number
' PinballMagic v1.7 Drain_Hit: "bsTrough.AddBall.0" (Windows: AddBall 0). Compile error on device.
Class C
  Public n
  Sub AddBall(x) : n = "got " & x : End Sub
End Class
Dim b : Set b = New C
b.AddBall.0
If b.n <> "got 0" Then Err.Raise 5, "case", "n=" & b.n
b.AddBall.5
If b.n <> "got 0.5" Then Err.Raise 5, "case", "n=" & b.n
