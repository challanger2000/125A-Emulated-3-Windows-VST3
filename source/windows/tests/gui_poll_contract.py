#!/usr/bin/env python3
"""R03: prevent the old full-project/HD-overlay copy in 100ms GUI status polls.

This is a SOURCE regression guard, not a throughput/underrun benchmark.
The packaged VST3 host test independently exercises actual GUI status calls.
"""
from pathlib import Path

source = (Path(__file__).resolve().parents[1] / "vst3-port" / "vst3_port.cpp").read_text(encoding="utf-8")
begin = 'if(std::strcmp(id,"EIII.GUI.Poll")==0) {'
end = 'if(std::strcmp(id,"EIII.GUI.WavCancel")==0)'
if source.count(begin) != 1 or source.count(end) != 1:
    raise SystemExit("FAIL R03: cannot uniquely locate production GUI poll branch")
poll = source.split(begin, 1)[1].split(end, 1)[0]
if len(poll) > 20000:
    raise SystemExit("FAIL R03: GUI polling branch grew unexpectedly; review required")
for forbidden in ("currentProject(", "hd_changes(", "refresh_hd_changes(", "encode_state(", "getState(", "m_hd_mutex"):
    if forbidden in poll:
        raise SystemExit(f"FAIL R03: expensive project/overlay operation in GUI poll: {forbidden}")
for required in ("engine_.snapshot(", "engine_.stats(", "view.analog_model", "sendMessage(response)"):
    if required not in poll:
        raise SystemExit(f"FAIL R03: missing expected lightweight GUI status path: {required}")
media_start = 'void pollMedia()'
media_end = 'eiii::vst3::ProjectState currentProject()'
if source.count(media_start) != 1 or source.count(media_end) != 1:
    raise SystemExit("FAIL R03: cannot uniquely locate pollMedia")
media = source.split(media_start, 1)[1].split(media_end, 1)[0]
for forbidden in ("currentProject(", "hd_changes(", "refresh_hd_changes(", "encode_state("):
    if forbidden in media:
        raise SystemExit(f"FAIL R03: expensive overlay copy introduced via pollMedia: {forbidden}")
print("PASS R03: frequent GUI poll uses bounded status, no serialized project/HD overlay copies")
