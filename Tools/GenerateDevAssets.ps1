# ---------------------------------------------------------------------------
# Stage 24: Development Placeholder Assets generator (GDI+ / System.Drawing)
# 生成 Data/Assets/asset_manifest.json 登记的全部开发占位 PNG：
#   - 8 方向角色 spritesheet（行 = Direction8 顺序 S,SW,W,NW,N,NE,E,SE；列 = 帧）
#   - NPC / Portal 单方向循环帧
#   - 技能特效 / 地图 Tile / Props / UI 图标
# 要求（阶段24 指令三十八）：不能只是纯色方块，必须是简单有辨识度的 sprite。
# 用法：powershell -ExecutionPolicy Bypass -File Tools/GenerateDevAssets.ps1
# ---------------------------------------------------------------------------
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$root = Split-Path -Parent $PSScriptRoot   # 仓库根目录
$outRoot = Join-Path $root 'Assets'

function New-Dir([string]$path) {
    if (-not (Test-Path $path)) { New-Item -ItemType Directory -Path $path | Out-Null }
}

function Save-Png([System.Drawing.Bitmap]$bmp, [string]$path) {
    New-Dir (Split-Path -Parent $path)
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
}

$dirS = @( @(0,1), @(-0.707,0.707), @(-1,0), @(-0.707,-0.707),
           @(0,-1), @(0.707,-0.707), @(1,0), @(0.707,0.707) )  # S,SW,W,NW,N,NE,E,SE (y-down)

function New-Sheet([int]$cols, [int]$rows, [int]$fw, [int]$fh) {
    $bmp = [System.Drawing.Bitmap]::new($cols * $fw, $rows * $fh)
    return $bmp
}

function Get-G([System.Drawing.Bitmap]$bmp) {
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.Clear([System.Drawing.Color]::Transparent)
    return $g
}

