#include "furnace_bridge.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// Note required by Furnace's ta-utils.h
void reportError(String what) {
    std::fprintf(stderr, "[Furnace Error] %s\n", what.c_str());
}

static const SystemPreset kPresets[] = {
    {"Sega Genesis (YM2612+PSG)", "Console", DIV_SYSTEM_YM2612, 6, DIV_SYSTEM_SMS, 4},
    {"Genesis Ext Ch3 (YM2612+PSG)", "Console", DIV_SYSTEM_YM2612_EXT, 9, DIV_SYSTEM_SMS, 4},
    {"Nintendo Game Boy", "Handheld", DIV_SYSTEM_GB, 4, DIV_SYSTEM_NULL, 0},
    {"Nintendo NES (2A03)", "Console", DIV_SYSTEM_NES, 5, DIV_SYSTEM_NULL, 0},
    {"NES + Konami VRC6", "Console", DIV_SYSTEM_NES, 5, DIV_SYSTEM_VRC6, 3},
    {"Super Nintendo (SPC700)", "Console", DIV_SYSTEM_SNES, 8, DIV_SYSTEM_NULL, 0},
    {"Commodore 64 (8580 SID)", "Computer", DIV_SYSTEM_C64_8580, 3, DIV_SYSTEM_NULL, 0},
    {"Commodore 64 (6581 SID)", "Computer", DIV_SYSTEM_C64_6581, 3, DIV_SYSTEM_NULL, 0},
    {"PC Engine / TurboGrafx-16", "Console", DIV_SYSTEM_PCE, 6, DIV_SYSTEM_NULL, 0},
    {"Arcade (YM2151 + SegaPCM)", "Arcade", DIV_SYSTEM_YM2151, 8, DIV_SYSTEM_SEGAPCM, 16},
    {"Sound Blaster OPL3 (YMF262)", "Computer", DIV_SYSTEM_OPL3, 18, DIV_SYSTEM_NULL, 0},
    {"Neo Geo (YM2610)", "Arcade", DIV_SYSTEM_YM2610_FULL, 14, DIV_SYSTEM_NULL, 0},
    {"Commodore Amiga (Paula)", "Computer", DIV_SYSTEM_AMIGA, 4, DIV_SYSTEM_NULL, 0},
    {"Atari 2600 (TIA)", "Console", DIV_SYSTEM_TIA, 2, DIV_SYSTEM_NULL, 0},
};

static const int kPresetCount = static_cast<int>(sizeof(kPresets) / sizeof(kPresets[0]));

FurnaceBridge& FurnaceBridge::instance() {
    static FurnaceBridge inst;
    return inst;
}

FurnaceBridge::FurnaceBridge() {
    for (int i = 0; i < 16; i++) {
        pad_active_chan_[i] = -1;
        pad_active_note_[i] = -1;
    }
    std::memset(clipboard_.data, -1, sizeof(clipboard_.data));
}

FurnaceBridge::~FurnaceBridge() {
    shutdown();
}

bool FurnaceBridge::init(int sample_rate) {
    if (initialized_) return true;

    sample_rate_ = (sample_rate > 0) ? sample_rate : 44100;
    out_buf_cap_ = 4096;
    out_buf_[0] = new float[out_buf_cap_];
    out_buf_[1] = new float[out_buf_cap_];
    std::memset(out_buf_[0], 0, out_buf_cap_ * sizeof(float));
    std::memset(out_buf_[1], 0, out_buf_cap_ * sizeof(float));

    eng_.prePreInit();
    eng_.setConf("audioRate", sample_rate_);
    eng_.setConf("audioBufSize", 512);
    eng_.setConf("audioChans", 2);
    eng_.setConf("lowLatency", true);
    eng_.setAudio(DIV_AUDIO_DUMMY);
    eng_.preInit(true);
    if (!eng_.init()) {
        status_msg = "Engine init failed";
        return false;
    }

    initialized_ = true;
    create_new_with_preset(0);
    create_demo_pattern();
    status_msg = "Furnace ready";
    return true;
}

void FurnaceBridge::shutdown() {
    if (!initialized_) return;
    eng_.quit(false);
    delete[] out_buf_[0];
    delete[] out_buf_[1];
    out_buf_[0] = nullptr;
    out_buf_[1] = nullptr;
    initialized_ = false;
}

