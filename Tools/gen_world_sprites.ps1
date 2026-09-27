# Generate World Actor debug resources: TestNPC + Slime + Wolf + Boar
# Each: 6 frames x 8 directions sprite sheet (96x96/frame), character.json, animations.json
# Monsters also get monster.json (AI definition). Frame layout identical to TestHero.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$frame = 96
$cols = 6
$rowDirs = @('south','southwest','west','northwest','north','northeast','east','southeast')
$dirVectors = @(@(@(0,1)),@(@(-1,1)),@(@(-1,0)),@(@(-1,-1)),@(@(0,-1)),@(@(1,-1)),@(@(1,0)),@(@(1,1)))

function New-AnimationsJson {
    $clips = [ordered]@{}
    for ($row = 0; $row -lt 8; $row++) {
        $n = $rowDirs[$row]; $base = $row * 6
        $clips["idle_$n"] = @{ loop = $true; frames = @(@{ index = $base; duration = 0.35 }, @{ index = $base + 1; duration = 0.35 }) }
        $clips["walk_$n"] = @{ loop = $true; frames = @(0..3 | ForEach-Object { @{ index = $base + 2 + $_; duration = 0.12 } }) }
    }
    return (@{ version = 1; clips = $clips } | ConvertTo-Json -Depth 6 -Compress)
}
$animJson = New-AnimationsJson

function Draw-Arrow([Drawing.Graphics]$gg, [int]$cx, [int]$cy, [int]$ax, [int]$ay, [Drawing.Color]$color) {
    $len = 16
    $tipX = $cx + $ax * $len; $tipY = $cy + $ay * $len
    $perpX = -$ay; $perpY = $ax
    $p1 = New-Object Drawing.Point ($tipX + $perpX*6 - $ax*4), ($tipY + $perpY*6 - $ay*4)
    $p2 = New-Object Drawing.Point ($tipX - $perpX*6 - $ax*4), ($tipY - $perpY*6 - $ay*4)
    $p3 = New-Object Drawing.Point $tipX, $tipY
    $brush = New-Object Drawing.SolidBrush $color
    $gg.FillPolygon($brush, @($p1, $p2, $p3))
    $brush.Dispose()
}