# ---- 人形角色帧绘制（开发占位：辨识度优先）----
# $pose: idle/walk/attack/cast/hit/death  $frame: 帧序  $dirIdx: 0..7
function Draw-Humanoid([System.Drawing.Graphics]$g, [int]$cx, [int]$feetY,
                       [string]$pose, [int]$frame, [int]$dirIdx,
                       [System.Drawing.Color]$primary, [System.Drawing.Color]$trim) {
    $dx = $dirS[$dirIdx][0]; $dy = $dirS[$dirIdx][1]
    $pen = [System.Drawing.Pen]::new($primary, 4)
    $brush = [System.Drawing.SolidBrush]::new($primary)
    $trimPen = [System.Drawing.Pen]::new($trim, 3)
    $faceBrush = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255,236,200,169))

    $t = $frame
    $phase = $t / 4.0 * 2 * [math]::PI
    $lean = 0.0
    $crouch = 0.0
    $alpha = 255
    $bodyRot = 0.0

    switch ($pose) {
        'idle'   { $crouch = [math]::Sin($phase) * 1.2 }
        'walk'   { $crouch = [math]::Abs([math]::Sin($phase)) * -1.5 }
        'attack' { $lean = 4.0 * [math]::Sin($t / 4.0 * [math]::PI) * [math]::Sign($dx + 0.1) }
        'cast'   { $crouch = [math]::Sin($phase) * 1.0 }
        'hit'    { $lean = -5.0; }
        'death'  { $bodyRot = ($t / 4.0) * 80.0 * ([math]::Sign($dx + 0.2)); $alpha = [int](255 - $t * 45) }
    }

    $hipY = $feetY - 26 + $crouch
    $shoulderY = $feetY - 44 + $crouch
    $headY = $feetY - 52 + $crouch

    # 腿
    if ($pose -eq 'death') {
        $pen.Color = [System.Drawing.Color]::FromArgb($alpha, $primary)
        $g.DrawLine($pen, $cx, $hipY, $cx - 10, $feetY)
        $g.DrawLine($pen, $cx, $hipY, $cx + 12, $feetY)
    } elseif ($pose -eq 'walk') {
        $swing = [math]::Sin($phase) * 7.0
        $g.DrawLine($pen, $cx, $hipY, $cx + $swing, $feetY)
        $g.DrawLine($pen, $cx, $hipY, $cx - $swing, $feetY)
    } else {
        $g.DrawLine($pen, $cx, $hipY, $cx - 6, $feetY)
        $g.DrawLine($pen, $cx, $hipY, $cx + 6, $feetY)
    }

    # 躯干（含 lean）
    $bodyPen = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb($alpha, $primary), 6)
    $g.DrawLine($bodyPen, $cx, $hipY, $cx + $lean, $shoulderY)
    # 胸甲色带
    $trimBrush = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb($alpha, $trim))
    $g.FillRectangle($trimBrush, $cx + $lean - 5, $shoulderY + 4, 10, 5)

    # 手臂
    $armPen = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb($alpha, $primary), 4)
    switch ($pose) {
        'attack' {
            $ext = [math]::Sin($t / 4.0 * [math]::PI) * 14.0
            $ax = if ([math]::Abs($dx) -gt 0.3) { [math]::Sign($dx) } else { 1 }
            $g.DrawLine($armPen, $cx + $lean, $shoulderY + 2, $cx + $lean + $ax * (8 + $ext), $shoulderY - 6 + $ext * 0.4)
            $g.DrawLine($armPen, $cx + $lean, $shoulderY + 2, $cx + $lean - $ax * 8, $shoulderY + 8)
            # 刀光弧线
            if ($t -ge 1) {
                $slashPen = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(200, 255, 255, 220), 2)
                $g.DrawArc($slashPen, $cx + $ax * 4 - 14, $shoulderY - 16, 28, 26, -50 * $ax, 110 * $ax)
            }
        }
        'cast' {
            $g.DrawLine($armPen, $cx + $lean, $shoulderY + 2, $cx + $lean - 8, $shoulderY - 10)
            $g.DrawLine($armPen, $cx + $lean, $shoulderY + 2, $cx + $lean + 8, $shoulderY - 10)
            $orb = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb($alpha, 160, 220, 255))
            $g.FillEllipse($orb, $cx + $lean - 4, $shoulderY - 18, 8, 8)
        }
        'hit' {
            $g.DrawLine($armPen, $cx + $lean, $shoulderY + 2, $cx + $lean - 10, $shoulderY + 10)
            $g.DrawLine($armPen, $cx + $lean, $shoulderY + 2, $cx + $lean + 9, $shoulderY + 12)
        }
        'death' {
            $g.DrawLine($armPen, $cx + $lean, $shoulderY + 2, $cx + $lean - 12, $shoulderY + 14)
            $g.DrawLine($armPen, $cx + $lean, $shoulderY + 2, $cx + $lean + 12, $shoulderY + 14)
        }
        default {
            $aswing = if ($pose -eq 'walk') { [math]::Sin($phase) * 5.0 } else { [math]::Sin($phase) * 1.5 }
            $g.DrawLine($armPen, $cx + $lean, $shoulderY + 2, $cx + $lean - 7, $shoulderY + 12 + $aswing)
            $g.DrawLine($armPen, $cx + $lean, $shoulderY + 2, $cx + $lean + 7, $shoulderY + 12 - $aswing)
        }
    }

    # 头
    $headPen = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb($alpha, $primary), 2)
    $faceBrush2 = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb($alpha, 236, 200, 169))
    $g.FillEllipse($faceBrush2, $cx + $lean - 6, $headY - 6, 12, 12)
    $g.DrawEllipse($headPen, $cx + $lean - 6, $headY - 6, 12, 12)
    # 头盔/帽沿色带（方向上时看不到面部细节——简化统一处理）
    $helmBrush = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb($alpha, $trim))
    $g.FillRectangle($helmBrush, $cx + $lean - 7, $headY - 8, 14, 4)

    $pen.Dispose(); $brush.Dispose(); $trimPen.Dispose(); $faceBrush.Dispose()
    $bodyPen.Dispose(); $armPen.Dispose(); $headPen.Dispose(); $trimBrush.Dispose()
    $faceBrush2.Dispose(); $helmBrush.Dispose()
}

