#pragma once
// A12: validated six-step EIII GUI zoom, controller state EIII-UI-2.
// No audio state, VST3 parameter or class ID changes.
#include <charconv>
#include <string>
#include <string_view>

namespace eiii::vst3 {
constexpr int zoom_default = 130;
inline constexpr bool valid_zoom(int value) noexcept {
    return value >= 100 && value <= 150 && value % 10 == 0;
}
inline bool parse_zoom_line(std::string_view line, int& value) noexcept {
    constexpr std::string_view prefix = "zoom_percent ";
    if (!line.starts_with(prefix)) return false;
    line.remove_prefix(prefix.size());
    if (line.empty()) return false;
    int parsed = 0;
    const auto result = std::from_chars(line.data(), line.data()+line.size(),parsed);
    if (result.ec != std::errc{} || result.ptr != line.data()+line.size() ||
        !valid_zoom(parsed)) return false;
    value=parsed;
    return true;
}

// The host confirms geometry via onSize(), not merely by returning success
// from IPlugFrame::resizeView. Failure and stale onSize acknowledgements must
// never persist an unaccepted zoom.
class zoom_resize_contract {
public:
    int selected() const noexcept { return selected_; }
    void set_initial(int zoom) noexcept {
        if (valid_zoom(zoom)) selected_=zoom;
        pending_=0;
        pending_user_=false;
    }
    bool request(int zoom, bool user) noexcept {
        if (!valid_zoom(zoom) || zoom==selected_) return false;
        pending_=zoom; pending_user_=user;
        return true;
    }
    void rejected() noexcept { pending_=0;pending_user_=false; }
    // Returns true only for a user-selected exact size accepted by onSize.
    bool host_sized(int actual) noexcept {
        if (!valid_zoom(actual)) return false;
        const bool notify = pending_==actual && pending_user_ && actual!=selected_;
        selected_=actual;
        pending_=0;pending_user_=false;
        return notify;
    }
private:
    int selected_=zoom_default;
    int pending_=0;
    bool pending_user_=false;
};
}
