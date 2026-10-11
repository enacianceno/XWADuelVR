cmake_minimum_required(VERSION 3.22)
project(XWADuelVR LANGUAGES C CXX)
if(NOT ANDROID OR NOT ANDROID_ABI STREQUAL "arm64-v8a")
    message(FATAL_ERROR "DuelVR requires Android arm64-v8a")
endif()
# Read-only third-party refs (NOT vendored, NOT modified):
# SDL3 headers + libSDL3.so from the proven M2 build, OpenXR headers from SDL3.
set(SDL3_ROOT "C:/OpenXWA/third_party/SDL3")
set(M2 "C:/OpenXWA/XWAQuest/m2")

add_library(XWADuelVR SHARED
    src/duel_main.c
    src/duel_sim.c
    src/duel_net.c
    src/duel_env.c
    src/duel_sky.c
    src/duel_sprite.c
    src/duel_depth.c
    src/duel_motion.c
    src/duel_audio.c
    src/duel_audio_core.c
    src/duel_xr.c
    src/duel_input.c
    src/duel_hands.c
    src/duel_render.c
    src/duel_font.c
    src/duel_menu.c
    src/duel_profile.c)
target_compile_definitions(XWADuelVR PRIVATE SDL_MAIN_AVAILABLE)
target_include_directories(XWADuelVR PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/src"
    "${SDL3_ROOT}/include"
    "${SDL3_ROOT}/src/video/khronos"
    "${M2}/build-android/SDL3/include-revision")
set_target_properties(XWADuelVR PROPERTIES LINKER_LANGUAGE C C_STANDARD 99)
target_link_options(XWADuelVR PRIVATE "-Wl,--no-undefined")
target_link_libraries(XWADuelVR PRIVATE
    "${M2}/build-android/SDL3/libSDL3.so"
    log android m dl)
