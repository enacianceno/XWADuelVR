param([string]$SdkRoot = "$env:LOCALAPPDATA/Android/Sdk")
$ErrorActionPreference = 'Stop'
$duelRoot = $PSScriptRoot.Replace('\','/')
$m2Root = "C:/OpenXWA/XWAQuest/m2"
$ndk = "$SdkRoot/ndk/28.2.13676358"
$cmake = "$SdkRoot/cmake/3.22.1/bin/cmake.exe"
New-Item -ItemType Directory "$duelRoot/evidence" -Force | Out-Null
& $cmake -S $duelRoot -B "$duelRoot/build-android" -G Ninja `
    "-DCMAKE_MAKE_PROGRAM=$SdkRoot/cmake/3.22.1/bin/ninja.exe" `
    "-DCMAKE_TOOLCHAIN_FILE=$ndk/build/cmake/android.toolchain.cmake" `
    '-DANDROID_ABI=arm64-v8a' '-DANDROID_PLATFORM=android-29' `
    '-DANDROID_STL=c++_shared' '-DCMAKE_BUILD_TYPE=RelWithDebInfo' 2>&1 | Tee-Object "$duelRoot/evidence/configure.log"
if ($LASTEXITCODE) { throw 'DuelVR configure failed' }
& $cmake --build "$duelRoot/build-android" -j 8 2>&1 | Tee-Object "$duelRoot/evidence/native-build.log"
if ($LASTEXITCODE) { throw 'DuelVR native build failed' }
$jni = "$duelRoot/app/build/generated/engineJni/arm64-v8a"
New-Item -ItemType Directory $jni -Force | Out-Null
Copy-Item "$duelRoot/build-android/libXWADuelVR.so" $jni
Copy-Item "$m2Root/build-android/SDL3/libSDL3.so" $jni
Copy-Item "$m2Root/app/build/generated/openxrJni/arm64-v8a/libopenxr_loader.so" $jni
Copy-Item "$ndk/toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so" $jni -ErrorAction SilentlyContinue