void FurnaceBridge::audio_fill_cb(void* ctx, int16_t* dst, int frames) {
    auto* self = static_cast<FurnaceBridge*>(ctx);
    if (self) {
        self->render_frames(dst, frames);
    } else if (dst && frames > 0) {
        std::memset(dst, 0, static_cast<size_t>(frames) * 2 * sizeof(int16_t));
    }
}

void FurnaceBridge::render_frames(int16_t* dst, int frames) {
    if (!initialized_ || !dst || frames <= 0) {
        if (dst && frames > 0) {
            std::memset(dst, 0, static_cast<size_t>(frames) * 2 * sizeof(int16_t));
        }
        return;
    }

    if (frames > out_buf_cap_) {
        delete[] out_buf_[0];
        delete[] out_buf_[1];
        out_buf_cap_ = frames + 512;
        out_buf_[0] = new float[out_buf_cap_];
        out_buf_[1] = new float[out_buf_cap_];
    }

    std::memset(out_buf_[0], 0, static_cast<size_t>(frames) * sizeof(float));
    std::memset(out_buf_[1], 0, static_cast<size_t>(frames) * sizeof(float));

    eng_.nextBuf(nullptr, out_buf_, 0, 2, static_cast<unsigned int>(frames));

    for (int i = 0; i < frames; i++) {
        float l = out_buf_[0][i];
        float r = out_buf_[1][i];
        if (l > 1.0f) l = 1.0f;
        if (l < -1.0f) l = -1.0f;
        if (r > 1.0f) r = 1.0f;
        if (r < -1.0f) r = -1.0f;
        dst[i * 2]     = static_cast<int16_t>(l * 32767.0f);
        dst[i * 2 + 1] = static_cast<int16_t>(r * 32767.0f);
    }
}

bool FurnaceBridge::load_song(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        status_msg = "Cannot open file";
        return false;
    }
    if (std::fseek(f, 0, SEEK_END) != 0) {
        std::fclose(f);
        status_msg = "Seek error";
        return false;
    }
    long len = std::ftell(f);
    if (len <= 0) {
        std::fclose(f);
        status_msg = "Empty file";
        return false;
    }
    std::fseek(f, 0, SEEK_SET);
    auto* buf = new unsigned char[len];
    if (std::fread(buf, 1, static_cast<size_t>(len), f) != static_cast<size_t>(len)) {
        delete[] buf;
        std::fclose(f);
        status_msg = "Read error";
        return false;
    }
    std::fclose(f);

    eng_.stop();
    // Note: DivEngine::load() takes ownership of `buf` and deletes it internally
    if (!eng_.load(buf, static_cast<size_t>(len), path.c_str())) {
        status_msg = "Load failed: " + eng_.getLastError();
        return false;
    }

    current_path = path;
    dirty = false;
    status_msg = "Loaded " + (eng_.song.name.empty() ? path : eng_.song.name);
    return true;
}

bool FurnaceBridge::save_fur(const std::string& path) {
    SafeWriter* w = eng_.saveFur();
    if (!w) {
        status_msg = "saveFur failed";
        return false;
    }
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        w->finish();
        delete w;
        status_msg = "Cannot write " + path;
        return false;
    }
    size_t written = std::fwrite(w->getFinalBuf(), 1, w->size(), f);
    std::fclose(f);
    bool ok = (written == w->size());
    w->finish();
    delete w;

    if (ok) {
        current_path = path;
        dirty = false;
        status_msg = "Saved " + path;
    } else {
        status_msg = "Write error";
    }
    return ok;
}

bool FurnaceBridge::export_vgm(const std::string& path) {
    SafeWriter* w = eng_.saveVGM();
    if (!w) {
        status_msg = "VGM export failed";
        return false;
    }
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        w->finish();
        delete w;
        status_msg = "Cannot write VGM";
        return false;
    }
    size_t written = std::fwrite(w->getFinalBuf(), 1, w->size(), f);
    std::fclose(f);
    bool ok = (written == w->size());
    w->finish();
    delete w;
    status_msg = ok ? ("Exported VGM: " + path) : "VGM write error";
    return ok;
}

