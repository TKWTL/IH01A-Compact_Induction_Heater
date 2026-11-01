#Requires -Version 5.1
<#
.SYNOPSIS
    CubeMX 重新生成工程后的一键修复脚本 (IH01A / USB-PD FULL)。

.DESCRIPTION
    CubeMX 的工程记录 (.mxproject) 仍把 USB-PD 登记成旧 MINSNK 那一套，点一次
    Generate Code 就会：
      - 用旧模板覆盖 Firmware/USBPD/**（丢掉 FULL DPM core 与 EPR 代码）
      - 把 .uvprojx 的 Define / 库路径 / IncludePath 改回 MINSNK
      - 从 .uvprojx 删掉 IH01A 自己的文件组（APP / BSP / PowerControl / u8g2 ...）

    本脚本把 .uvprojx 恢复成仓库版本，同步新生成/已删除的 Core 源文件，再做一致性
    校验。之后在 Keil 里 Rebuild 即可。

    用法：
        powershell -ExecutionPolicy Bypass -File Firmware/Tools/cubemx_post_generate.ps1

    忘记跑本脚本时，main.c 里的生成护栏会直接编译报错，不会静默烧上 MINSNK 栈。
#>
[CmdletBinding()]
param(
    [string]$RepoRoot = ''
)

$ErrorActionPreference = 'Stop'
if (-not $RepoRoot) {
    $here = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
    $RepoRoot = (Resolve-Path (Join-Path $here '..\..')).Path
}

$projectRel = 'Firmware/MDK-ARM/Compact_Induction_Heater.uvprojx'
$projectPath = Join-Path $RepoRoot $projectRel
$mdkRelDir = 'Firmware/MDK-ARM'
$problems = New-Object System.Collections.Generic.List[string]
$actions = New-Object System.Collections.Generic.List[string]

if (-not (Test-Path $projectPath)) { throw "project file not found: $projectPath" }

# --- 1. 恢复 uvprojx --------------------------------------------------------
$dirty = (& git -C $RepoRoot status --porcelain -- $projectRel | Out-String).Trim()
if ($dirty -ne '') {
    $backup = Join-Path $env:TEMP ("uvprojx_before_restore_" + (Get-Date -Format 'yyyyMMdd-HHmmss') + ".txt")
    Copy-Item $projectPath $backup -Force
    & git -C $RepoRoot checkout -- $projectRel
    if ($LASTEXITCODE -ne 0) { throw 'git checkout uvprojx failed' }
    $actions.Add("uvprojx restored from git (generated copy: $backup)")
} else {
    $actions.Add('uvprojx already matches git, nothing to restore')
}

$text = Get-Content -LiteralPath $projectPath -Raw
$originalText = $text
$nl = if ($text -match "`r`n") { "`r`n" } else { "`n" }

# --- 2. 同步 Core 源文件 ----------------------------------------------------
$coreGroup = 'Application/User/Core'
$pattern = '(?s)(<Group>\s*<GroupName>' + [regex]::Escape($coreGroup) + '</GroupName>\s*<Files>)(.*?)(</Files>\s*</Group>)'
$m = [regex]::Match($text, $pattern)

if (-not $m.Success) {
    $problems.Add("group '$coreGroup' not found; Core sources not synced")
} else {
    $bodyText = $m.Groups[2].Value
    $listedInGroup = @([regex]::Matches($bodyText, '<FilePath>([^<]+)</FilePath>') | ForEach-Object { $_.Groups[1].Value })
    $allPaths = @([regex]::Matches($text, '<FilePath>([^<]+)</FilePath>') | ForEach-Object { $_.Groups[1].Value })

    $onDisk = @(Get-ChildItem (Join-Path $RepoRoot 'Firmware/Core/Src') -Filter '*.c' -File |
                ForEach-Object { '../Core/Src/' + $_.Name })
    $toAdd = @($onDisk | Where-Object { $allPaths -notcontains $_ })

    if ($toAdd.Count -gt 0) {
        $block = ''
        foreach ($rel in $toAdd) {
            $name = Split-Path $rel -Leaf
            $block += "            <File>$nl              <FileName>$name</FileName>$nl              <FileType>1</FileType>$nl              <FilePath>$rel</FilePath>$nl            </File>$nl"
            $actions.Add("added to project: $rel")
        }
        $newBody = $bodyText.TrimEnd() + $nl + $block
        $text = $text.Substring(0, $m.Groups[2].Index) + $newBody + $text.Substring($m.Groups[2].Index + $m.Groups[2].Length)
    }

    $toRemove = @($listedInGroup | Where-Object {
        $full = Join-Path (Join-Path $RepoRoot $mdkRelDir) ($_ -replace '/', '\')
        -not (Test-Path $full)
    })
    foreach ($rel in $toRemove) {
        $esc = [regex]::Escape($rel)
        $blockRe = '(?s)[ ]*<File>\s*<FileName>[^<]*</FileName>\s*<FileType>[^<]*</FileType>\s*<FilePath>' + $esc + '</FilePath>\s*</File>\r?\n?'
        $before = $text.Length
        $text = [regex]::Replace($text, $blockRe, '')
        if ($text.Length -ne $before) { $actions.Add("removed from project (missing on disk): $rel") }
    }
}

if ($text -ne $originalText) {
    [System.IO.File]::WriteAllText($projectPath, $text, (New-Object System.Text.UTF8Encoding($false)))
}

# --- 3. 校验 ---------------------------------------------------------------
$text = Get-Content -LiteralPath $projectPath -Raw

$defineMatch = [regex]::Match($text, '<Define>([^<]*)</Define>')
$defines = @()
if ($defineMatch.Success -and $defineMatch.Groups[1].Value -ne '') { $defines = $defineMatch.Groups[1].Value -split ',' }
if ($defines -contains 'USBPDCORE_LIB_PD3_FULL') { $actions.Add('define USBPDCORE_LIB_PD3_FULL present') }
else { $problems.Add('define USBPDCORE_LIB_PD3_FULL missing') }
if ($defines -contains 'USBPDCORE_LIB_PD3_CONFIG_MINSNK') {
    $problems.Add('define USBPDCORE_LIB_PD3_CONFIG_MINSNK still present (conflicts with FULL lib)')
}

$libRel = '../Middlewares/ST/STM32_USBPD_Library_v541/Core/lib/USBPDCORE_PD3_FULL_CM0PLUS_Keil.lib'
if ($text.Contains($libRel)) { $actions.Add('FULL library entry present') }
else { $problems.Add("FULL library entry missing: $libRel") }
if (-not (Test-Path (Join-Path (Join-Path $RepoRoot $mdkRelDir) ($libRel -replace '/', '\')))) {
    $problems.Add("FULL library file not on disk: $libRel")
}

foreach ($inc in @('../Middlewares/ST/STM32_USBPD_Library_v541/Core/inc',
                   '../Middlewares/ST/STM32_USBPD_Library_v541/Devices/STM32G0XX/inc',
                   '../USBPD/App', '../USBPD/Target')) {
    if (-not $text.Contains($inc)) { $problems.Add("IncludePath missing: $inc") }
}
if ($text -match 'STM32_USBPD_Library/(Core|Devices)') {
    $problems.Add('still references old middleware dir (STM32_USBPD_Library, not _v541)')
}

$ioc = Join-Path $RepoRoot 'Firmware/Compact_Induction_Heater.ioc'
$startup = Join-Path $RepoRoot 'Firmware/MDK-ARM/startup_stm32g071xx.s'
if ((Test-Path $ioc) -and (Test-Path $startup)) {
    $iocHeap = [regex]::Match((Get-Content $ioc -Raw), 'ProjectManager\.HeapSize=(\S+)')
    $asmHeap = [regex]::Match((Get-Content $startup -Raw), 'Heap_Size\s+EQU\s+(\S+)')
    if ($iocHeap.Success -and $asmHeap.Success) {
        if ($iocHeap.Groups[1].Value -eq $asmHeap.Groups[1].Value) {
            $actions.Add("heap consistent: $($iocHeap.Groups[1].Value)")
        } else {
            $problems.Add("heap mismatch: .ioc=$($iocHeap.Groups[1].Value) startup=$($asmHeap.Groups[1].Value) (FULL stack needs >= 0x2000)")
        }
    }
}

# --- 4. 输出 ---------------------------------------------------------------
Write-Host ''
Write-Host '=== cubemx_post_generate ===' -ForegroundColor Cyan
foreach ($a in $actions) { Write-Host "  - $a" }
if ($problems.Count -eq 0) {
    Write-Host ''
    Write-Host '  OK - now Rebuild in Keil.' -ForegroundColor Green
    exit 0
}
Write-Host ''
Write-Host '  manual fix required:' -ForegroundColor Yellow
foreach ($p in $problems) { Write-Host "  ! $p" -ForegroundColor Yellow }
exit 1