# ---- 史莱姆帧 ----
function Draw-Slime([System.Drawing.Graphics]$g, [int]$cx, [int]$feetY,
                    [string]$pose, [int]$frame, [int]$dirIdx) {
    $dx = $dirS[$dirIdx][0]
    $phase = $frame / 4.0 * 2 * [math]::PI
    $sx = 20.0; $sy = 15.0; $alpha = 255; $flash = $false
    switch ($pose) {
        'idle'   { $sy = 15.0 + [math]::Sin($phase) * 2.0; $sx = 20.0 - [math]::Sin($phase) * 1.5 }
        'walk'   { $sx = 20.0 + [math]::Sin($phase) * 3.0; $sy = 15.0 - [math]::Abs([math]::Sin($phase)) * 3.0 }
        'attack' { $sx = 20.0 + 6.0 * [math]::Sin($frame / 4.0 * [math]::PI); $sy = 15.0 - 4.0 * [math]::Sin($frame / 4.0 * [math]::PI) }
        'hit'    { $flash = $true; $sx = 22.0; $sy = 13.0 }
        'death'  { $sx = 20.0 + $frame * 3.0; $sy = [math]::Max(4.0, 15.0 - $frame * 3.5); $alpha = [int](255 - $frame * 50) }
    }
    $bodyColor = [System.Drawing.Color]::FromArgb($alpha, 92, 200, 110)
    if ($flash) { $bodyColor = [System.Drawing.Color]::FromArgb($alpha, 235, 245, 235) }
    $brush = [System.Drawing.SolidBrush]::new($bodyColor)
    $dark = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb($alpha, 52, 150, 74))
    $g.FillEllipse($brush, $cx - $sx, $feetY - $sy * 2, $sx * 2, $sy * 2)
    $g.FillEllipse($dark, $cx - $sx * 0.55, $feetY - $sy * 0.8, $sx * 1.1, $sy * 0.8)
    # 眼睛（朝向偏移）
    $eyeColor = [System.Drawing.Color]::FromArgb($alpha, 20, 40, 26)
    $eye = [System.Drawing.SolidBrush]::new($eyeColor)
    $ex = [math]::Sign($dx) * 4.0
    $g.FillEllipse($eye, $cx - 6 + $ex, $feetY - $sy * 1.4, 3.5, 4.5)
    $g.FillEllipse($eye, $cx + 3 + $ex, $feetY - $sy * 1.4, 3.5, 4.5)
    if ($pose -eq 'attack' -and $frame -ge 2) {
        $mouth = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb($alpha, 20, 40, 26), 2)
        $g.DrawArc($mouth, $cx - 5 + $ex, $feetY - $sy * 0.95, 10, 6, 0, 180)
        $mouth.Dispose()
    }
    $brush.Dispose(); $dark.Dispose(); $eye.Dispose()
}

# ---- NPC 帧（长袍 + 类型配色 + 头饰）----
function Draw-Npc([System.Drawing.Graphics]$g, [int]$cx, [int]$feetY,
                  [int]$frame, [System.Drawing.Color]$robe, [System.Drawing.Color]$hat) {
    $phase = $frame / 4.0 * 2 * [math]::PI
    $bob = [math]::Sin($phase) * 1.2
    $robeBrush = [System.Drawing.SolidBrush]::new($robe)
    $hatBrush = [System.Drawing.SolidBrush]::new($hat)
    $skin = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 236, 200, 169))
    $line = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(255, 60, 50, 40), 2)
    # 长袍（梯形）
    $topY = $feetY - 40 + $bob
    $pts = @(
        ([System.Drawing.Point]::new($cx - 7, $topY)),
        ([System.Drawing.Point]::new($cx + 7, $topY)),
        ([System.Drawing.Point]::new($cx + 11, $feetY)),
        ([System.Drawing.Point]::new($cx - 11, $feetY))
    )
    $g.FillPolygon($robeBrush, $pts)
    $g.DrawPolygon($line, $pts)
    # 头
    $g.FillEllipse($skin, $cx - 6, $topY - 14 + $bob, 12, 12)
    # 帽/头饰
    $g.FillRectangle($hatBrush, $cx - 8, $topY - 19 + $bob, 16, 5)
    $g.FillRectangle($hatBrush, $cx - 5, $topY - 23 + $bob, 10, 5)
    # 手杖（QuestGiver/MultiFunction）或钱袋（Merchant）
    $g.DrawLine($line, $cx + 9, $topY + 6, $cx + 9, $feetY)
    $robeBrush.Dispose(); $hatBrush.Dispose(); $skin.Dispose(); $line.Dispose()
}

