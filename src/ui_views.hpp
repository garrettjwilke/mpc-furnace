#pragma once

#include <string>
#include "control_surface.hpp"

extern "C" {
#include "ui.h"
#include "widgets.h"
#include "filebrowser.h"
#include "usb_sd.h"
}

class TrackerViews {
public:
    TrackerViews();

    void init(const std::string& initial_dir);

    // Unbind the SD card if USB storage is still active. Safe to call on quit.
    void stop_usb();

    std::string get_active_save_dir() const;

    // Returns non-zero if the application should exit
    int draw_frame(struct ui* u, const struct input* in, ControlSurfaceState& cs);

private:
    void draw_header(struct ui* u, const ControlSurfaceState& cs, const char* k1, const char* k2, const char* k3);
    void draw_footer(struct ui* u, const ControlSurfaceState& cs);

    void draw_pattern_view(struct ui* u, const struct input* in, ControlSurfaceState& cs);
    void draw_orders_view(struct ui* u, const struct input* in, ControlSurfaceState& cs);
    void draw_instrument_view(struct ui* u, const struct input* in, ControlSurfaceState& cs);
    void draw_mixer_view(struct ui* u, const struct input* in, ControlSurfaceState& cs);
    void draw_sample_wave_view(struct ui* u, const struct input* in, ControlSurfaceState& cs);
    void draw_browser_view(struct ui* u, const struct input* in, ControlSurfaceState& cs);
    int  draw_menu_view(struct ui* u, const struct input* in, ControlSurfaceState& cs);
    void draw_help_view(struct ui* u, const struct input* in, ControlSurfaceState& cs);

    struct ui_soft_state soft_state_{};

    // Instrument view state
    int ins_sub_tab_ = 0; // 0=Synth/FM, 1=Macros, 2=Ins List
    int fm_selected_op_ = 0; // 0..3
    int fm_param_row_ = 0; // 0=Global(ALG/FB/FMS), 1=TL/AR/DR, 2=D2R/SL/RR, 3=MULT/DT/SSG
    int macro_target_ = 0; // 0=Vol, 1=Arp, 2=Duty, 3=Wave, 4=Pitch
    int macro_cursor_ = 0;

    // Mixer view state
    bool osc_per_channel_ = false;
    int osc_zoom_ = 1;

    // Sample/Wave view state
    bool wave_mode_ = false;
    int wave_cursor_ = 0;
    int wave_preset_idx_ = 0;

    // File browser state
    struct ui_filebrowser fb_state_{};
    bool fb_initialized_ = false;
    std::string browse_dir_ = ".";

    // VGM import wizard state (0=browse, 1=confirm popup, 2=select save folder, 3=enter .fur filename)
    int vgm_import_stage_ = 0;
    struct ui_filebrowser vgm_save_fb_{};
    std::string vgm_import_src_path_;
    std::string vgm_import_dst_dir_;

    // USB SD card access. The toolkit screen owns the frame while this is open.
    bool usb_open_ = false;
    struct ui_usb_sd usb_{};

    // Menu & Text Editor state
    int menu_cursor_ = 0;
    int selected_preset_ = 0;
    bool confirm_quit_ = false;
    bool text_edit_active_ = false;
    int text_edit_target_ = 0; // 0=Save filename, 1=Song title, 2=Author, 3=Ins name
    char text_buf_[128]{};
    struct ui_text_editor text_ed_{};
};

