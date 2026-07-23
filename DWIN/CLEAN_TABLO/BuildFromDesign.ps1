#Requires -Version 5.1
# CLEAN_TABLO: full UI + page 10 keypad; 32.icl + all page BMPs in DWIN_SET
$ErrorActionPreference = "Stop"
$Root = $PSScriptRoot
$Repo = Split-Path $Root -Parent
New-Item -ItemType Directory -Force -Path (Join-Path $Root "DWIN_SET"), (Join-Path $Root "image"), (Join-Path $Root "TFT") | Out-Null

$srcProj = Join-Path $Repo "TEST_PROJECT"
$srcSet = Join-Path $srcProj "DWIN_SET"
$dstSet = Join-Path $Root "DWIN_SET"
# Never clobber a DGUS-generated show with TEST_PROJECT bootstrap.
$dgusShow = Join-Path $dstSet "_from_dgus_generate\14ShowFile.bin"
foreach ($f in @("13TouchFile.bin", "14ShowFile.bin", "22_Config.bin")) {
    $dst = Join-Path $dstSet $f
    if ($f -eq "14ShowFile.bin" -and (Test-Path $dgusShow)) {
        Copy-Item $dgusShow $dst -Force
        Write-Host "Restored 14ShowFile.bin from _from_dgus_generate"
        continue
    }
    if ($f -eq "14ShowFile.bin" -and (Test-Path $dst)) {
        Write-Host "Keeping existing 14ShowFile.bin (no DGUS pristine yet)"
        continue
    }
    $s = Join-Path $srcSet $f
    if (Test-Path $s) { Copy-Item $s $dst -Force }
}

$userCfg = Join-Path $Root "design\T5LCFG_user_ok.CFG"
$cfgOut = Join-Path $dstSet "T5LCFG.CFG"
if (Test-Path $userCfg) {
    Copy-Item $userCfg $cfgOut -Force
    & python (Join-Path $Repo "scripts\t5lcfg_builder.py") -o $cfgOut -r $cfgOut --baud 115200 --rotation 0
} else {
    $ref = Join-Path $srcSet "T5LCFG.CFG"
    & python (Join-Path $Repo "scripts\t5lcfg_builder.py") -o $cfgOut -r $ref --preset 800x480 --baud 115200 --rotation 0
}
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
python -c "from pathlib import Path; p=Path(r'$cfgOut'); b=bytearray(p.read_bytes()); b[8]=0x20; p.write_bytes(b); print('T5LCFG[8]=', hex(b[8]))"

$hzkDst = Join-Path $dstSet "0_DWIN_ASC.HZK"
if (-not (Test-Path $hzkDst)) {
    foreach ($h in @(
        (Join-Path $Repo "TEST_PROJECT_2\DWIN_SET\0_DWIN_ASC.HZK"),
        (Join-Path $Repo "Software\DGUS_V7649\DGUS_V7649\0_DWIN_ASC.HZK")
    )) {
        if (Test-Path $h) { Copy-Item $h $hzkDst -Force; break }
    }
}
if (-not (Test-Path $hzkDst)) {
    Write-Host "ERROR: 0_DWIN_ASC.HZK missing" -ForegroundColor Red
    exit 1
}

& python (Join-Path $Repo "scripts\validate_clean_tablo_layout.py") --project $Root
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& python (Join-Path $Repo "scripts\render_clean_tablo_screen.py") --project $Root
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& python (Join-Path $Repo "scripts\gen_clean_tablo_pressed.py") --project $Root
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& python (Join-Path $Repo "scripts\render_clean_tablo_keypad.py") --project $Root
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& python (Join-Path $Repo "scripts\render_clean_tablo_error.py") --project $Root
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& python (Join-Path $Repo "scripts\render_clean_tablo_settings.py") --project $Root
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& python (Join-Path $Repo "scripts\gen_clean_tablo_touch_bin.py") --project $Root
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& python (Join-Path $Repo "scripts\gen_clean_tablo_digits.py") --project $Root
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& python (Join-Path $Repo "scripts\pack_paper_cutter_icl.py") --project $Root --which large
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& python (Join-Path $Repo "scripts\pack_paper_cutter_icl.py") --project $Root --which small
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# Progress: Process Bar 0x5A23 on VP 6030 (optional IconShow frames kept for fallback)
& python (Join-Path $Repo "scripts\gen_paper_cutter_progress.py") --project $Root
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& python (Join-Path $Repo "scripts\pack_paper_cutter_icl.py") --project $Root --which progress
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# Sync all page BMPs into DWIN_SET (for DGUS / inspection) then pack ICL
$pageIds = @(0, 1, 2, 10, 11, 12, 13, 14, 15, 16, 17)
$tftDir = Join-Path $Root "TFT"
New-Item -ItemType Directory -Force -Path $tftDir | Out-Null
foreach ($i in $pageIds) {
    $name = "{0:D2}.bmp" -f $i
    $src = Join-Path $Root "image\$name"
    if (-not (Test-Path $src)) { Write-Host "ERROR: missing $src" -ForegroundColor Red; exit 1 }
    Copy-Item $src (Join-Path $dstSet $name) -Force
    Copy-Item $src (Join-Path $Root $name) -Force
    Copy-Item $src (Join-Path $tftDir $name) -Force
}
Remove-Item (Join-Path $dstSet "04.bmp") -Force -ErrorAction SilentlyContinue
Remove-Item (Join-Path $Root "04.bmp") -Force -ErrorAction SilentlyContinue