# ---- Portal 帧 ----
function Draw-Portal([System.Drawing.Graphics]$g, [int]$cx, [int]$cy, [int]$frame) {
    $phase = $frame / 4.0 * 2 * [math]::PI
    $glow = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(70, 120, 200, 255))
    $ring1 = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(230, 90, 170, 255), 4)
    $ring2 = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(180, 190, 120, 255), 3)
    $core = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(120, 60, 120, 240))
    $w = 44 + [math]::Sin($phase) * 5
    $h = 78 + [math]::Sin($phase) * 4
    $g.FillEllipse($glow, $cx - $w * 0.75, $cy - $h * 0.6, $w * 1.5, $h * 1.2)
    $g.FillEllipse($core, $cx - $w / 2, $cy - $h / 2, $w, $h)
    $g.DrawEllipse($ring1, $cx - $w / 2, $cy - $h / 2, $w, $h)
    $rot = $phase * 40
    $g.TranslateTransform($cx, $cy)
    $g.RotateTransform($rot)
    $g.DrawEllipse($ring2, -$w * 0.35, -$h * 0.35, $w * 0.7, $h * 0.7)
    $g.ResetTransform()
    $glow.Dispose(); $ring1.Dispose(); $ring2.Dispose(); $core.Dispose()
}

# ================= 角色类色板 =================
$classes = @{
    warrior = @{ primary = [System.Drawing.Color]::FromArgb(255, 59, 111, 212); trim = [System.Drawing.Color]::FromArgb(255, 200, 214, 235) }
    mage    = @{ primary = [System.Drawing.Color]::FromArgb(255, 148, 79, 212); trim = [System.Drawing.Color]::FromArgb(255, 226, 200, 255) }
    taoist  = @{ primary = [System.Drawing.Color]::FromArgb(255, 63, 164, 106); trim = [System.Drawing.Color]::FromArgb(255, 205, 240, 210) }
}

# ---- 玩家 6 动作 × 8 方向 ----
$playerPoses = @(
    @{ name='idle';   frames=4; loopRows=$true },
    @{ name='walk';   frames=4; loopRows=$true },
    @{ name='attack'; frames=4; loopRows=$true },
    @{ name='cast';   frames=4; loopRows=$true },
    @{ name='hit';    frames=2; loopRows=$true },
    @{ name='death';  frames=4; loopRows=$true }
)
foreach ($cls in @('warrior','mage','taoist')) {
    $pal = $classes[$cls]
    foreach ($pose in $playerPoses) {
        $fw = 48; $fh = 64
        $bmp = New-Sheet $pose.frames 8 $fw $fh
        $g = Get-G $bmp
        for ($d = 0; $d -lt 8; ++$d) {
            for ($f = 0; $f -lt $pose.frames; ++$f) {
                $cx = $f * $fw + $fw / 2
                $feetY = $d * $fh + [int]($fh * 0.9)
                Draw-Humanoid $g $cx $feetY $pose.name $f $d $pal.primary $pal.trim
            }
        }
        $g.Dispose()
        Save-Png $bmp (Join-Path $outRoot "Characters/Player/${cls}_$($pose.name).png")
    }
}

