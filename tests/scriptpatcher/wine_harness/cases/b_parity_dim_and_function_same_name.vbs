' EXPECT: compile-error 1041
' WINDOWS: compile-error
' Guard for the B fix: a real global Dim/Function clash must still be rejected.
Dim X
Function X()
End Function