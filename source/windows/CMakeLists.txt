cmake_minimum_required(VERSION 3.23)
project(Emulated3PortableCore LANGUAGES CXX)
# M2: MSVC smoke for original portable voice core only.
# This does NOT compile the original emulator runtime, UI or VST3.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# SoftFloat's rounding mode/exception flags must be per worker thread, not
# process-wide globals shared between simultaneous EIII VST3 instances.
# Apply to the SoftFloat definitions AND every consuming translation unit.
add_compile_definitions(THREAD_LOCAL=thread_local)
set(ROOT "${CMAKE_CURRENT_LIST_DIR}/../upstream")
add_library(eiii_voice_core STATIC
  "${ROOT}/fchip/eiii_fchip.cpp"
  "${ROOT}/cem/eiii_cem3387.cpp"
  "${ROOT}/cem/eiii_cem_calibration.cpp"
  "${ROOT}/cem/eiii_cem_cv_conditioning.cpp"
  "${ROOT}/cem/eiii_cem3387_v2.cpp"
  "${ROOT}/host/eiii_backend.cpp"
  "${ROOT}/panel/eiii_panel_model.cpp"
  "${ROOT}/panel/eiii_lcd.cpp"
)
target_include_directories(eiii_voice_core PUBLIC "${ROOT}")
if(MSVC)
  target_compile_options(eiii_voice_core PRIVATE /W3 /fp:precise)
endif()
enable_testing()
add_executable(eiii_wav_import_test "tests/wav_import_tests.cpp" "${ROOT}/runtime/wav_import.cpp")
add_test(NAME eiii_wav_import COMMAND eiii_wav_import_test)

add_executable(eiii_progress_watchdog_test "tests/progress_watchdog_tests.cpp")
add_test(NAME eiii_progress_watchdog COMMAND eiii_progress_watchdog_test)

foreach(TEST_NAME IN ITEMS fchip_unit cem_unit)
  add_executable(${TEST_NAME} "${ROOT}/tests/${TEST_NAME}.cpp")
  target_link_libraries(${TEST_NAME} PRIVATE eiii_voice_core)
  add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endforeach()

# B02: capture-to-encoded-memory handoff, using mock worker.
add_executable(eiii_memory_capture_test "tests/memory_capture_tests.cpp")
add_test(NAME eiii_memory_capture COMMAND eiii_memory_capture_test)

