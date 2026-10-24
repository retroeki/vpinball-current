' EXPECT: compile-error 1041
' WINDOWS: compile-error
' Guard for the B fix: a real global Const/Function clash must still be rejected.
Const PI = 3.14
Function PI()
End Function