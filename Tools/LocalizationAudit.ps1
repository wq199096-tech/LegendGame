# ---------------------------------------------------------------------------
# Stage27 中文化专项：PlayerFacingEnglishAudit（临时检查脚本，不新增正式 exe）
# 聚焦玩家 UI 渲染文件与 Data JSON 玩家可见字段；日志/include/资源路径豁免。
# 输出：testlogs/stage27-zhcn-audit.txt
# 用法：powershell -File Tools/LocalizationAudit.ps1
# ---------------------------------------------------------------------------

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$outPath = Join-Path $root 'testlogs/stage27-zhcn-audit.txt'
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $outPath) | Out-Null

# 允许出现在玩家界面的英文白名单（词级）。
$whitelist = @('LegendGame', 'WASD', 'Esc', 'Enter', 'Tab', 'Ctrl', 'Alt',
               'Shift', 'FPS', 'Lv', 'F1', 'F2', 'F3', 'F4', 'F5', 'F6', 'F7', 'F8',
               'F9', 'F10', 'F11', 'F12', 'I', 'C', 'E', 'F', 'R', 'T', 'B',
               'lld', 'u', 'x')

# 只扫玩家 UI 渲染文件（其余为日志/网络/JSON 解析器内部代码）。
$uiFiles = @(
    'Client/Visuals/VisualRuntime.cpp',
    'Client/Visuals/FlowPages.cpp',
    'Client/Visuals/ChatWindow.cpp',
    'Client/Ui/UiModels.cpp',
    'Client/Ui/PlayerFacingErrorCatalog.cpp',
    'Client/Ui/CharacterVisualCatalog.cpp',
    'Client/Ui/ChatUiTheme.cpp'
)

$report = New-Object System.Collections.Generic.List[string]
$report.Add('==================================================')
$report.Add('Stage27 玩家可见英文审计（PlayerFacingEnglishAudit）')
$report.Add("生成时间：$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')")
$report.Add('范围：玩家 UI 渲染文件字符串字面量 + Data/ JSON 玩家可见字段')
$report.Add('内部豁免：LOG_*/F8-F12 debug 面板/诊断标记/资源路径/include')
$report.Add('==================================================')

$findingCount = 0

function Test-InternalText([string]$text) {
    # debug 面板/统计/资源/格式化内部串。
    if ($text -match 'Sprites|DrawCalls|Textures|FX %d|visible:|map %u|Pos %|packet|tick|EntityId|visualId|mapId|requestId|sessionId|protocol|rpc') { return $true }
    if ($text -match 'item_|fx_|ui_|ui-item|player_|monster_|npc_|vmap_|portal_|skill_|quest_|status_') { return $true }
    if ($text -match '\.png|\.json|\.ttf|\.otf|\.db|Assets/|Data/|Logs/|savedata|Config/|Client/|Server/') { return $true }
    if ($text -match 'idle|walk|attack|death|cast|hit|south|north|west|east') { return $true }
    # 日志拼接片段 / 资源 tag / 设置 JSON key（内部信息）。
    if ($text -match 'clips=|entities=|effects=|mapVisuals=|font=|name=|texture|portrait|skillbar|masterVolume|musicVolume|sfxVolume|fullscreen|resolutionIndex|tutorialShown|ground|object|placement|entity|effect') { return $true }
    return $false
}

$report.Add('')
$report.Add('---- 1. 玩家 UI 渲染文件（字符串字面量） ----')
foreach ($rel in $uiFiles) {
    $full = Join-Path $root $rel
    if (-not (Test-Path $full)) { continue }
    $lines = Get-Content $full -Encoding UTF8
    for ($i = 0; $i -lt $lines.Count; ++$i) {
        $line = $lines[$i]
        if ($line -match '^\s*//') { continue }
        if ($line -match 'LOG_|#include|Debug|debug|VisualSmoke|VsSmoke|ChatSmoke|AutoEnter|Diag') { continue }
        if ($line -match '== "') { continue } # 内部枚举值比较（非显示文本）
        foreach ($m in [regex]::Matches($line, '"([^"]*)"')) {
            $text = $m.Groups[1].Value
            if ($text.Length -eq 0) { continue }
            if (Test-InternalText $text) { continue }
            $words = [regex]::Matches($text, '[A-Za-z]{2,}') | ForEach-Object { $_.Value }
            $bad = @($words | Where-Object { $whitelist -notcontains $_ })
            if ($bad.Count -gt 0) {
                ++$findingCount
                $report.Add(('{0}({1}): "{2}" | 词:{3} | 待确认' -f $rel, ($i + 1),
                             ($text -replace "`t", ' '), ($bad -join ',')))
            }
        }
    }
}

# ---- 2. Data JSON 玩家可见字段 ----
$report.Add('')
$report.Add('---- 2. Data/ JSON（name/title/text/description 字段值） ----')
$dataFiles = Get-ChildItem -Path (Join-Path $root 'Data') -Recurse -Include *.json |
             Where-Object { $_.FullName -notmatch 'manifest|visual|\\.backup\\|loot_tables' }
foreach ($file in $dataFiles) {
    $rel = $file.FullName.Substring($root.Length + 1)
    $lines = Get-Content $file.FullName -Encoding UTF8
    for ($i = 0; $i -lt $lines.Count; ++$i) {
        $line = $lines[$i]
        if ($line -match '"(name|title|text|description)"\s*:\s*"([^"]*)"') {
            $field = $Matches[1]
            $value = $Matches[2]
            if ($value.Length -eq 0) { continue }
            $words = [regex]::Matches($value, '[A-Za-z]{2,}') | ForEach-Object { $_.Value }
            $bad = @($words | Where-Object { $whitelist -notcontains $_ })
            if ($bad.Count -gt 0) {
                ++$findingCount
                $report.Add(('{0}({1}): {2}="{3}" | 词:{4} | 待确认' -f $rel, ($i + 1),
                             $field, $value, ($bad -join ',')))
            }
        }
    }
}

$report.Add('')
$report.Add('==================================================')
if ($findingCount -eq 0) {
    $report.Add('结论：玩家可见英文残留 = 0 —— AUDIT PASS')
} else {
    $report.Add("结论：待确认条目 = $findingCount —— 需逐条处理或补充豁免")
}
$report.Add('==================================================')
$report | Set-Content -Path $outPath -Encoding UTF8
Write-Output "AUDIT WRITTEN: $outPath"
Write-Output "findings = $findingCount"