# B02: bounded versioned RAM/sample-RAM codec, no SDK/ROM dependencies.
add_executable(eiii_machine_memory_codec_test "tests/machine_memory_codec_tests.cpp")
add_test(NAME eiii_machine_memory_codec COMMAND eiii_machine_memory_codec_test)
add_executable(eiii_b02_core_memory_section_test "tests/b02_core_memory_section_tests.cpp")
add_test(NAME eiii_b02_core_memory_section COMMAND eiii_b02_core_memory_section_test)
add_executable(eiii_b02_core_midi_section_test "tests/b02_core_midi_section_tests.cpp")
add_test(NAME eiii_b02_core_midi_section COMMAND eiii_b02_core_midi_section_test)
add_executable(eiii_b02_core_partial_snapshot_test "tests/b02_core_partial_snapshot_tests.cpp")
add_test(NAME eiii_b02_core_partial_snapshot COMMAND eiii_b02_core_partial_snapshot_test)
add_executable(eiii_b02_core_execution_section_test "tests/b02_core_execution_section_tests.cpp")
add_test(NAME eiii_b02_core_execution_section COMMAND eiii_b02_core_execution_section_test)
add_executable(eiii_b02_core_checkpoint_bundle_test "tests/b02_core_checkpoint_bundle_tests.cpp")
add_test(NAME eiii_b02_core_checkpoint_bundle COMMAND eiii_b02_core_checkpoint_bundle_test)
add_executable(eiii_b02_core_cpu_registers_test "tests/b02_core_cpu_registers_tests.cpp")
add_test(NAME eiii_b02_core_cpu_registers COMMAND eiii_b02_core_cpu_registers_test)
add_executable(eiii_b02_core_fpu_section_test "tests/b02_core_fpu_section_tests.cpp")
add_test(NAME eiii_b02_core_fpu_section COMMAND eiii_b02_core_fpu_section_test)
add_executable(eiii_b02_core_scheduler_section_test "tests/b02_core_scheduler_section_tests.cpp")
add_test(NAME eiii_b02_core_scheduler_section COMMAND eiii_b02_core_scheduler_section_test)
add_executable(eiii_b02_core_ncr_section_test "tests/b02_core_ncr_section_tests.cpp")
add_test(NAME eiii_b02_core_ncr_section COMMAND eiii_b02_core_ncr_section_test)
add_executable(eiii_b02_core_scsi_bus_section_test "tests/b02_core_scsi_bus_section_tests.cpp")
add_test(NAME eiii_b02_core_scsi_bus_section COMMAND eiii_b02_core_scsi_bus_section_test)
add_executable(eiii_b02_core_scsi_target_section_test "tests/b02_core_scsi_target_section_tests.cpp")
add_test(NAME eiii_b02_core_scsi_target_section COMMAND eiii_b02_core_scsi_target_section_test)
add_executable(eiii_b02_core_hdd_section_test "tests/b02_core_hdd_section_tests.cpp")
add_test(NAME eiii_b02_core_hdd_section COMMAND eiii_b02_core_hdd_section_test)
add_executable(eiii_b02_core_cd_section_test "tests/b02_core_cd_section_tests.cpp")
add_test(NAME eiii_b02_core_cd_section COMMAND eiii_b02_core_cd_section_test)
add_executable(eiii_b02_core_section_container_test "tests/b02_core_section_container_tests.cpp")
add_test(NAME eiii_b02_core_section_container COMMAND eiii_b02_core_section_container_test)
add_executable(eiii_b02_core_section_roster_test "tests/b02_core_section_roster_tests.cpp")
add_test(NAME eiii_b02_core_section_roster COMMAND eiii_b02_core_section_roster_test)
add_executable(eiii_b02_core_scc_registers_test "tests/b02_core_scc_registers_tests.cpp")
add_test(NAME eiii_b02_core_scc_registers COMMAND eiii_b02_core_scc_registers_test)
add_executable(eiii_b02_core_scc_controller_test "tests/b02_core_scc_controller_tests.cpp")
add_test(NAME eiii_b02_core_scc_controller COMMAND eiii_b02_core_scc_controller_test)
add_executable(eiii_b02_core_scc_fifo_test "tests/b02_core_scc_fifo_tests.cpp")
add_test(NAME eiii_b02_core_scc_fifo COMMAND eiii_b02_core_scc_fifo_test)
add_executable(eiii_b02_core_scc_lines_test "tests/b02_core_scc_lines_tests.cpp")
add_test(NAME eiii_b02_core_scc_lines COMMAND eiii_b02_core_scc_lines_test)
add_executable(eiii_b02_core_cpu_clock_test "tests/b02_core_cpu_clock_tests.cpp")
add_test(NAME eiii_b02_core_cpu_clock COMMAND eiii_b02_core_cpu_clock_test)
add_executable(eiii_b02_core_motherboard_lcd_test "tests/b02_core_motherboard_lcd_tests.cpp")
add_test(NAME eiii_b02_core_motherboard_lcd COMMAND eiii_b02_core_motherboard_lcd_test)
add_executable(eiii_b02_core_motherboard_panel_test "tests/b02_core_motherboard_panel_tests.cpp")
add_test(NAME eiii_b02_core_motherboard_panel COMMAND eiii_b02_core_motherboard_panel_test)
add_executable(eiii_b02_core_motherboard_registers_test "tests/b02_core_motherboard_registers_tests.cpp")
add_test(NAME eiii_b02_core_motherboard_registers COMMAND eiii_b02_core_motherboard_registers_test)
add_executable(eiii_b02_core_motherboard_pit_test "tests/b02_core_motherboard_pit_tests.cpp")
add_test(NAME eiii_b02_core_motherboard_pit COMMAND eiii_b02_core_motherboard_pit_test)
add_executable(eiii_b02_core_fchip_section_test "tests/b02_core_fchip_section_tests.cpp")
add_test(NAME eiii_b02_core_fchip_section COMMAND eiii_b02_core_fchip_section_test)
add_executable(eiii_b02_fir_continuation_test "tests/b02_fir_continuation_tests.cpp")
add_test(NAME eiii_b02_fir_continuation COMMAND eiii_b02_fir_continuation_test)
add_executable(eiii_b02_core_fir_section_test "tests/b02_core_fir_section_tests.cpp")
add_test(NAME eiii_b02_core_fir_section COMMAND eiii_b02_core_fir_section_test)
add_executable(eiii_b02_cem_v1_section_test "tests/b02_cem_v1_section_tests.cpp")
add_test(NAME eiii_b02_cem_v1_section COMMAND eiii_b02_cem_v1_section_test)
add_executable(eiii_b02_cem_v2_section_test "tests/b02_cem_v2_section_tests.cpp")
target_link_libraries(eiii_b02_cem_v2_section_test PRIVATE eiii_voice_core)
add_test(NAME eiii_b02_cem_v2_section COMMAND eiii_b02_cem_v2_section_test)
add_executable(eiii_b02_core_audio_timing_test "tests/b02_core_audio_timing_tests.cpp")
add_test(NAME eiii_b02_core_audio_timing COMMAND eiii_b02_core_audio_timing_test)
add_executable(eiii_b02_core_audio_queue_test "tests/b02_core_audio_queue_tests.cpp")
add_test(NAME eiii_b02_core_audio_queue COMMAND eiii_b02_core_audio_queue_test)
add_executable(eiii_b02_cem_v1_continuation_test "tests/b02_cem_v1_continuation_tests.cpp")
target_link_libraries(eiii_b02_cem_v1_continuation_test PRIVATE eiii_voice_core)
add_test(NAME eiii_b02_cem_v1_continuation COMMAND eiii_b02_cem_v1_continuation_test)
add_executable(eiii_b02_cem_v2_cv_continuation_test "tests/b02_cem_v2_cv_continuation_tests.cpp")
target_link_libraries(eiii_b02_cem_v2_cv_continuation_test PRIVATE eiii_voice_core)
add_test(NAME eiii_b02_cem_v2_cv_continuation COMMAND eiii_b02_cem_v2_cv_continuation_test)
add_executable(eiii_b02_cem_v2_voice_continuation_test "tests/b02_cem_v2_voice_continuation_tests.cpp")
target_link_libraries(eiii_b02_cem_v2_voice_continuation_test PRIVATE eiii_voice_core)
add_test(NAME eiii_b02_cem_v2_voice_continuation COMMAND eiii_b02_cem_v2_voice_continuation_test)
add_executable(eiii_b02_cem_v2_output_continuation_test "tests/b02_cem_v2_output_continuation_tests.cpp")
target_link_libraries(eiii_b02_cem_v2_output_continuation_test PRIVATE eiii_voice_core)
add_test(NAME eiii_b02_cem_v2_output_continuation COMMAND eiii_b02_cem_v2_output_continuation_test)
add_executable(eiii_b02_cem_v2_bank_continuation_test "tests/b02_cem_v2_bank_continuation_tests.cpp")
target_link_libraries(eiii_b02_cem_v2_bank_continuation_test PRIVATE eiii_voice_core)
add_test(NAME eiii_b02_cem_v2_bank_continuation COMMAND eiii_b02_cem_v2_bank_continuation_test)