bool FurnaceBridge::export_wav(const std::string& path, int max_seconds) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        status_msg = "Cannot write WAV";
        return false;
    }

    bool was_playing = is_playing();
    eng_.stop();
    eng_.setOrder(0);
    eng_.setLoops(1);
    eng_.play();

    // Reserve 44-byte RIFF WAVE header
    uint8_t header[44];
    std::memset(header, 0, sizeof(header));
    std::fwrite(header, 1, sizeof(header), f);

    const int chunk_frames = 512;
    int16_t pcm[chunk_frames * 2];
    int total_frames = 0;
    int max_frames = sample_rate_ * std::max(1, max_seconds);

    while (eng_.isPlaying() && total_frames < max_frames) {
        render_frames(pcm, chunk_frames);
        std::fwrite(pcm, sizeof(int16_t), chunk_frames * 2, f);
        total_frames += chunk_frames;
    }

    eng_.stop();
    eng_.setLoops(-1);
    if (was_playing) eng_.play();

    uint32_t data_bytes = static_cast<uint32_t>(total_frames) * 4u;
    uint32_t riff_size = 36u + data_bytes;
    uint32_t rate = static_cast<uint32_t>(sample_rate_);
    uint32_t byte_rate = rate * 4u;

    auto put_u16 = [](uint8_t* p, uint16_t v) {
        p[0] = static_cast<uint8_t>(v & 0xff);
        p[1] = static_cast<uint8_t>((v >> 8) & 0xff);
    };
    auto put_u32 = [](uint8_t* p, uint32_t v) {
        p[0] = static_cast<uint8_t>(v & 0xff);
        p[1] = static_cast<uint8_t>((v >> 8) & 0xff);
        p[2] = static_cast<uint8_t>((v >> 16) & 0xff);
        p[3] = static_cast<uint8_t>((v >> 24) & 0xff);
    };

    std::memcpy(header + 0, "RIFF", 4);
    put_u32(header + 4, riff_size);
    std::memcpy(header + 8, "WAVEfmt ", 8);
    put_u32(header + 16, 16);
    put_u16(header + 20, 1); // PCM
    put_u16(header + 22, 2); // Stereo
    put_u32(header + 24, rate);
    put_u32(header + 28, byte_rate);
    put_u16(header + 32, 4);
    put_u16(header + 34, 16);
    std::memcpy(header + 36, "data", 4);
    put_u32(header + 40, data_bytes);

    std::fseek(f, 0, SEEK_SET);
    std::fwrite(header, 1, sizeof(header), f);
    std::fclose(f);

    status_msg = "Exported WAV: " + path;
    return true;
}

int FurnaceBridge::import_wav_sample(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        status_msg = "Cannot open WAV";
        return -1;
    }
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    if (sz < 44) {
        std::fclose(f);
        status_msg = "Invalid WAV file";
        return -1;
    }
    std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> buf(static_cast<size_t>(sz));
    if (std::fread(buf.data(), 1, buf.size(), f) != buf.size()) {
        std::fclose(f);
        status_msg = "WAV read error";
        return -1;
    }
    std::fclose(f);

    if (std::memcmp(buf.data(), "RIFF", 4) != 0 || std::memcmp(buf.data() + 8, "WAVE", 4) != 0) {
        status_msg = "Not a RIFF WAVE file";
        return -1;
    }

    auto get_u16 = [](const uint8_t* p) -> uint16_t {
        return static_cast<uint16_t>(p[0] | (p[1] << 8));
    };
    auto get_u32 = [](const uint8_t* p) -> uint32_t {
        return static_cast<uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
    };

    uint16_t channels = 1;
    uint32_t sample_rate = 44100;
    uint16_t bits_per_sample = 16;
    const uint8_t* data_ptr = nullptr;
    uint32_t data_size = 0;

    size_t pos = 12;
    while (pos + 8 <= buf.size()) {
        uint32_t chunk_sz = get_u32(buf.data() + pos + 4);
        if (pos + 8 + chunk_sz > buf.size()) break;
        if (std::memcmp(buf.data() + pos, "fmt ", 4) == 0 && chunk_sz >= 16) {
            channels = get_u16(buf.data() + pos + 10);
            sample_rate = get_u32(buf.data() + pos + 12);
            bits_per_sample = get_u16(buf.data() + pos + 22);
        } else if (std::memcmp(buf.data() + pos, "data", 4) == 0) {
            data_ptr = buf.data() + pos + 8;
            data_size = chunk_sz;
            break;
        }
        pos += 8 + chunk_sz + (chunk_sz & 1);
    }

    if (!data_ptr || data_size == 0 || channels == 0) {
        status_msg = "No PCM data in WAV";
        return -1;
    }

    int bytes_per_sample = std::max(1, bits_per_sample / 8);
    int frame_bytes = bytes_per_sample * channels;
    uint32_t frames = data_size / frame_bytes;
    if (frames == 0) return -1;

    auto* s = new DivSample;
    const char* slash = std::strrchr(path.c_str(), '/');
    s->name = slash ? (slash + 1) : path;
    s->legacyRate = static_cast<int>(sample_rate);
    s->centerRate = static_cast<int>(sample_rate);
    s->depth = DIV_SAMPLE_DEPTH_16BIT;
    s->init(frames);

    for (uint32_t i = 0; i < frames; i++) {
        const uint8_t* frame = data_ptr + i * frame_bytes;
        int16_t val = 0;
        if (bits_per_sample == 8) {
            val = static_cast<int16_t>((static_cast<int>(frame[0]) - 128) << 8);
        } else {
            val = static_cast<int16_t>(frame[0] | (frame[1] << 8));
        }
        s->data16[i] = val;
    }

    int idx = eng_.addSamplePtr(s);
    eng_.renderSamplesP();
    dirty = true;
    status_msg = "Imported sample: " + s->name;
    return idx;
}

