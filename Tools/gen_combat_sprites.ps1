# 阶段5：生成战斗扩展 Debug SpriteSheet（15列 x 8行 = 120帧，96x96/帧）
# 帧布局（每行15帧）：col 0-1 idle | col 2-5 walk | col 6-9 attack | col 10 hit | col 11-14 death
# 生成对象：TestHero / TestNPC / Slime / Wolf / Boar
# 同时生成 animations.json（attack 带 attack_hit 事件）、character.json（TestHero 带 combat 块）
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$frame = 96
$cols = 15
$rows = 8

# 8 方向定义（行序与 Direction8 枚举一致）：body 颜色 + 朝向箭头
$rowDirs = @(
    @{ name='south';     arrow=@(0,1) },
    @{ name='southwest'; arrow=@(-1,1) },
    @{ name='west';      arrow=@(-1,0) },
    @{ name='northwest'; arrow=@(-1,-1) },
    @{ name='north';     arrow=@(0,-1) },
    @{ name='northeast'; arrow=@(1,-1) },
    @{ name='east';      arrow=@(1,0) },
    @{ name='southeast'; arrow=@(1,1) }
)

# 角色定义：输出目录 + body 颜色 + 形状参数 + 是否带 combat 块
$actors = @(
    @{ dir='Characters\TestHero'; color=[Drawing.Color]::FromArgb(210,70,70);
       bodyW=30; bodyH=34; headR=11; combat=$true },
    @{ dir='Characters\TestNPC'; color=[Drawing.Color]::FromArgb(150,150,175);
       bodyW=28; bodyH=32; headR=10; combat=$false },
    @{ dir='Monsters\Slime'; color=[Drawing.Color]::FromArgb(90,200,95);
       bodyW=36; bodyH=26; headR=0; combat=$false },
    @{ dir='Monsters\Wolf'; color=[Drawing.Color]::FromArgb(135,135,150);
       bodyW=38; bodyH=24; headR=0; combat=$false },
    @{ dir='Monsters\Boar'; color=[Drawing.Color]::FromArgb(175,125,85);
       bodyW=34; bodyH=28; headR=0; combat=$false }
)

$sheetW = $frame * $cols
$sheetH = $frame * $rows
$flash = [Drawing.Color]::FromArgb(255,255,255)

function Draw-Arrow([Drawing.Graphics]$gg, [int]$cx, [int]$cy, $dirDef, [Drawing.Color]$tint) {
    $ax = $dirDef.arrow[0]; $ay = $dirDef.arrow[1]
    $pen = New-Object Drawing.Pen $tint, 3
    [void]$gg.DrawLine($pen, $cx - $ax * 10, $cy - $ay * 10, $cx + $ax * 14, $cy + $ay * 14)
    # 箭头头部
    $px = -$ay; $py = $ax
    [void]$gg.DrawLine($pen, $cx + $ax * 14, $cy + $ay * 14,
        $cx + $ax * 6 + $px * 6, $cy + $ay * 6 + $py * 6)
    [void]$gg.DrawLine($pen, $cx + $ax * 14, $cy + $ay * 14,
        $cx + $ax * 6 - $px * 6, $cy + $ay * 6 - $py * 6)
    $pen.Dispose()
}

