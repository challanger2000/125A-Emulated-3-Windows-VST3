// Original E-mu Emulator III rack panel, ported from upstream ui/panel_view.
// Rendering and hit-testing are the actual Emulated-3 code; only HWND presentation
// and the VST3 IMessage-backed panel_adapter are Windows-specific.
// The emulator and audio engine are unchanged.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <objidl.h>
#include <olectl.h>
#include <gdiplus.h>
#include <commdlg.h>
#include <shlobj.h>
#include "eiii_editor_win.h"
#include "../../upstream/standalone/shim/utf8_file_paths.h"
#include "eiii_zoom_state.h"
#include "panel/eiii_panel_adapter.h"
#include "ui/panel_view.h"
#include <algorithm>
#include <cstring>
#include <cmath>
#include <cwchar>
#include <mutex>
#include <string>

namespace {
// Original panel is rasterised directly at the selected size, not enlarged as a bitmap.
// The fixed-height toolbar belongs to the host wrapper, not the EIII hardware.
constexpr int kDefaultZoom = 130;
constexpr int kToolbarH = 36;
constexpr int kPanelW = 1472;
constexpr int kPanelH = 538;
constexpr int kZoomMin = 100;
constexpr int kZoomMax = 150;
constexpr int kZoomStep = 10;
constexpr int kZoomButtonX = 92;
constexpr int kZoomButtonW = 79;
constexpr int kZoomButtonGap = 7;
constexpr int kMidiX = 665, kMidiW = 135;
constexpr int kMediaX = 817, kMediaW = 122;
constexpr UINT kMediaMenuBase = 45000;
constexpr UINT kFloppyMenuBase = 45500;
constexpr UINT kAnalogMenuBase = 45200;
constexpr UINT kDiscardBase = 45300;
constexpr UINT kExportBase = 45400;
constexpr UINT kScsiMenuBase = 45100; // 3 commands per D4..D7: HDD, CD, eject
constexpr int kRecallX = 952, kRecallW = 182;
constexpr UINT kRecallOff = 47000;
constexpr UINT kRecallBankBase = 47100;
constexpr UINT kRecallPresetOff = 47200;
constexpr UINT kRecallPresetBase = 47300;
constexpr UINT kMidiPopupBase = 44000;
constexpr int rackWidth(int zoom) { return (kPanelW * zoom + 50) / 100; }
constexpr int rackHeight(int zoom) { return (kPanelH * zoom + 50) / 100; }
constexpr int viewHeight(int zoom) { return rackHeight(zoom) + kToolbarH; }
constexpr int kWidth = rackWidth(kDefaultZoom);
constexpr int kHeight = viewHeight(kDefaultZoom);
static_assert(kWidth == 1914 && kHeight == 735, "Unexpected 130 percent geometry");
static_assert(rackWidth(150) == 2208 && viewHeight(150) == 843, "Unexpected 150 percent geometry");
int zoomButtonAt(int x, int y)
{
    if (y < 0 || y >= kToolbarH) return 0;
    for (int zoom = kZoomMin; zoom <= kZoomMax; zoom += kZoomStep) {
        const int bx = kZoomButtonX + (zoom - kZoomMin) / kZoomStep *
                       (kZoomButtonW + kZoomButtonGap);
        if (x >= bx && x < bx + kZoomButtonW) return zoom;
    }
    return 0;
}
constexpr UINT_PTR kTimer = 25;
constexpr wchar_t kWindowClass[] = L"125A_EMULATED3_ORIGINAL_RACK_V1";
// Register the HWND class under the plugin DLL, never the host EXE.
// One registration shared among all live attached editor views.
struct RackWindowClass {
    std::mutex mutex;
    HINSTANCE module = nullptr;
    unsigned users = 0;
    bool retain(WNDPROC proc, HINSTANCE& out) {
        std::lock_guard<std::mutex> guard(mutex);
        if (users == 0) {
            HMODULE owner = nullptr;
            if (!GetModuleHandleExW(
                    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCWSTR>(&kWindowClass), &owner) || !owner)
                return false;
            WNDCLASSEXW wc{};
            wc.cbSize = sizeof(wc);
            wc.lpfnWndProc = proc;
            wc.hInstance = owner;
            wc.lpszClassName = kWindowClass;
            wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
            // Do not accept a stale callback from a previous plugin load.
            if (!RegisterClassExW(&wc))
                return false;
            module = owner;
        }
        ++users;
        out = module;
        return true;
    }
    void release() {
        std::lock_guard<std::mutex> guard(mutex);
        if (!users) return;
        if (--users == 0) {
            if (!UnregisterClassW(kWindowClass, module))
                OutputDebugStringW(L"125A EIII: window class unregistration failed");
            module = nullptr;
        }
    }
};
RackWindowClass& rack_window_class() {
    static RackWindowClass instance;
    return instance;
}
}

struct EiiiPanelView::Impl
{
    explicit Impl(EiiiPanelView& parent)
        : view(parent),
          adapter([this](int index, bool down) { view.sink_.panelButton(index, down); },
                  [this](int index, uint16_t adc) { return view.sink_.panelSlider(index, adc); })
    {
        // Original software renderer builds the exact rack panel and hit-map.
        panel.set_size(kWidth, kHeight - kToolbarH);
        init_floppy_art();
        lastDisplayState=adapter.display_state();
        // D0 only: real floppy door opens its own menu, eject lever ejects D0.
        panel.on_drive_click = [this](bool eject, float, float) {
            if (eject) view.sink_.panelMedia(7, "");
            else if (hwnd) open_floppy_menu(hwnd);
        };
    }