int FurnaceBridge::load_instrument_file(const std::string& path) {
    auto list = eng_.instrumentFromFile(path.c_str());
    if (list.empty()) {
        status_msg = "Ins load failed: " + eng_.getLastError();
        return -1;
    }
    int last_idx = -1;
    for (DivInstrument* ins : list) {
        last_idx = eng_.addInstrumentPtr(ins);
    }
    eng_.renderSamplesP();
    dirty = true;
    status_msg = "Loaded instrument";
    return last_idx;
}

int FurnaceBridge::get_preset_count() const {
    return kPresetCount;
}

const SystemPreset& FurnaceBridge::get_preset(int index) const {
    if (index < 0 || index >= kPresetCount) return kPresets[0];
    return kPresets[index];
}

void FurnaceBridge::create_new_with_preset(int preset_index) {
    const SystemPreset& p = get_preset(preset_index);

    eng_.stop();
    DivConfig desc;
    desc.set("id0", static_cast<int>(DivEngine::systemToFileFur(p.sys1)));
    desc.set("chans0", p.chans1);
    desc.set("vol0", 1.0f);
    desc.set("pan0", 0.0f);
    if (p.sys2 != DIV_SYSTEM_NULL && p.chans2 > 0) {
        desc.set("id1", static_cast<int>(DivEngine::systemToFileFur(p.sys2)));
        desc.set("chans1", p.chans2);
        desc.set("vol1", 1.0f);
        desc.set("pan1", 0.0f);
    }
    String cfg_str = desc.toString();
    eng_.createNew(cfg_str.c_str(), p.name, false);

    // Create two starter instruments so channels can play right away
    int ins0 = eng_.addInstrument(0);
    if (ins0 >= 0 && ins0 < static_cast<int>(eng_.song.ins.size())) {
        eng_.song.ins[ins0]->name = "Lead Synth";
    }
    if (eng_.getTotalChannelCount() > 1) {
        int ins1 = eng_.addInstrument(eng_.getTotalChannelCount() - 1);
        if (ins1 >= 0 && ins1 < static_cast<int>(eng_.song.ins.size())) {
            eng_.song.ins[ins1]->name = "Bass / Sub";
        }
    }

    eng_.song.name = "New MPC Song";
    eng_.song.author = "MPC Artist";
    current_path.clear();
    dirty = false;
    status_msg = std::string("New: ") + p.name;
}

