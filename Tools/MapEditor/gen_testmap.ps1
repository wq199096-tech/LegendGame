# Generate Assets/Maps/TestMap/map.json (100x100 structured test map)
# Deterministic: grass + dirt roads + stone patches + water + buildings + trees/rocks/decors + collision
$ErrorActionPreference = 'Stop'

$outDir = Join-Path $PSScriptRoot '..\..\Assets\Maps\TestMap'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$outFile = Join-Path $outDir 'map.json'

$W = 100
$H = 100
$TILE = 64

# TileId: 0=Empty 1=Grass 2=Dirt 3=Stone 4=Water
$ground = New-Object 'uint16[]' ($W * $H)
$collision = New-Object 'byte[]' ($W * $H)

# Fill entire map with grass first
for ($i = 0; $i -lt ($W * $H); $i++) { $ground[$i] = 1 }

function Set-Ground([int]$x, [int]$y, [uint16]$id) {
    if ($x -ge 0 -and $x -lt $W -and $y -ge 0 -and $y -lt $H) { $ground[$y * $W + $x] = $id }
}
function Get-Ground([int]$x, [int]$y) {
    if ($x -ge 0 -and $x -lt $W -and $y -ge 0 -and $y -lt $H) { return $ground[$y * $W + $x] }
    return 0
}
function Set-Collision([int]$x, [int]$y, [byte]$v) {
    if ($x -ge 0 -and $x -lt $W -and $y -ge 0 -and $y -lt $H) { $collision[$y * $W + $x] = $v }
}

# 1. Border wall collision (1 ring)
for ($x = 0; $x -lt $W; $x++) { Set-Collision $x 0 1; Set-Collision $x ($H - 1) 1 }
for ($y = 0; $y -lt $H; $y++) { Set-Collision 0 $y 1; Set-Collision ($W - 1) $y 1 }

# 2. Stone patches (circles)
$stonePatches = @(@{cx=30; cy=30; r=5}, @{cx=76; cy=68; r=4}, @{cx=22; cy=78; r=4})
foreach ($p in $stonePatches) {
    for ($y = $p.cy - $p.r; $y -le $p.cy + $p.r; $y++) {
        for ($x = $p.cx - $p.r; $x -le $p.cx + $p.r; $x++) {
            if (($x - $p.cx) * ($x - $p.cx) + ($y - $p.cy) * ($y - $p.cy) -le $p.r * $p.r) {
                Set-Ground $x $y 3
            }
        }
    }
}

# 3. Dirt roads: horizontal y=48..50, vertical x=48..50
for ($x = 5; $x -lt 95; $x++) { for ($y = 48; $y -le 50; $y++) { Set-Ground $x $y 2 } }
for ($y = 5; $y -lt 95; $y++) { for ($x = 48; $x -le 50; $x++) { Set-Ground $x $y 2 } }

# 4. Central stone plaza (circle, on grass only, roads pass through)
for ($y = 43; $y -le 57; $y++) {
    for ($x = 43; $x -le 57; $x++) {
        if (($x - 50) * ($x - 50) + ($y - 50) * ($y - 50) -le 49 -and (Get-Ground $x $y) -eq 1) {
            Set-Ground $x $y 3
        }
    }
}

# 5. Water: lake (ellipse) + pond (water is blocked by default)
$lakes = @(@{cx=72; cy=28; rx=11; ry=7}, @{cx=25; cy=62; rx=5; ry=4})
foreach ($l in $lakes) {
    for ($y = $l.cy - $l.ry; $y -le $l.cy + $l.ry; $y++) {
        for ($x = $l.cx - $l.rx; $x -le $l.cx + $l.rx; $x++) {
            $dx = ($x - $l.cx) / $l.rx
            $dy = ($y - $l.cy) / $l.ry
            if ($dx * $dx + $dy * $dy -le 1.0) {
                Set-Ground $x $y 4
                Set-Collision $x $y 1
            }
        }
    }
}

# Objects
$objects = New-Object System.Collections.Generic.List[object]
$occluders = New-Object System.Collections.Generic.List[int]
$nextId = 1
$seed = 42
function Get-Rand {
    $script:seed = ($script:seed * 1103515245 + 12345) -band 0x7FFFFFFF
    return $script:seed / 0x7FFFFFFF
}
function Test-Free([int]$x, [int]$y) {
    $g = Get-Ground $x $y
    if ($g -eq 0 -or $g -eq 4 -or $g -eq 2) { return $false }
    if ($collision[$y * $W + $x] -ne 0) { return $false }
    return $true
}
function Add-BlockingObject([string]$type, [string]$texId, [int]$w, [int]$h, [bool]$occluder) {
    for ($attempt = 0; $attempt -lt 400; $attempt++) {
        $tx = [int](2 + (Get-Rand) * ($W - 4))
        $ty = [int](2 + (Get-Rand) * ($H - 4))
        if (-not (Test-Free $tx $ty)) { continue }
        $id = $script:nextId; $script:nextId++
        $obj = [ordered]@{
            id = $id; name = "${type}_${id}"; textureId = $texId
            x = ($tx * $TILE + 32); y = ($ty * $TILE + 32)
            width = $w; height = $h; rotation = 0; renderOrder = 0
            blocking = $true; occluder = $occluder
        }
        $script:objects.Add($obj) | Out-Null
        if ($occluder) { $script:occluders.Add($id) | Out-Null }
        $x0 = [int][math]::Floor(($tx * $TILE + 32 - $w / 2 + 1) / $TILE)
        $x1 = [int][math]::Floor(($tx * $TILE + 32 + $w / 2 - 1) / $TILE)
        $y0 = [int][math]::Floor(($ty * $TILE + 32 - $h / 2 + 1) / $TILE)
        $y1 = [int][math]::Floor(($ty * $TILE + 32 + $h / 2 - 1) / $TILE)
        for ($yy = $y0; $yy -le $y1; $yy++) { for ($xx = $x0; $xx -le $x1; $xx++) { Set-Collision $xx $yy 1 } }
        return $true
    }
    return $false
}