# B08 VST3 MIDI CC, sustain and pitch bend conversion, SDK independent.
add_executable(eiii_midi_controller_test "tests/midi_controller_tests.cpp")
add_test(NAME eiii_midi_controller COMMAND eiii_midi_controller_test)

# B04 project restore while engine active must survive deactivation.
add_executable(eiii_project_activation_test "tests/project_activation_tests.cpp")
add_test(NAME eiii_project_activation COMMAND eiii_project_activation_test)

# Disk export must flush the worker cache before capturing disk overlays.
add_executable(eiii_disk_export_snapshot_test "tests/disk_export_snapshot_tests.cpp")
add_test(NAME eiii_disk_export_snapshot COMMAND eiii_disk_export_snapshot_test)

# R02: no exception may escape the disk-export std::thread entrypoint.
add_executable(eiii_export_worker_guard_test "tests/export_worker_guard_tests.cpp")
add_test(NAME eiii_export_worker_guard COMMAND eiii_export_worker_guard_test)

# R04: actual Win32 UI-message loop with editor-independent per-instance timers.
add_executable(eiii_headless_dirty_pump_test "tests/headless_dirty_pump_tests.cpp")
add_test(NAME eiii_headless_dirty_pump COMMAND eiii_headless_dirty_pump_test)

# Async song restore results must not override later projects or user MIDI edits.
add_executable(eiii_restore_revisions_test "tests/restore_revisions_tests.cpp")
add_test(NAME eiii_restore_revisions COMMAND eiii_restore_revisions_test)

# A03: whole-image fingerprint policy and stream segment stability.
add_executable(eiii_image_fingerprint_test "tests/image_fingerprint_tests.cpp")
add_test(NAME eiii_image_fingerprint COMMAND eiii_image_fingerprint_test)

# A17: CD READ(6/10) last-LBA/out-of-range protections.
add_executable(eiii_cd_read_bounds_test "tests/cd_read_bounds_tests.cpp")
add_test(NAME eiii_cd_read_bounds COMMAND eiii_cd_read_bounds_test)

# A05: bounded CD MODE SELECT(6) parsing shared with the productive device.
add_executable(eiii_cd_mode_select_test "tests/cd_mode_select_tests.cpp")
add_test(NAME eiii_cd_mode_select COMMAND eiii_cd_mode_select_test)

# A04: production EIIIX/ESI converter with corrupt FAT and cleanup cases.
add_executable(eiii_bank_corruption_test "tests/bank_corruption_tests.cpp"
  "${ROOT}/runtime/eiii_bank_import.cpp")
target_include_directories(eiii_bank_corruption_test PRIVATE "${ROOT}")
add_test(NAME eiii_bank_corruption COMMAND eiii_bank_corruption_test)

# B11: disk overlay save protection, tested in Release builds.
add_executable(eiii_overlay_restore_guard_test "tests/overlay_restore_guard_tests.cpp")
add_test(NAME eiii_overlay_restore_guard COMMAND eiii_overlay_restore_guard_test)

# Shared host/engine latency calculation regression.
add_executable(eiii_latency_policy_test "tests/latency_policy_tests.cpp")
add_test(NAME eiii_latency_policy COMMAND eiii_latency_policy_test)

# Fixed-buffer NoteOff queue overflow / sustain recovery (same code used by VST3).
add_executable(eiii_noteoff_recovery_test "tests/note_off_recovery_tests.cpp")
target_compile_features(eiii_noteoff_recovery_test PRIVATE cxx_std_17)
add_test(NAME eiii_noteoff_recovery COMMAND eiii_noteoff_recovery_test)

# A14: production 16-channel MIDI controller default/bend decoding.
add_executable(eiii_pitchbend_defaults_test "tests/midi_pitchbend_defaults_tests.cpp")
target_compile_features(eiii_pitchbend_defaults_test PRIVATE cxx_std_23)
add_test(NAME eiii_pitchbend_defaults COMMAND eiii_pitchbend_defaults_test)

# A12: six GUI zoom steps, host-acknowledged acceptance/rejection and strict codec.
add_executable(eiii_zoom_state_test "tests/zoom_state_tests.cpp")
add_test(NAME eiii_zoom_state COMMAND eiii_zoom_state_test)

# A10 accepted-project-change revision/handler delivery regression (Release asserts enabled).
add_executable(eiii_project_dirty_revision_test "tests/project_dirty_revision_tests.cpp")
add_test(NAME eiii_project_dirty_revision COMMAND eiii_project_dirty_revision_test)

# A08/A09 shared host-production controller dispatcher + full-queue pedal recovery.
add_executable(eiii_midi_dispatch_test "tests/midi_dispatch_tests.cpp")
add_test(NAME eiii_midi_dispatch COMMAND eiii_midi_dispatch_test)

# Pure routing regression (no ROM, OS image or Steinberg SDK required).
add_executable(eiii_midi_routing_test "tests/midi_routing_tests.cpp")
target_compile_features(eiii_midi_routing_test PRIVATE cxx_std_17)
add_test(NAME eiii_midi_routing COMMAND eiii_midi_routing_test)

# Engine lifecycle primitives: stale MIDI/panel and audio buffers must not survive stop/start.
add_executable(eiii_restart_queue_test "tests/restart_queue_tests.cpp")
target_compile_features(eiii_restart_queue_test PRIVATE cxx_std_17)
add_test(NAME eiii_restart_queue COMMAND eiii_restart_queue_test)

