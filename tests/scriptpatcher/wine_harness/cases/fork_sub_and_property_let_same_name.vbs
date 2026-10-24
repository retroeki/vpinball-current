' EXPECT: compile-ok
' WINDOWS: ok
' Fork fix: Lampz/Elvis MOD classes define Sub MassAssign and Property Let MassAssign.
Class Lampz
  Public Sub MassAssign(a, b) : End Sub
  Public Property Let MassAssign(a, b) : End Property
End Class