' EXPECT: run-ok
' WINDOWS: ok
' KNOWN-BUG: U standalone ReadFile (wine.c) returns one phantom 0xFF byte at end of file
' Every FSO text read on device: "abc" reads back as "abc" & ChrW(&HFFFD), and an
' empty file is never AtEndOfStream (Loony Labyrinth's high score loader then
' skips writing its defaults and reads past the end: error 62).
Dim fso, f, t, s
Set fso = CreateObject("Scripting.FileSystemObject")
Set f = fso.CreateTextFile("vbs_case_eof.txt", True)
f.Write "abc"
f.Close
Set t = fso.OpenTextFile("vbs_case_eof.txt", 1)
s = t.ReadAll
t.Close
If s <> "abc" Then Err.Raise 5, "case", "read back " & Len(s) & " characters"
Set f = fso.CreateTextFile("vbs_case_eof.txt", True)
f.Close
Set t = fso.OpenTextFile("vbs_case_eof.txt", 1)
If Not t.AtEndOfStream Then Err.Raise 5, "case", "empty file is not at end of stream"
t.Close