void FurnaceBridge::create_demo_pattern() {
    if (!eng_.curSubSong || eng_.getTotalChannelCount() <= 0) return;

    // Populate a musical starter groove on Ch0 (FM Bass) and Ch1 (FM Arpeggio)
    static const short bass_notes[16] = {
        84, -1, 84, -1,  96, -1, 84, -1,
        87, -1, 87, -1,  94, -1, 91, -1
    };
    static const short lead_notes[16] = {
        108, 111, 115, 120,  115, 111, 108, 111,
        106, 110, 113, 118,  115, 111, 108, DIV_NOTE_OFF
    };

    for (int step = 0; step < 64; step++) {
        int idx = (step / 2) % 16;
        if ((step % 2) == 0) {
            if (bass_notes[idx] != -1) {
                enter_note(0, 0, step, bass_notes[idx], 0, -1);
            }
            if (eng_.getTotalChannelCount() > 1 && lead_notes[idx] != -1) {
                enter_note(0, 1, step, lead_notes[idx], (eng_.song.insLen > 1) ? 1 : 0, -1);
            }
        }
    }
    dirty = false;
}

void FurnaceBridge::play(int order, int row) {
    if (order >= 0 && eng_.curSubSong && order < eng_.curSubSong->ordersLen) {
        eng_.setOrder(static_cast<unsigned char>(order));
    }
    if (row > 0) {
        eng_.playToRow(row);
    } else {
        eng_.play();
    }
}

void FurnaceBridge::stop() {
    eng_.autoNoteOffAll();
    eng_.stop();
}

bool FurnaceBridge::is_playing() const {
    return const_cast<DivEngine&>(eng_).isPlaying();
}

short FurnaceBridge::get_cell(int order, int chan, int row, int col) const {
    if (!eng_.curSubSong || !eng_.curOrders || !eng_.curPat) return -1;
    int nch = const_cast<DivEngine&>(eng_).getTotalChannelCount();
    if (chan < 0 || chan >= nch) return -1;
    if (order < 0 || order >= eng_.curSubSong->ordersLen) return -1;
    if (row < 0 || row >= eng_.curSubSong->patLen) return -1;
    if (col < 0 || col >= DIV_MAX_COLS) return -1;

    int pat_idx = eng_.curOrders->ord[chan][order];
    DivPattern* pat = eng_.curPat[chan].getPattern(pat_idx, false);
    if (!pat) return -1;
    return pat->newData[row][col];
}

void FurnaceBridge::set_cell(int order, int chan, int row, int col, short val) {
    if (!eng_.curSubSong || !eng_.curOrders || !eng_.curPat) return;
    int nch = eng_.getTotalChannelCount();
    if (chan < 0 || chan >= nch) return;
    if (order < 0 || order >= eng_.curSubSong->ordersLen) return;
    if (row < 0 || row >= eng_.curSubSong->patLen) return;
    if (col < 0 || col >= DIV_MAX_COLS) return;

    eng_.synchronized([&]() {
        int pat_idx = eng_.curOrders->ord[chan][order];
        DivPattern* pat = eng_.curPat[chan].getPattern(pat_idx, true);
        if (pat) {
            pat->newData[row][col] = val;
            dirty = true;
        }
    });
}

void FurnaceBridge::clear_row(int order, int chan, int row) {
    if (!eng_.curSubSong || !eng_.curOrders || !eng_.curPat) return;
    int nch = eng_.getTotalChannelCount();
    if (chan < 0 || chan >= nch) return;
    if (order < 0 || order >= eng_.curSubSong->ordersLen) return;
    if (row < 0 || row >= eng_.curSubSong->patLen) return;

    eng_.synchronized([&]() {
        int pat_idx = eng_.curOrders->ord[chan][order];
        DivPattern* pat = eng_.curPat[chan].getPattern(pat_idx, true);
        if (pat) {
            for (int c = 0; c < DIV_MAX_COLS; c++) {
                pat->newData[row][c] = -1;
            }
            dirty = true;
        }
    });
}