# 绘制单帧：col 决定姿态
function Draw-Frame([Drawing.Graphics]$gg, [int]$ox, [int]$oy, $actor, $dirDef, [int]$col) {
    $feetX = $ox + 48
    $feetY = $oy + 84
    $bodyColor = $actor.color
    $bodyW = $actor.bodyW
    $bodyH = $actor.bodyH

    # 姿态参数
    $sink = 0; $lean = 0.0; $bodyHScale = 1.0; $alpha = 255; $useFlash = $false; $armSwing = 0
    if ($col -le 1) {
        # idle：帧1 轻微下沉呼吸
        if ($col -eq 1) { $sink = 2 }
    } elseif ($col -le 5) {
        # walk：4 帧腿部摆动（身体上下 + 水平偏移）
        $walkPhases = @(0, 3, 0, -3)
        $sink = @(0, 3, 1, 3)[$col - 2]
        $armSwing = $walkPhases[$col - 2]
    } elseif ($col -le 9) {
        # attack：4 帧 挥击（6=抬 7=挥 8=命中最亮 9=收）
        $attackPhase = $col - 6
        $armSwing = @(8, -10, -16, -6)[$attackPhase]
        if ($attackPhase -eq 2) { $useFlash = $true; $lean = 2.0 }
        elseif ($attackPhase -eq 1) { $lean = 1.0 }
    } elseif ($col -eq 10) {
        # hit：后仰 + 闪白
        $lean = -5.0
        $sink = 2
        $useFlash = $true
    } else {
        # death：4 帧逐渐倒地（压扁 + 变暗）
        $deathPhase = $col - 11
        $bodyHScale = @(0.75, 0.45, 0.22, 0.10)[$deathPhase]
        $sink = @(4, 10, 16, 20)[$deathPhase]
        $alpha = @(255, 220, 180, 140)[$deathPhase]
    }

    $drawColor = $bodyColor
    if ($useFlash) { $drawColor = $flash }
    $drawColor = [Drawing.Color]::FromArgb($alpha, $drawColor.R, $drawColor.G, $drawColor.B)
    $dark = [Drawing.Color]::FromArgb($alpha, 40, 40, 46)
    $brush = New-Object Drawing.SolidBrush $drawColor
    $darkBrush = New-Object Drawing.SolidBrush $dark

    $cx = $feetX + $lean
    $cy = $feetY - $sink
    $h = [int]($bodyH * $bodyHScale)
    # 身体（椭圆）
    $bodyRect = New-Object Drawing.Rectangle ($cx - $bodyW / 2), ($cy - $h), $bodyW, $h
    $gg.FillEllipse($brush, $bodyRect)
    # 头（humanoid 才有）
    if ($actor.headR -gt 0) {
        $headR = $actor.headR
        $headY = $cy - $h - $headR + 2
        $headRect = New-Object Drawing.Rectangle ($cx - $headR), ($headY - $headR), ($headR * 2), ($headR * 2)
        $gg.FillEllipse($brush, $headRect)
        # 眼睛（白色，朝向侧）
        $eyeBrush = New-Object Drawing.SolidBrush ([Drawing.Color]::FromArgb($alpha, 255, 255, 255))
        $ax = $dirDef.arrow[0]; $ay = $dirDef.arrow[1]
        if ($ax -ne 0 -or $ay -ge 0) {
            [void]$gg.FillEllipse($eyeBrush, $cx + $ax * 3 - 2, $headY - 3, 4, 4)
            [void]$gg.FillEllipse($eyeBrush, $cx + $ax * 8 - 2, $headY - 3, 4, 4)
        }
        $eyeBrush.Dispose()
    }
    # 手臂/挥击弧（attack 帧）
    if ($col -ge 6 -and $col -le 9) {
        $ax = $dirDef.arrow[0]; $ay = $dirDef.arrow[1]
        $pen = New-Object Drawing.Pen $drawColor, 5
        $handX = $cx + $ax * (14 + $armSwing)
        $handY = $cy - $h * 0.6 + $ay * $armSwing * 0.6
        [void]$gg.DrawLine($pen, $cx, $cy - $h * 0.7, $handX, $handY)
        if ($col -eq 8) {
            # 命中帧：白色挥击弧
            $arcPen = New-Object Drawing.Pen ([Drawing.Color]::FromArgb(230, 255, 255, 255)), 3
            $rect = New-Object Drawing.Rectangle ($cx - 34), ($cy - $h - 20), 68, 52
            $startAng = 0; $sweep = 0
            if ($ax -gt 0) { $startAng = -70; $sweep = 140 }
            elseif ($ax -lt 0) { $startAng = 110; $sweep = 140 }
            elseif ($ay -gt 0) { $startAng = 20; $sweep = 140 }
            else { $startAng = 200; $sweep = 140 }
            $gg.DrawArc($arcPen, $rect, $startAng, $sweep)
            $arcPen.Dispose()
        }
        $pen.Dispose()
    } elseif ($col -ge 2 -and $col -le 5) {
        # walk 摆臂
        $ax = $dirDef.arrow[0]; $ay = $dirDef.arrow[1]
        $pen = New-Object Drawing.Pen $drawColor, 4
        [void]$gg.DrawLine($pen, $cx, $cy - $h * 0.7, $cx + $ax * 8 - $ay * $armSwing, $cy - $h * 0.35 + $ax * $armSwing)
        $pen.Dispose()
    }
    # 方向箭头（脚底下方）
    if ($col -eq 0 -or $col -eq 6) {
        Draw-Arrow $gg $feetX ($feetY + 6) $dirDef ([Drawing.Color]::FromArgb($alpha, 220, 220, 230))
    }
    # 脚底阴影
    $shadow = New-Object Drawing.SolidBrush ([Drawing.Color]::FromArgb(60, 0, 0, 0))
    $gg.FillEllipse($shadow, $feetX - 14, $feetY - 4, 28, 8)
    $shadow.Dispose()
    $brush.Dispose()
    $darkBrush.Dispose()
}

