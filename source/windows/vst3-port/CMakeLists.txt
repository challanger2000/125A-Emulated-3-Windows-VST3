cmake_minimum_required(VERSION 3.25)
project(Emulated3VST3 VERSION 0.0.1 LANGUAGES CXX)
if(NOT DEFINED VST3_SDK_ROOT)
  message(FATAL_ERROR "Pass -DVST3_SDK_ROOT=path/to/vst3sdk")
endif()
set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES ON CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "" FORCE)
add_subdirectory("${VST3_SDK_ROOT}" "${CMAKE_BINARY_DIR}/sdk" EXCLUDE_FROM_ALL)
smtg_add_vst3plugin(EIII125A "vst3_port.cpp")
# Two independent factory IDs built over the same eiii_runtime_probe object graph.
smtg_add_vst3plugin(VintageSamplerIII125A "vst3_port.cpp")
target_compile_definitions(VintageSamplerIII125A PRIVATE EIII_125A_PUBLIC_VARIANT=1)
# Port the upstream cross-platform artwork/rasterizer and LCD/LED interaction.
# Only the X11 blitter is replaced by an HWND DIBSection.
set(EIII_UPSTREAM "${CMAKE_CURRENT_LIST_DIR}/../../upstream")
find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(EIII_LOGO_SVG "${EIII_UPSTREAM}/ui/125A_Logo_Master_FINAL.svg")
set(EIII_LOGO_HEADER "${CMAKE_CURRENT_BINARY_DIR}/125a_logo_master_geometry.h")
execute_process(
 COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/generate_125a_logo.py" "${EIII_LOGO_SVG}" "${EIII_LOGO_HEADER}"
 RESULT_VARIABLE EIII_LOGO_STATUS
 OUTPUT_VARIABLE EIII_LOGO_OUTPUT
 ERROR_VARIABLE EIII_LOGO_ERROR)
if(NOT EIII_LOGO_STATUS EQUAL 0)
 message(FATAL_ERROR "Official 125A logo vector preparation failed: ${EIII_LOGO_ERROR}")
