' EXPECT: run-ok
' WINDOWS: ok
' Fork fix (Wine bug 58056): writing an element of an object's public array (core.vbs vpmFlips).
Class Flips
  Public FlipperSolNumber(3)
End Class
Dim f : Set f = New Flips
f.FlipperSolNumber(2) = 7
If f.FlipperSolNumber(2) <> 7 Then Err.Raise 1000