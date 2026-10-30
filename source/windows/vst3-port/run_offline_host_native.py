#!/usr/bin/env python3
"""Build/run the real Processor + Steinberg SDK on Linux, without a Windows GUI.
No substitute SDK or emulator implementation. Requires g++, make, ar and an
external complete VST3 SDK. Proprietary ROM/images/state stay outside git.
The Windows target tests the packaged factory instead; this runner does not.
"""
import argparse
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--sdk', type=Path, required=True)
parser.add_argument('--build', type=Path, required=True)
parser.add_argument('--jobs', type=int, default=2)
parser.add_argument('--build-only', action='store_true')
args = parser.parse_args()
repo = Path(__file__).resolve().parents[2]
sdk, build = args.sdk.resolve(), args.build.resolve()
build.mkdir(parents=True, exist_ok=True)
port = repo / 'windows/vst3-port'
flags = ['g++', '-std=c++23', '-O2', '-g', '-ffp-contract=off',
         '-DTHREAD_LOCAL=thread_local', '-DRELEASE=1', '-pthread',
         '-I'+str(sdk), '-I'+str(repo/'upstream'), '-I'+str(port), '-I'+str(build)]
standalone = repo/'upstream/standalone'
flags += ['-I'+str(standalone/p) for p in (
    'shim', 'third_party/mame', 'third_party/mame/src/emu',
    'third_party/mame/src/lib', 'third_party/mame/src/lib/util',
    'third_party/mame/src/devices', 'third_party/mame/src/osd', 'third_party/mame/3rdparty')]

def run(cmd, **kwargs):
    subprocess.run([str(x) for x in cmd], check=True, **kwargs)

def compile_one(source, obj):
    # Include changes matter, so compile all SDK/bridge TUs on every invocation.
    run(flags + ['-c', source, '-o', obj])

# The production Processor has internal linkage in vst3_port.cpp. Compile its
# unchanged source prefix; omit only Controller/factory/Windows editor code.
source = (port/'vst3_port.cpp').read_text()
marker = 'class Controller final :'
assert source.count(marker) == 1
(build/'eiii_offline_processor.inc').write_text(source.split(marker)[0]+'}\n')

core_sources = ('../fchip/eiii_fchip.cpp ../cem/eiii_cem3387.cpp '
                '../cem/eiii_cem_calibration.cpp ../cem/eiii_cem_cv_conditioning.cpp '
                '../cem/eiii_cem3387_v2.cpp ../host/eiii_backend.cpp '
                '../runtime/eiii_runtime.cpp ../runtime/eiii_bank_import.cpp '
                '../runtime/wav_import.cpp ../panel/eiii_lcd.cpp ../panel/eiii_panel_model.cpp')
core = build/'core'
run(['make', '-j'+str(args.jobs), 'BUILD='+str(core),
     'SRC_HOST=shim/emu_shim.cpp eiii_scsi.cpp diserial_shim.cpp',
     'SRC_CORE='+core_sources,
     'CXXFLAGS=-std=c++23 -O2 -g -ffp-contract=off -DTHREAD_LOCAL=thread_local -I../runtime -w',
     'SFFLAGS=-x c++ -std=c++17 -O2 -ffp-contract=off -DTHREAD_LOCAL=thread_local -w',
     core/'libeiiiruntime.a', core/'libsoftfloat3.a'], cwd=repo/'upstream/standalone')

sdk_sources = '''base/source/baseiids.cpp base/source/fbuffer.cpp base/source/fdebug.cpp
base/source/fobject.cpp base/source/fstreamer.cpp base/source/fstring.cpp
base/source/updatehandler.cpp base/thread/source/flock.cpp
pluginterfaces/base/coreiids.cpp pluginterfaces/base/funknown.cpp pluginterfaces/base/ustring.cpp
public.sdk/source/vst/vstinitiids.cpp public.sdk/source/vst/vstaudioeffect.cpp
public.sdk/source/vst/vstbus.cpp public.sdk/source/vst/vstcomponent.cpp
public.sdk/source/vst/vstcomponentbase.cpp public.sdk/source/common/commoniids.cpp
public.sdk/source/common/commonstringconvert.cpp public.sdk/source/vst/utility/stringconvert.cpp
public.sdk/source/vst/hosting/hostclasses.cpp public.sdk/source/vst/hosting/eventlist.cpp
public.sdk/source/vst/hosting/pluginterfacesupport.cpp'''.split()
objects = []
for name in sdk_sources:
    obj = build/(name.replace('/', '_')+'.o')
    compile_one(sdk/name, obj)
    objects.append(obj)
run(['ar', 'rcs', build/'libvst3test.a', *objects])
for name in ('eiii_engine', 'wav_firmware_import'):
    compile_one(repo/'upstream/runtime'/f'{name}.cpp', build/f'{name}.o')
libs = [build/'eiii_engine.o', build/'wav_firmware_import.o',
        core/'libeiiiruntime.a', core/'libsoftfloat3.a']
# Compile factory-facing helpers without the in-process Processor's incidental
# includes. This catches missing direct headers in the packaged Windows host.
run(flags + ['-DEIII_OFFLINE_HOST_FACTORY_COMPILE_ONLY', '-c',
             port/'midi_defaults_host_test.cpp', '-o', build/'midi-factory-preflight.o'])
run(flags + ['-DEIII_OFFLINE_IN_PROCESS', port/'offline_host_test.cpp', *libs,
             build/'libvst3test.a', '-ldl', '-o', build/'offline-host'])
run(flags + ['-DEIII_OFFLINE_IN_PROCESS', port/'midi_defaults_host_test.cpp', *libs,
             build/'libvst3test.a', '-ldl', '-o', build/'midi-defaults-host'])
run(flags + [repo/'windows/tests/offline_engine_tests.cpp', *libs, '-o', build/'offline-engine'])
run(flags + ['-DEIII_OFFLINE_IN_PROCESS', port/'b02_host_test.cpp', *libs,
             build/'libvst3test.a', '-ldl', '-o', build/'b02-host'])
run(flags + ['-DEIII_OFFLINE_IN_PROCESS', port/'wav_host_test.cpp', *libs,
             build/'libvst3test.a', '-ldl', '-o', build/'wav-host'])
if not args.build_only:
    # Hard bounds detect a deadlock; they do not alter production timing.
    run([build/'offline-engine'], timeout=60)
    run([build/'offline-host'], timeout=240)
    if os.environ.get('EIII_REPRO_WAV'):
        run([build/'midi-defaults-host'], timeout=600)
        run([build/'b02-host'], timeout=600)
        run([build/'wav-host'], timeout=600)
