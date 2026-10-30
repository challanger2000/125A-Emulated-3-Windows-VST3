// Windows-only diagnostic front panel for the original EIII emulator.
// The VST3 controller owns views; processor snapshots arrive through IMessage.
#pragma once
#include "public.sdk/source/common/pluginview.h"
#include "runtime/eiii_runtime.h"
#include <cstdint>
#include <string>

struct EiiiUiState
{
    // Original hardware LCD/CGRAM/LED state for the original artwork renderer.
    eiii::panel_snapshot hardware{};
    char lcd[4][21] = {};
    char status[96] = {};
    char media_status[160] = {}; // results of live ROM/HD/CD media changes
    char floppy_name[128] = {};  // D0 display only (basename, read-only)
    std::uint32_t midi_note_ons = 0;
    std::uint32_t last_midi_channel = 0;   // 1-16, zero before first note
    std::uint32_t forwarded_midi_channel = 0; // selected host MIDI routing
    std::int32_t autoload_bank = -1;   // D1 B00..B99 or -1 disabled
    std::int32_t autoload_preset = -1; // P00..P99 or -1 none
    std::uint32_t output_peak_milli = 0;   // 1000 = 0 dBFS, most recent process block
    std::uint32_t analog_model = 2; // 1=CEM3387 V1, 2=CEM3387 V2 (new default)
    std::uint32_t wav_busy = 0, wav_serial = 0;
    // Notification-only serial, not a new VST3 song-state field.
    std::uint64_t project_dirty_revision = 0;
    std::uint32_t running = 0;
    std::uint32_t led_bits = 0;            // active-high lit LED bits
    std::uint32_t seconds = 0;
    // Read-only render diagnostics; never serialized as project state.
    std::int32_t setup_mode = 0, process_mode = -1;
    std::uint32_t engine_starts = 0, latency_frames = 0;
    std::uint64_t rendered_frames = 0, underrun_frames = 0, midi_dropped = 0;
    std::uint64_t midi_events = 0, midi_late_frames = 0;
    std::uint64_t midi_trace_hash = 14695981039346656037ull;
    std::uint64_t realtime_frames = 0, offline_frames = 0, prefetch_frames = 0;
};

class EiiiPanelView;
class EiiiPanelSink
{
public:
    virtual ~EiiiPanelSink() = default;
    virtual void requestPanelState() = 0;
    virtual void panelButton(int index, bool down) = 0;
    virtual bool panelSlider(int index, std::uint16_t adc) = 0; // true only if worker input accepted
    virtual void panelMidiRouting(int targetChannel) = 0;
    virtual void panelAutoRecall(int bank, int preset) = 0;
    virtual void panelWavImport(const char* path, unsigned root, unsigned low, unsigned high, unsigned preset) = 0;
    virtual void panelWavCancel() = 0;
    virtual void panelScsi(int id, int kind, const char* nativePath) = 0;
    virtual void panelAnalogModel(int model) = 0;
    virtual void panelDiscardDiskChanges(int id) = 0;
    virtual void panelExportDisk(int id, const char* nativePath) = 0;
    // action: 1=ROM, 2=D1 HD, 3=D3 CD, 4=D3 eject, 6=D0 insert, 7=D0 eject
    virtual void panelMedia(int action, const char* nativePath) = 0;
    virtual void panelDisplaySettingsChanged(const std::string& settings) = 0;
    virtual void panelZoomChanged(int acceptedZoomPercent) = 0;
    virtual void panelViewClosed(EiiiPanelView* view) = 0;
};

class EiiiPanelView final : public Steinberg::CPluginView
{
public:
    explicit EiiiPanelView(EiiiPanelSink& sink);
    ~EiiiPanelView() override;

    Steinberg::tresult PLUGIN_API isPlatformTypeSupported(Steinberg::FIDString type) override;
    Steinberg::tresult PLUGIN_API attached(void* parent, Steinberg::FIDString type) override;
    Steinberg::tresult PLUGIN_API removed() override;
    Steinberg::tresult PLUGIN_API onSize(Steinberg::ViewRect* rect) override;
    Steinberg::tresult PLUGIN_API canResize() override;
    Steinberg::tresult PLUGIN_API checkSizeConstraint(Steinberg::ViewRect* rect) override;
    void setStatus(const EiiiUiState& status);
    std::string displayState() const;
    void restoreDisplayState(const std::string& settings);
    // Host-driven window resize; original panel artwork and hit zones scale together.
    void requestZoom(int zoomPercent);
    void restoreZoom(int zoomPercent); // host state restore, never a user edit
    int currentZoom() const;
private:
    void requestZoomImpl(int zoomPercent, bool userInitiated);
    struct Impl;
    Impl* ui_ = nullptr;
    EiiiPanelSink& sink_;
};