void FurnaceBridge::enter_note(int order, int chan, int row, short note, short ins, short vol) {
    if (!eng_.curSubSong || !eng_.curOrders || !eng_.curPat) return;
    int nch = eng_.getTotalChannelCount();
    if (chan < 0 || chan >= nch) return;
    if (order < 0 || order >= eng_.curSubSong->ordersLen) return;
    if (row < 0 || row >= eng_.curSubSong->patLen) return;

    eng_.synchronized([&]() {
        int pat_idx = eng_.curOrders->ord[chan][order];
        DivPattern* pat = eng_.curPat[chan].getPattern(pat_idx, true);
        if (pat) {
            pat->newData[row][DIV_PAT_NOTE] = note;
            if (note != DIV_NOTE_OFF && note != DIV_NOTE_REL && note != DIV_MACRO_REL && note >= 0) {
                if (ins >= 0) pat->newData[row][DIV_PAT_INS] = ins;
                if (vol >= 0) pat->newData[row][DIV_PAT_VOL] = vol;
            } else {
                pat->newData[row][DIV_PAT_INS] = -1;
            }
            dirty = true;
        }
    });
}

void FurnaceBridge::copy_pattern(int order, int chan) {
    if (!eng_.curSubSong || !eng_.curOrders || !eng_.curPat) return;
    int nch = eng_.getTotalChannelCount();
    if (chan < 0 || chan >= nch || order < 0 || order >= eng_.curSubSong->ordersLen) return;

    int pat_idx = eng_.curOrders->ord[chan][order];
    DivPattern* pat = eng_.curPat[chan].getPattern(pat_idx, false);
    if (!pat) return;

    clipboard_.valid = true;
    clipboard_.rows = eng_.curSubSong->patLen;
    clipboard_.effect_cols = eng_.curPat[chan].effectCols;
    std::memcpy(clipboard_.data, pat->newData, sizeof(clipboard_.data));
    status_msg = "Copied Pattern";
}

void FurnaceBridge::paste_pattern(int order, int chan) {
    if (!clipboard_.valid || !eng_.curSubSong || !eng_.curOrders || !eng_.curPat) return;
    int nch = eng_.getTotalChannelCount();
    if (chan < 0 || chan >= nch || order < 0 || order >= eng_.curSubSong->ordersLen) return;

    eng_.synchronized([&]() {
        int pat_idx = eng_.curOrders->ord[chan][order];
        DivPattern* pat = eng_.curPat[chan].getPattern(pat_idx, true);
        if (pat) {
            std::memcpy(pat->newData, clipboard_.data, sizeof(clipboard_.data));
            dirty = true;
            status_msg = "Pasted Pattern";
        }
    });
}

void FurnaceBridge::clone_pattern(int order, int chan) {
    if (!eng_.curSubSong || !eng_.curOrders || !eng_.curPat) return;
    int nch = eng_.getTotalChannelCount();
    if (chan < 0 || chan >= nch || order < 0 || order >= eng_.curSubSong->ordersLen) return;

    eng_.synchronized([&]() {
        int src_idx = eng_.curOrders->ord[chan][order];
        DivPattern* src = eng_.curPat[chan].getPattern(src_idx, true);
        int free_idx = -1;
        for (int p = 0; p < DIV_MAX_PATTERNS; p++) {
            if (p != src_idx && eng_.curPat[chan].data[p] == nullptr) {
                free_idx = p;
                break;
            }
        }
        if (free_idx >= 0) {
            DivPattern* dst = eng_.curPat[chan].getPattern(free_idx, true);
            src->copyOn(dst);
            eng_.curOrders->ord[chan][order] = static_cast<unsigned char>(free_idx);
            dirty = true;
            status_msg = "Cloned to Pat " + std::to_string(free_idx);
        }
    });
}

void FurnaceBridge::clear_pattern(int order, int chan) {
    if (!eng_.curSubSong || !eng_.curOrders || !eng_.curPat) return;
    int nch = eng_.getTotalChannelCount();
    if (chan < 0 || chan >= nch || order < 0 || order >= eng_.curSubSong->ordersLen) return;

    eng_.synchronized([&]() {
        int pat_idx = eng_.curOrders->ord[chan][order];
        DivPattern* pat = eng_.curPat[chan].getPattern(pat_idx, true);
        if (pat) {
            pat->clear();
            dirty = true;
            status_msg = "Cleared Pattern";
        }
    });
}

