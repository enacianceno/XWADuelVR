# Pushes audio to the Quest (duel_assets/audio). Proprietary XWA/XWAUP
# sounds stay in the user's install + device storage; NEVER in repo/APK.
# Music is decoded on-device from the user's own .IMC (real VIMA decoder).
param([string]$Serial = "3487C10H9T0BKZ")
# NOTE: adb reports via stderr; keep Continue and verify with the final ls.
$ErrorActionPreference = "Continue"
$adb = "$env:LOCALAPPDATA\Android\Sdk\platform-tools\adb.exe"
$xwa = "C:\games\XWAU-Mods\XWAU2025"
$pkg = "org.openxwa.xwaduellvr"
$dst = "/data/user/0/$pkg/files/duel_assets/audio"
$stg = "/data/local/tmp/duel_audio_staging"
& $adb -s $Serial shell "run-as $pkg mkdir -p files/duel_assets/audio" | Out-Null
& $adb -s $Serial shell "mkdir -p $stg" | Out-Null
$map = @(
    @("$xwa\Wave\Sfx_Weapon\RebelLaser.WAV", "fire_player.wav"),
    @("$xwa\Wave\Sfx_Weapon\EmpireLaser.wav", "fire_tie.wav"),
    @("$xwa\Wave\FE_HIGH_RES\EXPLOSIONSPARKS1.WAV", "hit_player.wav"),
    @("$xwa\Wave\FE_HIGH_RES\EXPLOSIONSML1.WAV", "hit_tie.wav"),
    @("$xwa\Wave\FE_HIGH_RES\EXPLOSIONFIGHTER1.WAV", "boom_player.wav"),
    @("$xwa\Wave\FE_HIGH_RES\EXPLOSIONFIGHTER1.WAV", "boom_tie.wav"),
    @("$xwa\Wave\Sfx_Interior\EngineIntXW.WAV", "engine_xw.wav"),
    @("$xwa\Wave\Sfx_Interior\EngineIntTI.WAV", "engine_tie.wav"),
    @("$xwa\Wave\Sfx_Sounds\AlarmDanger.WAV", "alarm.wav"),
    @("$xwa\Sfx\R2A.WAV", "r2.wav")
)
foreach ($m in $map) {
    & $adb -s $Serial push $m[0] "$stg/$($m[1])" 2>&1 | Select-Object -Last 1
    & $adb -s $Serial shell "run-as $pkg cp $stg/$($m[1]) files/duel_assets/audio/$($m[1])" | Out-Null
}
# Music: decode STCONFREB (rebel conflict, 97 s loop) on-device.
& $adb -s $Serial push "$xwa\Music\STCONFREB.IMC" $stg/music.imc 2>&1 | Select-Object -Last 1
& $adb -s $Serial shell /data/local/tmp/imc2wav $stg/music.imc $stg/music.wav
& $adb -s $Serial shell "run-as $pkg cp $stg/music.wav files/duel_assets/audio/music.wav"
& $adb -s $Serial shell "rm -rf $stg; run-as $pkg ls -l files/duel_assets/audio"
