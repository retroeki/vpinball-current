' EXPECT: run-ok
' WINDOWS: ok
' ITEMS: Trim=Wall
' FIXED (crash-fixes-2026-10-23): T lookup_identifier resolves Wine's built-in Trim() before a table element named Trim (interp.c)
' FROM: Gladiators / Surf 'n Safari / Wipe Out (Kiwi): Trim.visible=RailsLights in Table1_Init, 450 (2026-10-23)
' Windows: a named item shadows the built-in function of the same name.
Dim n
n = Trim.Count
