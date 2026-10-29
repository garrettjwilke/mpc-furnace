#include "control_surface.hpp"
#include "furnace_bridge.hpp"

#include <algorithm>
#include <ctime>

static int64_t get_now_ms(const struct ui* u) {
    if (u && u->now_ms > 0) return u->now_ms;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
}

ControlSurface& ControlSurface::instance() {
    static ControlSurface inst;
    return inst;
}

ControlSurface::ControlSurface() = default;

int ControlSurface::pad_to_note(int pad_index) const {
    if (state.erase_down) {
        return state.shift_down ? DIV_NOTE_REL : DIV_NOTE_OFF;
    }
    if (state.levels_mode == LevelsMode::Velocity) {
        return state.last_played_note;
    }
    if (state.levels_mode == LevelsMode::ArpInterval) {
        // Semitone offset around last_played_note (-7 .. +8)
        int note = state.last_played_note + (pad_index - 7);
        return std::max(0, std::min(179, note));
    }
    // Chromatic note: C-0 is 60, so C-(base_octave) is 60 + base_octave * 12
    int note = 60 + state.base_octave * 12 + pad_index;
    return std::max(0, std::min(179, note));
}

int ControlSurface::pad_to_velocity(int pad_index, int hw_vel) const {
    if (state.levels_mode == LevelsMode::Velocity) {
        return 8 + (pad_index * (127 - 8)) / 15;
    }
    return (hw_vel > 0) ? hw_vel : 100;
}