# Production ADC quantization boundaries and non-finite values.
add_executable(eiii_adc_quantize_test "tests/adc_quantize_tests.cpp")
add_test(NAME eiii_adc_quantize COMMAND eiii_adc_quantize_test)

# ADC/sample-input adapter unit test, without ROM images or Steinberg SDK.
add_executable(eiii_sample_input_test "tests/sample_input_tests.cpp")
target_compile_features(eiii_sample_input_test PRIVATE cxx_std_17)
add_test(NAME eiii_sample_input COMMAND eiii_sample_input_test)

# Project-state codec regression: standalone, no plugin SDK or proprietary ROMs.
add_executable(eiii_state_codec_test "tests/state_codec_tests.cpp")
target_compile_features(eiii_state_codec_test PRIVATE cxx_std_17)
add_test(NAME eiii_state_codec COMMAND eiii_state_codec_test)

# Large Windows file offsets use the same helpers as production disk devices.
add_executable(eiii_large_file_test "tests/large_file_tests.cpp")
add_test(NAME eiii_large_file COMMAND eiii_large_file_test)

# Regression of the exact production HD-sector boundary predicate.
add_executable(eiii_sector_bounds_test "tests/sector_bounds_tests.cpp")
target_compile_features(eiii_sector_bounds_test PRIVATE cxx_std_17)
add_test(NAME eiii_sector_bounds COMMAND eiii_sector_bounds_test)

# M2 runtime compile probe; Windows port incompatibilities are intentionally
# exposed by MSVC. Do not treat successful voice-core tests as runtime QA.
add_library(eiii_runtime_probe OBJECT
  "${ROOT}/runtime/eiii_runtime.cpp"
  "${ROOT}/runtime/eiii_bank_import.cpp"
  "${ROOT}/runtime/eiii_engine.cpp"
  "${ROOT}/runtime/wav_import.cpp"
  "${ROOT}/runtime/wav_firmware_import.cpp"
  "${ROOT}/standalone/shim/emu_shim.cpp"
  "${ROOT}/standalone/eiii_scsi.cpp"
  "${ROOT}/standalone/diserial_shim.cpp"
  "${ROOT}/standalone/third_party/mame/src/devices/cpu/ns32000/ns32000.cpp"
  "${ROOT}/standalone/third_party/mame/src/devices/machine/ns32081.cpp"
  "${ROOT}/standalone/third_party/mame/src/devices/machine/ncr5380.cpp"
  "${ROOT}/standalone/third_party/mame/src/devices/machine/wd_fdc.cpp"
  "${ROOT}/standalone/third_party/mame/src/devices/machine/fdc_pll.cpp"
  "${ROOT}/standalone/third_party/mame/src/devices/cpu/ns32000/ns32000d.cpp"
  "${ROOT}/standalone/third_party/mame/src/devices/machine/nscsi_bus.cpp"
  "${ROOT}/standalone/third_party/mame/src/devices/machine/nscsi_hle.cpp"
  "${ROOT}/standalone/third_party/mame/src/devices/bus/nscsi/hd.cpp"
  "${ROOT}/standalone/third_party/mame/src/devices/bus/nscsi/cd.cpp"
  "${ROOT}/standalone/third_party/mame/src/devices/machine/z80scc.cpp"
  "${ROOT}/standalone/third_party/mame/src/devices/machine/z80daisy.cpp"
  "${ROOT}/standalone/third_party/mame/src/lib/util/strformat.cpp"
  "${ROOT}/standalone/third_party/mame/src/lib/util/coreutil.cpp"
  "${ROOT}/standalone/third_party/mame/src/lib/util/disasmintf.cpp"
)
set_target_properties(eiii_runtime_probe PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)
target_include_directories(eiii_runtime_probe PRIVATE "${ROOT}" "${ROOT}/standalone" "${ROOT}/standalone/shim"
  "${ROOT}/standalone/third_party/mame" "${ROOT}/standalone/third_party/mame/src/emu"
  "${ROOT}/standalone/third_party/mame/src/lib" "${ROOT}/standalone/third_party/mame/src/lib/util"
  "${ROOT}/standalone/third_party/mame/src/devices" "${ROOT}/standalone/third_party/mame/src/osd"
  "${ROOT}/standalone/third_party/mame/src/devices/cpu/ns32000"
  "${ROOT}/standalone/third_party/mame/3rdparty")

# M2 linking probe: force all runtime object files into a Windows executable.
# Expected initially to expose missing MAME/SoftFloat symbols.
add_executable(eiii_runtime_link_probe
  "${CMAKE_CURRENT_LIST_DIR}/runtime_link_probe.cpp"
  $<TARGET_OBJECTS:eiii_runtime_probe>
)
set_target_properties(eiii_runtime_link_probe PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)
target_link_libraries(eiii_runtime_link_probe PRIVATE eiii_voice_core)
target_include_directories(eiii_runtime_link_probe PRIVATE
  "${ROOT}/standalone"
  "${ROOT}/standalone/shim"
  "${ROOT}/standalone/third_party/mame"
  "${ROOT}/standalone/third_party/mame/src/emu"
  "${ROOT}/standalone/third_party/mame/src/lib"
  "${ROOT}/standalone/third_party/mame/src/lib/util"
  "${ROOT}/standalone/third_party/mame/src/devices"
  "${ROOT}/standalone/third_party/mame/src/osd"
  "${ROOT}/standalone/third_party/mame/3rdparty")


