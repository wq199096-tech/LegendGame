# Generate TestHero debug sprite sheet (6 frames x 8 directions, 96x96 per frame)
# Layout: columns = frames (0-1 idle, 2-5 walk), rows = directions
# Row order: South, Southwest, West, Northwest, North, Northeast, East, Southeast
# Also generates character.json + animations.json
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$outDir = 'd:\LegendGame\Assets\Characters\TestHero'
New-Item -ItemType Directory -Force -Path (Join-Path $outDir 'sprites') | Out-Null

$frame = 96
$cols = 6
$rowDirs = @(
    @{ name='south';     color=[Drawing.Color]::FromArgb(210,70,70);   arrow=@(0,1) },
    @{ name='southwest'; color=[Drawing.Color]::FromArgb(225,145,55);  arrow=@(-1,1) },
    @{ name='west';      color=[Drawing.Color]::FromArgb(215,205,70);  arrow=@(-1,0) },
    @{ name='northwest'; color=[Drawing.Color]::FromArgb(95,195,95);   arrow=@(-1,-1) },
    @{ name='north';     color=[Drawing.Color]::FromArgb(75,195,205);  arrow=@(0,-1) },
    @{ name='northeast'; color=[Drawing.Color]::FromArgb(85,125,225);  arrow=@(1,-1) },
    @{ name='east';      color=[Drawing.Color]::FromArgb(165,95,215);  arrow=@(1,0) },
    @{ name='southeast'; color=[Drawing.Color]::FromArgb(225,115,165); arrow=@(1,1) }
)

$sheetW = $frame * $cols
$sheetH = $frame * $rowDirs.Count
$bmp = New-Object Drawing.Bitmap $sheetW, $sheetH
$g = [Drawing.Graphics]::FromImage($bmp)
$g.InterpolationMode = 'NearestNeighbor'
$g.SmoothingMode = 'AntiAlias'
$g.TextRenderingHint = 'AntiAlias'

$font = New-Object Drawing.Font('Consolas', 9)
$white = [Drawing.Brushes]::White
$dark = New-Object Drawing.SolidBrush ([Drawing.Color]::FromArgb(40,40,46))
$g.FillRectangle($dark, 0, 0, $sheetW, $sheetH)

function Draw-Frame([Drawing.Graphics]$gg, [int]$px, [int]$py, $dirDef, [int]$frameIndex, [bool]$isWalk) {
    $cx = $px + 48
    $bodyBrush = New-Object Drawing.SolidBrush $dirDef.color
    $skin = New-Object Drawing.SolidBrush ([Drawing.Color]::FromArgb(235,205,175))

    # breathing / walk bob offset
    $bob = 0
    if ($isWalk) { $bob = @(-2,0,-1,0)[$frameIndex] } else { $bob = @(0,-2)[$frameIndex] }

    # legs (walk swing)
    $legSwing = 0
    if ($isWalk) { $legSwing = @(-4,0,4,0)[$frameIndex] }
    $legColor = New-Object Drawing.SolidBrush ([Drawing.Color]::FromArgb(60,60,70))
    $gg.FillRectangle($legColor, $px + 38, $py + 70 + $bob, 8, 20 + $legSwing)
    $gg.FillRectangle($legColor, $px + 50, $py + 70 + $bob, 8, 20 - $legSwing)

    # body
    $gg.FillRectangle($bodyBrush, $px + 33, $py + 40 + $bob, 30, 32)
    # head
    $gg.FillEllipse($skin, $px + 36, $py + 14 + $bob, 24, 24)

    # arms (walk swing)
    $armSwing = 0
    if ($isWalk) { $armSwing = @(4,0,-4,0)[$frameIndex] }
    $gg.FillRectangle($bodyBrush, $px + 26, $py + 42 + $bob + $armSwing, 7, 22)
    $gg.FillRectangle($bodyBrush, $px + 63, $py + 42 + $bob - $armSwing, 7, 22)

    # direction arrow (rotation by arrow vector)
    $ax = $dirDef.arrow[0]; $ay = $dirDef.arrow[1]
    $acx = $cx; $acy = $py + 56 + $bob
    $len = 16
    $tipX = $acx + $ax * $len; $tipY = $acy + $ay * $len
    $perpX = -$ay; $perpY = $ax
    $p1 = New-Object Drawing.Point ([int]($tipX + $perpX*6 - $ax*4), ([int]($tipY + $perpY*6 - $ay*4)))
    $p2 = New-Object Drawing.Point ([int]($tipX - $perpX*6 - $ax*4), ([int]($tipY - $perpY*6 - $ay*4)))
    $p3 = New-Object Drawing.Point ([int]$tipX, [int]$tipY)
    $arrowBrush = New-Object Drawing.SolidBrush ([Drawing.Color]::FromArgb(250,250,250))
    $gg.FillPolygon($arrowBrush, @($p1, $p2, $p3))

    # feet marker (dark line at feet position ~ y=85% of frame)
    $feetPen = New-Object Drawing.Pen ([Drawing.Color]::FromArgb(255,240,200)), 2
    $gg.DrawLine($feetPen, $px + 34, $py + 82, $px + 62, $py + 82)

    $arrowBrush.Dispose(); $bodyBrush.Dispose(); $skin.Dispose(); $legColor.Dispose(); $feetPen.Dispose()
}

