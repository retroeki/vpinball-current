' EXPECT: run-ok
' WINDOWS: ok
' Control for C: the same With-dot after Then with a space already works.
Class Box
  Public Value
End Class
Dim b : Set b = New Box
Dim x : x = 1
With b
  If x = 1 Then .Value = 5
End With
If b.Value <> 5 Then Err.Raise 1000