# ---- Slime 5 动作 × 8 方向 ----
$slimePoses = @('idle','walk','attack','hit','death')
foreach ($pose in $slimePoses) {
    $fw = 48; $fh = 48; $frames = 4
    $bmp = New-Sheet $frames 8 $fw $fh
    $g = Get-G $bmp
    for ($d = 0; $d -lt 8; ++$d) {
        for ($f = 0; $f -lt $frames; ++$f) {
            $cx = $f * $fw + $fw / 2
            $feetY = $d * $fh + [int]($fh * 0.9)
            Draw-Slime $g $cx $feetY $pose $f $d
        }
    }
    $g.Dispose()
    Save-Png $bmp (Join-Path $outRoot "Characters/Monsters/slime_$pose.png")
}

# ---- NPC idle（1 方向 4 帧）----
$npcs = @(
    @{ file='npc_elder_idle';    robe=[System.Drawing.Color]::FromArgb(255,122,92,60);  hat=[System.Drawing.Color]::FromArgb(255,80,60,40) },
    @{ file='npc_merchant_idle'; robe=[System.Drawing.Color]::FromArgb(255,214,168,42); hat=[System.Drawing.Color]::FromArgb(255,120,90,30) },
    @{ file='npc_wayfarer_idle'; robe=[System.Drawing.Color]::FromArgb(255,110,120,135);hat=[System.Drawing.Color]::FromArgb(255,60,66,80) },
    @{ file='npc_guide_idle';    robe=[System.Drawing.Color]::FromArgb(255,52,150,150); hat=[System.Drawing.Color]::FromArgb(255,30,100,105) }
)
foreach ($npc in $npcs) {
    $fw = 48; $fh = 64; $frames = 4
    $bmp = New-Sheet $frames 1 $fw $fh
    $g = Get-G $bmp
    for ($f = 0; $f -lt $frames; ++$f) {
        Draw-Npc $g ($f * $fw + $fw / 2) ([int]($fh * 0.9)) $f $npc.robe $npc.hat
    }
    $g.Dispose()
    Save-Png $bmp (Join-Path $outRoot "Characters/NPC/$($npc.file).png")
}

# ---- Portal（1 方向 4 帧 64×96）----
$bmp = New-Sheet 4 1 64 96
$g = Get-G $bmp
for ($f = 0; $f -lt 4; ++$f) {
    Draw-Portal $g ($f * 64 + 32) 52 $f
}
$g.Dispose()
Save-Png $bmp (Join-Path $outRoot 'Effects/portal_default.png')

# ---- 技能特效 ----
# 刀光 4 帧 48×48
$bmp = New-Sheet 4 1 48 48
$g = Get-G $bmp
for ($f = 0; $f -lt 4; ++$f) {
    $cx = $f * 48 + 24
    $alpha = [int](240 - $f * 45)
    $pen = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb($alpha, 255, 250, 210), 4)
    $g.DrawArc($pen, $cx - 16, 8, 30, 30, (-70 + $f * 40), 100)
    $pen2 = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb([int]($alpha*0.6), 255, 255, 255), 2)
    $g.DrawArc($pen2, $cx - 12, 12, 22, 22, (-60 + $f * 40), 80)
    $pen.Dispose(); $pen2.Dispose()
}
$g.Dispose()
Save-Png $bmp (Join-Path $outRoot 'Effects/fx_slash.png')

# 火弹 4 帧 24×24
$bmp = New-Sheet 4 1 24 24
$g = Get-G $bmp
for ($f = 0; $f -lt 4; ++$f) {
    $cx = $f * 24 + 12
    $core = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 255, 200, 90))
    $halo = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(120, 255, 120, 40))
    $r = 5 + [math]::Sin($f / 4.0 * 2 * [math]::PI) * 1.5
    $g.FillEllipse($halo, $cx - $r * 2, 12 - $r * 2, $r * 4, $r * 4)
    $g.FillEllipse($core, $cx - $r, 12 - $r, $r * 2, $r * 2)
    $tail = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(160, 255, 150, 50), 2)
    $g.DrawLine($tail, $cx - $r, 12, $cx - 11, 12)
    $core.Dispose(); $halo.Dispose(); $tail.Dispose()
}
$g.Dispose()
Save-Png $bmp (Join-Path $outRoot 'Effects/fx_fire_bolt.png')