# 5 buildings (3x2 tiles = 192x128), hand-picked spread positions
$buildingTiles = @(@(18,20), @(62,18), @(15,68), @(82,80), @(64,72))
foreach ($b in $buildingTiles) {
    $tx = $b[0]; $ty = $b[1]
    $id = $nextId; $nextId++
    $obj = [ordered]@{
        id = $id; name = "building_${id}"; textureId = "building"
        x = ($tx * $TILE + 96); y = ($ty * $TILE + 64)
        width = 192; height = 128; rotation = 0; renderOrder = 0
        blocking = $true; occluder = $true
    }
    $objects.Add($obj) | Out-Null
    $occluders.Add($id) | Out-Null
    for ($yy = $ty; $yy -le $ty + 1; $yy++) { for ($xx = $tx; $xx -le $tx + 2; $xx++) { Set-Collision $xx $yy 1 } }
}

# 20 trees (96x96, blocking + occluder)
$placed = 0
while ($placed -lt 20) { if (Add-BlockingObject 'tree' 'tree' 96 96 $true) { $placed++ } else { break } }

# 10 rocks (48x48, blocking, not occluder)
$placed = 0
while ($placed -lt 10) { if (Add-BlockingObject 'rock' 'rock' 48 48 $false) { $placed++ } else { break } }

# 8 flower decorations (24x24, non-blocking)
$placed = 0
while ($placed -lt 8) {
    $tx = [int](2 + (Get-Rand) * ($W - 4))
    $ty = [int](2 + (Get-Rand) * ($H - 4))
    if (Test-Free $tx $ty) {
        $id = $nextId; $nextId++
        $objects.Add([ordered]@{
            id = $id; name = "flower_${id}"; textureId = "flower"
            x = ($tx * $TILE + 32); y = ($ty * $TILE + 32)
            width = 24; height = 24; rotation = 0; renderOrder = 0
            blocking = $false; occluder = $false
        }) | Out-Null
        $placed++
    }
}

# Serialize compact JSON
$sb = New-Object System.Text.StringBuilder
[void]$sb.Append('{"version":1,"name":"TestMap","tileSize":64,"width":100,"height":100,"layers":[')

[void]$sb.Append('{"name":"Ground","type":"tile","visible":true,"data":[')
for ($y = 0; $y -lt $H; $y++) {
    if ($y -gt 0) { [void]$sb.Append(',') }
    $row = ($ground[($y * $W)..($y * $W + $W - 1)] | ForEach-Object { $_ }) -join ','
    [void]$sb.Append($row)
}
[void]$sb.Append(']},')

[void]$sb.Append('{"name":"Objects","type":"object","visible":true,"objects":[')
$first = $true
foreach ($o in $objects) {
    if (-not $first) { [void]$sb.Append(',') }
    $first = $false
    [void]$sb.Append('{"id":' + $o.id + ',"name":"' + $o.name + '","textureId":"' + $o.textureId +
        '","x":' + $o.x + ',"y":' + $o.y + ',"width":' + $o.width + ',"height":' + $o.height +
        ',"rotation":' + $o.rotation + ',"renderOrder":' + $o.renderOrder +
        ',"blocking":' + ($o.blocking.ToString().ToLower()) + ',"occluder":' + ($o.occluder.ToString().ToLower()) + '}')
}
[void]$sb.Append(']},')

[void]$sb.Append('{"name":"Collision","type":"collision","visible":true,"data":[')
for ($y = 0; $y -lt $H; $y++) {
    if ($y -gt 0) { [void]$sb.Append(',') }
    $row = ($collision[($y * $W)..($y * $W + $W - 1)] | ForEach-Object { $_ }) -join ','
    [void]$sb.Append($row)
}
[void]$sb.Append(']},')

[void]$sb.Append('{"name":"Occlusion","type":"occlusion","visible":true,"objects":[')
[void]$sb.Append(($occluders | ForEach-Object { $_ }) -join ',')
[void]$sb.Append(']}')

[void]$sb.Append(']}')

[System.IO.File]::WriteAllText($outFile, $sb.ToString())
$size = (Get-Item $outFile).Length
Write-Host "Map generated: $outFile ($size bytes)"
Write-Host "Objects: $($objects.Count), Occluders: $($occluders.Count)"
$waterCount = ($ground | Where-Object { $_ -eq 4 }).Count
$blockedCount = ($collision | Where-Object { $_ -eq 1 }).Count
Write-Host "Water tiles: $waterCount, Blocked tiles: $blockedCount"
