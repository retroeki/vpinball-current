' EXPECT: compile-ok
' WINDOWS: ok
' KNOWN-BUG: O the Wine grammar rejects a member access on a string literal; Windows only fails if it runs
' FROM: Ramones (HauntFreaks 2021) line 1995 (7 reports, 2026-10-23)
Sub PlaySoundAt(a, b) : End Sub
Sub Rubber32_Hit
    PlaySoundAt "flip_hit_1". Rubber32
End Sub