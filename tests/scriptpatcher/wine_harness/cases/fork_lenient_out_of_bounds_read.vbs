' EXPECT: run-ok
' WINDOWS: run-error
' Fork divergence on purpose (interp.c lenient OOB read): AC-DC Premium reads LampState(402) of Dim LampState(200).
Dim LampState(200), v
v = LampState(402)
If Not IsEmpty(v) Then Err.Raise 1000