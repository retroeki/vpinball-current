' EXPECT: run-ok
' WINDOWS: ok
' FIXED (crash-fixes-2026-10-23): Q global.c FormatDateTime raised E_FAIL for every named format
' but vbGeneralDate: the standalone build has no NLS locale data (The Price Is Right:
' FormatDateTime(Now(), vbLongTime)). The exact text depends on the locale, so only the parts
' every locale shows are checked.
Dim d : d = DateSerial(2026, 10, 24) + TimeSerial(13, 5, 9)
If InStr(FormatDateTime(d, vbLongDate), "2026") = 0 Then Err.Raise 5, "case", "vbLongDate"
If InStr(FormatDateTime(d, vbShortDate), "2026") = 0 Then Err.Raise 5, "case", "vbShortDate"
If InStr(FormatDateTime(d, vbLongTime), "05:09") = 0 Then Err.Raise 5, "case", "vbLongTime"
If InStr(FormatDateTime(d, vbShortTime), "13:05") = 0 Then Err.Raise 5, "case", "vbShortTime"