endif()
target_include_directories(VintageSamplerIII125A PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
enable_language(RC)
set(EIII_FONT_REGULAR "${EIII_UPSTREAM}/third_party/fonts/NotoSans-CondensedMedium.ttf")
set(EIII_FONT_BOLD "${EIII_UPSTREAM}/third_party/fonts/NotoSans-CondensedSemiBold.ttf")
set(EIII_FONT_NARROW "${EIII_UPSTREAM}/third_party/fonts/ArchivoNarrow-wght.ttf")
set(EIII_FONT_RC "${CMAKE_CURRENT_BINARY_DIR}/eiii_fonts.rc")
set(EIII_FLOPPY_PNG "${CMAKE_CURRENT_LIST_DIR}/../../docs/retro_diskettenlaufwerk_mit_warmem_leuchtschlitz.png")
# Configure a valid RC script with forward-slash absolute paths. Do not
# double-escape CMake quoted strings (that produces invalid Windows resources).
configure_file("${CMAKE_CURRENT_LIST_DIR}/eiii_fonts.rc.in" "${EIII_FONT_RC}" @ONLY)
target_sources(EIII125A PRIVATE
  "eiii_editor_win.cpp"
  "${EIII_FONT_RC}"
  "${EIII_UPSTREAM}/panel/eiii_panel_adapter.cpp"
  "${EIII_UPSTREAM}/ui/panel_view.cpp"
  "${EIII_UPSTREAM}/ui/panel_menu.cpp"
  "${EIII_UPSTREAM}/ui/raster.cpp"
)
target_sources(VintageSamplerIII125A PRIVATE
  "eiii_editor_win.cpp"
  "${EIII_FONT_RC}"
  "${EIII_UPSTREAM}/panel/eiii_panel_adapter.cpp"
  "${EIII_UPSTREAM}/ui/panel_view.cpp"
  "${EIII_UPSTREAM}/ui/panel_menu.cpp"
  "${EIII_UPSTREAM}/ui/raster.cpp"
)
target_compile_features(EIII125A PRIVATE cxx_std_23)
target_include_directories(EIII125A PRIVATE "${VST3_SDK_ROOT}")
target_link_libraries(EIII125A PRIVATE sdk user32 gdi32 gdiplus comdlg32 shell32 ole32)
target_compile_features(VintageSamplerIII125A PRIVATE cxx_std_23)
target_include_directories(VintageSamplerIII125A PRIVATE "${VST3_SDK_ROOT}")
target_link_libraries(VintageSamplerIII125A PRIVATE sdk user32 gdi32 gdiplus comdlg32 shell32 ole32)

# Reuse the identical runtime/MAME/SoftFloat source graph tested in Windows M2.
add_subdirectory(".." "${CMAKE_BINARY_DIR}/emulator-core")
target_sources(EIII125A PRIVATE $<TARGET_OBJECTS:eiii_runtime_probe>)
target_link_libraries(EIII125A PRIVATE eiii_voice_core eiii_softfloat)
target_include_directories(EIII125A PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../../upstream")
target_sources(VintageSamplerIII125A PRIVATE $<TARGET_OBJECTS:eiii_runtime_probe>)
target_link_libraries(VintageSamplerIII125A PRIVATE eiii_voice_core eiii_softfloat)
target_include_directories(VintageSamplerIII125A PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../../upstream")

# Bank autoload is implemented exclusively by the shared emulator worker.

# Exercise the packaged factory and actual processor state, not a codec mock.
add_executable(eiii_wav_host_test wav_host_test.cpp)
target_compile_features(eiii_wav_host_test PRIVATE cxx_std_23)
target_include_directories(eiii_wav_host_test PRIVATE "${VST3_SDK_ROOT}" "${EIII_UPSTREAM}")
target_link_libraries(eiii_wav_host_test PRIVATE sdk_hosting)

# Explicit local QA target; no workflow dispatch or automatic Windows build.
add_executable(eiii_offline_host_test offline_host_test.cpp)
target_compile_features(eiii_offline_host_test PRIVATE cxx_std_23)
target_include_directories(eiii_offline_host_test PRIVATE "${VST3_SDK_ROOT}" "${EIII_UPSTREAM}")
target_link_libraries(eiii_offline_host_test PRIVATE sdk_hosting)

# A14: test real packaged VST3 controller parameter-info defaults and MIDI mapping.
add_executable(eiii_pitchbend_host_test midi_pitchbend_host_test.cpp)
target_compile_features(eiii_pitchbend_host_test PRIVATE cxx_std_23)
target_include_directories(eiii_pitchbend_host_test PRIVATE "${VST3_SDK_ROOT}" "${EIII_UPSTREAM}")
target_link_libraries(eiii_pitchbend_host_test PRIVATE sdk_hosting)

# A10: packaged processor/controller IComponentHandler2 dirty integration without
# original firmware; verifies host notification rather than only core helper.
add_executable(eiii_dirty_host_test dirty_host_test.cpp)
target_compile_features(eiii_dirty_host_test PRIVATE cxx_std_23)
target_include_directories(eiii_dirty_host_test PRIVATE "${VST3_SDK_ROOT}" "${EIII_UPSTREAM}")
target_link_libraries(eiii_dirty_host_test PRIVATE sdk_hosting)

# A12: load the real VST3, restore state, attach HWND, reject/accept host resize
# and reopen all 6 geometry choices without ROMs.
add_executable(eiii_zoom_host_test zoom_host_test.cpp)
target_compile_features(eiii_zoom_host_test PRIVATE cxx_std_23)
target_include_directories(eiii_zoom_host_test PRIVATE "${VST3_SDK_ROOT}" "${EIII_UPSTREAM}")
target_link_libraries(eiii_zoom_host_test PRIVATE sdk_hosting user32)

# Firmware-menu macro plumbing test; this does not replace original-ROM acceptance.
add_executable(eiii_midi_panel_macro_test "../tests/ui_macro_adc_tests.cpp")
target_compile_features(eiii_midi_panel_macro_test PRIVATE cxx_std_23)

# Opt-in original-media test; uses the packaged VST3 factory on Windows.
add_executable(eiii_midi_defaults_host_test midi_defaults_host_test.cpp)
target_compile_features(eiii_midi_defaults_host_test PRIVATE cxx_std_23)
target_include_directories(eiii_midi_defaults_host_test PRIVATE "${VST3_SDK_ROOT}" "${EIII_UPSTREAM}")
target_link_libraries(eiii_midi_defaults_host_test PRIVATE sdk_hosting eiii_voice_core)

# Original-media B02 host gate; no synthetic replacement for missing firmware.
add_executable(eiii_b02_host_test b02_host_test.cpp)
target_compile_features(eiii_b02_host_test PRIVATE cxx_std_23)
target_include_directories(eiii_b02_host_test PRIVATE "${VST3_SDK_ROOT}" "${EIII_UPSTREAM}")
target_link_libraries(eiii_b02_host_test PRIVATE sdk_hosting eiii_voice_core)
