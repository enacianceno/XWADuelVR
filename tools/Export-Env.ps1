# Cooks the planetary spheremap (downscale for Quest) and pushes it to
# duel_assets/env. Source: spacespheremaps.com (free personal+commercial).
# Kept in TEMP + device storage; NOT in repo/APK (8 MB source policy).
param([string]$Serial = "3487C10H9T0BKZ",
      [string]$SrcDir = "$env:TEMP\duel_env")
$ErrorActionPreference = "Continue"
$adb = "$env:LOCALAPPDATA\Android\Sdk\platform-tools\adb.exe"
$pkg = "org.openxwa.xwaduellvr"
$dst = "/data/user/0/$pkg/files/duel_assets/env"
$stg = "/data/local/tmp/duel_env_staging"
$src = Join-Path $SrcDir "earthlike_planet.png"
$cook = Join-Path $SrcDir "space_env.png"
if (-not (Test-Path $src)) { Write-Host "ABORT: missing $src"; exit 1 }
# Quest cook: 2048x1024 RGBA (8 MB GPU). The source map has a visible
# vertical seam (up to 68/255 in dark nebula rows): single-sided wrap
# crossfade over the last N columns toward the first N (0.07 deg smear,
# invisible; boundary becomes continuous), then downscale.
python -c "from PIL import Image
im = Image.open('$($src -replace '\\','/')').convert('RGB')
px = im.load(); w, h = im.size; N = 32
for j in range(w-N, w):
    t = float(j-(w-N)+1)/float(N+1)
    for y in range(h):
        a = px[j, y]; b = px[j-(w-N), y]
        px[j, y] = (int(a[0]*(1-t)+b[0]*t), int(a[1]*(1-t)+b[1]*t), int(a[2]*(1-t)+b[2]*t))
seam = max(abs(px[0,y][0]-px[w-1,y][0])+abs(px[0,y][1]-px[w-1,y][1])+abs(px[0,y][2]-px[w-1,y][2]) for y in range(0,h,97))
print('seam max RGB diff after blend:', seam)
im.resize((2048,1024), Image.LANCZOS).convert('RGBA').save('$($cook -replace '\\','/')')
print('cooked space_env.png')"
& $adb -s $Serial shell "run-as $pkg mkdir -p files/duel_assets/env" | Out-Null
& $adb -s $Serial shell "mkdir -p $stg" | Out-Null
& $adb -s $Serial push $cook "$stg/space_env.png" 2>&1 | Select-Object -Last 1
& $adb -s $Serial shell "run-as $pkg cp $stg/space_env.png files/duel_assets/env/space_env.png" | Out-Null
& $adb -s $Serial shell "rm -rf $stg; run-as $pkg ls -l files/duel_assets/env"