# 火弹命中 4 帧 48×48
$bmp = New-Sheet 4 1 48 48
$g = Get-G $bmp
for ($f = 0; $f -lt 4; ++$f) {
    $cx = $f * 48 + 24
    $r = 6 + $f * 7
    $alpha = [int](230 - $f * 55)
    $halo = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb($alpha, 255, 140, 40))
    $core = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb([int]($alpha * 0.8), 255, 230, 120))
    $g.FillEllipse($halo, $cx - $r, 24 - $r, $r * 2, $r * 2)
    $g.FillEllipse($core, $cx - $r * 0.5, 24 - $r * 0.5, $r, $r)
    $spike = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb($alpha, 255, 190, 70), 2)
    for ($i = 0; $i -lt 6; ++$i) {
        $ang = $i * 60.0 * [math]::PI / 180.0 + $f * 0.35
        $g.DrawLine($spike, $cx + [math]::Cos($ang) * $r * 0.6, 24 + [math]::Sin($ang) * $r * 0.6,
                            $cx + [math]::Cos($ang) * ($r + 6), 24 + [math]::Sin($ang) * ($r + 6))
    }
    $halo.Dispose(); $core.Dispose(); $spike.Dispose()
}
$g.Dispose()
Save-Png $bmp (Join-Path $outRoot 'Effects/fx_fire_bolt_impact.png')

# 旋风斩 4 帧 128×128
$bmp = New-Sheet 4 1 128 128
$g = Get-G $bmp
for ($f = 0; $f -lt 4; ++$f) {
    $cx = $f * 128 + 64
    $rot = $f * 45.0
    $pen = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(190, 200, 230, 255), 5)
    $pen2 = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(120, 255, 255, 255), 2)
    $g.TranslateTransform($cx, 64)
    $g.RotateTransform($rot)
    $g.DrawArc($pen, -44, -44, 88, 88, 0, 240)
    $g.DrawArc($pen2, -30, -30, 60, 60, 140, 200)
    $g.DrawArc($pen, -16, -16, 32, 32, 60, 260)
    $g.ResetTransform()
    $pen.Dispose(); $pen2.Dispose()
}
$g.Dispose()
Save-Png $bmp (Join-Path $outRoot 'Effects/fx_whirlwind.png')

# ---- Tile（带噪点与边缘阴影）----
function New-Tile([string]$path, [System.Drawing.Color]$base, [System.Drawing.Color]$speck,
                  [System.Drawing.Color]$shade) {
    $bmp = [System.Drawing.Bitmap]::new(64, 64)
    $g = Get-G $bmp
    $b = [System.Drawing.SolidBrush]::new($base)
    $g.FillRectangle($b, 0, 0, 64, 64)
    $rnd = [System.Random]::new(12345)
    $s = [System.Drawing.SolidBrush]::new($speck)
    for ($i = 0; $i -lt 90; ++$i) {
        $x = $rnd.Next(0, 62); $y = $rnd.Next(0, 62)
        $g.FillRectangle($s, $x, $y, 2, 2)
    }
    $edge = [System.Drawing.Pen]::new($shade, 2)
    $g.DrawRectangle($edge, 1, 1, 61, 61)
    $g.Dispose(); $b.Dispose(); $s.Dispose(); $edge.Dispose()
    Save-Png $bmp $path
}
New-Tile (Join-Path $outRoot 'World/Tiles/tile_grass_a.png') `
    ([System.Drawing.Color]::FromArgb(255, 96, 158, 84)) `
    ([System.Drawing.Color]::FromArgb(255, 122, 186, 100)) `
    ([System.Drawing.Color]::FromArgb(255, 74, 128, 66))
New-Tile (Join-Path $outRoot 'World/Tiles/tile_grass_b.png') `
    ([System.Drawing.Color]::FromArgb(255, 108, 172, 88)) `
    ([System.Drawing.Color]::FromArgb(255, 140, 200, 108)) `
    ([System.Drawing.Color]::FromArgb(255, 84, 142, 70))