    ~Impl() {
        delete floppyArt;
        if(floppyStream)floppyStream->Release();
        if(gdiplusToken)Gdiplus::GdiplusShutdown(gdiplusToken);
    }
    EiiiPanelView& view;
    eiii::panel_adapter adapter;
    eiii::ui::panel_view panel;
    eiii::ui::canvas frame;
    HWND hwnd = nullptr;
    HINSTANCE classModule = nullptr; // owns one shared registration reference
    HDC backDc = nullptr;
    HBITMAP backBitmap = nullptr;
    HGDIOBJ oldBitmap = nullptr;
    void* dibPixels = nullptr;
    bool fullPaint = true;
    bool mouseCaptured = false;
    eiii::vst3::zoom_resize_contract zoom;
    int pixelWidth = kWidth;
    int pixelHeight = kHeight;
    int pressedZoom = 0;
    bool zoomRejected = false;
    std::string lastDisplayState; // UI-thread-only: compare actual theme/contrast edits
    void notify_display_change() {
        const std::string current=adapter.display_state();
        if(current==lastDisplayState)return;
        lastDisplayState=current;
        view.sink_.panelDisplaySettingsChanged(current);
    }
    EiiiUiState status{};
    ULONG_PTR gdiplusToken = 0;
    IStream* floppyStream = nullptr;
    Gdiplus::Bitmap* floppyArt = nullptr;
    void init_floppy_art() {
        Gdiplus::GdiplusStartupInput si{};
        if(Gdiplus::GdiplusStartup(&gdiplusToken, &si, nullptr)!=Gdiplus::Ok)return;
        HMODULE module=nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(&kWindowClass),&module) || !module)return;
        const HRSRC res=FindResourceW(module,MAKEINTRESOURCEW(104),MAKEINTRESOURCEW(10));
        if(!res)return;
        const DWORD bytes=SizeofResource(module,res);
        const HGLOBAL src=LoadResource(module,res);
        if(!src || !bytes)return;
        const void* p=LockResource(src);
        if(!p)return;
        HGLOBAL copy=GlobalAlloc(GMEM_MOVEABLE,bytes);
        if(!copy)return;
        void* dest=GlobalLock(copy);
        if(!dest){GlobalFree(copy);return;}
        std::memcpy(dest,p,bytes);
        GlobalUnlock(copy);
        if(CreateStreamOnHGlobal(copy,TRUE,&floppyStream)!=S_OK) {
            GlobalFree(copy);return;
        }
        floppyArt=Gdiplus::Bitmap::FromStream(floppyStream);
        if(!floppyArt || floppyArt->GetLastStatus()!=Gdiplus::Ok) {
            delete floppyArt; floppyArt=nullptr;
        }
    }
    void draw_floppy_art(HDC dc, int verticalOffset=0) {
        if(!floppyArt)return;
        const float sx=float(pixelWidth)/float(kPanelW);
        const float sy=float(pixelHeight-kToolbarH)/float(kPanelH);
        const int x=int(std::lround(112.f*sx));
        const int y=int(std::lround(419.f*sy))+verticalOffset;
        const int w=std::max(1,int(std::lround(301.f*sx)));
        const int h=std::max(1,int(std::lround(91.f*sy)));
        // Hide the procedural fallback artwork first: otherwise its orange
        // shutter remains visible above the transparent PNG pixels.
        RECT oldArea{x,y,x+w,y+h};
        HBRUSH base=CreateSolidBrush(RGB(41,40,42));
        FillRect(dc,&oldArea,base);
        DeleteObject(base);
        Gdiplus::Graphics g(dc);
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        // Use the occupied portion; the source PNG has transparent top/bottom.
        // Center the photographed drive within its existing rectangular recess.
        const int insetX=std::max(2,int(std::lround(7.f*sx)));
        const int insetY=std::max(2,int(std::lround(6.f*sy)));
        const int innerW=std::max(1,w-2*insetX);
        const int innerH=std::max(1,h-2*insetY);
        g.DrawImage(floppyArt,Gdiplus::Rect(x+insetX,y+insetY,innerW,innerH),
                    0,147,1422,876,Gdiplus::UnitPixel);
        // A restrained inset shadow anchors the photographed drive bezel to
        // the EIII panel. All shadows are inside the original drive hit region.
        Gdiplus::SolidBrush shadow(Gdiplus::Color(120,0,0,0));
        g.FillRectangle(&shadow,x+insetX,y+insetY,innerW,std::max(2,int(2.f*sy)));
        g.FillRectangle(&shadow,x+insetX,y+insetY,std::max(2,int(2.f*sx)),innerH);
        Gdiplus::SolidBrush lowerShade(Gdiplus::Color(65,0,0,0));
        g.FillRectangle(&lowerShade,x+insetX,y+insetY+innerH-std::max(2,int(3.f*sy)),
                        innerW,std::max(2,int(3.f*sy)));

    }


    void release_mouse(HWND h)
    {
        panel.focus_lost(adapter); // release any held hardware switch
        if (mouseCaptured && GetCapture() == h) ReleaseCapture();
        mouseCaptured = false;
    }

    void initialise_buffer(HWND h)
    {
        HDC dc = GetDC(h);
        if (!dc) return;
        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = pixelWidth;
        bmi.bmiHeader.biHeight = -(pixelHeight - kToolbarH); // top-down original rack canvas
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        backDc = CreateCompatibleDC(dc);
        if (backDc) {
            backBitmap = CreateDIBSection(dc, &bmi, DIB_RGB_COLORS, &dibPixels, nullptr, 0);
            if (backBitmap && dibPixels) oldBitmap = SelectObject(backDc, backBitmap);
        }
        ReleaseDC(h, dc);
    }

    void free_buffer()
    {
        if (backDc) {
            if (oldBitmap) SelectObject(backDc, oldBitmap);
            DeleteDC(backDc);
        }
        if (backBitmap) DeleteObject(backBitmap);
        backDc = nullptr;
        backBitmap = nullptr;
        oldBitmap = nullptr;
        dibPixels = nullptr;
    }

    void draw_toolbar(HDC dc)
    {
        RECT bar{0, 0, pixelWidth, kToolbarH};
        HBRUSH bg = CreateSolidBrush(RGB(30, 33, 39));
        FillRect(dc, &bar, bg);
        DeleteObject(bg);

        HFONT typeface = CreateFontW(-15, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        HGDIOBJ oldFont = typeface ? SelectObject(dc, typeface) : nullptr;
        const int oldMode = SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(221, 224, 229));
        // Compact zoom selector at the far right; all 100–150% sizes remain available.
        RECT zoomBox{pixelWidth-122,5,pixelWidth-8,kToolbarH-5};
        HBRUSH zoomBrush=CreateSolidBrush(RGB(58,61,68));
        HPEN zoomPen=CreatePen(PS_SOLID,1,RGB(111,146,166));
        HGDIOBJ oldZoomBrush=SelectObject(dc,zoomBrush);
        HGDIOBJ oldZoomPen=SelectObject(dc,zoomPen);
        RoundRect(dc,zoomBox.left,zoomBox.top,zoomBox.right,zoomBox.bottom,6,6);
        SelectObject(dc,oldZoomPen);SelectObject(dc,oldZoomBrush);
        DeleteObject(zoomPen);DeleteObject(zoomBrush);
        wchar_t zoomLabel[32]{};
        std::swprintf(zoomLabel,32,L"ZOOM %d%%  v",zoom.selected());
        SetTextColor(dc,RGB(233,237,238));
        DrawTextW(dc,zoomLabel,-1,&zoomBox,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        // The MIDI mapping is a host convenience, not an EIII firmware setting.
        RECT midiButton{kMidiX, 5, kMidiX+kMidiW, kToolbarH-5};
        HBRUSH midiBg=CreateSolidBrush(RGB(52,69,79));
        HBRUSH oldMidi=(HBRUSH)SelectObject(dc,midiBg);
        HPEN midiBorder=CreatePen(PS_SOLID,1,RGB(111,146,166));
        HPEN oldBorder=(HPEN)SelectObject(dc,midiBorder);
        RoundRect(dc,midiButton.left,midiButton.top,midiButton.right,midiButton.bottom,6,6);
        SelectObject(dc,oldBorder);
        SelectObject(dc,oldMidi);
        DeleteObject(midiBorder);
        DeleteObject(midiBg);
        wchar_t midiLabel[64]{};
        if (status.forwarded_midi_channel)
            std::swprintf(midiLabel,64,L"MIDI -> CH %02u  v",status.forwarded_midi_channel);
        else
            std::swprintf(midiLabel,64,L"MIDI -> HOST  v");
        SetTextColor(dc,RGB(239,243,244));
        DrawTextW(dc,midiLabel,-1,&midiButton,DT_CENTER|DT_VCENTER|DT_SINGLELINE);

        RECT mediaButton{kMediaX,5,kMediaX+kMediaW,kToolbarH-5};
        HBRUSH mediaBg=CreateSolidBrush(RGB(58,61,68));
        HGDIOBJ oldBg=SelectObject(dc,mediaBg);
        HPEN mediaPen=CreatePen(PS_SOLID,1,RGB(94,98,106));
        HGDIOBJ oldPen=SelectObject(dc,mediaPen);
        RoundRect(dc,mediaButton.left,mediaButton.top,mediaButton.right,mediaButton.bottom,6,6);
        SelectObject(dc,oldPen);
        SelectObject(dc,oldBg);
        DeleteObject(mediaPen);
        DeleteObject(mediaBg);
        SetTextColor(dc,RGB(233,237,238));
        DrawTextW(dc,L"MEDIA  v",-1,&mediaButton,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        RECT recallButton{kRecallX,5,kRecallX+kRecallW,kToolbarH-5};
        HBRUSH recallBg=CreateSolidBrush(RGB(52,69,79));
        HGDIOBJ oldRecall=SelectObject(dc,recallBg);
        HPEN recallPen=CreatePen(PS_SOLID,1,RGB(111,146,166));
        HGDIOBJ oldRecallPen=SelectObject(dc,recallPen);
        RoundRect(dc,recallButton.left,recallButton.top,recallButton.right,recallButton.bottom,6,6);
        SelectObject(dc,oldRecallPen);
        SelectObject(dc,oldRecall);
        DeleteObject(recallPen);
        DeleteObject(recallBg);
        wchar_t recallLabel[70]{};
        if(status.autoload_bank<0)
            std::swprintf(recallLabel,70,L"RECALL  OFF  v");
        else if(status.autoload_preset<0)
            std::swprintf(recallLabel,70,L"RECALL  B%02d  v",status.autoload_bank);
        else
            std::swprintf(recallLabel,70,L"RECALL  B%02d / P%02d  v",
                          status.autoload_bank,status.autoload_preset);
        SetTextColor(dc,RGB(239,243,244));
        DrawTextW(dc,recallLabel,-1,&recallButton,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        RECT mediaStatusRect{kRecallX+kRecallW+15, 0, pixelWidth-135,kToolbarH};
        if (!zoomRejected && mediaStatusRect.right > mediaStatusRect.left) {
            std::wstring message;
            for (unsigned char c : std::string(status.media_status))
                message += (c>=32 && c<=126) ? wchar_t(c) : L'?';
            SetTextColor(dc,RGB(182,214,194));
            DrawTextW(dc,message.c_str(),-1,&mediaStatusRect,
                      DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        }
        if (zoomRejected) {
            RECT notice{kRecallX+kRecallW+15,0,pixelWidth-135,kToolbarH};
            SetTextColor(dc, RGB(249,175,155));
            DrawTextW(dc, L"HOST: SIZE NOT ACCEPTED", -1, &notice,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
        SetBkMode(dc, oldMode);
        if (oldFont) SelectObject(dc, oldFont);
        if (typeface) DeleteObject(typeface);
    }

    // Windows dialogs provide UTF-16; the entire EIII runtime uses strict
    // UTF-8 path strings and converts to wide Windows I/O at the last mile.
    // Reject malformed UTF-16, never best-fit or substitute '?'.
    std::string native_path(HWND h,const wchar_t* wide)
    {
        if(!wide||!*wide)return {};
        const auto utf8=eiii::shim::utf8_from_path(std::filesystem::path(wide));
        if(!utf8.empty())return utf8;
        MessageBoxW(h,L"The selected path cannot be represented as valid Unicode.",
                    L"125A Vintage Sampler III: File path",MB_OK|MB_ICONWARNING);
        return {};
    }

    std::string choose_image(HWND h,int action)
    {
        wchar_t buffer[4096]{};
        constexpr wchar_t hdFilter[] =
            L"Hard disk images (*.img;*.iso;*.hdf)\0*.img;*.iso;*.hdf\0All files (*.*)\0*.*\0";
        constexpr wchar_t cdFilter[] =
            L"CD images (*.iso;*.bin;*.img)\0*.iso;*.bin;*.img\0Alle Dateien (*.*)\0*.*\0";
        OPENFILENAMEW ofn{};
        ofn.lStructSize=sizeof(ofn);
        ofn.hwndOwner=h;
        ofn.lpstrFilter=action==3?cdFilter:hdFilter;
        ofn.lpstrFile=buffer;
        ofn.nMaxFile=DWORD(sizeof(buffer)/sizeof(buffer[0]));
        ofn.Flags=OFN_EXPLORER|OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
        if(!GetOpenFileNameW(&ofn)) return {};
        return native_path(h,buffer);
    }
    struct WavOptions { unsigned root=60,low=36,high=84,preset=0; };
    static std::wstring eiii_note_name(unsigned midi)
    {
        // Original EIII panel convention: MIDI 60 is C3, not Studio One C4.
        static constexpr const wchar_t* notes[]={
            L"C",L"C#",L"D",L"D#",L"E",L"F",L"F#",L"G",L"G#",L"A",L"A#",L"B"
        };
        wchar_t label[40]{};
        std::swprintf(label,40,L"%ls%d  (MIDI %u)",
                      notes[midi%12],int(midi/12)-2,midi);
        return label;
    }
    static INT_PTR CALLBACK wav_options_proc(HWND h,UINT msg,WPARAM w,LPARAM l)
    {
        auto* options=reinterpret_cast<WavOptions*>(GetWindowLongPtrW(h,DWLP_USER));
        if(msg==WM_INITDIALOG) {
            options=reinterpret_cast<WavOptions*>(l);
            SetWindowLongPtrW(h,DWLP_USER,l);
            CreateWindowW(L"STATIC",L"Keyboard Mapping",WS_CHILD|WS_VISIBLE,
                          18,13,310,22,h,nullptr,nullptr,nullptr);
            constexpr const wchar_t* labels[]={
                L"Root Note (Original Pitch)",
                L"Lowest Playable Note",
                L"Highest Playable Note"
            };
            const unsigned values[]={options->root,options->low,options->high};
            for(int i=0;i<3;++i) {
                const int y=44+i*39;
                CreateWindowW(L"STATIC",labels[i],WS_CHILD|WS_VISIBLE,
                              18,y+4,185,23,h,nullptr,nullptr,nullptr);
                HWND combo=CreateWindowW(L"COMBOBOX",L"",
                    WS_CHILD|WS_VISIBLE|WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,
                    218,y,172,250,h,reinterpret_cast<HMENU>(INT_PTR(100+i)),nullptr,nullptr);
                for(unsigned midi=0;midi<=127;++midi) {
                    const auto label=eiii_note_name(midi);
                    SendMessageW(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));
                }
                SendMessageW(combo,CB_SETCURSEL,values[i],0);
            }
            CreateWindowW(L"STATIC",L"Preset Number (00-99)",WS_CHILD|WS_VISIBLE,
                          18,166,185,23,h,nullptr,nullptr,nullptr);
            CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",
                WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_NUMBER,
                218,163,72,25,h,reinterpret_cast<HMENU>(INT_PTR(103)),nullptr,nullptr);
            SetDlgItemInt(h,103,options->preset,FALSE);
            CreateWindowW(L"STATIC",
                L"Your current bank is backed up on D1 before import. "
                L"Audio is muted during import. Two free bank slots are required.",
                WS_CHILD|WS_VISIBLE|SS_LEFT,18,203,374,54,h,nullptr,nullptr,nullptr);
            CreateWindowW(L"BUTTON",L"Import WAV",
                WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON,
                174,264,128,30,h,reinterpret_cast<HMENU>(IDOK),nullptr,nullptr);
            CreateWindowW(L"BUTTON",L"Cancel",WS_CHILD|WS_VISIBLE|WS_TABSTOP,
                310,264,83,30,h,reinterpret_cast<HMENU>(IDCANCEL),nullptr,nullptr);
            return TRUE;
        }
        if(msg==WM_COMMAND && LOWORD(w)==IDOK && options) {
            unsigned v[4]{};
            for(int i=0;i<3;++i) {
                LRESULT selected=SendDlgItemMessageW(h,100+i,CB_GETCURSEL,0,0);
                if(selected==CB_ERR) return TRUE;
                v[i]=unsigned(selected);
            }
            BOOL valid=FALSE;
            v[3]=GetDlgItemInt(h,103,&valid,FALSE);
            if(!valid || v[1]>v[0] || v[2]<v[0] || v[3]>99) {
                MessageBoxW(h,
                    L"Check mapping: Lowest Note <= Root Note <= Highest Note; "
                    L"Preset must be 00-99.",
                    L"EIII WAV-Import",MB_OK|MB_ICONWARNING);
                return TRUE;
            }
            *options={v[0],v[1],v[2],v[3]};
            EndDialog(h,IDOK);
            return TRUE;
        }
        if((msg==WM_COMMAND && LOWORD(w)==IDCANCEL)||msg==WM_CLOSE) {
            EndDialog(h,IDCANCEL);
            return TRUE;
        }
        return FALSE;
    }
    void open_wav_import(HWND h)
    {
        wchar_t buffer[4096]{};OPENFILENAMEW file{};
        file.lStructSize=sizeof(file);file.hwndOwner=h;
        file.lpstrFilter=L"WAV PCM16/PCM24/Float32 (*.wav)\0*.wav\0";
        file.lpstrFile=buffer;file.nMaxFile=4096;
        file.Flags=OFN_EXPLORER|OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
        if(!GetOpenFileNameW(&file))return;
        const std::string path=native_path(h,buffer);if(path.empty())return;
        alignas(DWORD) WORD data[128]{};
        auto* dialog=reinterpret_cast<DLGTEMPLATE*>(data);
        dialog->style=WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME;
        dialog->cdit=0;dialog->cx=250;dialog->cy=196;
        size_t index=sizeof(DLGTEMPLATE)/sizeof(WORD);data[index++]=0;data[index++]=0;
        for(wchar_t c:std::wstring(L"Import WAV into Emulator III"))data[index++]=WORD(c);
        data[index]=0;
        WavOptions options;
        if(DialogBoxIndirectParamW(GetModuleHandleW(nullptr),dialog,h,wav_options_proc,reinterpret_cast<LPARAM>(&options))==IDOK)
            view.sink_.panelWavImport(path.c_str(),options.root,options.low,options.high,options.preset);
    }

    std::string choose_export_path(HWND h)
    {
        wchar_t buffer[4096]{};
        constexpr wchar_t filter[] = L"Disk image (*.img)\0*.img\0All files (*.*)\0*.*\0";
        OPENFILENAMEW ofn{};
        ofn.lStructSize=sizeof(ofn);
        ofn.hwndOwner=h;
        ofn.lpstrFilter=filter;
        ofn.lpstrFile=buffer;
        ofn.nMaxFile=DWORD(sizeof(buffer)/sizeof(buffer[0]));
        ofn.lpstrDefExt=L"img";
        ofn.Flags=OFN_EXPLORER|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR|OFN_OVERWRITEPROMPT;
        if(!GetSaveFileNameW(&ofn)) return {};
        return native_path(h,buffer);
    }
    std::string choose_rom_directory(HWND h)
    {
        BROWSEINFOW bi{};
        bi.hwndOwner=h;
        bi.lpszTitle=L"Select the folder with both original EIII boot ROMs";
        bi.ulFlags=BIF_RETURNONLYFSDIRS|BIF_NEWDIALOGSTYLE;
        PIDLIST_ABSOLUTE pid=SHBrowseForFolderW(&bi);
        if(!pid) return {};
        wchar_t buffer[4096]{};
        const bool ok=SHGetPathFromIDListW(pid,buffer)!=FALSE;
        CoTaskMemFree(pid);
        return ok?native_path(h,buffer):std::string{};
    }
    void open_floppy_menu(HWND h)
    {
        HMENU menu=CreatePopupMenu();
        if(!menu)return;
        AppendMenuW(menu,MF_STRING|MF_GRAYED,0,L"D0 - 3.5-inch DD floppy (read-only)");
        if(status.floppy_name[0]){
            std::wstring name;
            const int required=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,
                                                   status.floppy_name,-1,nullptr,0);
            if(required>0){name.resize(size_t(required),L'\0');
                MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,status.floppy_name,-1,name.data(),required);
                name.resize(size_t(required-1));
            }else name=L"Inserted floppy";
            AppendMenuW(menu,MF_STRING|MF_GRAYED,0,name.c_str());
        }else AppendMenuW(menu,MF_STRING|MF_GRAYED,0,L"No floppy inserted");
        AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
        AppendMenuW(menu,MF_STRING,kFloppyMenuBase+1,L"D0: Insert / change floppy...");
        AppendMenuW(menu,MF_STRING|(status.floppy_name[0]?0:MF_GRAYED),
                    kFloppyMenuBase+2,L"D0: Eject floppy");
        POINT pos{};
        GetCursorPos(&pos);
        const UINT selected=TrackPopupMenu(menu,TPM_LEFTALIGN|TPM_TOPALIGN|
                                          TPM_RETURNCMD|TPM_NONOTIFY,pos.x,pos.y,0,h,nullptr);
        DestroyMenu(menu);
        if(selected==kFloppyMenuBase+2){view.sink_.panelMedia(7,"");return;}
        if(selected!=kFloppyMenuBase+1)return;
        wchar_t buffer[4096]{};
        constexpr wchar_t filter[]=L"EIII floppy images (*.img;*.hfe)\0*.img;*.hfe\0Raw IMG (*.img)\0*.img\0HFE v1 (*.hfe)\0*.hfe\0";
        OPENFILENAMEW ofn{};
        ofn.lStructSize=sizeof(ofn);ofn.hwndOwner=h;ofn.lpstrFilter=filter;
        ofn.lpstrFile=buffer;ofn.nMaxFile=4096;
        ofn.Flags=OFN_EXPLORER|OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
        if(!GetOpenFileNameW(&ofn))return;
        const std::string path=native_path(h,buffer);
        if(!path.empty())view.sink_.panelMedia(6,path.c_str());
    }

    void open_media_menu(HWND h)
    {
        HMENU menu=CreatePopupMenu();
        if(!menu) return;
        AppendMenuW(menu,MF_STRING|(status.wav_busy?MF_GRAYED:0),kMediaMenuBase+6,L"Import WAV through EIII ADC...");
        AppendMenuW(menu,MF_STRING|(!status.wav_busy?MF_GRAYED:0),kMediaMenuBase+7,L"Cancel WAV import");
        AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
        AppendMenuW(menu,MF_STRING,kMediaMenuBase+1,L"Select ROM folder... (EIII restarts)");
        AppendMenuW(menu,MF_STRING,kMediaMenuBase+2,L"D1: Select OS hard disk... (EIII restarts)");
        AppendMenuW(menu,MF_STRING,kDiscardBase+1,L"D1: Discard saved disk changes...");
        AppendMenuW(menu,MF_STRING,kExportBase+1,L"D1: Export changed disk as new image...");
        AppendMenuW(menu,MF_STRING,kMediaMenuBase+3,L"D3: Insert / change CD-ROM...");
        AppendMenuW(menu,MF_STRING,kMediaMenuBase+5,L"D3: Import EIIIX / ESI-32 bank disc...");
        AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
        AppendMenuW(menu,MF_STRING,kMediaMenuBase+4,L"D3: Eject CD-ROM");
        AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
        for(int id=4;id<=7;++id) {
            HMENU sub=CreatePopupMenu();
            if(!sub) continue;
            const UINT base=kScsiMenuBase+UINT(id-4)*3;
            AppendMenuW(sub,MF_STRING,base,L"Insert hard disk image...");
            AppendMenuW(sub,MF_STRING,base+1,L"Insert CD-ROM image...");
            AppendMenuW(sub,MF_STRING,base+2,L"Remove drive");
            AppendMenuW(sub,MF_SEPARATOR,0,nullptr);
            AppendMenuW(sub,MF_STRING,kDiscardBase+UINT(id),L"Discard saved disk changes...");
            AppendMenuW(sub,MF_STRING,kExportBase+UINT(id),L"Export changed disk as new image...");
            wchar_t title[16]{};
            std::swprintf(title,16,L"D%d: Extra drive",id);
            AppendMenuW(menu,MF_POPUP,UINT_PTR(sub),title);
        }
        AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
        AppendMenuW(menu,MF_STRING | (status.analog_model==1 ? MF_CHECKED : 0),
                    kAnalogMenuBase+1,L"Analog V1 (MAME matched)");
        AppendMenuW(menu,MF_STRING | (status.analog_model==2 ? MF_CHECKED : 0),
                    kAnalogMenuBase+2,L"Analog V2 (CEM3387 circuit)");
        POINT pos{kMediaX,kToolbarH-2};
        ClientToScreen(h,&pos);
        const UINT selected=TrackPopupMenu(menu,TPM_LEFTALIGN|TPM_TOPALIGN|TPM_RETURNCMD|
                                               TPM_NONOTIFY,pos.x,pos.y,0,h,nullptr);
        DestroyMenu(menu);
        if(selected==kMediaMenuBase+6) {open_wav_import(h);return;}
        if(selected==kMediaMenuBase+7) {view.sink_.panelWavCancel();return;}
        if(selected==kExportBase+1 || (selected>=kExportBase+4 && selected<=kExportBase+7)) {
            const int id=int(selected-kExportBase);
            const std::string file=choose_export_path(h);
            if(!file.empty()) view.sink_.panelExportDisk(id,file.c_str());
            return;
        }
        if(selected==kDiscardBase+1 || (selected>=kDiscardBase+4 && selected<=kDiscardBase+7)) {
            const int id=int(selected-kDiscardBase);
            wchar_t warning[400]{};
            std::swprintf(warning,400,
                L"Discard all project-saved disk changes for D%d?\n\n"
                L"This permanently removes the virtual disk writes in the current project.\n"
                L"The original disk image is never modified.\n\n"
                L"The Emulator III will restart if changes are present.",id);
            if(MessageBoxW(h,warning,L"125A Emulated-3: Discard changes",
                           MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2)==IDYES)
                view.sink_.panelDiscardDiskChanges(id);
            return;
        }
        if(selected==kAnalogMenuBase+1 || selected==kAnalogMenuBase+2) {
            view.sink_.panelAnalogModel(int(selected-kAnalogMenuBase));
            return;
        }
        if(selected>=kScsiMenuBase && selected<kScsiMenuBase+12) {
            const UINT offset=selected-kScsiMenuBase;
            const int id=4+int(offset/3);
            const int command=int(offset%3);
            if(command==2) { view.sink_.panelScsi(id,0,""); return; }
            const int kind=command==0?1:2;
            const std::string file=choose_image(h,kind==1?2:3);
            if(!file.empty()) view.sink_.panelScsi(id,kind,file.c_str());
            return;
        }
        if(selected<kMediaMenuBase+1 || selected>kMediaMenuBase+5) return;
        const int action=int(selected-kMediaMenuBase);
        if(action==4) { view.sink_.panelMedia(4,""); return; }
        const std::string file=action==1?choose_rom_directory(h):choose_image(h,action==5?3:action);
        if(!file.empty()) view.sink_.panelMedia(action,file.c_str());
    }

    void open_recall_menu(HWND h)
    {
        // The hardware's D1 preset loader runs at the NEXT activation;
        // use submenus to avoid an unwieldy 100-entry menu.
        HMENU menu=CreatePopupMenu();
        HMENU banks=CreatePopupMenu(), presets=CreatePopupMenu();
        if(!menu || !banks || !presets) {
            if(menu) DestroyMenu(menu);
            if(banks) DestroyMenu(banks);
            if(presets) DestroyMenu(presets);
            return;
        }
        AppendMenuW(menu,MF_STRING | (status.autoload_bank<0 ? MF_CHECKED : 0),
                    kRecallOff,L"OFF (do not load a bank)");
        AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
        for(int decade=0;decade<10;++decade) {
            HMENU group=CreatePopupMenu();
            for(int i=0;i<10;++i) {
                int value=decade*10+i;
                wchar_t item[30]{};
                std::swprintf(item,30,L"Bank B%02d",value);
                AppendMenuW(group,MF_STRING|(status.autoload_bank==value?MF_CHECKED:0),
                            kRecallBankBase+value,item);
            }
            wchar_t range[25]{};
            std::swprintf(range,25,L"B%02d - B%02d",decade*10,decade*10+9);
            AppendMenuW(banks,MF_POPUP,UINT_PTR(group),range);
        }
        AppendMenuW(menu,MF_POPUP,UINT_PTR(banks),L"D1 bank (at next startup)");
        AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
        AppendMenuW(presets,MF_STRING|(status.autoload_preset<0?MF_CHECKED:0),
                    kRecallPresetOff,L"No preset override");
        for(int decade=0;decade<10;++decade) {
            HMENU group=CreatePopupMenu();
            for(int i=0;i<10;++i) {
                int value=decade*10+i;
                wchar_t item[30]{};
                std::swprintf(item,30,L"Preset P%02d",value);
                AppendMenuW(group,MF_STRING|(status.autoload_preset==value?MF_CHECKED:0),
                            kRecallPresetBase+value,item);
            }
            wchar_t range[25]{};
            std::swprintf(range,25,L"P%02d - P%02d",decade*10,decade*10+9);
            AppendMenuW(presets,MF_POPUP,UINT_PTR(group),range);
        }
        AppendMenuW(menu,MF_POPUP|(status.autoload_bank<0?MF_GRAYED:0),
                    UINT_PTR(presets),L"Preset after loading bank");
        POINT at{kRecallX,kToolbarH-2};
        ClientToScreen(h,&at);
        UINT selected=TrackPopupMenu(menu,TPM_LEFTALIGN|TPM_TOPALIGN|TPM_RETURNCMD|
                                         TPM_NONOTIFY,at.x,at.y,0,h,nullptr);
        DestroyMenu(menu); // also destroys the attached submenus
        if(selected==kRecallOff) view.sink_.panelAutoRecall(-1,-1);
        else if(selected>=kRecallBankBase && selected<kRecallBankBase+100)
            view.sink_.panelAutoRecall(int(selected-kRecallBankBase),status.autoload_preset);
        else if(selected==kRecallPresetOff && status.autoload_bank>=0)
            view.sink_.panelAutoRecall(status.autoload_bank,-1);
        else if(selected>=kRecallPresetBase && selected<kRecallPresetBase+100 &&
                status.autoload_bank>=0)
            view.sink_.panelAutoRecall(status.autoload_bank,int(selected-kRecallPresetBase));
    }

    void open_zoom_menu(HWND h)
    {
        HMENU menu=CreatePopupMenu();
        if(!menu)return;
        for(int z=kZoomMin;z<=kZoomMax;z+=kZoomStep) {
            wchar_t item[24]{};
            std::swprintf(item,24,L"%d%%",z);
            AppendMenuW(menu,MF_STRING|(z==zoom.selected()?MF_CHECKED:0),48000+z,item);
        }
        POINT pt{pixelWidth-122,kToolbarH-2};
        ClientToScreen(h,&pt);
        const UINT id=TrackPopupMenu(menu,TPM_RIGHTALIGN|TPM_TOPALIGN|TPM_RETURNCMD|
                                         TPM_NONOTIFY,pt.x+114,pt.y,0,h,nullptr);
        DestroyMenu(menu);
        if(id>=48100 && id<=48150 && (id-48000)%10==0)
            view.requestZoom(int(id-48000));
    }

    void open_midi_menu(HWND h)
    {
        HMENU menu=CreatePopupMenu();
        if (!menu) return;
        for (unsigned i=0; i<=16; ++i) {
            wchar_t label[70]{};
            if (i==0) std::swprintf(label,70,L"HOST (original channel)");
            else std::swprintf(label,70,L"EIII channel %02u",i);
            const UINT flags=MF_STRING | (status.forwarded_midi_channel==i ? MF_CHECKED : 0);
            AppendMenuW(menu,flags,kMidiPopupBase+i,label);
        }
        POINT at{kMidiX, kToolbarH-2};
        ClientToScreen(h,&at);
        const UINT selection=TrackPopupMenu(menu,TPM_LEFTALIGN|TPM_TOPALIGN|TPM_RETURNCMD|
                                                 TPM_NONOTIFY,at.x,at.y,0,h,nullptr);
        DestroyMenu(menu);
        if (selection>=kMidiPopupBase && selection<=kMidiPopupBase+16)
            view.sink_.panelMidiRouting(int(selection-kMidiPopupBase));
    }

    void apply_size(HWND h, int width, int height)
    {
        if (width < 32 || height <= kToolbarH + 16) return;
        release_mouse(h);
        free_buffer();
        pixelWidth = width;
        pixelHeight = height;
        panel.set_size(pixelWidth, pixelHeight - kToolbarH);
        fullPaint = true;
        initialise_buffer(h);
        InvalidateRect(h, nullptr, FALSE);
    }

    void draw(HDC dc)
    {
        draw_toolbar(dc);
        auto const changed = panel.render(frame, adapter, fullPaint);
        fullPaint = false;
        if (frame.px.empty()) return;
        if (backDc && dibPixels) {
            if (!changed.empty()) {
                std::memcpy(dibPixels, frame.px.data(), frame.px.size() * sizeof(uint32_t));
                // Composite into the same DIB before the only screen blit.
                // Never expose the procedural drive without the PNG overlay.
                draw_floppy_art(backDc);
            }
            BitBlt(dc, 0, kToolbarH, pixelWidth, pixelHeight-kToolbarH, backDc, 0, 0, SRCCOPY);
        } else {
            BITMAPINFO bmi{};
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = pixelWidth;
            bmi.bmiHeader.biHeight = -(pixelHeight-kToolbarH);
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;
            StretchDIBits(dc, 0, kToolbarH, pixelWidth, pixelHeight-kToolbarH,
                          0, 0, pixelWidth, pixelHeight-kToolbarH,
                          frame.px.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);
            draw_floppy_art(dc,kToolbarH);
        }
    }

    LRESULT message(HWND h, UINT u, WPARAM w, LPARAM l)
    {
        switch (u) {
        case WM_CREATE:
            hwnd = h;
            initialise_buffer(h);
            SetTimer(h, kTimer, 100, nullptr);
            view.sink_.requestPanelState();
            return 0;
        case WM_TIMER:
            if (w == kTimer) { view.sink_.requestPanelState(); return 0; }
            break;
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK: {
            const int x = GET_X_LPARAM(l), y = GET_Y_LPARAM(l);
            if (y < kToolbarH) {
                if(x>=pixelWidth-122 && x<pixelWidth-8) {
                    open_zoom_menu(h);
                    return 0;
                }
                if (x>=kMidiX && x<kMidiX+kMidiW) open_midi_menu(h);
                else if(x>=kMediaX && x<kMediaX+kMediaW) open_media_menu(h);
                else if(x>=kRecallX && x<kRecallX+kRecallW) open_recall_menu(h);
                return 0;
            }
            if (status.wav_busy) return 0;
            SetCapture(h);
            mouseCaptured = true;
            panel.mouse_down(float(x), float(y-kToolbarH), 1, adapter);
            notify_display_change();
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        case WM_MOUSEMOVE:
            if (mouseCaptured) {
                panel.mouse_move(float(GET_X_LPARAM(l)), float(GET_Y_LPARAM(l)-kToolbarH), adapter);
                notify_display_change();
            }
            if (mouseCaptured) InvalidateRect(h, nullptr, FALSE);
            return 0;
        case WM_LBUTTONUP: {
            if (pressedZoom) {
                const int requested = pressedZoom;
                pressedZoom = 0;
                const bool clicked = (zoomButtonAt(GET_X_LPARAM(l), GET_Y_LPARAM(l)) == requested);
                if (GetCapture() == h) ReleaseCapture();
                if (clicked) view.requestZoom(requested);
                return 0;
            }
            if (mouseCaptured) {
                panel.mouse_up(float(GET_X_LPARAM(l)), float(GET_Y_LPARAM(l)-kToolbarH), 1, adapter);
                notify_display_change();
            }
            if (mouseCaptured && GetCapture() == h) ReleaseCapture();
            mouseCaptured = false;
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        case WM_MOUSEWHEEL: {
            if (status.wav_busy) return 0;
            POINT pt{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            ScreenToClient(h, &pt);
            if (pt.y >= kToolbarH)
                {
                    panel.wheel(float(pt.x), float(pt.y-kToolbarH),
                                GET_WHEEL_DELTA_WPARAM(w) / WHEEL_DELTA, adapter);
                    notify_display_change();
                }
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        case WM_CAPTURECHANGED:
            pressedZoom = 0;
            if (mouseCaptured) {
                mouseCaptured = false;
                panel.focus_lost(adapter);
                InvalidateRect(h, nullptr, FALSE);
            }
            return 0;
        case WM_KILLFOCUS:
            release_mouse(h);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(h, &ps);
            draw(dc);
            EndPaint(h, &ps);
            return 0;
        }
        case WM_DESTROY:
            KillTimer(h, kTimer);
            release_mouse(h);
            free_buffer();
            return 0;
        case WM_NCDESTROY:
            hwnd = nullptr;
            return 0;
        default: break;
        }
        return DefWindowProcW(h, u, w, l);
    }

    static LRESULT CALLBACK WindowProc(HWND h, UINT u, WPARAM w, LPARAM l)
    {
        Impl* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (u == WM_NCCREATE) {
            self = reinterpret_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        return self ? self->message(h, u, w, l) : DefWindowProcW(h, u, w, l);
    }
};

EiiiPanelView::EiiiPanelView(EiiiPanelSink& sink)
    : CPluginView(nullptr), ui_(new Impl(*this)), sink_(sink)
{
    setRect(Steinberg::ViewRect(0, 0, kWidth, kHeight));
}

EiiiPanelView::~EiiiPanelView()
{
    if (ui_->hwnd) DestroyWindow(ui_->hwnd);
    if (ui_->classModule) {
        ui_->classModule = nullptr;
        rack_window_class().release();
    }
    sink_.panelViewClosed(this);
    delete ui_;
}

Steinberg::tresult PLUGIN_API EiiiPanelView::isPlatformTypeSupported(Steinberg::FIDString type)
{
    return type && std::strcmp(type, Steinberg::kPlatformTypeHWND) == 0 ?
        Steinberg::kResultTrue : Steinberg::kResultFalse;
}

Steinberg::tresult PLUGIN_API EiiiPanelView::attached(void* parent, Steinberg::FIDString type)
{
    if (isPlatformTypeSupported(type) != Steinberg::kResultTrue || !parent || ui_->hwnd)
        return Steinberg::kResultFalse;
    HINSTANCE module = nullptr;
    if (!rack_window_class().retain(Impl::WindowProc, module))
        return Steinberg::kResultFalse;
    ui_->classModule = module;
    Steinberg::ViewRect desired;
    getSize(&desired);
    HWND window = CreateWindowExW(0, kWindowClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                                  0, 0, desired.getWidth(), desired.getHeight(), reinterpret_cast<HWND>(parent),
                                  nullptr, module, ui_);
    if (!window) {
        ui_->classModule = nullptr;
        rack_window_class().release();
        return Steinberg::kResultFalse;
    }
    ui_->apply_size(window, desired.getWidth(), desired.getHeight());
    CPluginView::attached(parent, type);
    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API EiiiPanelView::removed()
{
    if (ui_->hwnd) DestroyWindow(ui_->hwnd);
    if (ui_->classModule) {
        ui_->classModule = nullptr;
        rack_window_class().release();
    }
    return CPluginView::removed();
}

Steinberg::tresult PLUGIN_API EiiiPanelView::onSize(Steinberg::ViewRect* rect)
{
    if (!rect) return Steinberg::kResultFalse;
    int actualZoom=0;
    for (int zoom=kZoomMin; zoom<=kZoomMax; zoom+=kZoomStep)
        if (rect->getWidth()==rackWidth(zoom) && rect->getHeight()==viewHeight(zoom))
            actualZoom=zoom;
    if (!actualZoom) return Steinberg::kResultFalse;
    // A12: persist a newly selected zoom only after exact host onSize().
    // Host-initiated attach/restore must not signal a user change.
    const bool committedUserChange=ui_->zoom.host_sized(actualZoom);
    setRect(*rect);
    if (ui_->hwnd) {
        ui_->apply_size(ui_->hwnd, rect->getWidth(), rect->getHeight());
        MoveWindow(ui_->hwnd, 0, 0, rect->getWidth(), rect->getHeight(), TRUE);
    }
    ui_->zoomRejected=false;
    if(committedUserChange) sink_.panelZoomChanged(actualZoom);
    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API EiiiPanelView::canResize() { return Steinberg::kResultFalse; }

Steinberg::tresult PLUGIN_API EiiiPanelView::checkSizeConstraint(Steinberg::ViewRect* rect)
{
    if (!rect) return Steinberg::kResultFalse;
    // Host-initiated free resizing is disabled. Allow our six exact zoom sizes.
    for (int zoom=kZoomMin; zoom<=kZoomMax; zoom+=kZoomStep) {
        if (rect->getWidth() == rackWidth(zoom) &&
            rect->getHeight() == viewHeight(zoom))
            return Steinberg::kResultTrue;
    }
    rect->right = rect->left + rackWidth(ui_->zoom.selected());
    rect->bottom = rect->top + viewHeight(ui_->zoom.selected());
    return Steinberg::kResultTrue;
}

void EiiiPanelView::requestZoom(int zoom) { requestZoomImpl(zoom,true); }

void EiiiPanelView::requestZoomImpl(int zoom, bool userInitiated)
{
    if (!eiii::vst3::valid_zoom(zoom) || zoom==ui_->zoom.selected()) return;
    if(!plugFrame || !ui_->hwnd) return;
    if(!ui_->zoom.request(zoom,userInitiated)) return;
    Steinberg::ViewRect target(0,0,rackWidth(zoom),viewHeight(zoom));
    // The return value alone is not the host's confirmed geometry.
    if(plugFrame->resizeView(this,&target)!=Steinberg::kResultTrue){
        ui_->zoom.rejected();
        ui_->zoomRejected=true;
        InvalidateRect(ui_->hwnd,nullptr,FALSE);
    }
}

void EiiiPanelView::restoreZoom(int zoom)
{
    if(!eiii::vst3::valid_zoom(zoom))return;
    if (!ui_->hwnd) {
        ui_->zoom.set_initial(zoom);
        setRect(Steinberg::ViewRect(0,0,rackWidth(zoom),viewHeight(zoom)));
    } else requestZoomImpl(zoom,false); // state restore is never user edit
}
int EiiiPanelView::currentZoom() const { return ui_->zoom.selected(); }

std::string EiiiPanelView::displayState() const { return ui_->adapter.display_state(); }
void EiiiPanelView::restoreDisplayState(const std::string& settings)
{
    size_t start=0;
    while(start<settings.size()) {
        const size_t end=settings.find('\n',start);
        const std::string line=settings.substr(start,end==std::string::npos?std::string::npos:end-start);
        ui_->adapter.apply_display_state_line(line.c_str());
        if(end==std::string::npos) break;
        start=end+1;
    }
    // A host-driven controller-state restore is not a user edit.
    ui_->lastDisplayState=ui_->adapter.display_state();
    if(ui_->hwnd) InvalidateRect(ui_->hwnd,nullptr,FALSE);
}
void EiiiPanelView::setStatus(const EiiiUiState& state)
{
    ui_->status = state;
    ui_->adapter.publish_remote(state.hardware);
    if (ui_->hwnd) InvalidateRect(ui_->hwnd, nullptr, FALSE);
}