# SoftFloat is included as upstream .c sources compiled as C++ (matching Linux Makefile).
set(SOFTFLOAT_SOURCES
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/s_eq128.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/s_le128.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/s_lt128.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/s_shortShiftLeft128.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/s_shortShiftRight128.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/s_shortShiftRightJam64.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/s_shortShiftRightJam64Extra.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/s_shortShiftRightJam128.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/s_shortShiftRightJam128Extra.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/s_shiftRightJam32.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/s_shiftRightJam64.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/s_shiftRightJam64Extra.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/s_shiftRightJam128.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/s_shiftRightJam128Extra.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/s_shiftRightJam256M.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/s_countLeadingZeros8.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/s_countLeadingZeros16.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/s_countLeadingZeros32.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/s_countLeadingZeros64.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/s_add128.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/s_add256M.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/s_sub128.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/s_sub256M.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/s_mul64ByShifted32To128.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/s_mul64To128.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/s_mul128By32.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/s_mul128To256M.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/s_approxRecip_1Ks.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/s_approxRecip32_1.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/s_approxRecipSqrt_1Ks.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/s_approxRecipSqrt32_1.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/8086/softfloat_raiseFlags.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/8086/s_commonNaNToF16UI.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/8086/s_propagateNaNF16UI.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/8086/s_f32UIToCommonNaN.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/8086/s_commonNaNToF32UI.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/8086/s_propagateNaNF32UI.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/8086/s_f64UIToCommonNaN.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/8086/s_commonNaNToF64UI.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/8086/s_propagateNaNF64UI.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/8086/extF80M_isSignalingNaN.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/8086/s_extF80UIToCommonNaN.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/8086/s_commonNaNToExtF80UI.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/8086/s_propagateNaNExtF80UI.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/8086/f128M_isSignalingNaN.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/8086/s_f128UIToCommonNaN.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/8086/s_commonNaNToF128UI.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/8086/s_propagateNaNF128UI.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/s_roundToUI32.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/s_roundToUI64.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/s_roundToI32.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/s_roundToI64.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/s_normSubnormalF16Sig.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/s_roundPackToF16.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/s_normRoundPackToF16.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/s_addMagsF16.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/s_subMagsF16.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/s_mulAddF16.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/s_normSubnormalF32Sig.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/s_roundPackToF32.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/s_normRoundPackToF32.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/s_addMagsF32.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/s_subMagsF32.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/s_mulAddF32.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/s_normSubnormalF64Sig.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/s_roundPackToF64.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/s_normRoundPackToF64.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/s_addMagsF64.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/s_subMagsF64.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/s_mulAddF64.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/s_normSubnormalExtF80Sig.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/s_roundPackToExtF80.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/s_normRoundPackToExtF80.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/s_addMagsExtF80.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/s_subMagsExtF80.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/s_normSubnormalF128Sig.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/s_roundPackToF128.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/s_normRoundPackToF128.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/s_addMagsF128.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/s_subMagsF128.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/s_mulAddF128.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/softfloat_state.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/ui32_to_f16.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/ui32_to_f32.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/ui32_to_f64.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/ui32_to_extF80.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/ui32_to_extF80M.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/ui32_to_f128.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/ui32_to_f128M.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/ui64_to_f16.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/ui64_to_f32.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/ui64_to_f64.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/ui64_to_extF80.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/ui64_to_extF80M.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/ui64_to_f128.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/ui64_to_f128M.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/i32_to_f16.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/i32_to_f32.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/i32_to_f64.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/i32_to_extF80.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/i32_to_extF80M.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/i32_to_f128.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/i32_to_f128M.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/i64_to_f16.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/i64_to_f32.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/i64_to_f64.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/i64_to_extF80.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/i64_to_extF80M.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/i64_to_f128.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/i64_to_f128M.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f16_to_ui32.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f16_to_ui64.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f16_to_i32.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f16_to_i64.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f16_to_ui32_r_minMag.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f16_to_ui64_r_minMag.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f16_to_i32_r_minMag.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f16_to_i64_r_minMag.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f16_to_f32.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f16_to_f64.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f16_to_extF80.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f16_to_extF80M.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f16_to_f128.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f16_to_f128M.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f16_roundToInt.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f16_add.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f16_sub.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f16_mul.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f16_mulAdd.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f16_div.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f16_rem.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f16_sqrt.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f16_eq.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f16_le.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f16_lt.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f16_eq_signaling.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f16_le_quiet.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f16_lt_quiet.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f16_isSignalingNaN.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f32_to_ui32.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f32_to_ui64.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f32_to_i32.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f32_to_i64.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f32_to_ui32_r_minMag.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f32_to_ui64_r_minMag.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f32_to_i32_r_minMag.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f32_to_i64_r_minMag.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f32_to_f16.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f32_to_f64.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f32_to_extF80.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f32_to_extF80M.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f32_to_f128.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f32_to_f128M.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f32_roundToInt.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f32_add.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f32_sub.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f32_mul.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f32_mulAdd.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f32_div.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f32_rem.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f32_sqrt.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f32_eq.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f32_le.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f32_lt.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f32_eq_signaling.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f32_le_quiet.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f32_lt_quiet.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f32_isSignalingNaN.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f64_to_ui32.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f64_to_ui64.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f64_to_i32.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f64_to_i64.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f64_to_ui32_r_minMag.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f64_to_ui64_r_minMag.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f64_to_i32_r_minMag.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f64_to_i64_r_minMag.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f64_to_f16.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f64_to_f32.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f64_to_extF80.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f64_to_extF80M.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f64_to_f128.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f64_to_f128M.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f64_roundToInt.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f64_add.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f64_sub.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f64_mul.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f64_mulAdd.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f64_div.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f64_rem.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f64_sqrt.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f64_eq.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f64_le.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f64_lt.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f64_eq_signaling.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f64_le_quiet.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f64_lt_quiet.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f64_isSignalingNaN.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/extF80_to_ui32.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/extF80_to_ui64.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/extF80_to_i32.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/extF80_to_i64.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/extF80_to_ui32_r_minMag.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/extF80_to_ui64_r_minMag.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/extF80_to_i32_r_minMag.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/extF80_to_i64_r_minMag.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/extF80_to_f16.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/extF80_to_f32.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/extF80_to_f64.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/extF80_to_f128.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/extF80_roundToInt.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/extF80_add.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/extF80_sub.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/extF80_mul.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/extF80_div.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/extF80_rem.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/extF80_sqrt.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/extF80_eq.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/extF80_le.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/extF80_lt.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/extF80_eq_signaling.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/extF80_le_quiet.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/extF80_lt_quiet.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/extF80_isSignalingNaN.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/extF80M_to_ui32.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/extF80M_to_ui64.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/extF80M_to_i32.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/extF80M_to_i64.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/extF80M_to_ui32_r_minMag.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/extF80M_to_ui64_r_minMag.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/extF80M_to_i32_r_minMag.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/extF80M_to_i64_r_minMag.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/extF80M_to_f16.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/extF80M_to_f32.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/extF80M_to_f64.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/extF80M_to_f128M.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/extF80M_roundToInt.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/extF80M_add.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/extF80M_sub.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/extF80M_mul.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/extF80M_div.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/extF80M_rem.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/extF80M_sqrt.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/extF80M_eq.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/extF80M_le.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/extF80M_lt.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/extF80M_eq_signaling.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/extF80M_le_quiet.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/extF80M_lt_quiet.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f128_to_ui32.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f128_to_ui64.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f128_to_i32.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f128_to_i64.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f128_to_ui32_r_minMag.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f128_to_ui64_r_minMag.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f128_to_i32_r_minMag.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f128_to_i64_r_minMag.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f128_to_f16.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f128_to_f32.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f128_to_extF80.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f128_to_f64.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f128_roundToInt.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f128_add.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f128_sub.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f128_mul.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f128_mulAdd.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f128_div.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f128_rem.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f128_sqrt.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f128_eq.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f128_le.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f128_lt.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f128_eq_signaling.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f128_le_quiet.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f128_lt_quiet.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f128_isSignalingNaN.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f128M_to_ui32.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f128M_to_ui64.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f128M_to_i32.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f128M_to_i64.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f128M_to_ui32_r_minMag.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f128M_to_ui64_r_minMag.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f128M_to_i32_r_minMag.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f128M_to_i64_r_minMag.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f128M_to_f16.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f128M_to_f32.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f128M_to_extF80M.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f128M_to_f64.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f128M_roundToInt.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f128M_add.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f128M_sub.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f128M_mul.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f128M_mulAdd.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f128M_div.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f128M_rem.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f128M_sqrt.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f128M_eq.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f128M_le.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f128M_lt.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f128M_eq_signaling.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f128M_le_quiet.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/f128M_lt_quiet.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/bochs_ext/f2xm1.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/bochs_ext/fpatan.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/bochs_ext/fprem.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/bochs_ext/fsincos.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/bochs_ext/fyl2x.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/bochs_ext/poly.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/bochs_ext/extF80_scale.c"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/bochs_ext/isNaN.c"
)
set_source_files_properties(${SOFTFLOAT_SOURCES} PROPERTIES LANGUAGE CXX)
add_library(eiii_softfloat STATIC ${SOFTFLOAT_SOURCES})
set_target_properties(eiii_softfloat PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_compile_features(eiii_softfloat PRIVATE cxx_std_17)
target_compile_definitions(eiii_softfloat PRIVATE
  SOFTFLOAT_ROUND_ODD INLINE_LEVEL=5 SOFTFLOAT_FAST_DIV32TO16
  SOFTFLOAT_FAST_DIV64TO32 SOFTFLOAT_FAST_INT64 LITTLEENDIAN=1)
target_include_directories(eiii_softfloat PRIVATE
  "${ROOT}/standalone/third_party/mame/src/osd"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/build/MAME"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/include"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/8086"
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/bochs_ext")
target_link_libraries(eiii_runtime_link_probe PRIVATE eiii_softfloat)

# B01: real bundled SoftFloat globals and NS32081 consumers are built with
# THREAD_LOCAL=thread_local. Reject a cross-worker rounding/flag data race.
add_executable(eiii_softfloat_tls_test "tests/softfloat_tls_tests.cpp")
target_include_directories(eiii_softfloat_tls_test PRIVATE
  "${ROOT}/standalone/third_party/mame/3rdparty/softfloat3/source/include")
target_link_libraries(eiii_softfloat_tls_test PRIVATE eiii_softfloat)
add_test(NAME eiii_softfloat_tls COMMAND eiii_softfloat_tls_test)


# A13: genuine Windows UTF-16/UTF-8 filesystem roundtrip and ROM, HD, CD,
# WAV, bank-cache and export I/O, regardless of Windows ANSI codepage.
# Synthetic media only: this test never uses or embeds copyrighted firmware.
add_executable(eiii_unicode_paths_test
  "tests/unicode_file_paths_tests.cpp"
  $<TARGET_OBJECTS:eiii_runtime_probe>)
target_compile_features(eiii_unicode_paths_test PRIVATE cxx_std_23)
target_link_libraries(eiii_unicode_paths_test PRIVATE eiii_voice_core eiii_softfloat)
add_test(NAME eiii_unicode_paths COMMAND eiii_unicode_paths_test)

# A11: real runtime ADC snapshot published to the exact panel_adapter used
# by the Windows VST3. Verify reopen/two views/firmware move/rejected input.
add_executable(eiii_panel_adc_snapshot_test
  "tests/panel_adc_snapshot_tests.cpp"
  "${ROOT}/panel/eiii_panel_adapter.cpp"
  $<TARGET_OBJECTS:eiii_runtime_probe>)
target_link_libraries(eiii_panel_adc_snapshot_test PRIVATE eiii_voice_core eiii_softfloat)
add_test(NAME eiii_panel_adc_snapshot COMMAND eiii_panel_adc_snapshot_test)

add_test(NAME windows_runtime_missing_rom COMMAND eiii_runtime_link_probe)

# Native VST3 SDK prerequisite, not yet the wrapper itself.
if(DEFINED VST3_SDK_ROOT AND EXISTS "${VST3_SDK_ROOT}/pluginterfaces/vst/ivstaudioprocessor.h")
  add_library(vst3_sdk_probe OBJECT vst3_sdk_probe.cpp)
  target_include_directories(vst3_sdk_probe PRIVATE "${VST3_SDK_ROOT}")
  set_target_properties(vst3_sdk_probe PROPERTIES CXX_STANDARD 17)
endif()

# The runtime includes variable is defined later for legacy targets; capture it
# here too so this earlier D0 test sees the real MAME shim headers.
get_target_property(EIII_D0_RUNTIME_INCLUDES eiii_runtime_probe INCLUDE_DIRECTORIES)
# D0: genuine MAME WD1772 and rotating 800 KiB synthetic disk sectors.
# This test needs no private EIII boot ROMs or proprietary disk images.
add_executable(eiii_d0_controller_test "tests/eiii_d0_controller_tests.cpp"
  $<TARGET_OBJECTS:eiii_runtime_probe>)
target_compile_features(eiii_d0_controller_test PRIVATE cxx_std_23)
target_include_directories(eiii_d0_controller_test PRIVATE ${EIII_D0_RUNTIME_INCLUDES})
target_link_libraries(eiii_d0_controller_test PRIVATE eiii_voice_core eiii_softfloat)
add_test(NAME eiii_d0_controller COMMAND eiii_d0_controller_test)
set_tests_properties(eiii_d0_controller PROPERTIES TIMEOUT 60)

add_executable(eiii_wav_engine_test "tests/wav_engine_tests.cpp" $<TARGET_OBJECTS:eiii_runtime_probe>)
target_link_libraries(eiii_wav_engine_test PRIVATE eiii_voice_core eiii_softfloat)
add_test(NAME eiii_wav_engine COMMAND eiii_wav_engine_test)
set_tests_properties(eiii_wav_engine PROPERTIES TIMEOUT 30)

add_executable(eiii_offline_engine_test "tests/offline_engine_tests.cpp" $<TARGET_OBJECTS:eiii_runtime_probe>)
target_link_libraries(eiii_offline_engine_test PRIVATE eiii_voice_core eiii_softfloat)
add_test(NAME eiii_offline_engine COMMAND eiii_offline_engine_test)
set_tests_properties(eiii_offline_engine PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)

# External original media only; CI without private files reports a skip.
add_executable(eiii_wav_firmware_capture_test "tests/wav_firmware_capture_tests.cpp" $<TARGET_OBJECTS:eiii_runtime_probe>)
get_target_property(EIII_RUNTIME_PROBE_INCLUDES eiii_runtime_probe INCLUDE_DIRECTORIES)
target_include_directories(eiii_wav_firmware_capture_test PRIVATE ${EIII_RUNTIME_PROBE_INCLUDES})

# B02: ROM-free production motherboard peripheral register transaction test.
# Runtime object graph is linked to catch both template/header and ABI errors.
add_executable(eiii_motherboard_recall_test
  "tests/motherboard_recall_tests.cpp"
  $<TARGET_OBJECTS:eiii_runtime_probe>)
target_compile_features(eiii_motherboard_recall_test PRIVATE cxx_std_23)
target_include_directories(eiii_motherboard_recall_test PRIVATE ${EIII_RUNTIME_PROBE_INCLUDES})
target_link_libraries(eiii_motherboard_recall_test PRIVATE eiii_voice_core eiii_softfloat)
add_test(NAME eiii_motherboard_recall COMMAND eiii_motherboard_recall_test)

# B02: stopped CPU clock/cycle restore must not leak between instances.
add_executable(eiii_cpu_clock_recall_test
  "tests/cpu_clock_recall_tests.cpp"
  $<TARGET_OBJECTS:eiii_runtime_probe>)
target_compile_features(eiii_cpu_clock_recall_test PRIVATE cxx_std_23)
target_include_directories(eiii_cpu_clock_recall_test PRIVATE ${EIII_RUNTIME_PROBE_INCLUDES})
target_link_libraries(eiii_cpu_clock_recall_test PRIVATE eiii_voice_core eiii_softfloat)
add_test(NAME eiii_cpu_clock_recall COMMAND eiii_cpu_clock_recall_test)

# B02: real NCR5380 and resolved SCSI bus recall; no original ROM required.
add_executable(eiii_scsi_recall_test
  "tests/scsi_recall_tests.cpp"
  $<TARGET_OBJECTS:eiii_runtime_probe>)
target_compile_features(eiii_scsi_recall_test PRIVATE cxx_std_23)
target_include_directories(eiii_scsi_recall_test PRIVATE ${EIII_RUNTIME_PROBE_INCLUDES})
target_link_libraries(eiii_scsi_recall_test PRIVATE eiii_voice_core eiii_softfloat)
add_test(NAME eiii_scsi_recall COMMAND eiii_scsi_recall_test)
# B02: actual MAME nscsi_full_device command and queued transfer state.
add_executable(eiii_scsi_target_recall_test
  "tests/scsi_target_recall_tests.cpp"
  $<TARGET_OBJECTS:eiii_runtime_probe>)
target_compile_features(eiii_scsi_target_recall_test PRIVATE cxx_std_23)
target_include_directories(eiii_scsi_target_recall_test PRIVATE ${EIII_RUNTIME_PROBE_INCLUDES})
target_link_libraries(eiii_scsi_target_recall_test PRIVATE eiii_voice_core eiii_softfloat)
add_test(NAME eiii_scsi_target_recall COMMAND eiii_scsi_target_recall_test)
# B02: real HDD device with synthetic temporary mounted media, 512B sectors.
add_executable(eiii_hdd_target_recall_test
  "tests/hdd_target_recall_tests.cpp"
  $<TARGET_OBJECTS:eiii_runtime_probe>)
target_compile_features(eiii_hdd_target_recall_test PRIVATE cxx_std_23)
target_include_directories(eiii_hdd_target_recall_test PRIVATE ${EIII_RUNTIME_PROBE_INCLUDES})
target_link_libraries(eiii_hdd_target_recall_test PRIVATE eiii_voice_core eiii_softfloat)
add_test(NAME eiii_hdd_target_recall COMMAND eiii_hdd_target_recall_test)
# B02: full ISO source fingerprint and media identity for future CD resume.
# The actual shim image device is linked from the production runtime graph.
add_executable(eiii_cd_image_identity_test
  "tests/cd_image_identity_tests.cpp"
  $<TARGET_OBJECTS:eiii_runtime_probe>)
target_compile_features(eiii_cd_image_identity_test PRIVATE cxx_std_23)
target_include_directories(eiii_cd_image_identity_test PRIVATE ${EIII_RUNTIME_PROBE_INCLUDES})
target_link_libraries(eiii_cd_image_identity_test PRIVATE eiii_voice_core eiii_softfloat)
add_test(NAME eiii_cd_image_identity COMMAND eiii_cd_image_identity_test)
add_executable(eiii_cd_target_recall_test "tests/cd_target_recall_tests.cpp" $<TARGET_OBJECTS:eiii_runtime_probe>)
target_compile_features(eiii_cd_target_recall_test PRIVATE cxx_std_23)
target_include_directories(eiii_cd_target_recall_test PRIVATE ${EIII_RUNTIME_PROBE_INCLUDES})
target_link_libraries(eiii_cd_target_recall_test PRIVATE eiii_voice_core eiii_softfloat)
add_test(NAME eiii_cd_target_recall COMMAND eiii_cd_target_recall_test)
# B02 firmware-dependent E2E: missing licensed owner fixtures is SKIP, never PASS.
add_executable(eiii_b02_firmware_continuation_test
  "tests/b02_firmware_continuation_tests.cpp"
  $<TARGET_OBJECTS:eiii_runtime_probe>)
target_compile_features(eiii_b02_firmware_continuation_test PRIVATE cxx_std_23)
target_include_directories(eiii_b02_firmware_continuation_test PRIVATE ${EIII_RUNTIME_PROBE_INCLUDES})
target_link_libraries(eiii_b02_firmware_continuation_test PRIVATE eiii_voice_core eiii_softfloat)
add_test(NAME eiii_b02_firmware_continuation COMMAND eiii_b02_firmware_continuation_test)
set_tests_properties(eiii_b02_firmware_continuation PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 600)





# B02: exact CEM V1/V2 model, F-Chip and FIR/audio-buffer continuation.
# No private firmware needed; real voice-core library and models are linked.
add_executable(eiii_audio_backend_recall_test "tests/audio_backend_recall_tests.cpp")
target_compile_features(eiii_audio_backend_recall_test PRIVATE cxx_std_23)
target_link_libraries(eiii_audio_backend_recall_test PRIVATE eiii_voice_core)
add_test(NAME eiii_audio_backend_recall COMMAND eiii_audio_backend_recall_test)




target_link_libraries(eiii_wav_firmware_capture_test PRIVATE eiii_voice_core eiii_softfloat)
add_test(NAME eiii_wav_firmware_capture COMMAND eiii_wav_firmware_capture_test)
set_tests_properties(eiii_wav_firmware_capture PROPERTIES TIMEOUT 600 SKIP_RETURN_CODE 77)

# B02 complete serial shifters and lossless native scheduler time transport.
add_executable(eiii_b02_serial_context_test tests/b02_serial_context_tests.cpp)
target_include_directories(eiii_b02_serial_context_test PRIVATE ${EIII_RUNTIME_PROBE_INCLUDES})
add_test(NAME eiii_b02_serial_context COMMAND eiii_b02_serial_context_test)
add_executable(eiii_b02_core_time_test tests/b02_core_time_tests.cpp)
add_test(NAME eiii_b02_core_time COMMAND eiii_b02_core_time_test)
add_executable(eiii_b02_audio_timeline_test tests/b02_audio_timeline_tests.cpp)
target_include_directories(eiii_b02_audio_timeline_test PRIVATE ${EIII_RUNTIME_PROBE_INCLUDES})
target_link_libraries(eiii_b02_audio_timeline_test PRIVATE eiii_voice_core)
add_test(NAME eiii_b02_audio_timeline COMMAND eiii_b02_audio_timeline_test)
add_executable(eiii_b02_cpu_initial_state_test tests/b02_cpu_initial_state_tests.cpp $<TARGET_OBJECTS:eiii_runtime_probe>)
target_include_directories(eiii_b02_cpu_initial_state_test PRIVATE ${EIII_RUNTIME_PROBE_INCLUDES})
target_link_libraries(eiii_b02_cpu_initial_state_test PRIVATE eiii_voice_core eiii_softfloat)
add_test(NAME eiii_b02_cpu_initial_state COMMAND eiii_b02_cpu_initial_state_test)
