#pragma once

#include <cstdint>

extern "C" {
#include "input.h"
#include "ui.h"
}

enum class AppView {
    Pattern,
    Orders,
    Instrument,
    Mixer,
    SampleWave,
    Browser,
    Menu,
    Help
};

enum class LevelsMode {
    Off = 0,
    Velocity = 1,
    ArpInterval = 2
};

struct KnobDeltaTracker {
    int last[3] = {-1, -1, -1};
    int accum[3] = {0, 0, 0};

    void reset() {
        for (int i = 0; i < 3; i++) {
            last[i] = -1;
            accum[i] = 0;
        }
    }

    // Returns stepped delta for knob index k (0..2), ignoring first absolute jump
    int consume(const struct input* in, int k, int counts_per_step = 4) {
        if (!in || k < 0 || k > 2) return 0;
        if (!in->knob_moved[k]) return 0;
        int pos = in->knob[k];
        if (last[k] < 0) {
            last[k] = pos;
            return 0;
        }
        int diff = pos - last[k];
        last[k] = pos;
        accum[k] += diff;
        int step = (counts_per_step > 0) ? counts_per_step : 1;
        int steps = accum[k] / step;
        accum[k] -= steps * step;
        return steps;
    }
};

struct ControlSurfaceState {
    AppView active_view = AppView::Pattern;
    AppView prev_view = AppView::Pattern;

    // Tracker cursor state
    int cur_order = 0;
    int cur_row = 0;
    int cur_chan = 0;
    int cur_col = 0; // 0=Note, 1=Ins, 2=Vol, 3=FX0_Type, 4=FX0_Val
    int cur_ins = 0;
    int cur_sample = 0;
    int cur_wave = 0;

    // Pattern editor settings
    bool compact_pattern_view = false;
    bool cell_edit_locked = false;
    int base_octave = 3; // C-3 (note 96)
    int edit_step = 1;
    int default_vol = -1; // -1 = use channel max

    // Modifiers & Recording
    bool shift_down = false;
    bool erase_down = false;
    bool recording = false;
    bool follow_playhead = true;
    bool modal_lock = false;

    // 16 Levels & Note Repeat
    LevelsMode levels_mode = LevelsMode::Off;
    int last_played_note = 96;
    bool note_repeat_down = false;
    int repeat_div_idx = 1; // 0=240ms, 1=160ms, 2=110ms, 3=75ms
    int64_t last_repeat_ms = 0;
    int64_t last_tap_ms = 0;

    KnobDeltaTracker knobs;
};

class ControlSurface {
public:
    static ControlSurface& instance();

    ControlSurface();

    void process_frame(struct ui* u, const struct input* in);
    void update_leds(struct ui* u, const struct input* in, int left_meter, int right_meter);

    int pad_to_note(int pad_index) const;
    int pad_to_velocity(int pad_index, int hw_vel) const;

    ControlSurfaceState state;
};