function Draw-Creature([Drawing.Graphics]$gg, [int]$px, [int]$py, [string]$kind,
                       [Drawing.Color]$bodyColor, [int]$frameIdx, [bool]$isWalk, [int]$dirIndex) {
    $cx = $px + 48
    $body = New-Object Drawing.SolidBrush $bodyColor
    $dark = New-Object Drawing.SolidBrush ([Drawing.Color]::FromArgb(50,50,58))
    $white = New-Object Drawing.SolidBrush ([Drawing.Color]::FromArgb(245,245,245))
    $eye = New-Object Drawing.SolidBrush ([Drawing.Color]::FromArgb(30,30,34))
    $bob = 0; if ($isWalk) { $bob = @(-2,0,-1,0)[$frameIdx] } else { $bob = @(0,-2)[$frameIdx] }
    $swing = 0; if ($isWalk) { $swing = @(-4,0,4,0)[$frameIdx] }

    switch ($kind) {
        'humanoid' {
            $skin = New-Object Drawing.SolidBrush ([Drawing.Color]::FromArgb(235,205,175))
            $gg.FillRectangle($dark, $px + 38, $py + 70 + $bob, 8, 20 + $swing)
            $gg.FillRectangle($dark, $px + 50, $py + 70 + $bob, 8, 20 - $swing)
            $gg.FillRectangle($body, $px + 33, $py + 40 + $bob, 30, 32)
            $gg.FillRectangle($body, $px + 26, $py + 42 + $bob + $swing, 7, 22)
            $gg.FillRectangle($body, $px + 63, $py + 42 + $bob - $swing, 7, 22)
            $gg.FillEllipse($skin, $px + 36, $py + 14 + $bob, 24, 24)
            $gg.FillEllipse($white, $cx - 4, $py + 22 + $bob, 8, 8)
            $skin.Dispose()
            $ax = $dirVectors[$dirIndex][0][0]; $ay = $dirVectors[$dirIndex][0][1]
            Draw-Arrow $gg $cx ($py + 56 + $bob) $ax $ay ([Drawing.Color]::FromArgb(250,250,250))
        }
        'slime' {
            $squash = 0; if ($isWalk) { $squash = @(4,0,-3,0)[$frameIdx] } else { $squash = @(0,3)[$frameIdx] }
            $gg.FillEllipse($body, $px + 22, $py + 40 + $bob + $squash, 52, 44 - $squash)
            $gg.FillEllipse($eye, $px + 34, $py + 54 + $bob + $squash, 6, 9)
            $gg.FillEllipse($eye, $px + 56, $py + 54 + $bob + $squash, 6, 9)
            $gg.FillEllipse($white, $px + 26, $py + 46 + $bob + $squash, 12, 8)
            $ax = $dirVectors[$dirIndex][0][0]; $ay = $dirVectors[$dirIndex][0][1]
            Draw-Arrow $gg $cx ($py + 66 + $bob) $ax $ay ([Drawing.Color]::FromArgb(250,250,250))
        }
        'wolf' {
            $gg.FillRectangle($dark, $px + 30, $py + 70 + $bob, 7, 18 + $swing)
            $gg.FillRectangle($dark, $px + 44, $py + 70 + $bob, 7, 18 - $swing)
            $gg.FillRectangle($dark, $px + 58, $py + 70 + $bob, 7, 18 + $swing)
            $gg.FillRectangle($body, $px + 24, $py + 46 + $bob, 48, 26)
            $gg.FillRectangle($body, $px + 60, $py + 36 + $bob, 22, 22)
            $gg.FillRectangle($body, $px + 62, $py + 28 + $bob, 6, 10)
            $gg.FillRectangle($body, $px + 14, $py + 40 + $bob, 12, 10)
            $gg.FillEllipse($eye, $px + 72, $py + 42 + $bob, 5, 5)
            $ax = $dirVectors[$dirIndex][0][0]; $ay = $dirVectors[$dirIndex][0][1]
            Draw-Arrow $gg ($px + 71) ($py + 44 + $bob) $ax $ay ([Drawing.Color]::FromArgb(250,250,250))
        }
        'boar' {
            $gg.FillRectangle($dark, $px + 32, $py + 70 + $bob, 8, 16 + $swing)
            $gg.FillRectangle($dark, $px + 56, $py + 70 + $bob, 8, 16 - $swing)
            $gg.FillEllipse($body, $px + 22, $py + 44 + $bob, 52, 34)
            $gg.FillEllipse($body, $px + 58, $py + 50 + $bob, 22, 24)
            $gg.FillRectangle($white, $px + 66, $py + 64 + $bob, 8, 3)
            $gg.FillEllipse($eye, $px + 66, $py + 56 + $bob, 5, 5)
            $ax = $dirVectors[$dirIndex][0][0]; $ay = $dirVectors[$dirIndex][0][1]
            Draw-Arrow $gg ($px + 69) ($py + 58 + $bob) $ax $ay ([Drawing.Color]::FromArgb(250,250,250))
        }
    }
    $body.Dispose(); $dark.Dispose(); $white.Dispose(); $eye.Dispose()
}