$rowIndex = 0
foreach ($dirDef in $rowDirs) {
    $py = $rowIndex * $frame
    for ($f = 0; $f -lt $cols; $f++) {
        $px = $f * $frame
        $isWalk = $f -ge 2
        $frameIdx = 0
        if ($isWalk) { $frameIdx = $f - 2 } else { $frameIdx = $f }
        Draw-Frame $g $px $py $dirDef $frameIdx $isWalk
        # frame cell border + labels
        $cellPen = New-Object Drawing.Pen ([Drawing.Color]::FromArgb(70,70,80))
        $g.DrawRectangle($cellPen, $px, $py, $frame - 1, $frame - 1)
        $g.DrawString("$($dirDef.name.Substring(0,2)) $f", $font, $white, $px + 3, $py + 2)
        $cellPen.Dispose()
    }
    $rowIndex++
}

$pngPath = Join-Path $outDir 'sprites\hero_debug.png'
$bmp.Save($pngPath, [Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
Write-Host "SpriteSheet saved: $pngPath ($sheetW x $sheetH)"

# ---- animations.json ----
# Frame index = row * 6 + col（行优先），每方向独占一行：
# South=row0(0-5) SW=row1(6-11) West=row2(12-17) NW=row3(18-23)
# North=row4(24-29) NE=row5(30-35) East=row6(36-41) SE=row7(42-47)
$durationsIdle = 0.35
$durationsWalk = 0.12
$clips = [ordered]@{}
for ($row = 0; $row -lt $rowDirs.Count; $row++) {
    $n = $rowDirs[$row].name
    $base = $row * 6
    $clips["idle_$n"] = @{
        loop = $true
        frames = @(
            @{ index = $base;     duration = $durationsIdle },
            @{ index = $base + 1; duration = $durationsIdle }
        )
    }
    $clips["walk_$n"] = @{
        loop = $true
        frames = @(0..3 | ForEach-Object { @{ index = $base + 2 + $_; duration = $durationsWalk } })
    }
}
$animJson = @{ version = 1; clips = $clips } | ConvertTo-Json -Depth 6 -Compress
[System.IO.File]::WriteAllText((Join-Path $outDir 'animations.json'), $animJson)
Write-Host "animations.json saved (frame index = row*6 + col)"

# ---- character.json ----
$charJson = @'
{
  "version": 1,
  "name": "TestHero",
  "spriteSheet": "Characters/TestHero/sprites/hero_debug.png",
  "frameWidth": 96,
  "frameHeight": 96,
  "visualWidth": 96,
  "visualHeight": 96,
  "footprint": { "width": 28, "height": 18, "offsetX": 0, "offsetY": 4 },
  "pivot": { "x": 0.5, "y": 0.85 },
  "moveSpeed": 200,
  "animations": "Characters/TestHero/animations.json"
}
'@
[System.IO.File]::WriteAllText((Join-Path $outDir 'character.json'), $charJson)
Write-Host "character.json saved"