New-Tile (Join-Path $outRoot 'World/Tiles/tile_stone_a.png') `
    ([System.Drawing.Color]::FromArgb(255, 136, 134, 130)) `
    ([System.Drawing.Color]::FromArgb(255, 158, 156, 150)) `
    ([System.Drawing.Color]::FromArgb(255, 108, 106, 102))

# ---- Props ----
# 树 96×128
$bmp = [System.Drawing.Bitmap]::new(96, 128)
$g = Get-G $bmp
$trunk = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 108, 76, 48))
$leaf1 = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 62, 130, 66))
$leaf2 = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 84, 158, 82))
$g.FillRectangle($trunk, 44, 82, 10, 38)
$g.FillEllipse($leaf2, 20, 34, 58, 52)
$g.FillEllipse($leaf1, 14, 52, 70, 46)
$g.FillEllipse($leaf2, 30, 18, 40, 40)
$g.Dispose(); $trunk.Dispose(); $leaf1.Dispose(); $leaf2.Dispose()
Save-Png $bmp (Join-Path $outRoot 'World/Props/prop_tree_oak.png')

# 岩石 64×48
$bmp = [System.Drawing.Bitmap]::new(64, 48)
$g = Get-G $bmp
$rock = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 138, 136, 132))
$rockDark = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 110, 108, 104))
$pts = @(
    ([System.Drawing.Point]::new(8, 42)), ([System.Drawing.Point]::new(14, 22)),
    ([System.Drawing.Point]::new(30, 10)), ([System.Drawing.Point]::new(48, 16)),
    ([System.Drawing.Point]::new(58, 42))
)
$g.FillPolygon($rock, $pts)
$g.FillEllipse($rockDark, 20, 26, 22, 14)
$g.Dispose(); $rock.Dispose(); $rockDark.Dispose()
Save-Png $bmp (Join-Path $outRoot 'World/Props/prop_rock_gray.png')

# 灌木 48×32
$bmp = [System.Drawing.Bitmap]::new(48, 32)
$g = Get-G $bmp
$b1 = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 70, 140, 70))
$b2 = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 92, 168, 88))
$g.FillEllipse($b1, 4, 12, 22, 18)
$g.FillEllipse($b1, 20, 10, 24, 20)
$g.FillEllipse($b2, 10, 8, 18, 14)
$g.Dispose(); $b1.Dispose(); $b2.Dispose()
Save-Png $bmp (Join-Path $outRoot 'World/Props/prop_bush_green.png')

# 花丛 48×24
$bmp = [System.Drawing.Bitmap]::new(48, 24)
$g = Get-G $bmp
$grass = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 88, 158, 78))
$petal = @([System.Drawing.Color]::FromArgb(255, 232, 96, 120), [System.Drawing.Color]::FromArgb(255, 240, 210, 80), [System.Drawing.Color]::FromArgb(255, 150, 130, 240))
$g.FillRectangle($grass, 2, 14, 44, 8)
$rnd = [System.Random]::new(777)
for ($i = 0; $i -lt 9; ++$i) {
    $c = [System.Drawing.SolidBrush]::new($petal[$i % 3])
    $g.FillEllipse($c, $rnd.Next(4, 40), $rnd.Next(6, 16), 4, 4)
    $c.Dispose()
}
$g.Dispose(); $grass.Dispose()
Save-Png $bmp (Join-Path $outRoot 'World/Props/prop_flower_patch.png')

# 遗迹石柱 48×96
$bmp = [System.Drawing.Bitmap]::new(48, 96)
$g = Get-G $bmp
$stone = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 168, 162, 148))
$stoneDark = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 132, 126, 114))
$crack = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(255, 108, 102, 92), 2)
$g.FillRectangle($stone, 14, 18, 20, 68)
$g.FillRectangle($stoneDark, 10, 80, 28, 10)
$g.FillRectangle($stone, 10, 8, 28, 10)
$g.DrawLine($crack, 20, 24, 26, 44)
$g.DrawLine($crack, 26, 44, 22, 66)
$g.Dispose(); $stone.Dispose(); $stoneDark.Dispose(); $crack.Dispose()
Save-Png $bmp (Join-Path $outRoot 'World/Props/prop_ruin_pillar.png')

# ---- UI ----
# 职业头像 48×48
foreach ($cls in @('warrior','mage','taoist')) {
    $pal = $classes[$cls]
    $bmp = [System.Drawing.Bitmap]::new(48, 48)
    $g = Get-G $bmp
    $bg = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 34, 40, 54))
    $frame = [System.Drawing.Pen]::new($pal.primary, 3)
    $skin = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 236, 200, 169))
    $g.FillRectangle($bg, 0, 0, 48, 48)
    # 肩部（袍/甲）
    $body = [System.Drawing.SolidBrush]::new($pal.primary)
    $g.FillRectangle($body, 8, 34, 32, 14)
    # 头
    $g.FillEllipse($skin, 15, 10, 18, 20)
    # 发/盔
    $trim = [System.Drawing.SolidBrush]::new($pal.trim)
    $g.FillRectangle($trim, 13, 7, 22, 6)
    $g.DrawRectangle($frame, 1, 1, 45, 45)
    $g.Dispose(); $bg.Dispose(); $frame.Dispose(); $skin.Dispose(); $body.Dispose(); $trim.Dispose()
    Save-Png $bmp (Join-Path $outRoot "UI/portrait_$cls.png")
}

# 技能图标 32×32
# 1001 Quick Strike：剑
$bmp = [System.Drawing.Bitmap]::new(32, 32)
$g = Get-G $bmp
$bg = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 40, 48, 64))
$blade = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 210, 220, 235))
$hilt = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 180, 140, 60))
$g.FillRectangle($bg, 0, 0, 32, 32)
$g.FillPolygon($blade, @(
    ([System.Drawing.Point]::new(16, 3)), ([System.Drawing.Point]::new(19, 8)),
    ([System.Drawing.Point]::new(19, 20)), ([System.Drawing.Point]::new(13, 20)),
    ([System.Drawing.Point]::new(13, 8))))
$g.FillRectangle($hilt, 10, 20, 12, 3)
$g.FillRectangle($hilt, 14, 23, 4, 7)
$g.Dispose(); $bg.Dispose(); $blade.Dispose(); $hilt.Dispose()
Save-Png $bmp (Join-Path $outRoot 'UI/icon_skill_1001.png')
# 1002 Fire Bolt：火球
$bmp = [System.Drawing.Bitmap]::new(32, 32)
$g = Get-G $bmp
$bg = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 48, 32, 30))
$halo = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(180, 255, 120, 40))
$core = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 255, 210, 90))
$g.FillRectangle($bg, 0, 0, 32, 32)
$g.FillEllipse($halo, 5, 5, 22, 22)
$g.FillEllipse($core, 10, 10, 12, 12)
$g.Dispose(); $bg.Dispose(); $halo.Dispose(); $core.Dispose()
Save-Png $bmp (Join-Path $outRoot 'UI/icon_skill_1002.png')
# 1003 Whirlwind：旋风
$bmp = [System.Drawing.Bitmap]::new(32, 32)
$g = Get-G $bmp
$bg = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 30, 42, 58))
$w1 = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(230, 170, 220, 255), 3)
$w2 = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(160, 255, 255, 255), 2)
$g.FillRectangle($bg, 0, 0, 32, 32)
$g.DrawArc($w1, 5, 5, 22, 22, 0, 260)
$g.DrawArc($w2, 9, 9, 14, 14, 130, 220)
$g.Dispose(); $bg.Dispose(); $w1.Dispose(); $w2.Dispose()
Save-Png $bmp (Join-Path $outRoot 'UI/icon_skill_1003.png')

Write-Host 'Dev placeholder assets generated OK.'