function Generate-Actor([string]$outDir, [string]$kind, [Drawing.Color]$bodyColor, [string]$displayName,
                        [float]$moveSpeed, [string]$direction, [hashtable]$extraJson) {
    New-Item -ItemType Directory -Force -Path (Join-Path $outDir 'sprites') | Out-Null
    $bmp = New-Object Drawing.Bitmap ($frame * $cols), ($frame * 8)
    $g = [Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = 'AntiAlias'
    $g.TextRenderingHint = 'AntiAlias'
    $bg = New-Object Drawing.SolidBrush ([Drawing.Color]::FromArgb(40,40,46))
    $g.FillRectangle($bg, 0, 0, $bmp.Width, $bmp.Height)
    $font = New-Object Drawing.Font('Consolas', 9)
    $white = [Drawing.Brushes]::White

    for ($row = 0; $row -lt 8; $row++) {
        $py = $row * $frame
        for ($f = 0; $f -lt $cols; $f++) {
            $px = $f * $frame
            $isWalk = $f -ge 2
            $frameIdx = 0
            if ($isWalk) { $frameIdx = $f - 2 } else { $frameIdx = $f }
            Draw-Creature $g $px $py $kind $bodyColor $frameIdx $isWalk $row
            $pen = New-Object Drawing.Pen ([Drawing.Color]::FromArgb(70,70,80))
            $g.DrawRectangle($pen, $px, $py, $frame - 1, $frame - 1)
            $g.DrawString("$($rowDirs[$row].Substring(0,2)) $f", $font, $white, $px + 3, $py + 2)
            $pen.Dispose()
        }
    }
    $pngPath = Join-Path $outDir 'sprites\actor_debug.png'
    $bmp.Save($pngPath, [Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose(); $bmp.Dispose(); $font.Dispose(); $bg.Dispose()

    [System.IO.File]::WriteAllText((Join-Path $outDir 'animations.json'), $animJson)

    $charObj = [ordered]@{
        version = 1; name = $displayName
        spriteSheet = ($outDir.Replace('d:\LegendGame\Assets\','') + '\sprites\actor_debug.png').Replace('\','/')
        frameWidth = $frame; frameHeight = $frame
        visualWidth = $frame; visualHeight = $frame
        footprint = @{ width = 28; height = 18; offsetX = 0; offsetY = 4 }
        pivot = @{ x = 0.5; y = 0.85 }
        moveSpeed = $moveSpeed
        direction = $direction
        animations = ($outDir.Replace('d:\LegendGame\Assets\','') + '\animations.json').Replace('\','/')
    }
    if ($extraJson) { foreach ($k in $extraJson.Keys) { $charObj[$k] = $extraJson[$k] } }
    $charJson = $charObj | ConvertTo-Json -Depth 6
    [System.IO.File]::WriteAllText((Join-Path $outDir 'character.json'), $charJson)
    Write-Host "Generated: $outDir"
}

$assets = 'd:\LegendGame\Assets'
Generate-Actor "$assets\Characters\TestNPC" 'humanoid' ([Drawing.Color]::FromArgb(150,150,175)) 'TestNPC' 0 'south' $null
Generate-Actor "$assets\Monsters\Slime" 'slime' ([Drawing.Color]::FromArgb(90,200,95)) 'Slime' 90 'south' $null
Generate-Actor "$assets\Monsters\Wolf" 'wolf' ([Drawing.Color]::FromArgb(135,135,150)) 'Wolf' 150 'south' $null
Generate-Actor "$assets\Monsters\Boar" 'boar' ([Drawing.Color]::FromArgb(175,125,85)) 'Boar' 110 'south' $null

# monster.json per template
$monsters = @(
    @{ id='slime'; name='Slime'; dir='Slime';   aggro=260; leash=520; wander=160; vmin=2.0; vmax=5.0; stop=55; resume=75 },
    @{ id='wolf';  name='Wolf';  dir='Wolf';    aggro=340; leash=700; wander=200; vmin=2.5; vmax=6.0; stop=60; resume=85 },
    @{ id='boar';  name='Boar';  dir='Boar';    aggro=300; leash=600; wander=180; vmin=2.0; vmax=5.5; stop=60; resume=80 }
)
foreach ($m in $monsters) {
    $def = [ordered]@{
        version = 1
        id = $m.id
        name = $m.name
        character = ("Monsters/" + $m.dir + "/character.json")
        ai = [ordered]@{
            aggroRange = $m.aggro; leashRange = $m.leash; wanderRadius = $m.wander
            wanderIntervalMin = $m.vmin; wanderIntervalMax = $m.vmax
            stopDistance = $m.stop; resumeDistance = $m.resume
        }
    }
    $path = Join-Path $assets ("Monsters\" + $m.dir + "\monster.json")
    [System.IO.File]::WriteAllText($path, ($def | ConvertTo-Json -Depth 6))
    Write-Host "Generated: $path"
}
Write-Host "World actor resources done."
