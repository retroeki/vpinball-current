' EXPECT: run-ok
' WINDOWS: ok
' GLOBALS: Version,MusicVolume
' KNOWN-BUG: K interp_const rejects a Const whose name a global-members host object exposes (interp.c)
' FROM: Fantastic Journey (Const MusicVolume), Gargamel Park (Const Version), 2026-10-23
' (cscript has no global-members host object, so the WINDOWS line only proves the Consts are legal.)
Const Version = "1.1"
Const MusicVolume = 0.6
If Version <> "1.1" Then Err.Raise 1000
If MusicVolume <> 0.6 Then Err.Raise 1001