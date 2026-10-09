# Cooks authentic XWA DAT sprites to PNG and pushes them to the Quest
# (duel_assets/fx). Proprietary art stays in the user's install + device
# storage; NEVER in repo/APK. Requires DatFileEditor+XwaTexture DLLs next
# to a Resdata extract (see CHECKPOINT; re-download if missing) OR falls
# back to already-cooked PNGs in $PngDir (default: TEMP\duel_fx).
param([string]$Serial = "3487C10H9T0BKZ",
      [string]$PngDir = "$env:TEMP\duel_fx",
      [switch]$PushOnly)
$ErrorActionPreference = "Continue"
$adb = "$env:LOCALAPPDATA\Android\Sdk\platform-tools\adb.exe"
$pkg = "org.openxwa.xwaduellvr"
$dst = "/data/user/0/$pkg/files/duel_assets/fx"
$stg = "/data/local/tmp/duel_fx_staging"

# device file <- cook name (DAT group/image documented in checkpoint P1.5)
$map = @(
    @("boom_00.png", "Effects1.dat g2000 img0 (explosion frame A)"),
    @("boom_01.png", "Effects1.dat g2000 img23 (explosion frame B)"),
    @("spark_00.png", "Effects1.dat g3000 img0 (impact spark)"),
    @("sun.png", "Effects3.dat g9001 img0 (starburst flare)")
)
if (-not $PushOnly) {
    Write-Host "Cook step: re-extract via DatFileEditor (JeremyAnsel) if PNGs are stale."
    Write-Host "Expected PNGs in $PngDir :"
    foreach ($m in $map) {
        $p = Join-Path $PngDir $m[0]
        if (Test-Path $p) { "{0}  {1}B  {2}" -f $m[0], (Get-Item $p).Length, $m[1] }
        else { Write-Host ("MISSING {0}  expected from {1}" -f $m[0], $m[1]) }
    }
    Write-Host "To re-cook: load DatFileEditor.dll+XwaTexture.dll, TextureManager.GetTextures(dat,group,idx), GetBitmap+FlipY, save PNG."
}
$missing = @($map | Where-Object { -not (Test-Path (Join-Path $PngDir $_[0])) })
if ($missing.Count -gt 0) { Write-Host ("ABORT push: {0} PNG(s) missing." -f $missing.Count); exit 1 }
# P1.5a: the additive sprite pipe reads texel RGB ignoring alpha, so every
# PNG MUST be premultiplied (rgb *= a) or transparent texels add white
# (the "white sheet" sun bug). Re-bake idempotently before each push.
python -c "from PIL import Image; import glob, os
for p in sorted(glob.glob('$($PngDir -replace '\\','/')/*.png')):
    im = Image.open(p).convert('RGBA'); px = im.load(); w, h = im.size
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if a < 255: px[x, y] = ((r*a+127)//255, (g*a+127)//255, (b*a+127)//255, a)
    im.save(p); print('premult ok', os.path.basename(p))"
& $adb -s $Serial shell "run-as $pkg mkdir -p files/duel_assets/fx" | Out-Null
& $adb -s $Serial shell "mkdir -p $stg" | Out-Null
foreach ($m in $map) {
    & $adb -s $Serial push (Join-Path $PngDir $m[0]) "$stg/$($m[0])" 2>&1 | Select-Object -Last 1
    & $adb -s $Serial shell "run-as $pkg cp $stg/$($m[0]) files/duel_assets/fx/$($m[0])" | Out-Null
}
& $adb -s $Serial shell "rm -rf $stg; run-as $pkg ls -l files/duel_assets/fx"
