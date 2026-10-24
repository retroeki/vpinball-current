' EXPECT: run-ok
' WINDOWS: ok
' Guard for the C fix (lex.c): after a reserved word, '.' starts a With member or a
' number, as in Windows. (A one-line If ... Then .Foo Else .Foo is a syntax error on
' Windows even with spaces, so it is not used here.)
Class C
  Public Count, Flag, Obj, Called
  Sub Foo : Called = Called + 1 : End Sub
End Class
Dim o, i, n, x : Set o = New C : o.Count = 3 : o.Flag = 1 : o.Called = 0
With o
  For i = 0 To.Count - 1 : n = n + 1 : Next
  x = 1 And.Flag
  If x = 1 Then.Foo
  If x = 0 Then
    n = 99
  Else.Foo
  End If
  Call.Foo
  Set.Obj = o
  If Not.Obj Is Nothing Then x = 2
  Select Case.Count
    Case 3 : x = x + 1
  End Select
  Do While.Count < 0 : Loop
  x = x + (.Count Mod.Count) + (1 Or.Flag)
End With
If n <> 3 Then Err.Raise 5, "case", "n=" & n
If o.Called <> 3 Then Err.Raise 5, "case", "called=" & o.Called
If x <> 4 Then Err.Raise 5, "case", "x=" & x
Dim stepv : stepv = 0
For i = 0 To 1 Step.5 : stepv = stepv + 1 : Next
If stepv <> 3 Then Err.Raise 5, "case", "step=" & stepv
