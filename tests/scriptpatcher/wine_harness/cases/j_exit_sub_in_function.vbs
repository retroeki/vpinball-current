' EXPECT: compile-error 1039
' WINDOWS: compile-error
' Why patcher bug J is fatal: pPauseIf writes Exit Sub inside Function vpmKeyUp (Bow And Arrow).
Function vpmKeyUp(keycode)
  If keycode = 1 Then Exit Sub
End Function