void FurnaceBridge::transpose_pattern(int order, int chan, int semitones) {
    if (!eng_.curSubSong || !eng_.curOrders || !eng_.curPat) return;
    int nch = eng_.getTotalChannelCount();
    if (chan < 0 || chan >= nch || order < 0 || order >= eng_.curSubSong->ordersLen) return;

    eng_.synchronized([&]() {
        int pat_idx = eng_.curOrders->ord[chan][order];
        DivPattern* pat = eng_.curPat[chan].getPattern(pat_idx, true);
        if (!pat) return;
        for (int r = 0; r < eng_.curSubSong->patLen; r++) {
            short n = pat->newData[r][DIV_PAT_NOTE];
            if (n >= 0 && n <= 179) {
                int nn = std::max(0, std::min(179, static_cast<int>(n) + semitones));
                pat->newData[r][DIV_PAT_NOTE] = static_cast<short>(nn);
            }
        }
        dirty = true;
        status_msg = (semitones > 0) ? "Transposed +1" : "Transposed -1";
    });
}

void FurnaceBridge::pad_note_on(int pad_idx, int chan, int ins, int note, int vel_127,
                                bool record, int cur_order, int* io_cur_row, int edit_step) {
    int nch = eng_.getTotalChannelCount();
    if (nch <= 0) return;
    if (chan < 0) chan = 0;
    if (chan >= nch) chan = nch - 1;

    if (pad_idx >= 0 && pad_idx < 16) {
        pad_active_chan_[pad_idx] = chan;
        pad_active_note_[pad_idx] = note;
    }

    if (note == DIV_NOTE_OFF || note == DIV_NOTE_REL) {
        eng_.noteOff(chan);
    } else {
        eng_.noteOn(chan, ins, note, vel_127);
    }

    if (record && eng_.curSubSong) {
        int max_vol = std::max(1, eng_.getMaxVolumeChan(chan));
        short mapped_vol = static_cast<short>((vel_127 * max_vol) / 127);
        if (is_playing()) {
            int play_ord = eng_.getOrder();
            int play_row = eng_.getRow();
            enter_note(play_ord, chan, play_row, static_cast<short>(note),
                       static_cast<short>(ins), mapped_vol);
        } else if (io_cur_row) {
            enter_note(cur_order, chan, *io_cur_row, static_cast<short>(note),
                       static_cast<short>(ins), mapped_vol);
            if (edit_step > 0 && eng_.curSubSong->patLen > 0) {
                *io_cur_row = (*io_cur_row + edit_step) % eng_.curSubSong->patLen;
            }
        }
    }
}

void FurnaceBridge::pad_note_off(int pad_idx, int chan) {
    int target_ch = chan;
    if (pad_idx >= 0 && pad_idx < 16 && pad_active_chan_[pad_idx] >= 0) {
        target_ch = pad_active_chan_[pad_idx];
        pad_active_chan_[pad_idx] = -1;
        pad_active_note_[pad_idx] = -1;
    }
    if (target_ch >= 0 && target_ch < eng_.getTotalChannelCount()) {
        if (!is_playing()) {
            eng_.noteOff(target_ch);
        }
    }
}

void FurnaceBridge::panic() {
    eng_.autoNoteOffAll();
    for (int ch = 0; ch < eng_.getTotalChannelCount(); ch++) {
        eng_.noteOff(ch);
    }
    for (int i = 0; i < 16; i++) {
        pad_active_chan_[i] = -1;
        pad_active_note_[i] = -1;
    }
}

void FurnaceBridge::format_note(short note_val, char out[4]) {
    static const char* kNoteNames[12] = {
        "C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"
    };
    if (note_val < 0) {
        std::memcpy(out, "...", 4);
        return;
    }
    if (note_val == DIV_NOTE_OFF) {
        std::memcpy(out, "===", 4);
        return;
    }
    if (note_val == DIV_NOTE_REL) {
        std::memcpy(out, "~~~", 4);
        return;
    }
    if (note_val == DIV_MACRO_REL) {
        std::memcpy(out, "^^^", 4);
        return;
    }
    if (note_val > 179) {
        std::memcpy(out, "???", 4);
        return;
    }
    int semi = note_val % 12;
    int oct = (note_val / 12) - 5;
    char oct_ch = (oct >= 0 && oct <= 9) ? static_cast<char>('0' + oct) : '-';
    out[0] = kNoteNames[semi][0];
    out[1] = kNoteNames[semi][1];
    out[2] = oct_ch;
    out[3] = '\0';
}

