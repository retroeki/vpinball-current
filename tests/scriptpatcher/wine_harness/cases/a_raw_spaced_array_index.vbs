' EXPECT: run-ok
' WINDOWS: ok
' The unpatched Skyrocket line. PatchAmbiguousCallParens turned it into ScoreQueue ((Player,1)+s10).
Dim ScoreQueue(4,4), Player, s10
Player = 1 : s10 = 10
ScoreQueue (Player,1)=ScoreQueue (Player,1)+s10
If ScoreQueue(1,1) <> 10 Then Err.Raise 1000