#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "sndfile_stub.h"
#include "engine/engine.h"

struct SystemPreset {
    const char* name;
    const char* category;
    DivSystem sys1;
    int chans1;
    DivSystem sys2;
    int chans2;
};

struct PatternClipboard {
    bool valid = false;
    int rows = 64;
    int effect_cols = 1;
    short data[DIV_MAX_ROWS][DIV_MAX_COLS];
};

class FurnaceBridge {
public:
    static FurnaceBridge& instance();

    FurnaceBridge();
    ~FurnaceBridge();

    bool init(int sample_rate = 44100);
    void shutdown();

    DivEngine& engine() { return eng_; }
    const DivEngine& engine() const { return eng_; }

    // Real-time audio rendering (called from ALSA thread or host test)
    static void audio_fill_cb(void* ctx, int16_t* dst, int frames);
    void render_frames(int16_t* dst, int frames);

    // Song file operations
    bool load_song(const std::string& path);
    bool save_fur(const std::string& path);
    bool export_vgm(const std::string& path);
    bool export_wav(const std::string& path, int max_seconds = 60);

    // Sample & Instrument file operations
    int import_wav_sample(const std::string& path);
    int load_instrument_file(const std::string& path);

    // System presets & New song creation
    int get_preset_count() const;
    const SystemPreset& get_preset(int index) const;
    void create_new_with_preset(int preset_index);
    void create_demo_pattern();

    // Transport
    void play(int order = -1, int row = 0);
    void stop();
    bool is_playing() const; // true only when pattern playback is running (!freelance)

    // Pattern cell editing
    short get_cell(int order, int chan, int row, int col) const;
    void set_cell(int order, int chan, int row, int col, short val);
    void clear_row(int order, int chan, int row);
    void enter_note(int order, int chan, int row, short note, short ins, short vol);

    // Pattern block operations
    void copy_pattern(int order, int chan);
    void paste_pattern(int order, int chan);
    void clone_pattern(int order, int chan);
    void clear_pattern(int order, int chan);
    void transpose_pattern(int order, int chan, int semitones);

    // Live pad jamming & step/live recording
    void pad_note_on(int pad_idx, int chan, int ins, int note, int vel_127,
                     bool record, int cur_order, int* io_cur_row, int edit_step);
    void pad_note_off(int pad_idx, int chan);
    void panic();

    // Formatting helpers
    static void format_note(short note_val, char out[4]);

    bool dirty = false;
    std::string current_path;
    std::string status_msg = "Ready";

private:
    DivEngine eng_;
    bool initialized_ = false;
    int sample_rate_ = 44100;
    float* out_buf_[2] = {nullptr, nullptr};
    int out_buf_cap_ = 0;

    int pad_active_chan_[16];
    int pad_active_note_[16];
    PatternClipboard clipboard_;
};