foreach ($actor in $actors) {
    $outDir = Join-Path 'd:\LegendGame\Assets' $actor.dir
    New-Item -ItemType Directory -Force -Path (Join-Path $outDir 'sprites') | Out-Null

    $bmp = New-Object Drawing.Bitmap $sheetW, $sheetH
    $g = [Drawing.Graphics]::FromImage($bmp)
    $g.InterpolationMode = 'NearestNeighbor'
    $g.SmoothingMode = 'AntiAlias'
    $bg = New-Object Drawing.SolidBrush ([Drawing.Color]::FromArgb(255, 40, 40, 46))
    $g.FillRectangle($bg, 0, 0, $sheetW, $sheetH)

    for ($row = 0; $row -lt $rows; $row++) {
        for ($col = 0; $col -lt $cols; $col++) {
            Draw-Frame $g ($col * $frame) ($row * $frame) $actor $rowDirs[$row] $col
        }
    }
    $pngPath = Join-Path $outDir 'sprites\actor_debug.png'
    $bmp.Save($pngPath, [Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose(); $bmp.Dispose(); $bg.Dispose()

    # ---- animations.json（原生对象 + ConvertTo-Json，PS5.1 安全） ----
    $clips = [ordered]@{}
    for ($row = 0; $row -lt $rows; $row++) {
        $dirName = $rowDirs[$row].name
        $base = $row * $cols
        # idle: col 0-1, loop
        $clips["idle_$dirName"] = [ordered]@{
            loop = $true
            frames = @(
                [ordered]@{ index = $base + 0; duration = 0.5 },
                [ordered]@{ index = $base + 1; duration = 0.5 }
            )
        }
        # walk: col 2-5, loop
        $clips["walk_$dirName"] = [ordered]@{
            loop = $true
            frames = @(
                [ordered]@{ index = $base + 2; duration = 0.12 },
                [ordered]@{ index = $base + 3; duration = 0.12 },
                [ordered]@{ index = $base + 4; duration = 0.12 },
                [ordered]@{ index = $base + 5; duration = 0.12 }
            )
        }
        # attack: col 6-9, NonLoop，第3帧(ordinal 2) attack_hit 事件
        $clips["attack_$dirName"] = [ordered]@{
            loop = $false
            frames = @(
                [ordered]@{ index = $base + 6; duration = 0.1 },
                [ordered]@{ index = $base + 7; duration = 0.1 },
                [ordered]@{ index = $base + 8; duration = 0.1; event = 'attack_hit' },
                [ordered]@{ index = $base + 9; duration = 0.1 }
            )
        }
        # hit: col 10, NonLoop 单帧
        $clips["hit_$dirName"] = [ordered]@{
            loop = $false
            frames = @(
                [ordered]@{ index = $base + 10; duration = 0.25 }
            )
        }
        # death: col 11-14, NonLoop
        $clips["death_$dirName"] = [ordered]@{
            loop = $false
            frames = @(
                [ordered]@{ index = $base + 11; duration = 0.2 },
                [ordered]@{ index = $base + 12; duration = 0.2 },
                [ordered]@{ index = $base + 13; duration = 0.2 },
                [ordered]@{ index = $base + 14; duration = 0.25 }
            )
        }
    }
    $animations = [ordered]@{ version = 1; clips = $clips }
    $animJson = $animations | ConvertTo-Json -Depth 6
    [System.IO.File]::WriteAllText((Join-Path $outDir 'animations.json'), $animJson)

    # ---- character.json ----
    $character = [ordered]@{
        version = 1
        name = (Split-Path $actor.dir -Leaf)
        spriteSheet = ($actor.dir -replace '\\', '/') + '/sprites/actor_debug.png'
        frameWidth = $frame
        frameHeight = $frame
        visualWidth = $frame
        visualHeight = $frame
        footprint = [ordered]@{ width = 28; height = 18; offsetX = 0; offsetY = 4 }
        pivot = [ordered]@{ x = 0.5; y = 0.85 }
        moveSpeed = 200
        direction = 'south'
        animations = ($actor.dir -replace '\\', '/') + '/animations.json'
    }
    if ($actor.combat) {
        # TestHero 战斗属性（数据驱动，不硬编码 PlayerController）
        $character.combat = [ordered]@{
            maxHp = 500; attack = 80; defense = 20; attackRange = 90; attackInterval = 0.8
        }
    }
    $charJson = $character | ConvertTo-Json -Depth 6
    [System.IO.File]::WriteAllText((Join-Path $outDir 'character.json'), $charJson)

    Write-Host "Generated: $outDir (120 frames)"
}

Write-Host 'Combat sprite sheets generated.'
