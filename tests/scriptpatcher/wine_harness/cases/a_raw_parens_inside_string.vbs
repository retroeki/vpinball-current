' EXPECT: run-ok
' WINDOWS: ok
' The unpatched Nightmare Before Christmas line. PatchAmbiguousCallParens matched the ( inside the string.
Class Img
  Public Bitmap
End Class
Function NewImage(a, b)
  Set NewImage = New Img
  NewImage.Bitmap = b
End Function
Dim n, r : n = 3
r = NewImage("", "VPX.zdog (" & n - 1 & ")&dmd=2").Bitmap
If r <> "VPX.zdog (2)&dmd=2" Then Err.Raise 1000