void ControlSurface::process_frame(struct ui* u, const struct input* in) {
    if (!in) return;

    auto& fb = FurnaceBridge::instance();
    auto& eng = fb.engine();
    int nch = std::max(1, eng.getTotalChannelCount());

    state.shift_down = (in->shift != 0) || ((in->held & (1u << IN_SHIFT)) != 0);
    state.erase_down = (in->held & (1u << IN_ERASE)) != 0;
    state.note_repeat_down = (in->held & (1u << IN_NOTE_REPEAT)) != 0;

    if (state.cur_chan >= nch) state.cur_chan = nch - 1;
    if (state.cur_chan < 0) state.cur_chan = 0;

    if (eng.curSubSong) {
        if (state.cur_order >= eng.curSubSong->ordersLen)
            state.cur_order = std::max(0, eng.curSubSong->ordersLen - 1);
        if (state.cur_row >= eng.curSubSong->patLen)
            state.cur_row = std::max(0, eng.curSubSong->patLen - 1);
    }

    if (state.modal_lock) return;

    // -------------------------------------------------------------------------
    // 1. Hardware View Navigation
    // -------------------------------------------------------------------------
    AppView old_view = state.active_view;

    if (in->down & (1u << IN_SEQ)) {
        if (state.active_view == AppView::Pattern) {
            state.compact_pattern_view = !state.compact_pattern_view;
        } else {
            state.prev_view = state.active_view;
            state.active_view = AppView::Pattern;
        }
    } else if (in->down & (1u << IN_LOOP)) {
        state.prev_view = state.active_view;
        state.active_view = AppView::Orders;
    } else if (in->down & (1u << IN_PAD_FX)) {
        state.prev_view = state.active_view;
        state.active_view = AppView::Instrument;
    } else if ((in->down & (1u << IN_KNOB_FX)) || (in->down & (1u << IN_MUTE))) {
        state.prev_view = state.active_view;
        state.active_view = AppView::Mixer;
    } else if ((in->down & (1u << IN_SAMPLE)) || (in->down & (1u << IN_CHOP))) {
        state.prev_view = state.active_view;
        state.active_view = AppView::SampleWave;
    } else if (in->down & (1u << IN_SAMPLE_SELECT)) {
        if (state.shift_down) {
            if (state.active_view == AppView::Menu) {
                state.active_view = (state.prev_view == AppView::Menu) ? AppView::Pattern : state.prev_view;
            } else {
                state.prev_view = state.active_view;
                state.active_view = AppView::Menu;
            }
        } else {
            state.prev_view = state.active_view;
            state.active_view = AppView::Browser;
        }
    } else if (in->down & (1u << IN_SAMPLE_REC)) {
        if (state.shift_down) {
            if (state.active_view == AppView::Help) {
                state.active_view = (state.prev_view == AppView::Help) ? AppView::Pattern : state.prev_view;
            } else {
                state.prev_view = state.active_view;
                state.active_view = AppView::Help;
            }
        } else {
            state.prev_view = state.active_view;
            state.active_view = AppView::Browser;
        }
    }

    if (state.active_view != old_view) {
        state.knobs.reset();
        state.cell_edit_locked = false;
    }

    // -------------------------------------------------------------------------
    // 2. Global Transport & Performance Buttons
    // -------------------------------------------------------------------------
    if (in->down & (1u << IN_PLAY)) {
        if (state.shift_down) {
            state.cur_order = 0;
            state.cur_row = 0;
            fb.play(0, 0);
        } else if (!fb.is_playing()) {
            fb.play(state.cur_order, state.cur_row);
        } else {
            // Re-trigger from current order
            fb.play(state.cur_order, 0);
        }
    }

    if (in->down & (1u << IN_STOP)) {
        fb.stop();
        if (state.shift_down) {
            state.cur_row = 0;
        }
    }

    if (in->down & (1u << IN_SEQ_REC)) {
        if (state.shift_down) {
            eng.setMetronome(!eng.getMetronome());
            fb.status_msg = eng.getMetronome() ? "Metronome: ON" : "Metronome: OFF";
        } else {
            state.recording = !state.recording;
            fb.status_msg = state.recording ? "REC Armed (Step/Live)" : "REC Off";
        }
    }

    if (in->down & (1u << IN_PAD_BANK)) {
        if (state.shift_down) {
            int nins = std::max(1, static_cast<int>(eng.song.ins.size()));
            state.cur_ins = (state.cur_ins + 1) % nins;
        } else {
            state.base_octave++;
            if (state.base_octave > 6) state.base_octave = 1;
        }
    }

    if (in->down & (1u << IN_LEVELS)) {
        switch (state.levels_mode) {
            case LevelsMode::Off:
                state.levels_mode = LevelsMode::Velocity;
                fb.status_msg = "16 Levels: Velocity";
                break;
            case LevelsMode::Velocity:
                state.levels_mode = LevelsMode::ArpInterval;
                fb.status_msg = "16 Levels: Pitch Interval";
                break;
            case LevelsMode::ArpInterval:
                state.levels_mode = LevelsMode::Off;
                fb.status_msg = "16 Levels: Chromatic";
                break;
        }
    }

    if ((in->down & (1u << IN_NOTE_REPEAT)) && state.shift_down) {
        state.repeat_div_idx = (state.repeat_div_idx + 1) % 4;
        static const char* kDivNames[4] = {"1/4 Slow", "1/8 Med", "1/16 Fast", "1/32 Turbo"};
        fb.status_msg = std::string("Repeat: ") + kDivNames[state.repeat_div_idx];
    }

    // Tap Tempo
    if (in->down & (1u << IN_TAP_TEMPO)) {
        if (state.shift_down && eng.curSubSong) {
            eng.curSubSong->virtualTempoN = 150;
            eng.curSubSong->virtualTempoD = 150;
            eng.virtualTempoChanged();
            fb.status_msg = "Tempo Reset 150/150";
        } else if (eng.curSubSong) {
            int64_t now = get_now_ms(u);
            if (state.last_tap_ms > 0) {
                int64_t delta = now - state.last_tap_ms;
                if (delta >= 180 && delta <= 2000) {
                    int bpm = static_cast<int>(60000 / delta);
                    eng.curSubSong->virtualTempoN = static_cast<short>(std::max(40, std::min(300, bpm)));
                    eng.curSubSong->virtualTempoD = 150;
                    eng.virtualTempoChanged();
                    fb.status_msg = "Tap Tempo: " + std::to_string(eng.curSubSong->virtualTempoN);
                }
            }
            state.last_tap_ms = now;
        }
    }

    // Fader controls active chip volume
    if (in->fader_moved && eng.song.systemLen > 0) {
        int chip = eng.song.dispatchOfChan[state.cur_chan];
        if (chip >= 0 && chip < eng.song.systemLen) {
            eng.song.systemVol[chip] = static_cast<float>(in->fader) / 100.0f;
        }
    }

    // Track -/+ navigation (when not in Browser/Menu)
    if (state.active_view != AppView::Browser && state.active_view != AppView::Menu) {
        if (in->down & (1u << IN_MINUS)) {
            if (state.shift_down && state.active_view == AppView::Pattern) {
                state.cur_col = (state.cur_col + 4) % 5;
            } else {
                state.cur_chan = (state.cur_chan + nch - 1) % nch;
            }
        }
        if (in->down & (1u << IN_PLUS)) {
            if (state.shift_down && state.active_view == AppView::Pattern) {
                state.cur_col = (state.cur_col + 1) % 5;
            } else {
                state.cur_chan = (state.cur_chan + 1) % nch;
            }
        }
    }

    // -------------------------------------------------------------------------
    // 3. 16 Pads: Chromatic Jamming, Sample Audition, or Channel Mute Matrix
    // -------------------------------------------------------------------------
    if (state.active_view == AppView::Browser || state.active_view == AppView::Menu || state.active_view == AppView::Help) {
        return;
    }

    if (state.active_view == AppView::Mixer) {
        // In Mixer view, Pads 1-16 toggle Channel Mute (or Shift+Pad for Solo)
        for (int p = 0; p < 16; p++) {
            if (in->pad_hit[p] > 0 && p < nch) {
                if (state.shift_down) {
                    eng.toggleSolo(p);
                } else {
                    eng.toggleMute(p);
                }
            }
        }
        return;
    }

    int64_t now_ms = get_now_ms(u);
    static const int kRepeatIntervalsMs[4] = {240, 160, 110, 75};
    int repeat_ms = kRepeatIntervalsMs[state.repeat_div_idx & 3];

    for (int p = 0; p < 16; p++) {
        if (in->pad_hit[p] > 0) {
            int note = pad_to_note(p);
            int vel = pad_to_velocity(p, in->pad_hit[p]);
            if (note >= 0 && note <= 179) {
                state.last_played_note = note;
            }
            if (state.active_view == AppView::SampleWave && !eng.song.sample.empty()) {
                eng.previewSample(state.cur_sample, note);
            } else {
                fb.pad_note_on(p, state.cur_chan, state.cur_ins, note, vel,
                               state.recording, state.cur_order, &state.cur_row, state.edit_step);
            }
            state.last_repeat_ms = now_ms;
        } else if (in->pad_release[p]) {
            if (state.active_view == AppView::SampleWave) {
                eng.stopSamplePreview();
            } else {
                fb.pad_note_off(p, state.cur_chan);
            }
        } else if (state.note_repeat_down && in->pad_down[p]) {
            if (now_ms - state.last_repeat_ms >= repeat_ms) {
                int note = pad_to_note(p);
                int vel = pad_to_velocity(p, in->pad_vel[p]);
                fb.pad_note_on(p, state.cur_chan, state.cur_ins, note, vel,
                               state.recording, state.cur_order, &state.cur_row, state.edit_step);
                state.last_repeat_ms = now_ms;
            }
        }
    }

    if (state.note_repeat_down && u) {
        ui_wake(u, 20);
    }
}