& python (Join-Path $Repo "scripts\pack_dwin_set_screen_to_icl.py") --project $Root --icl-id 32 --bmps "00.bmp,01.bmp,02.bmp,10.bmp,11.bmp,12.bmp,13.bmp,14.bmp,15.bmp,16.bmp,17.bmp" --quality 92
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Remove-Item (Join-Path $dstSet "03.bmp") -Force -ErrorAction SilentlyContinue
Remove-Item (Join-Path $Root "03.bmp") -Force -ErrorAction SilentlyContinue
Remove-Item (Join-Path $dstSet "23.icl") -Force -ErrorAction SilentlyContinue
$hmi = Join-Path $Root "DWprj.hmi"
if (-not (Test-Path $hmi)) {
    $template = Join-Path $srcProj "DWprj.hmi"
    if (Test-Path $template) { Copy-Item $template $hmi -Force }
}
$hmiText = Get-Content $hmi -Raw -Encoding UTF8
$hmiText = [regex]::Replace($hmiText, 'SCREENDSIZE=\d+X\d+', 'SCREENDSIZE=800X480')
$imgLines = @("[IMG]")
foreach ($i in $pageIds) { $imgLines += ("{0:D2}={0:D2}.bmp" -f $i) }
$imgBlock = ($imgLines -join "`r`n") + "`r`n"
if ($hmiText -match '(?s)\[IMG\].*?(?=\[|\z)') {
    $hmiText = [regex]::Replace($hmiText, '(?s)\[IMG\].*?(?=\[|\z)', $imgBlock)
} else {
    $hmiText = $hmiText.TrimEnd() + "`r`n" + $imgBlock
}
[System.IO.File]::WriteAllText($hmi, $hmiText.TrimEnd() + "`r`n", [System.Text.UTF8Encoding]::new($false))

$csproj = Join-Path $Repo "tools\GenerateTestTft\GenerateTestTft.csproj"
dotnet run --project $csproj -c Release -- $Root --init-clean-tablo --verify
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& python (Join-Path $Repo "scripts\form_clean_tablo_show_pages.py") --project $Root
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host ''
Write-Host 'IMPORTANT: form_clean_tablo_show_pages now ONLY patches VarType (LONG32).' -ForegroundColor Yellow
Write-Host 'Do NOT rewrite 14ShowFile by hand. After layout/TFT changes:' -ForegroundColor Yellow
Write-Host '  1) Open CLEAN_TABLO\DWprj.hmi in DGUS' -ForegroundColor Yellow
Write-Host '  2) Save -> Generate  (writes real 13/14/22)' -ForegroundColor Yellow
Write-Host '  3) python scripts\form_clean_tablo_show_pages.py --project CLEAN_TABLO' -ForegroundColor Yellow
Write-Host '  4) Copy DWIN_SET to F:\ and F:\DWIN_SET, full power cycle' -ForegroundColor Yellow
Write-Host ''

& python (Join-Path $Repo "scripts\gen_clean_tablo_touch_bin.py") --project $Root
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# Keep BMPs in DWIN_SET after tools
foreach ($i in $pageIds) {
    $name = "{0:D2}.bmp" -f $i
    $src = Join-Path $Root "image\$name"
    if (Test-Path $src) {
        Copy-Item $src (Join-Path $dstSet $name) -Force
        Copy-Item $src (Join-Path $Root $name) -Force
        Copy-Item $src (Join-Path $tftDir $name) -Force
    }
}
python -c "from pathlib import Path; p=Path(r'$cfgOut'); b=bytearray(p.read_bytes()); b[8]=0x20; p.write_bytes(b); print('final T5LCFG[8]=', hex(b[8]))"

$fwDir = "D:\paper_cutter\BlackPill\paper_cutter\CLEAN_TABLO_FW"
if (Test-Path $fwDir) {
    Copy-Item (Join-Path $Root "VP_MEMORY_MAP.md") (Join-Path $fwDir "VP_MEMORY_MAP.md") -Force
}

Write-Host 'OK: CLEAN_TABLO; BMPs kept; 14Show LONG32 VP6000/6010 — sync F:\ and F:\DWIN_SET' -ForegroundColor Green

# Always mirror flash show to SD if present (panel often reads F:\DWIN_SET)
if (Test-Path 'F:\') {
    $show = Join-Path $dstSet '14ShowFile.bin'
    if (Test-Path $show) {
        Copy-Item $show 'F:\14ShowFile.bin' -Force
        New-Item -ItemType Directory -Path 'F:\DWIN_SET' -Force | Out-Null
        Copy-Item $show 'F:\DWIN_SET\14ShowFile.bin' -Force
        Write-Host 'Synced 14ShowFile.bin -> F:\ and F:\DWIN_SET'
    }
}
