' EXPECT: run-ok
' WINDOWS: ok
' FIXED (crash-fixes-2026-10-23): P wine.c FindFirstFileW reported every entry as a file, so a
' folder's Files collection tried to open its subfolders as files (E_FAIL; Iron Maiden Virtual
' Time: For Each f In fso.GetFolder(path).Files) and SubFolders was always empty.
Dim fso, f, n, t
Set fso = CreateObject("Scripting.FileSystemObject")
fso.CreateFolder "media"
fso.CreateFolder "media\sub"
Set t = fso.CreateTextFile("media\one.txt", True) : t.Write "a" : t.Close
Set t = fso.CreateTextFile("media\two.dat", True) : t.Write "bb" : t.Close
n = 0 : For Each f In fso.GetFolder("media").Files : n = n + 1 : Next
If n <> 2 Then Err.Raise 5, "case", "files=" & n
n = 0 : For Each f In fso.GetFolder("media").SubFolders : n = n + 1 : Next
If n <> 1 Then Err.Raise 5, "case", "subfolders=" & n
If fso.GetFile("media\two.dat").Size <> 2 Then Err.Raise 5, "case", "size"