void ControlSurface::update_leds(struct ui* u, const struct input* in, int left_meter, int right_meter) {
    if (!u) return;

    auto& fb = FurnaceBridge::instance();
    auto& eng = fb.engine();
    int nch = std::max(1, eng.getTotalChannelCount());

    // Output level meters
    ui_meter(u, left_meter & 0x1f, right_meter & 0x1f);

    // Active Track A..H LED
    ui_track_led(u, state.cur_chan % 8);

    // Front-panel button LEDs
    ui_button_led(u, IN_PLAY, fb.is_playing() ? 2 : 0);
    ui_button_led(u, IN_SEQ_REC, state.recording ? 2 : 0);
    ui_button_led(u, IN_LEVELS, state.levels_mode != LevelsMode::Off ? 2 : 0);
    ui_button_led(u, IN_NOTE_REPEAT, state.note_repeat_down ? 2 : 0);

    ui_button_led(u, IN_SEQ, state.active_view == AppView::Pattern ? 2 : 1);
    ui_button_led(u, IN_LOOP, state.active_view == AppView::Orders ? 2 : 1);
    ui_button_led(u, IN_PAD_FX, state.active_view == AppView::Instrument ? 2 : 1);
    ui_button_led(u, IN_MUTE, state.active_view == AppView::Mixer ? 2 : 1);
    ui_button_led(u, IN_KNOB_FX, state.active_view == AppView::Mixer ? 2 : 1);
    ui_button_led(u, IN_SAMPLE, state.active_view == AppView::SampleWave ? 2 : 1);
    ui_button_led(u, IN_CHOP, state.active_view == AppView::SampleWave ? 2 : 1);
    ui_button_led(u, IN_SAMPLE_SELECT, state.active_view == AppView::Browser ? 2 : 1);
    ui_button_led(u, IN_SAMPLE_REC, state.active_view == AppView::Help ? 2 : 0);

    // 16 RGB Pads (when not owned by ui_text_editor on MenuView)
    if (state.modal_lock) return;

    if (state.active_view == AppView::Mixer) {
        for (int p = 0; p < 16; p++) {
            if (p >= nch) {
                ui_led(u, p, 0, 0, 0);
            } else if (eng.isChannelMuted(p)) {
                ui_led(u, p, 140, 20, 20); // Muted: Red
            } else if (eng.keyHit[p]) {
                ui_led(u, p, 255, 240, 120); // Active hit: Bright gold
            } else {
                ui_led(u, p, 20, 160, 80); // Unmuted: Green
            }
        }
        return;
    }

    // Octave bank color palette
    static const uint8_t kOctColors[6][3] = {
        {180,  50,  20}, // Oct 1: Deep Orange
        {220, 120,  20}, // Oct 2: Amber
        { 20, 170, 200}, // Oct 3: Cyan
        { 40, 200,  90}, // Oct 4: Emerald
        {170,  50, 210}, // Oct 5: Purple
        {220,  60, 140}, // Oct 6: Magenta
    };
    int oct_idx = std::max(0, std::min(5, state.base_octave - 1));
    uint8_t br = kOctColors[oct_idx][0];
    uint8_t bg = kOctColors[oct_idx][1];
    uint8_t bb = kOctColors[oct_idx][2];

    static const bool kIsBlackKey[12] = {
        false, true, false, true, false, false, true, false, true, false, true, false
    };

    for (int p = 0; p < 16; p++) {
        if (in && in->pad_down[p]) {
            ui_led(u, p, 255, 255, 255);
        } else if ((p % 12) == 0) {
            // Root C notes stand out brightly
            ui_led(u, p, 255, 210, 60);
        } else if (kIsBlackKey[p % 12]) {
            ui_led(u, p, br / 3, bg / 3, bb / 3);
        } else {
            ui_led(u, p, br, bg, bb);
        }
    }
}

