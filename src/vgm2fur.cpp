#include "vgm2fur.hpp"
#include "filebrowser.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <tuple>
#include <utility>
#include <vector>
#include <zlib.h>

namespace vgm2fur {
namespace {

constexpr int kSampleRate = 44100;
constexpr uint16_t kTargetFurnaceVersion = 228; // Furnace v0.6.8.1

// Note constants matching vgm2fur/furnace/notes.py
constexpr int kNoteB_1 = 59;
constexpr int kNoteC0  = 60;
constexpr int kNoteCs0 = 61;
constexpr int kNoteD0  = 62;
constexpr int kNoteA0  = 69;
constexpr int kNoteC4  = 108;
constexpr int kNoteC8  = 156;
constexpr int kNoteB9  = 179;
constexpr int kNoteOff = 180;

// =============================================================================
// Little-endian binary readers & writers
// =============================================================================
uint16_t read_u16_le(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}

uint32_t read_u32_le(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

void put_u8(std::vector<uint8_t>& out, uint8_t v) {
    out.push_back(v);
}

void put_u16_le(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

void put_u32_le(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

void put_u64_le(std::vector<uint8_t>& out, uint64_t v) {
    put_u32_le(out, static_cast<uint32_t>(v & 0xFFFFFFFFu));
    put_u32_le(out, static_cast<uint32_t>((v >> 32) & 0xFFFFFFFFu));
}

void put_f32_le(std::vector<uint8_t>& out, float v) {
    uint32_t bits = 0;
    std::memcpy(&bits, &v, sizeof(float));
    put_u32_le(out, bits);
}

void put_cstr(std::vector<uint8_t>& out, const std::string& s) {
    out.insert(out.end(), s.begin(), s.end());
    out.push_back(0);
}

void put_bytes(std::vector<uint8_t>& out, const uint8_t* data, size_t len) {
    out.insert(out.end(), data, data + len);
}

void put_bytes(std::vector<uint8_t>& out, const std::vector<uint8_t>& v) {
    out.insert(out.end(), v.begin(), v.end());
}

void write_u32_at(std::vector<uint8_t>& out, size_t offset, uint32_t v) {
    out[offset]     = static_cast<uint8_t>(v & 0xFF);
    out[offset + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    out[offset + 2] = static_cast<uint8_t>((v >> 16) & 0xFF);
    out[offset + 3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

// =============================================================================
// Decompression (handles raw VGM, gzip .vgz, or zlib-compressed VGM)
// =============================================================================
bool load_or_decompress_vgm(const uint8_t* src, size_t src_len,
                            std::vector<uint8_t>& out_raw,
                            std::string* err_msg) {
    if (!src || src_len < 4) {
        if (err_msg) *err_msg = "File is empty or too short";
        return false;
    }

    if (src_len >= 0x40 && std::memcmp(src, "Vgm ", 4) == 0) {
        out_raw.assign(src, src + src_len);
        return true;
    }

    // Attempt gzip / zlib decompression using windowBits = 15 + 32 (auto-detect header)
    z_stream strm{};
    strm.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(src));
    strm.avail_in = static_cast<uInt>(src_len);

    if (inflateInit2(&strm, 15 + 32) != Z_OK) {
        if (err_msg) *err_msg = "Not a VGM file";
        return false;
    }

    out_raw.clear();
    out_raw.reserve(src_len * 4);
    uint8_t chunk[32768];
    int ret = Z_OK;
    constexpr size_t kMaxDecompressedSize = 64 * 1024 * 1024; // 64 MB safety cap

    while (ret == Z_OK) {
        strm.next_out = chunk;
        strm.avail_out = sizeof(chunk);
        ret = inflate(&strm, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END) {
            inflateEnd(&strm);
            if (err_msg) *err_msg = "Not a valid VGM/VGZ file";
            return false;
        }
        size_t have = sizeof(chunk) - strm.avail_out;
        out_raw.insert(out_raw.end(), chunk, chunk + have);
        if (out_raw.size() > kMaxDecompressedSize) {
            inflateEnd(&strm);
            if (err_msg) *err_msg = "Decompressed VGM exceeds size limit";
            return false;
        }
    }
    inflateEnd(&strm);

    if (out_raw.size() < 0x40 || std::memcmp(out_raw.data(), "Vgm ", 4) != 0) {
        if (err_msg) *err_msg = "File is not a VGM file";
        return false;
    }
    return true;
}

bool read_file_bytes(const std::string& path, std::vector<uint8_t>& out_buf, std::string* err_msg) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        if (err_msg) *err_msg = "Cannot open file: " + path;
        return false;
    }
    if (std::fseek(f, 0, SEEK_END) != 0) {
        std::fclose(f);
        if (err_msg) *err_msg = "File seek error";
        return false;
    }
    long sz = std::ftell(f);
    if (sz <= 0) {
        std::fclose(f);
        if (err_msg) *err_msg = "File is empty";
        return false;
    }
    std::fseek(f, 0, SEEK_SET);
    out_buf.resize(static_cast<size_t>(sz));
    if (std::fread(out_buf.data(), 1, out_buf.size(), f) != out_buf.size()) {
        std::fclose(f);
        if (err_msg) *err_msg = "File read error";
        return false;
    }
    std::fclose(f);
    return true;
}

// =============================================================================
// VGM Header & GD3 Inspection (Sega Genesis / Mega Drive check)
// =============================================================================
struct VgmHeaderInfo {
    uint32_t version = 0;
    uint32_t sn76489_clock = 0;
    uint32_t ym2413_clock = 0;
    uint32_t gd3_offset = 0;
    uint32_t total_wait = 0;
    uint32_t playback_rate = 0;
    uint32_t ym2612_clock = 0;
    size_t data_start = 0x40;
    std::string track_name_en;
    std::string game_name_en;
    std::string system_name_en;
    std::string author_en;
};

std::string utf16le_to_ascii_or_utf8(const uint8_t* p, size_t byte_len) {
    std::string out;
    for (size_t i = 0; i + 1 < byte_len; i += 2) {
        uint16_t ch = read_u16_le(p + i);
        if (ch == 0) break;
        if (ch < 0x80) {
            out.push_back(static_cast<char>(ch));
        } else if (ch < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (ch >> 6)));
            out.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xE0 | (ch >> 12)));
            out.push_back(static_cast<char>(0x80 | ((ch >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
        }
    }
    return out;
}

void parse_gd3(const uint8_t* data, size_t size, uint32_t gd3_rel, VgmHeaderInfo& info) {
    if (gd3_rel == 0) return;
    size_t off = 0x14 + static_cast<size_t>(gd3_rel);
    if (off + 12 > size) return;
    if (std::memcmp(data + off, "Gd3 ", 4) != 0) return;
    uint32_t len = read_u32_le(data + off + 8);
    size_t pos = off + 12;
    size_t end = std::min(size, pos + static_cast<size_t>(len));

    std::string fields[11];
    for (int f = 0; f < 11 && pos + 1 < end; f++) {
        size_t start = pos;
        while (pos + 1 < end && read_u16_le(data + pos) != 0) {
            pos += 2;
        }
        fields[f] = utf16le_to_ascii_or_utf8(data + start, pos - start);
        pos += 2; // skip null terminator
    }
    info.track_name_en  = fields[0];
    info.game_name_en   = fields[2];
    info.system_name_en = fields[4];
    info.author_en      = fields[6];
}

bool contains_nocase(const std::string& haystack, const char* needle) {
    std::string h = haystack;
    for (char& c : h) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    std::string n = needle;
    for (char& c : n) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return h.find(n) != std::string::npos;
}

bool parse_and_validate_genesis_vgm(const std::vector<uint8_t>& raw,
                                    VgmHeaderInfo& out_info,
                                    std::string* err_msg) {
    if (raw.size() < 0x40 || std::memcmp(raw.data(), "Vgm ", 4) != 0) {
        if (err_msg) *err_msg = "Not a VGM file";
        return false;
    }

    const uint8_t* d = raw.data();
    out_info.version       = read_u32_le(d + 0x08);
    out_info.sn76489_clock = read_u32_le(d + 0x0C);
    out_info.ym2413_clock  = read_u32_le(d + 0x10);
    out_info.gd3_offset    = read_u32_le(d + 0x14);
    out_info.total_wait    = read_u32_le(d + 0x18);
    out_info.playback_rate = read_u32_le(d + 0x24);

    // Seek VGM data start (matching vgm2fur/vgm/song.py _seek_vgm_data_start)
    uint32_t rel = read_u32_le(d + 0x34);
    if (out_info.version < 0x150 || rel == 0) {
        out_info.data_start = 0x40;
    } else {
        out_info.data_start = 0x34 + static_cast<size_t>(rel);
    }

    if (out_info.data_start > raw.size()) {
        if (err_msg) *err_msg = "Corrupt VGM data offset";
        return false;
    }

    if (out_info.data_start >= 0x30 && raw.size() >= 0x30) {
        out_info.ym2612_clock = read_u32_le(d + 0x2C);
    }

    parse_gd3(d, raw.size(), out_info.gd3_offset, out_info);

    uint32_t opn2_clk = out_info.ym2612_clock & 0x3FFFFFFFu;
    if (opn2_clk == 0 && out_info.version < 0x110 &&
        (out_info.ym2413_clock & 0x3FFFFFFFu) >= 5000000u) {
        opn2_clk = out_info.ym2413_clock & 0x3FFFFFFFu;
    }
    uint32_t psg_clk = out_info.sn76489_clock & 0x3FFFFFFFu;

    if ((out_info.ym2612_clock & 0x40000000u) != 0) {
        if (err_msg) *err_msg = "Dual YM2612 VGM is not supported";
        return false;
    }

    // Check if other non-Genesis chips are present when opn2_clk is 0
    bool gd3_says_genesis = contains_nocase(out_info.system_name_en, "genesis") ||
                            contains_nocase(out_info.system_name_en, "mega drive") ||
                            contains_nocase(out_info.system_name_en, "megadrive");

    bool is_genesis = (opn2_clk != 0) || (psg_clk != 0 && gd3_says_genesis);
    if (!is_genesis) {
        if (err_msg) *err_msg = "Not a Sega Genesis / Mega Drive VGM";
        return false;
    }

    return true;
}

// =============================================================================
// Chip State Emulation (SN76489, YM2612, Sampler)
// =============================================================================
struct TonalChannel {
    int freq = 0;
    int vol = 15;
    bool operator==(const TonalChannel& o) const {
        return freq == o.freq && vol == o.vol;
    }
};

struct NoiseChannel {
    int mode = 0;
    int vol = 15;
    bool operator==(const NoiseChannel& o) const {
        return mode == o.mode && vol == o.vol;
    }
};

struct SN76489State {
    TonalChannel tonal[3];
    NoiseChannel noise;

    bool operator==(const SN76489State& o) const {
        return tonal[0] == o.tonal[0] &&
               tonal[1] == o.tonal[1] &&
               tonal[2] == o.tonal[2] &&
               noise == o.noise;
    }
    bool operator!=(const SN76489State& o) const { return !(*this == o); }
};

class SN76489Chip {
public:
    SN76489State state;

    void update(uint8_t data) {
        int is_action = (data >> 7) & 1;
        int channel   = (data >> 5) & 3;
        int is_volume = (data >> 4) & 1;
        int payload_l = data & 0x0F;
        int payload_h = data & 0x3F;

        if (is_action) {
            if (is_volume) {
                if (channel < 3) state.tonal[channel].vol = payload_l;
                else state.noise.vol = payload_l;
            } else if (channel != 3) {
                freq_latch_ = payload_l;
                last_ch_ = channel;
            } else {
                state.noise.mode = payload_l;
            }
        } else {
            if (last_ch_ >= 0 && last_ch_ < 3) {
                state.tonal[last_ch_].freq = freq_latch_ | (payload_h << 4);
            }
        }
    }

private:
    int last_ch_ = 0;
    int freq_latch_ = 0;
};

struct FreqLatch {
    int state = 0;
    int freq = 0;
    int block = 0;

    bool low(uint8_t data, bool use_latch, int& out_freq, int& out_block) {
        freq = (freq & ~0xFF) | (data & 0xFF);
        if (!use_latch) {
            out_freq = freq;
            out_block = block;
            return true;
        }
        static const int kLowTable[3] = {2, 0, 2};
        state = kLowTable[state];
        if (state == 0) {
            out_freq = freq;
            out_block = block;
            return true;
        }
        return false;
    }

    bool high(uint8_t data, bool use_latch, int& out_freq, int& out_block) {
        freq = (freq & 0xFF) | ((data & 0x07) << 8);
        block = (data >> 3) & 0x07;
        if (!use_latch) {
            out_freq = freq;
            out_block = block;
            return true;
        }
        static const int kHighTable[3] = {1, 1, 0};
        state = kHighTable[state];
        if (state == 0) {
            out_freq = freq;
            out_block = block;
            return true;
        }
        return false;
    }
};

struct YMOperator {
    int mult = 0;
    int dt = 0;
    int tl = 0;
    int ar = 0;
    int rs = 0;
    int dr = 0;
    int am = 0;
    int sr = 0;
    int rr = 0;
    int sl = 0;
    int ssg = 0;
    int ssg_en = 0;
    // Used by Channel 3 operators 1..3 in special mode
    int freq = 0;
    int block = 0;
};

struct YMChannel {
    int keyid = 0;
    int opmask = 0;
    int freq = 0;
    int block = 0;
    int alg = 0;
    int fb = 0;
    int pms = 0;
    int ams = 0;
    int pan = 0;
    int lfo = 0;     // Channel 1
    int lfo_en = 0;  // Channel 1
    int mode = 0;    // Channel 3
    int dac_en = 0;  // Channel 6
    YMOperator operators[4];

    int op_freq(int op_idx) const {
        return (op_idx == 3) ? freq : operators[op_idx].freq;
    }
    int op_block(int op_idx) const {
        return (op_idx == 3) ? block : operators[op_idx].block;
    }
};

struct YM2612State {
    YMChannel channels[6];
    uint8_t regs[0xC0 * 2]{};

    bool operator==(const YM2612State& o) const {
        if (std::memcmp(regs, o.regs, sizeof(regs)) != 0) return false;
        for (int i = 0; i < 6; i++) {
            if (channels[i].keyid != o.channels[i].keyid) return false;
        }
        return true;
    }
    bool operator!=(const YM2612State& o) const { return !(*this == o); }
};

class YM2612Chip {
public:
    YM2612State state;
    bool use_latch = true;

    void update(int port, uint8_t addr, uint8_t data) {
        if (addr < 0xC0) {
            state.regs[port * 0xC0 + addr] = data;
        }

        static const int kOpMap[4] = {0, 2, 1, 3};
        static const int kCh3OpMap[3] = {2, 0, 1};

        auto get_op = [&](int p, uint8_t a) -> YMOperator* {
            int subch = a & 3;
            if (subch == 3) return nullptr;
            int ch = subch + p * 3;
            int op = kOpMap[(a >> 2) & 3];
            return &state.channels[ch].operators[op];
        };

        auto get_ch = [&](int p, uint8_t a) -> int {
            int subch = a & 3;
            if (subch == 3) return -1;
            return subch + p * 3;
        };

        if (port == 0 && addr == 0x22) {
            state.channels[0].lfo = data & 7;
            state.channels[0].lfo_en = (data >> 3) & 1;
        } else if (port == 0 && addr == 0x27) {
            state.channels[2].mode = (data >> 6) & 3;
        } else if (port == 0 && addr == 0x28) {
            int subch = data & 3;
            if (subch == 3) return;
            int ch_idx = subch + 3 * ((data >> 2) & 1);
            int opmask = (data >> 4) & 0x0F;
            if (state.channels[ch_idx].opmask != opmask) {
                state.channels[ch_idx].opmask = opmask;
                state.channels[ch_idx].keyid += 1;
            }
        } else if (port == 0 && addr == 0x2B) {
            state.channels[5].dac_en = (data >> 7) & 1;
        } else if ((addr & 0xF0) == 0x30) {
            if (YMOperator* op = get_op(port, addr)) {
                op->mult = data & 0x0F;
                int dt_mag = (data >> 4) & 3;
                op->dt = (((data >> 6) & 1) == 0) ? dt_mag : -dt_mag;
            }
        } else if ((addr & 0xF0) == 0x40) {
            if (YMOperator* op = get_op(port, addr)) {
                op->tl = data & 0x7F;
            }
        } else if ((addr & 0xF0) == 0x50) {
            if (YMOperator* op = get_op(port, addr)) {
                op->ar = data & 0x1F;
                op->rs = (data >> 6) & 3;
            }
        } else if ((addr & 0xF0) == 0x60) {
            if (YMOperator* op = get_op(port, addr)) {
                op->dr = data & 0x1F;
                op->am = (data >> 7) & 1;
            }
        } else if ((addr & 0xF0) == 0x70) {
            if (YMOperator* op = get_op(port, addr)) {
                op->sr = data & 0x1F;
            }
        } else if ((addr & 0xF0) == 0x80) {
            if (YMOperator* op = get_op(port, addr)) {
                op->rr = data & 0x0F;
                op->sl = (data >> 4) & 0x0F;
            }
        } else if ((addr & 0xF0) == 0x90) {
            if (YMOperator* op = get_op(port, addr)) {
                op->ssg = data & 7;
                op->ssg_en = (data >> 3) & 1;
            }
        } else if ((addr & 0xFC) == 0xA0) {
            int c = get_ch(port, addr);
            if (c >= 0) {
                ch_latch_[c].low(data, use_latch, state.channels[c].freq, state.channels[c].block);
            }
        } else if ((addr & 0xFC) == 0xA4) {
            int c = get_ch(port, addr);
            if (c >= 0) {
                ch_latch_[c].high(data, use_latch, state.channels[c].freq, state.channels[c].block);
            }
        } else if (port == 0 && (addr & 0xFC) == 0xA8) {
            int idx = addr & 3;
            if (idx < 3) {
                int op_idx = kCh3OpMap[idx];
                YMOperator& op = state.channels[2].operators[op_idx];
                ch3_op_latch_[op_idx].low(data, use_latch, op.freq, op.block);
            }
        } else if (port == 0 && (addr & 0xFC) == 0xAC) {
            int idx = addr & 3;
            if (idx < 3) {
                int op_idx = kCh3OpMap[idx];
                YMOperator& op = state.channels[2].operators[op_idx];
                ch3_op_latch_[op_idx].high(data, use_latch, op.freq, op.block);
            }
        } else if ((addr & 0xFC) == 0xB0) {
            int c = get_ch(port, addr);
            if (c >= 0) {
                state.channels[c].alg = data & 7;
                state.channels[c].fb  = (data >> 3) & 7;
            }
        } else if ((addr & 0xFC) == 0xB4) {
            int c = get_ch(port, addr);
            if (c >= 0) {
                state.channels[c].pms = data & 7;
                state.channels[c].ams = (data >> 4) & 3;
                state.channels[c].pan = (data >> 6) & 3;
            }
        }
    }

private:
    FreqLatch ch_latch_[6];
    FreqLatch ch3_op_latch_[3];
};

// Shared cell pool for Sampler._length and Sampler._duration
struct SamplerCell {
    int length = 0;
    int duration = 0;
};

struct SamplerState {
    int keyid = 0;
    int start = 0;
    int cell_idx = 0;

    bool operator==(const SamplerState& o) const {
        return keyid == o.keyid;
    }
    bool operator!=(const SamplerState& o) const { return keyid != o.keyid; }
};

class SamplerChip {
public:
    static constexpr int kSeparationMargin = 512;

    SamplerState state;
    int pause = 0;
    bool idle = true;
    std::vector<SamplerCell>* pool = nullptr;

    explicit SamplerChip(std::vector<SamplerCell>* cell_pool) : pool(cell_pool) {
        pool->push_back({0, 0});
        state.cell_idx = 0;
    }

    int length() const { return (*pool)[state.cell_idx].length; }
    void set_length(int v) { (*pool)[state.cell_idx].length = v; }
    int duration() const { return (*pool)[state.cell_idx].duration; }
    void set_duration(int v) { (*pool)[state.cell_idx].duration = v; }

    void alloc_new_cell() {
        pool->push_back({0, 0});
        state.cell_idx = static_cast<int>(pool->size()) - 1;
    }

    void set(int ptr) {
        if (duration() > 0 || idle) {
            state.keyid += 1;
            state.start = ptr;
            alloc_new_cell();
            pause = 0;
            idle = false;
        } else {
            state.start = ptr;
            set_length(0);
            pause = 0;
        }
    }

    void play() {
        if (length() > 0) {
            if (pause > kSeparationMargin) {
                if (duration() > 0) {
                    state.keyid += 1;
                    state.start += length();
                    alloc_new_cell();
                } else {
                    state.start += length();
                    set_length(0);
                }
            } else {
                set_duration(duration() + pause);
            }
        }
        set_length(length() + 1);
        pause = 0;
    }

    // Support for VGM DAC Stream Control commands (0x92, 0x93, 0x95)
    void start_stream(int ptr, int len, int freq) {
        if (len <= 0 || freq <= 0) return;
        state.keyid += 1;
        state.start = ptr;
        alloc_new_cell();
        set_length(len);
        int dur = static_cast<int>((static_cast<int64_t>(len) * kSampleRate) / std::max(1, freq));
        set_duration(std::max(1, dur));
        pause = 0;
        idle = false;
    }

    void wait(int delta) {
        pause += delta;
    }
};

// =============================================================================
// Tabulation & Interpolation (vgm2fur/transform/tabulate.py)
// =============================================================================
template <typename T>
struct TableEntry {
    int64_t t = 0;
    T chip;
};

struct RawDataBlock {
    uint8_t type = 0;
    std::vector<uint8_t> payload;
};

struct DecodedPlay {
    int keyid = 0;
    int start = 0;
    int length = 0;
    int duration = 0;
    int rate() const {
        return (duration > 0) ? static_cast<int>((static_cast<int64_t>(kSampleRate) * length) / duration) : 8000;
    }
};

bool parse_and_tabulate_vgm(const std::vector<uint8_t>& raw,
                            const VgmHeaderInfo& hdr,
                            bool use_latch,
                            std::vector<TableEntry<YM2612State>>& table_fm,
                            std::vector<TableEntry<SN76489State>>& table_psg,
                            std::vector<TableEntry<SamplerState>>& table_dac,
                            std::vector<SamplerCell>& dac_cells,
                            std::vector<RawDataBlock>& table_data,
                            int64_t& out_accum_wait,
                            std::string* err_msg) {
    YM2612Chip fm;
    fm.use_latch = use_latch;
    SN76489Chip psg;
    SamplerChip dac(&dac_cells);

    // Track data block offsets for 0x95 fast stream calls
    std::vector<std::pair<int, int>> block_spans;
    int current_pcm_bank_offset = 0;
    int stream_freq = 8000;

    int64_t t = 0;
    bool empty = true;

    auto record_wait = [&](int delta_t) {
        if (empty) {
            empty = false;
            table_fm.push_back({t, fm.state});
            table_psg.push_back({t, psg.state});
            table_dac.push_back({t, dac.state});
        } else {
            if (table_fm.back().chip != fm.state) {
                table_fm.push_back({t, fm.state});
            }
            if (table_psg.back().chip != psg.state) {
                table_psg.push_back({t, psg.state});
            }
            if (table_dac.back().chip != dac.state) {
                table_dac.push_back({t, dac.state});
            }
        }
        t += delta_t;
        dac.wait(delta_t);
    };

    size_t pos = hdr.data_start;
    const size_t end = raw.size();
    const uint8_t* d = raw.data();
    bool done = false;

    while (!done && pos < end) {
        uint8_t com = d[pos++];
        if (com == 0x66) {
            done = true;
        } else if (com == 0x52) {
            if (pos + 2 > end) break;
            fm.update(0, d[pos], d[pos + 1]);
            pos += 2;
        } else if (com == 0x53) {
            if (pos + 2 > end) break;
            fm.update(1, d[pos], d[pos + 1]);
            pos += 2;
        } else if (com == 0x50) {
            if (pos + 1 > end) break;
            psg.update(d[pos]);
            pos += 1;
        } else if (com == 0x61) {
            if (pos + 2 > end) break;
            uint16_t w = read_u16_le(d + pos);
            pos += 2;
            record_wait(w);
        } else if (com == 0x62) {
            record_wait(735);
        } else if (com == 0x63) {
            record_wait(882);
        } else if (com >= 0x70 && com <= 0x7F) {
            record_wait((com - 0x70) + 1);
        } else if (com == 0x80) {
            dac.play();
        } else if (com >= 0x81 && com <= 0x8F) {
            dac.play();
            record_wait(com - 0x80);
        } else if (com == 0xE0) {
            if (pos + 4 > end) break;
            uint32_t ptr = read_u32_le(d + pos);
            pos += 4;
            dac.set(static_cast<int>(ptr));
        } else if (com == 0x67) {
            if (pos + 6 > end) {
                if (err_msg) *err_msg = "Truncated VGM data block";
                return false;
            }
            if (d[pos] != 0x66) {
                if (err_msg) *err_msg = "Malformed VGM data block header";
                return false;
            }
            uint8_t btype = d[pos + 1];
            uint32_t blen = read_u32_le(d + pos + 2) & 0x7FFFFFFFu;
            pos += 6;
            if (pos + blen > end) {
                blen = static_cast<uint32_t>(end - pos);
            }
            RawDataBlock blk;
            blk.type = btype;
            blk.payload.assign(d + pos, d + pos + blen);
            if (btype == 0x00) {
                block_spans.push_back({current_pcm_bank_offset, static_cast<int>(blen)});
                current_pcm_bank_offset += static_cast<int>(blen);
            }
            table_data.push_back(std::move(blk));
            pos += blen;
        } else if (com == 0x68) {
            if (pos + 11 > end) break;
            pos += 11;
        } else if (com == 0x90 || com == 0x91) {
            if (pos + 4 > end) break;
            pos += 4;
        } else if (com == 0x92) {
            if (pos + 5 > end) break;
            stream_freq = static_cast<int>(read_u32_le(d + pos + 1));
            pos += 5;
        } else if (com == 0x93) {
            if (pos + 10 > end) break;
            uint32_t start = read_u32_le(d + pos + 1);
            uint32_t length = read_u32_le(d + pos + 6);
            pos += 10;
            dac.start_stream(static_cast<int>(start), static_cast<int>(length), stream_freq);
        } else if (com == 0x94) {
            if (pos + 1 > end) break;
            pos += 1;
        } else if (com == 0x95) {
            if (pos + 4 > end) break;
            uint16_t blk_id = read_u16_le(d + pos + 1);
            pos += 4;
            if (blk_id < block_spans.size()) {
                dac.start_stream(block_spans[blk_id].first, block_spans[blk_id].second, stream_freq);
            }
        } else if ((com >= 0x30 && com <= 0x3F) || com == 0x4F) {
            if (pos + 1 > end) break;
            pos += 1;
        } else if (com >= 0x41 && com <= 0x4E) {
            size_t step = (hdr.version >= 0x160) ? 2 : 1;
            if (pos + step > end) break;
            pos += step;
        } else if (com == 0x40 || (com >= 0x51 && com <= 0x5F) ||
                   (com >= 0xA0 && com <= 0xBF)) {
            if (pos + 2 > end) break;
            pos += 2;
        } else if (com >= 0xC0 && com <= 0xDF) {
            if (pos + 3 > end) break;
            pos += 3;
        } else if (com >= 0xE1 && com <= 0xFF) {
            if (pos + 4 > end) break;
            pos += 4;
        } else {
            // Unknown command: stop parsing rather than desyncing
            break;
        }
    }

    if (table_fm.empty()) {
        table_fm.push_back({0, fm.state});
        table_psg.push_back({0, psg.state});
        table_dac.push_back({0, dac.state});
    }

    out_accum_wait = t;
    return true;
}

template <typename T>
std::vector<T> interpolate_table(const std::vector<TableEntry<T>>& table,
                                 int64_t t_end, double period, double skip) {
    std::vector<T> out;
    if (table.empty() || period <= 0.0) return out;
    size_t i = 0;
    double t = skip;
    while (t < static_cast<double>(t_end)) {
        while (i + 1 < table.size() &&
               !(static_cast<double>(table[i].t) <= t && t < static_cast<double>(table[i + 1].t))) {
            i++;
        }
        out.push_back(table[i].chip);
        t += period;
    }
    return out;
}

// =============================================================================
// Furnace Pattern Entry & Module Builder (vgm2fur/furnace/*)
// =============================================================================
struct FxPair {
    uint8_t type = 0;
    uint8_t val = 0;
};

struct PatternEntry {
    int note = -1; // -1 = None
    int ins  = -1; // -1 = None
    int vol  = -1; // -1 = None
    std::vector<FxPair> fx;

    std::vector<uint8_t> encode() const {
        uint32_t mask = 0;
        int masklen = 1;
        std::vector<uint8_t> payload;

        if (note >= 0) {
            mask |= 1;
            payload.push_back(static_cast<uint8_t>(note));
        }
        if (ins >= 0) {
            mask |= 2;
            payload.push_back(static_cast<uint8_t>(ins));
        }
        if (vol >= 0) {
            mask |= 4;
            payload.push_back(static_cast<uint8_t>(vol));
        }
        if (!fx.empty()) {
            if (fx.size() == 1) {
                mask |= (8 | 16);
                payload.push_back(fx[0].type);
                payload.push_back(fx[0].val);
            } else {
                size_t nfx = std::min<size_t>(8, fx.size());
                if (nfx <= 4) {
                    mask |= (8 | 16 | 32);
                    masklen = 2;
                } else {
                    mask |= (8 | 16 | 32 | 64);
                    masklen = 3;
                }
                uint32_t m = 256 | 512;
                for (size_t i = 0; i < nfx; i++) {
                    payload.push_back(fx[i].type);
                    payload.push_back(fx[i].val);
                    mask |= m;
                    m <<= 2;
                }
            }
        }

        std::vector<uint8_t> res;
        res.reserve(static_cast<size_t>(masklen) + payload.size());
        for (int b = 0; b < masklen; b++) {
            res.push_back(static_cast<uint8_t>((mask >> (b * 8)) & 0xFF));
        }
        res.insert(res.end(), payload.begin(), payload.end());
        return res;
    }

    int fx_count() const {
        return static_cast<int>(std::min<size_t>(8, fx.size()));
    }
};

// =============================================================================
// PSG Transformation (vgm2fur/transform/to_patterns_psg.py)
// =============================================================================
std::pair<int, int> find_best_psg_note(int freq) {
    static const int kPsgFreqsDesc[] = {
        0x3F9, 0x3C0, 0x38A, 0x357, 0x327, 0x2FA, 0x2CF, 0x2A7, 0x281, 0x25D,
        0x23B, 0x21B, 0x1FC, 0x1E0, 0x1C5, 0x1AC, 0x194, 0x17D, 0x168, 0x153,
        0x140, 0x12E, 0x11D, 0x10D, 0x0FE, 0x0F0, 0x0E2, 0x0D6, 0x0CA, 0x0BE,
        0x0B4, 0x0AA, 0x0A0, 0x097, 0x08F, 0x087, 0x07F, 0x078, 0x071, 0x06B,
        0x065, 0x05F, 0x05A, 0x055, 0x050, 0x04C, 0x047, 0x043, 0x040, 0x03C,
        0x039, 0x035, 0x032, 0x030, 0x02D, 0x02A, 0x028, 0x026, 0x024, 0x022,
        0x020, 0x01E, 0x01C, 0x01B, 0x019, 0x018, 0x016, 0x015, 0x014, 0x013,
        0x012, 0x011, 0x010, 0x00F, 0x00E, 0x00D, 0x00C, 0x00B, 0x00A, 0x009,
        0x008, 0x007, 0x006, 0x005, 0x004, 0x003, 0x002, 0x001, 0x000
    };
    // Note: kPsgFreqsDesc has 89 elements (A0..C#8)
    constexpr int kCount = static_cast<int>(sizeof(kPsgFreqsDesc) / sizeof(kPsgFreqsDesc[0]));
    // Reversed map (ascending frequency, matching PSG_NOTE_MAP in to_patterns_psg.py)
    int best_note = kNoteA0;
    int best_diff = 0x7FFFFFFF;
    for (int rev_i = 0; rev_i < kCount; rev_i++) {
        int orig_i = kCount - 1 - rev_i;
        int note = kNoteA0 + orig_i;
        int f = kPsgFreqsDesc[orig_i];
        int diff = freq - f;
        if (std::abs(diff) < std::abs(best_diff)) {
            best_diff = diff;
            best_note = note;
        }
    }
    return {best_note, best_diff};
}

void append_pitch_fx(std::vector<FxPair>& fx, int delta) {
    if (delta > 0) {
        fx.push_back({0xF1, static_cast<uint8_t>(std::min(255, delta))});
    } else if (delta < 0) {
        fx.push_back({0xF2, static_cast<uint8_t>(std::min(255, -delta))});
    }
}

std::vector<PatternEntry> transform_psg_tonal(const std::vector<SN76489State>& psg_states, int ch_idx) {
    std::vector<PatternEntry> out;
    out.reserve(psg_states.size());

    int note_c = kNoteOff;
    int vol_c = 0;
    int disp_c = 0;

    for (const auto& st : psg_states) {
        auto nd = find_best_psg_note(st.tonal[ch_idx].freq);
        int note = nd.first;
        int disp = nd.second;
        int vol = 15 - st.tonal[ch_idx].vol;
        if (vol == 0) {
            note = kNoteOff;
            disp = 0;
        }
        disp = -disp;

        int mask = 0;
        if (note != note_c) mask += 1;
        if (disp != disp_c) mask += 2;
        if (vol != vol_c)   mask += 4;

        PatternEntry e;
        switch (mask) {
            case 0:
                break;
            case 2:
                append_pitch_fx(e.fx, disp - disp_c);
                break;
            case 4:
                e.vol = vol;
                break;
            case 6:
                e.vol = vol;
                append_pitch_fx(e.fx, disp - disp_c);
                break;
            default:
                if (note == kNoteOff) {
                    e.note = note;
                } else {
                    e.note = note;
                    e.ins = 0;
                    e.vol = vol;
                    append_pitch_fx(e.fx, disp);
                }
                break;
        }
        out.push_back(std::move(e));
        note_c = note;
        disp_c = disp;
        vol_c = vol;
    }
    return out;
}

std::vector<PatternEntry> transform_psg_noise(const std::vector<SN76489State>& psg_states) {
    std::vector<PatternEntry> out;
    out.reserve(psg_states.size());

    int note_c = kNoteOff;
    int vol_c = 0;
    int disp_c = 0;
    int fmode_c = -1;

    for (const auto& st : psg_states) {
        auto nd3 = find_best_psg_note(st.tonal[2].freq);
        int note = nd3.first;
        int disp = nd3.second;
        int vol = 15 - st.noise.vol;
        int mode = st.noise.mode;

        int wave = mode >> 2;
        int spec = mode & 3;
        int fmode = wave | ((spec == 3) ? 0x10 : 0);

        PatternEntry e;
        if (vol == 0) {
            note = kNoteOff;
            disp = 0;
        } else if (spec == 0) {
            note = kNoteC0;
            disp = 0;
        } else if (spec == 1) {
            note = kNoteCs0;
            disp = 0;
        } else if (spec == 2) {
            note = kNoteD0;
            disp = 0;
        }

        if (fmode != fmode_c) {
            e.fx.push_back({0x20, static_cast<uint8_t>(fmode)});
        }
        if (note != note_c) {
            if (note == kNoteOff) {
                e.note = note;
            } else {
                if (disp != 0) append_pitch_fx(e.fx, disp);
                e.note = note;
                e.ins = 0;
                e.vol = vol;
            }
        } else {
            if (disp != disp_c) append_pitch_fx(e.fx, disp - disp_c);
            if (vol != vol_c) e.vol = vol;
        }

        out.push_back(std::move(e));
        note_c = note;
        disp_c = disp;
        vol_c = vol;
        fmode_c = fmode;
    }
    return out;
}

// =============================================================================
// FM Transformation (vgm2fur/transform/to_patterns_fm.py)
// =============================================================================
struct FMOpVoice {
    int mult = 0;
    int dt = 0;
    int tl = 0;
    int ar = 0;
    int rs = 0;
    int dr = 0;
    int am = 0;
    int sr = 0;
    int rr = 0;
    int sl = 0;
    int ssg = 0;
    int ssg_en = 0;

    auto tie() const {
        return std::tie(mult, dt, tl, ar, rs, dr, am, sr, rr, sl, ssg, ssg_en);
    }
    bool operator<(const FMOpVoice& o) const { return tie() < o.tie(); }
    bool operator==(const FMOpVoice& o) const { return tie() == o.tie(); }
};

struct FMVoice {
    int alg = 0;
    int fb = 0;
    int ams = 0;
    int pms = 0;
    FMOpVoice op[4];
    bool ch3 = false;

    auto tie() const {
        return std::tie(alg, fb, ams, pms, op[0], op[1], op[2], op[3], ch3);
    }
    bool operator<(const FMVoice& o) const { return tie() < o.tie(); }
    bool operator==(const FMVoice& o) const { return tie() == o.tie(); }
};

class FmFreqLookup {
public:
    FmFreqLookup() {
        static const int kBaseFreqs[14] = {
            0x260,
            0x284, 0x2AA, 0x2D2, 0x2FD, 0x32B, 0x35B,
            0x38E, 0x3C4, 0x3FE, 0x43B, 0x47B, 0x4C0,
            0x508
        };
        int dist_min = (kBaseFreqs[1] - kBaseFreqs[0]) / 2;
        int dist_max = (kBaseFreqs[13] - kBaseFreqs[12]) / 2;
        for (int i = 1; i <= 12; i++) {
            freq_map_.push_back({kNoteB_1 + i, kBaseFreqs[i]});
        }
        freq_min_ = freq_map_.front().second - dist_min;
        freq_max_ = freq_map_.back().second + dist_max;

        // Overfreq map
        {
            int note = kNoteC8;
            int shift = 1;
            int i = 0;
            int freq = freq_map_[i].second << shift;
            while (freq < 0x800) {
                overfreq_map_.push_back({note, freq});
                note += 1;
                shift += (i < 11) ? 0 : 1;
                i = (i < 11) ? (i + 1) : 0;
                freq = freq_map_[i].second << shift;
            }
        }

        // Underfreq map
        {
            std::vector<std::pair<int, int>> umap;
            int note = kNoteB_1;
            int shift = 1;
            int i = 11;
            int freq = freq_map_[i].second >> shift;
            while (note >= 0) {
                umap.push_back({note, freq});
                note -= 1;
                shift += (i > 0) ? 0 : 1;
                i = (i > 0) ? (i - 1) : 11;
                freq = freq_map_[i].second >> shift;
            }
            std::reverse(umap.begin(), umap.end());
            underfreq_map_ = std::move(umap);
        }
    }

    std::pair<int, int> find_best_note(int freq, int block) const {
        if (freq < freq_min_) {
            while (freq < freq_min_ && block > 0) {
                freq *= 2;
                block -= 1;
            }
            if (freq < freq_min_) {
                return bisect_map(freq, underfreq_map_);
            }
        } else if (freq > freq_max_) {
            if (block < 7) {
                freq /= 2;
                block += 1;
            } else {
                return bisect_map(freq, overfreq_map_);
            }
        }
        auto nd = bisect_map(freq, freq_map_);
        nd.first += block * 12;
        return nd;
    }

private:
    static std::pair<int, int> bisect_map(int freq, const std::vector<std::pair<int, int>>& m) {
        int best_note = m.front().first;
        int best_diff = 0x7FFFFFFF;
        for (const auto& nf : m) {
            int diff = freq - nf.second;
            if (std::abs(diff) < std::abs(best_diff)) {
                best_diff = diff;
                best_note = nf.first;
            }
        }
        return {best_note, best_diff};
    }

    std::vector<std::pair<int, int>> freq_map_;
    std::vector<std::pair<int, int>> overfreq_map_;
    std::vector<std::pair<int, int>> underfreq_map_;
    int freq_min_ = 0;
    int freq_max_ = 0;
};

FMVoice extract_fm_voice(const YMChannel& ch) {
    FMVoice v;
    v.ch3 = false;
    v.alg = ch.alg;
    v.fb  = ch.fb;
    v.pms = ch.pms;
    v.ams = ch.ams;
    for (int i = 0; i < 4; i++) {
        const YMOperator& op = ch.operators[i];
        v.op[i] = FMOpVoice{
            op.mult, op.dt, op.tl, op.ar, op.rs,
            op.dr, op.am, op.sr, op.rr, op.sl, op.ssg, op.ssg_en
        };
    }
    return v;
}

std::pair<FMVoice, int> normalize_fm_voice(FMVoice v) {
    int vol = 0;
    switch (v.alg) {
        case 0:
        case 1:
        case 2:
        case 3:
            vol = v.op[3].tl;
            v.op[3].tl = 0;
            break;
        case 4:
            vol = std::min(v.op[1].tl, v.op[3].tl);
            v.op[1].tl -= vol;
            v.op[3].tl -= vol;
            break;
        case 5:
        case 6:
            vol = std::min({v.op[1].tl, v.op[2].tl, v.op[3].tl});
            v.op[1].tl -= vol;
            v.op[2].tl -= vol;
            v.op[3].tl -= vol;
            break;
        case 7:
            vol = std::min({v.op[0].tl, v.op[1].tl, v.op[2].tl, v.op[3].tl});
            v.op[0].tl -= vol;
            v.op[1].tl -= vol;
            v.op[2].tl -= vol;
            v.op[3].tl -= vol;
            break;
        default:
            break;
    }
    return {v, 0x7F - vol};
}

FMVoice normalize_fm_voice_ch3(FMVoice v) {
    v.ch3 = true;
    for (int i = 0; i < 4; i++) {
        v.op[i].tl = 0;
    }
    return v;
}

struct FMKey {
    int note = kNoteOff;
    int disp = 0;
    int vol = 0;
    int id = 0;
    int pan = 0;
};

struct FMRowState {
    FMKey key;
    bool has_voice = false;
    FMVoice voice;
    int lfo = -1; // -1 = None (for ch1)
    int dac_en = 0; // for ch6
};

FxPair fx_pan(int pan) {
    switch (pan) {
        case 0: return {0x08, 0x00};
        case 1: return {0x08, 0x01};
        case 2: return {0x08, 0x10};
        default: return {0x08, 0x11};
    }
}

FxPair fx_legato(bool on) {
    return {0xEA, static_cast<uint8_t>(on ? 0x01 : 0x00)};
}

FxPair fx_lfo(int lfo_val) {
    if (lfo_val < 0) return {0x10, 0x00};
    return {0x10, static_cast<uint8_t>(0x10 + lfo_val)};
}

std::vector<PatternEntry> transform_fm_channel(const std::vector<FMRowState>& rows,
                                               const std::map<FMVoice, int>& voices,
                                               bool is_fm1,
                                               bool is_fm6) {
    std::vector<PatternEntry> out;
    out.reserve(rows.size());

    int note_c = kNoteOff;
    int disp_c = 0;
    int vol_c = 0;
    int keyid_c = -1;
    int ins_c = -1;
    int pan_c = 0;
    int lfo_c = -1;
    bool legato = false;

    for (const auto& r : rows) {
        int note  = r.key.note;
        int disp  = r.key.disp;
        int vol   = r.key.vol;
        int keyid = r.key.id;
        int pan   = r.key.pan;

        if (is_fm6 && r.dac_en) {
            note = kNoteOff;
        }

        PatternEntry e;
        if (is_fm1 && r.lfo != lfo_c) {
            e.fx.push_back(fx_lfo(r.lfo));
            lfo_c = r.lfo;
        }

        if (note == kNoteOff || !r.has_voice) {
            if (legato) e.fx.push_back(fx_legato(false));
            if (note != note_c) {
                e.note = kNoteOff;
            }
            out.push_back(std::move(e));
            note_c = kNoteOff;
            disp_c = 0;
            vol_c = 0;
            legato = false;
            continue;
        }

        int ins = voices.at(r.voice);
        if (keyid != keyid_c) {
            if (disp != 0) append_pitch_fx(e.fx, disp);
            if (legato) e.fx.push_back(fx_legato(false));
            if (pan != pan_c) e.fx.push_back(fx_pan(pan));
            e.note = note;
            e.ins = ins;
            e.vol = vol;
            out.push_back(std::move(e));
            note_c = note;
            disp_c = disp;
            vol_c = vol;
            keyid_c = keyid;
            ins_c = ins;
            pan_c = pan;
            legato = false;
        } else if (note != note_c || ins != ins_c) {
            if (disp != 0) append_pitch_fx(e.fx, disp);
            if (!legato) e.fx.push_back(fx_legato(true));
            if (pan != pan_c) e.fx.push_back(fx_pan(pan));
            e.note = note;
            e.ins = ins;
            e.vol = vol;
            out.push_back(std::move(e));
            note_c = note;
            disp_c = disp;
            vol_c = vol;
            ins_c = ins;
            pan_c = pan;
            legato = true;
        } else {
            if (disp != disp_c) append_pitch_fx(e.fx, disp - disp_c);
            if (pan != pan_c) e.fx.push_back(fx_pan(pan));
            if (vol != vol_c) e.vol = vol;
            out.push_back(std::move(e));
            disp_c = disp;
            vol_c = vol;
            pan_c = pan;
        }
    }
    return out;
}

// =============================================================================
// DAC Transformation (vgm2fur/transform/to_patterns_dac.py)
// =============================================================================
std::vector<int> bitstream_unpack(const uint8_t* enc, size_t enc_len, int bc) {
    std::vector<int> out;
    if (bc <= 0) return out;
    uint32_t n = 0;
    int bits = 0;
    uint32_t mask = (1u << bc) - 1u;
    for (size_t i = 0; i < enc_len; i++) {
        n = (n << 8) | enc[i];
        bits += 8;
        while (bits >= bc) {
            int val = static_cast<int>((n >> (bits - bc)) & mask);
            out.push_back(val);
            bits -= bc;
            n &= (bits > 0) ? ((1u << bits) - 1u) : 0u;
        }
    }
    return out;
}

bool resolve_dac_sample_bank(const std::vector<RawDataBlock>& datablocks,
                             std::vector<uint8_t>& out_signed_pcm) {
    out_signed_pcm.clear();
    std::map<std::pair<int, int>, std::vector<int>> comp_maps;

    for (const auto& blk : datablocks) {
        uint8_t type = blk.type;
        const auto& payload = blk.payload;

        if (type >= 0x40 && type <= 0x7E) {
            if (payload.size() < 10) continue;
            int tt = payload[0];
            int bd = payload[5];
            int bc = payload[6];
            int st = payload[7];
            int offset_start = read_u16_le(payload.data() + 8);
            auto unpacked = bitstream_unpack(payload.data() + 10, payload.size() - 10, bc);
            std::vector<uint8_t> decoded;
            decoded.reserve(unpacked.size());

            if (tt == 0 && st == 0) {
                for (int x : unpacked) decoded.push_back(static_cast<uint8_t>(x + offset_start));
            } else if (tt == 0 && st == 1) {
                int shift = bd - bc;
                for (int x : unpacked) decoded.push_back(static_cast<uint8_t>((x << shift) + offset_start));
            } else if (tt == 0 && st == 2) {
                auto it = comp_maps.find({tt, st});
                if (it == comp_maps.end()) continue;
                const auto& m = it->second;
                for (int x : unpacked) {
                    if (x >= 0 && static_cast<size_t>(x) < m.size())
                        decoded.push_back(static_cast<uint8_t>(m[x]));
                }
            } else if (tt == 1 && st == 0) {
                auto it = comp_maps.find({tt, st});
                if (it == comp_maps.end()) continue;
                const auto& m = it->second;
                int x = offset_start;
                for (int dx : unpacked) {
                    if (dx >= 0 && static_cast<size_t>(dx) < m.size()) x += m[dx];
                    decoded.push_back(static_cast<uint8_t>(x));
                }
            }

            if ((type - 0x40) == 0x00) {
                for (uint8_t u : decoded) {
                    out_signed_pcm.push_back(static_cast<uint8_t>((static_cast<int>(u) - 0x80) & 0xFF));
                }
            }
        } else if (type == 0x7F) {
            if (payload.size() < 6) continue;
            int tt = payload[0];
            int st = payload[1];
            int bd = payload[2];
            int count = read_u16_le(payload.data() + 4);
            int elem_sz = (bd + 7) / 8;
            if (elem_sz <= 0) continue;
            std::vector<int> tbl;
            size_t pos = 6;
            for (int i = 0; i < count && pos + elem_sz <= payload.size(); i++) {
                int val = 0;
                for (int b = 0; b < elem_sz; b++) {
                    val |= static_cast<int>(payload[pos + b]) << (b * 8);
                }
                tbl.push_back(val);
                pos += elem_sz;
            }
            comp_maps[{tt, st}] = std::move(tbl);
        } else if (type == 0x00) {
            for (uint8_t u : payload) {
                out_signed_pcm.push_back(static_cast<uint8_t>((static_cast<int>(u) - 0x80) & 0xFF));
            }
        }
    }

    return !out_signed_pcm.empty();
}

// =============================================================================
// Binary Builders for Furnace Instruments, Samples, Patterns & Module
// =============================================================================
std::vector<uint8_t> build_ins_psg_blank(const std::string& name) {
    std::vector<uint8_t> feat_na;
    put_bytes(feat_na, reinterpret_cast<const uint8_t*>("NA"), 2);
    put_u16_le(feat_na, static_cast<uint16_t>(name.size() + 1));
    put_cstr(feat_na, name);

    std::vector<uint8_t> body;
    put_u16_le(body, kTargetFurnaceVersion);
    put_u16_le(body, 0); // type 0 = Standard/PSG
    put_bytes(body, feat_na);
    put_bytes(body, reinterpret_cast<const uint8_t*>("EN"), 2);

    std::vector<uint8_t> ins;
    put_bytes(ins, reinterpret_cast<const uint8_t*>("INS2"), 4);
    put_u32_le(ins, static_cast<uint32_t>(body.size()));
    put_bytes(ins, body);
    return ins;
}

std::vector<uint8_t> build_ins_fm_opn(const FMVoice& voice, const std::string& name) {
    std::vector<uint8_t> feat_na;
    put_bytes(feat_na, reinterpret_cast<const uint8_t*>("NA"), 2);
    put_u16_le(feat_na, static_cast<uint16_t>(name.size() + 1));
    put_cstr(feat_na, name);

    std::vector<uint8_t> fm_body;
    uint8_t flags = 4 | (15 << 4);
    uint8_t alg_fb = static_cast<uint8_t>((voice.fb & 7) | ((voice.alg & 7) << 4));
    uint8_t ams_fms = static_cast<uint8_t>((voice.pms & 7) | ((voice.ams & 3) << 3));
    put_u8(fm_body, flags);
    put_u8(fm_body, alg_fb);
    put_u8(fm_body, ams_fms);
    put_u8(fm_body, 0);
    put_u8(fm_body, 0);

    static const int kOpOrder[4] = {0, 2, 1, 3};
    for (int idx : kOpOrder) {
        const FMOpVoice& op = voice.op[idx];
        uint8_t dt_mult = static_cast<uint8_t>((op.mult & 0x0F) | (((3 + op.dt) & 0x07) << 4));
        uint8_t tl      = static_cast<uint8_t>(op.tl & 0x7F);
        uint8_t rs_ar   = static_cast<uint8_t>((op.ar & 0x1F) | ((op.rs & 0x03) << 6));
        uint8_t am_dr   = static_cast<uint8_t>((op.dr & 0x1F) | ((op.am & 0x01) << 7));
        uint8_t kvs_sr  = static_cast<uint8_t>((op.sr & 0x1F) | (2 << 5));
        uint8_t sl_rr   = static_cast<uint8_t>((op.rr & 0x0F) | ((op.sl & 0x0F) << 4));
        uint8_t ssg     = static_cast<uint8_t>((op.ssg & 0x07) | ((op.ssg_en & 0x01) << 3));
        put_u8(fm_body, dt_mult);
        put_u8(fm_body, tl);
        put_u8(fm_body, rs_ar);
        put_u8(fm_body, am_dr);
        put_u8(fm_body, kvs_sr);
        put_u8(fm_body, sl_rr);
        put_u8(fm_body, ssg);
        put_u8(fm_body, 0);
    }

    std::vector<uint8_t> feat_fm;
    put_bytes(feat_fm, reinterpret_cast<const uint8_t*>("FM"), 2);
    put_u16_le(feat_fm, static_cast<uint16_t>(fm_body.size()));
    put_bytes(feat_fm, fm_body);

    std::vector<uint8_t> body;
    put_u16_le(body, kTargetFurnaceVersion);
    put_u16_le(body, 1); // type 1 = FM (OPN)
    put_bytes(body, feat_na);
    put_bytes(body, feat_fm);
    put_bytes(body, reinterpret_cast<const uint8_t*>("EN"), 2);

    std::vector<uint8_t> ins;
    put_bytes(ins, reinterpret_cast<const uint8_t*>("INS2"), 4);
    put_u32_le(ins, static_cast<uint32_t>(body.size()));
    put_bytes(ins, body);
    return ins;
}

std::vector<uint8_t> build_ins_sample_map(int first_sample, int note_count, const std::string& name) {
    std::vector<uint8_t> feat_na;
    put_bytes(feat_na, reinterpret_cast<const uint8_t*>("NA"), 2);
    put_u16_le(feat_na, static_cast<uint16_t>(name.size() + 1));
    put_cstr(feat_na, name);

    std::vector<uint8_t> sm_body;
    put_u16_le(sm_body, static_cast<uint16_t>(first_sample));
    put_u8(sm_body, 1);  // flags: use sample map
    put_u8(sm_body, 31);
    int count = 0;
    for (int i = 0; i < note_count && count < 120; i++, count++) {
        put_u16_le(sm_body, static_cast<uint16_t>(kNoteC4 - kNoteC0));
        put_u16_le(sm_body, static_cast<uint16_t>(first_sample + i));
    }
    while (count < 120) {
        put_u16_le(sm_body, static_cast<uint16_t>(count));
        put_u16_le(sm_body, 0xFFFF);
        count++;
    }

    std::vector<uint8_t> feat_sm;
    put_bytes(feat_sm, reinterpret_cast<const uint8_t*>("SM"), 2);
    put_u16_le(feat_sm, static_cast<uint16_t>(sm_body.size()));
    put_bytes(feat_sm, sm_body);

    std::vector<uint8_t> body;
    put_u16_le(body, kTargetFurnaceVersion);
    put_u16_le(body, 4); // type 4 = Amiga / Sample
    put_bytes(body, feat_na);
    put_bytes(body, feat_sm);
    put_bytes(body, reinterpret_cast<const uint8_t*>("EN"), 2);

    std::vector<uint8_t> ins;
    put_bytes(ins, reinterpret_cast<const uint8_t*>("INS2"), 4);
    put_u32_le(ins, static_cast<uint32_t>(body.size()));
    put_bytes(ins, body);
    return ins;
}

std::vector<uint8_t> build_furnace_sample(const std::vector<uint8_t>& data, int rate, const std::string& name) {
    std::vector<uint8_t> body;
    put_cstr(body, name);
    put_u32_le(body, static_cast<uint32_t>(data.size()));
    put_u32_le(body, static_cast<uint32_t>(rate));
    put_u32_le(body, static_cast<uint32_t>(rate));
    put_u8(body, 8); // 8-bit PCM
    put_u8(body, 0); // loop mode
    put_u8(body, 0); // BRR emphasis
    put_u8(body, 0); // dithering
    put_u32_le(body, 0xFFFFFFFFu); // loop start: none
    put_u32_le(body, 0xFFFFFFFFu); // loop end: none
    for (int i = 0; i < 4; i++) {
        put_u32_le(body, 0xFFFFFFFFu); // sample presence bitfields
    }
    put_bytes(body, data);

    std::vector<uint8_t> smp;
    put_bytes(smp, reinterpret_cast<const uint8_t*>("SMP2"), 4);
    put_u32_le(smp, static_cast<uint32_t>(body.size()));
    put_bytes(smp, body);
    return smp;
}

void emit_skip_n(std::vector<uint8_t>& payload, int n) {
    while (n > 128) {
        payload.push_back(0xFE);
        n -= 128;
    }
    if (n >= 2) {
        payload.push_back(static_cast<uint8_t>((n - 2) + 0x80));
    } else if (n == 1) {
        payload.push_back(0x00);
    }
}

std::vector<uint8_t> build_patn_block(int channel, int index, const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> body;
    put_u8(body, 0); // subsong
    put_u8(body, static_cast<uint8_t>(channel));
    put_u16_le(body, static_cast<uint16_t>(index));
    put_cstr(body, ""); // pattern name
    put_bytes(body, payload);
    put_u8(body, 0xFF); // end of pattern

    std::vector<uint8_t> pat;
    put_bytes(pat, reinterpret_cast<const uint8_t*>("PATN"), 4);
    put_u32_le(pat, static_cast<uint32_t>(body.size()));
    put_bytes(pat, body);
    return pat;
}

std::vector<uint8_t> build_empty_patn_block(int channel, int index) {
    std::vector<uint8_t> empty_payload;
    return build_patn_block(channel, index, empty_payload);
}

std::vector<std::vector<uint8_t>> build_channel_patterns(int channel,
                                                         int pat_len,
                                                         const std::vector<PatternEntry>& entries,
                                                         int& out_max_fx) {
    out_max_fx = 1;
    std::vector<std::vector<uint8_t>> patterns;
    std::vector<uint8_t> cur_payload;
    int left = pat_len;
    int skipcount = 0;
    int pat_index = 0;

    auto flush_pattern = [&]() {
        patterns.push_back(build_patn_block(channel, pat_index++, cur_payload));
        cur_payload.clear();
        left = pat_len;
    };

    auto flush_wait = [&](int n) {
        while (n > left) {
            emit_skip_n(cur_payload, left);
            n -= left;
            flush_pattern();
        }
        if (n > 0) {
            emit_skip_n(cur_payload, n);
            left -= n;
            if (left == 0) {
                flush_pattern();
            }
        }
    };

    for (const auto& entry : entries) {
        int fxc = entry.fx_count();
        if (fxc > out_max_fx) out_max_fx = fxc;

        std::vector<uint8_t> data = entry.encode();
        bool is_empty = (data.size() == 1 && data[0] == 0);
        if (!is_empty) {
            if (skipcount > 0) {
                flush_wait(skipcount);
                skipcount = 0;
            }
            cur_payload.insert(cur_payload.end(), data.begin(), data.end());
            left -= 1;
            if (left == 0) {
                flush_pattern();
            }
        } else {
            skipcount += 1;
        }
    }

    if (left < pat_len) {
        flush_pattern();
    }

    return patterns;
}

std::vector<uint8_t> make_asset_dir(int count) {
    std::vector<uint8_t> body;
    if (count <= 0) {
        put_u32_le(body, 0);
    } else {
        int clamped = std::min(256, count);
        put_u32_le(body, 1);
        put_cstr(body, "");
        put_u16_le(body, static_cast<uint16_t>(clamped));
        for (int i = 0; i < clamped; i++) {
            put_u8(body, static_cast<uint8_t>(i));
        }
    }
    std::vector<uint8_t> adir;
    put_bytes(adir, reinterpret_cast<const uint8_t*>("ADIR"), 4);
    put_u32_le(adir, static_cast<uint32_t>(body.size()));
    put_bytes(adir, body);
    return adir;
}

class FurnaceModuleBuilder {
public:
    int channel_count = 10;
    int pattern_length = 128;
    float ticks_per_second = 60.0f;
    std::vector<std::vector<std::vector<uint8_t>>> pattern_matrix;
    std::vector<int> effects_count;
    int order_count = 0;
    int pattern_count = 0;
    std::vector<std::vector<uint8_t>> instruments;
    std::vector<std::vector<uint8_t>> samples;
    float ym2612_volume = 1.0f;
    float sn76489_volume = 1.0f;
    std::string song_name;
    std::string song_author;
    std::string song_comment = "Imported from Sega Genesis VGM";
    bool fm3_special_mode = false;

    FurnaceModuleBuilder() {
        pattern_matrix.resize(10);
        effects_count.assign(10, 1);
    }

    void enable_fm3_special_mode() {
        fm3_special_mode = true;
        channel_count = 13;
        pattern_matrix.resize(13);
        effects_count.assign(13, 1);
    }

    void add_patterns(int ch_idx, const std::vector<PatternEntry>& entries) {
        int max_fx = 1;
        auto pats = build_channel_patterns(ch_idx, pattern_length, entries, max_fx);
        if (pats.size() > 256) {
            pats.resize(256);
        }
        effects_count[ch_idx] = std::max(1, max_fx);
        if (static_cast<int>(pats.size()) > order_count) {
            order_count = static_cast<int>(pats.size());
        }
        pattern_count += static_cast<int>(pats.size());
        pattern_matrix[ch_idx] = std::move(pats);
    }

    bool build(std::vector<uint8_t>& out_fur, bool compress = true) {
        if (order_count <= 0) order_count = 1;
        if (order_count > 256) order_count = 256;

        // Prebuild: pad each channel's pattern list up to order_count
        for (int ch = 0; ch < channel_count; ch++) {
            int idx = static_cast<int>(pattern_matrix[ch].size());
            while (static_cast<int>(pattern_matrix[ch].size()) < order_count) {
                pattern_matrix[ch].push_back(build_empty_patn_block(ch, idx));
                idx++;
                pattern_count++;
            }
        }

        std::vector<uint8_t> file;
        put_bytes(file, reinterpret_cast<const uint8_t*>("-Furnace module-"), 16);
        put_u16_le(file, kTargetFurnaceVersion);
        put_u16_le(file, 0);
        put_u32_le(file, 32); // song info pointer (header is 32 bytes)
        put_u64_le(file, 0);

        uint8_t system_id = fm3_special_mode ? 0x42 : 0x02;

        std::vector<uint8_t> info_1;
        put_u8(info_1, 0); // time base
        put_u8(info_1, 1); // speed 1
        put_u8(info_1, 1); // speed 2
        put_u8(info_1, 1); // initial arp time
        put_f32_le(info_1, ticks_per_second);
        put_u16_le(info_1, static_cast<uint16_t>(pattern_length));
        put_u16_le(info_1, static_cast<uint16_t>(order_count));
        put_u8(info_1, 4);  // highlight A
        put_u8(info_1, 16); // highlight B
        put_u16_le(info_1, static_cast<uint16_t>(instruments.size()));
        put_u16_le(info_1, 0); // wavetable count
        put_u16_le(info_1, static_cast<uint16_t>(samples.size()));
        put_u32_le(info_1, static_cast<uint32_t>(pattern_count));
        put_u8(info_1, system_id);
        for (int i = 0; i < 31; i++) put_u8(info_1, 0);
        for (int i = 0; i < 32; i++) put_u8(info_1, 0x40);
        for (int i = 0; i < 32; i++) put_u8(info_1, 0);
        for (int i = 0; i < 32; i++) put_u32_le(info_1, 0);
        put_cstr(info_1, song_name);
        put_cstr(info_1, song_author);
        put_f32_le(info_1, 440.0f);
        for (int i = 0; i < 20; i++) put_u8(info_1, 0); // 20 compat flags

        std::vector<uint8_t> info_2;
        for (int ch = 0; ch < channel_count; ch++) {
            for (int o = 0; o < order_count; o++) {
                put_u8(info_2, static_cast<uint8_t>(o));
            }
        }
        for (int ch = 0; ch < channel_count; ch++) {
            put_u8(info_2, static_cast<uint8_t>(effects_count[ch]));
        }
        for (int ch = 0; ch < channel_count; ch++) put_u8(info_2, 3);
        for (int ch = 0; ch < channel_count; ch++) put_u8(info_2, 0);
        for (int ch = 0; ch < channel_count; ch++) put_cstr(info_2, "");
        for (int ch = 0; ch < channel_count; ch++) put_cstr(info_2, "");
        put_cstr(info_2, song_comment);
        put_f32_le(info_2, 1.0f); // master volume
        for (int i = 0; i < 28; i++) put_u8(info_2, 0); // ext compat flags
        put_u16_le(info_2, 150); // virtual tempo N
        put_u16_le(info_2, 150); // virtual tempo D
        put_cstr(info_2, "");    // first subsong name
        put_cstr(info_2, "");    // first subsong comment
        put_u8(info_2, 0);       // additional subsongs
        for (int i = 0; i < 3; i++) put_u8(info_2, 0);
        put_cstr(info_2, "SEGA Genesis");
        put_cstr(info_2, "");
        put_cstr(info_2, "");
        put_cstr(info_2, "");
        put_cstr(info_2, "SEGA MegaDrive");
        put_cstr(info_2, "");
        put_f32_le(info_2, ym2612_volume);
        put_f32_le(info_2, 0.0f);
        put_f32_le(info_2, 0.0f);
        put_f32_le(info_2, sn76489_volume);
        put_f32_le(info_2, 0.0f);
        put_f32_le(info_2, 0.0f);
        put_u32_le(info_2, 0); // patchbay connection count
        put_u8(info_2, 1);     // automatic patchbay
        for (int i = 0; i < 8; i++) put_u8(info_2, 0);
        put_u8(info_2, 1);     // speed pattern length
        put_u8(info_2, 1);     // speed pattern[0]
        for (int i = 0; i < 15; i++) put_u8(info_2, 0);
        put_u8(info_2, 0);     // groove entry count

        size_t adir_ptrs_offset_in_info2 = info_2.size();
        put_u32_le(info_2, 0); // ins asset dir ptr
        put_u32_le(info_2, 0); // wave asset dir ptr
        put_u32_le(info_2, 0); // sample asset dir ptr

        size_t ptr_tables_bytes = (instruments.size() + samples.size() + static_cast<size_t>(pattern_count)) * 4;
        uint32_t info_size = static_cast<uint32_t>(info_1.size() + ptr_tables_bytes + info_2.size());

        uint32_t fileptr = static_cast<uint32_t>(file.size()) + 8 + info_size;

        std::vector<uint8_t> ins_adir  = make_asset_dir(static_cast<int>(instruments.size()));
        std::vector<uint8_t> wave_adir = make_asset_dir(0);
        std::vector<uint8_t> samp_adir = make_asset_dir(static_cast<int>(samples.size()));

        write_u32_at(info_2, adir_ptrs_offset_in_info2 + 0, fileptr);
        fileptr += static_cast<uint32_t>(ins_adir.size());
        write_u32_at(info_2, adir_ptrs_offset_in_info2 + 4, fileptr);
        fileptr += static_cast<uint32_t>(wave_adir.size());
        write_u32_at(info_2, adir_ptrs_offset_in_info2 + 8, fileptr);
        fileptr += static_cast<uint32_t>(samp_adir.size());

        std::vector<uint32_t> ins_ptrs(instruments.size());
        for (size_t i = 0; i < instruments.size(); i++) {
            ins_ptrs[i] = fileptr;
            fileptr += static_cast<uint32_t>(instruments[i].size());
        }

        std::vector<uint32_t> samp_ptrs(samples.size());
        for (size_t i = 0; i < samples.size(); i++) {
            samp_ptrs[i] = fileptr;
            fileptr += static_cast<uint32_t>(samples[i].size());
        }

        std::vector<uint32_t> pat_ptrs;
        pat_ptrs.reserve(static_cast<size_t>(pattern_count));
        for (int ch = 0; ch < channel_count; ch++) {
            for (const auto& pat : pattern_matrix[ch]) {
                pat_ptrs.push_back(fileptr);
                fileptr += static_cast<uint32_t>(pat.size());
            }
        }

        // Assemble INFO chunk
        put_bytes(file, reinterpret_cast<const uint8_t*>("INFO"), 4);
        put_u32_le(file, info_size);
        put_bytes(file, info_1);
        for (uint32_t p : ins_ptrs)  put_u32_le(file, p);
        for (uint32_t p : samp_ptrs) put_u32_le(file, p);
        for (uint32_t p : pat_ptrs)  put_u32_le(file, p);
        put_bytes(file, info_2);

        // Append asset dirs, instruments, samples, patterns
        put_bytes(file, ins_adir);
        put_bytes(file, wave_adir);
        put_bytes(file, samp_adir);
        for (const auto& ins : instruments) put_bytes(file, ins);
        for (const auto& smp : samples)     put_bytes(file, smp);
        for (int ch = 0; ch < channel_count; ch++) {
            for (const auto& pat : pattern_matrix[ch]) {
                put_bytes(file, pat);
            }
        }

        if (!compress) {
            out_fur = std::move(file);
            return true;
        }

        uLongf comp_bound = compressBound(static_cast<uLong>(file.size()));
        out_fur.resize(comp_bound);
        if (compress2(out_fur.data(), &comp_bound, file.data(), static_cast<uLong>(file.size()), Z_DEFAULT_COMPRESSION) != Z_OK) {
            out_fur = std::move(file);
            return true;
        }
        out_fur.resize(comp_bound);
        return true;
    }
};

} // namespace

// =============================================================================
// Public API Implementation
// =============================================================================
bool is_genesis_vgm_memory(const uint8_t* data, size_t size, std::string* err_msg) {
    std::vector<uint8_t> raw;
    if (!load_or_decompress_vgm(data, size, raw, err_msg)) {
        return false;
    }
    VgmHeaderInfo info;
    return parse_and_validate_genesis_vgm(raw, info, err_msg);
}

bool is_genesis_vgm_file(const std::string& path, std::string* err_msg) {
    std::vector<uint8_t> buf;
    if (!read_file_bytes(path, buf, err_msg)) {
        return false;
    }
    return is_genesis_vgm_memory(buf.data(), buf.size(), err_msg);
}

bool convert_vgm_to_fur_memory(const uint8_t* vgm_data, size_t vgm_size,
                               std::vector<uint8_t>& out_fur,
                               std::string* err_msg,
                               const ConvertOptions& opts) {
    std::vector<uint8_t> raw;
    if (!load_or_decompress_vgm(vgm_data, vgm_size, raw, err_msg)) {
        return false;
    }

    VgmHeaderInfo hdr;
    if (!parse_and_validate_genesis_vgm(raw, hdr, err_msg)) {
        return false;
    }

    std::vector<TableEntry<YM2612State>> table_fm;
    std::vector<TableEntry<SN76489State>> table_psg;
    std::vector<TableEntry<SamplerState>> table_dac;
    std::vector<SamplerCell> dac_cells;
    std::vector<RawDataBlock> table_data;
    int64_t accum_wait = 0;

    if (!parse_and_tabulate_vgm(raw, hdr, opts.use_latch,
                                table_fm, table_psg, table_dac, dac_cells, table_data,
                                accum_wait, err_msg)) {
        return false;
    }

    // Check CSM mode on FM3
    for (const auto& entry : table_fm) {
        if (entry.chip.channels[2].mode == 2) {
            if (err_msg) *err_msg = "YM2612 CSM mode is not supported";
            return false;
        }
    }

    double row_duration = opts.row_duration;
    double playback_rate = opts.playback_rate;
    if (row_duration <= 0.0 && playback_rate <= 0.0) {
        if (hdr.playback_rate == 0) {
            row_duration = 735.0;
            playback_rate = 60.0;
        } else {
            playback_rate = static_cast<double>(hdr.playback_rate);
            row_duration = static_cast<double>(kSampleRate) / playback_rate;
        }
    } else if (row_duration > 0.0 && playback_rate <= 0.0) {
        playback_rate = static_cast<double>(kSampleRate) / row_duration;
    } else if (row_duration <= 0.0 && playback_rate > 0.0) {
        row_duration = static_cast<double>(kSampleRate) / playback_rate;
    }

    int64_t total_wait = (hdr.total_wait > 0) ? static_cast<int64_t>(hdr.total_wait) : accum_wait;
    if (total_wait <= 0) {
        total_wait = static_cast<int64_t>(row_duration * 64);
    }

    int pat_len = std::max(1, std::min(256, opts.pattern_length));
    int64_t song_len = total_wait - opts.skip_samples;
    int64_t max_len = static_cast<int64_t>(row_duration * pat_len * 256);
    if (song_len > max_len && pat_len < 256) {
        pat_len = 256;
        max_len = static_cast<int64_t>(row_duration * pat_len * 256);
    }
    if (song_len > max_len) {
        if (err_msg) *err_msg = "VGM song is too long for 256 orders";
        return false;
    }

    auto ym_interp  = interpolate_table(table_fm, total_wait, row_duration, opts.skip_samples);
    auto psg_interp = interpolate_table(table_psg, total_wait, row_duration, opts.skip_samples);
    auto dac_interp = interpolate_table(table_dac, total_wait, row_duration, opts.skip_samples);

    size_t num_rows = ym_interp.size();
    bool fm3_special = false;
    for (const auto& ym : ym_interp) {
        if (ym.channels[2].mode == 1) {
            fm3_special = true;
            break;
        }
    }

    FurnaceModuleBuilder fur;
    fur.ticks_per_second = static_cast<float>(playback_rate);
    fur.pattern_length = pat_len;
    fur.ym2612_volume = opts.ym2612_volume;
    fur.sn76489_volume = opts.sn76489_volume;
    if (!hdr.game_name_en.empty() && !hdr.track_name_en.empty()) {
        fur.song_name = hdr.game_name_en + " - " + hdr.track_name_en;
    } else if (!hdr.track_name_en.empty()) {
        fur.song_name = hdr.track_name_en;
    } else {
        fur.song_name = hdr.game_name_en;
    }
    fur.song_author = hdr.author_en;
    if (fm3_special) {
        fur.enable_fm3_special_mode();
    }

    // 1. PSG Blank Instrument (index 0) & PSG patterns
    fur.instruments.push_back(build_ins_psg_blank("PSG_BLANK"));

    int psg_ch_base = fm3_special ? 9 : 6;
    fur.add_patterns(psg_ch_base + 0, transform_psg_tonal(psg_interp, 0));
    fur.add_patterns(psg_ch_base + 1, transform_psg_tonal(psg_interp, 1));
    fur.add_patterns(psg_ch_base + 2, transform_psg_tonal(psg_interp, 2));
    fur.add_patterns(psg_ch_base + 3, transform_psg_noise(psg_interp));

    // 2. Prepare FM row states & collect unique FM voices
    FmFreqLookup fm_freq;
    std::vector<FMRowState> fm_rows[6];
    std::vector<FMRowState> fm3_op_rows[4];

    for (int c = 0; c < 6; c++) fm_rows[c].reserve(num_rows);
    if (fm3_special) {
        for (int o = 0; o < 4; o++) fm3_op_rows[o].reserve(num_rows);
    }

    int opmask_prev[4] = {0, 0, 0, 0};
    int keyid_prev[4]  = {0, 0, 0, 0};

    for (size_t r = 0; r < num_rows; r++) {
        const YM2612State& ym = ym_interp[r];
        for (int c = 0; c < 6; c++) {
            if (c == 2 && fm3_special) continue;
            const YMChannel& ch = ym.channels[c];
            FMRowState rs;
            rs.lfo = (c == 0 && ch.lfo_en) ? ch.lfo : -1;
            rs.dac_en = (c == 5) ? ch.dac_en : 0;
            if (ch.opmask != 0) {
                auto nd = fm_freq.find_best_note(ch.freq, ch.block);
                auto vv = normalize_fm_voice(extract_fm_voice(ch));
                rs.key = FMKey{nd.first, nd.second, vv.second, ch.keyid, ch.pan};
                rs.has_voice = true;
                rs.voice = vv.first;
            } else {
                rs.key = FMKey{kNoteOff, 0, 0, ch.keyid, ch.pan};
                rs.has_voice = false;
            }
            fm_rows[c].push_back(rs);
        }

        if (fm3_special) {
            const YMChannel& ch3 = ym.channels[2];
            if (ch3.opmask == 0) {
                for (int i = 0; i < 4; i++) {
                    FMRowState rs;
                    rs.key = FMKey{kNoteOff, 0, 0, ch3.keyid, ch3.pan};
                    rs.has_voice = false;
                    fm3_op_rows[i].push_back(rs);
                    opmask_prev[i] = 0;
                    keyid_prev[i] = ch3.keyid;
                }
            } else if (ch3.mode == 1) {
                FMVoice v3 = normalize_fm_voice_ch3(extract_fm_voice(ch3));
                for (int i = 0; i < 4; i++) {
                    int bit = (ch3.opmask >> i) & 1;
                    int note = kNoteOff;
                    int disp = 0;
                    int vol = 0;
                    if (bit) {
                        auto nd = fm_freq.find_best_note(ch3.op_freq(i), ch3.op_block(i));
                        note = nd.first;
                        disp = nd.second;
                        vol = 0x7F - ch3.operators[i].tl;
                    }
                    int kid = (bit != opmask_prev[i]) ? ch3.keyid : keyid_prev[i];
                    FMRowState rs;
                    rs.key = FMKey{note, disp, vol, kid, ch3.pan};
                    rs.has_voice = true;
                    rs.voice = v3;
                    fm3_op_rows[i].push_back(rs);
                    opmask_prev[i] = bit;
                    keyid_prev[i] = kid;
                }
            } else {
                FMVoice v3 = normalize_fm_voice_ch3(extract_fm_voice(ch3));
                auto nd = fm_freq.find_best_note(ch3.freq, ch3.block);
                for (int i = 0; i < 4; i++) {
                    int bit = (ch3.opmask >> i) & 1;
                    int note = bit ? nd.first : kNoteOff;
                    int disp = bit ? nd.second : 0;
                    int vol  = bit ? (0x7F - ch3.operators[i].tl) : 0;
                    FMRowState rs;
                    rs.key = FMKey{note, disp, vol, ch3.keyid, ch3.pan};
                    rs.has_voice = true;
                    rs.voice = v3;
                    fm3_op_rows[i].push_back(rs);
                    opmask_prev[i] = bit;
                    keyid_prev[i] = ch3.keyid;
                }
            }
        }
    }

    // Collect voices in exact channel order (fm1, fm2, fm3, fm4, fm5, fm6)
    std::map<FMVoice, int> voices;
    std::vector<FMVoice> voice_list;
    auto register_voice = [&](const FMRowState& rs) {
        if (rs.has_voice && voices.find(rs.voice) == voices.end()) {
            int ins_idx = static_cast<int>(fur.instruments.size() + voice_list.size());
            // Clamp to max 255 instrument index in pattern bytes
            if (ins_idx > 255) ins_idx = 255;
            voices[rs.voice] = ins_idx;
            if (fur.instruments.size() + voice_list.size() < 256) {
                voice_list.push_back(rs.voice);
            }
        }
    };

    for (const auto& rs : fm_rows[0]) register_voice(rs);
    for (const auto& rs : fm_rows[1]) register_voice(rs);
    if (fm3_special) {
        for (const auto& rs : fm3_op_rows[0]) register_voice(rs);
    } else {
        for (const auto& rs : fm_rows[2]) register_voice(rs);
    }
    for (const auto& rs : fm_rows[3]) register_voice(rs);
    for (const auto& rs : fm_rows[4]) register_voice(rs);
    for (const auto& rs : fm_rows[5]) register_voice(rs);

    for (size_t i = 0; i < voice_list.size(); i++) {
        fur.instruments.push_back(build_ins_fm_opn(voice_list[i], "FM_VOICE_" + std::to_string(i)));
    }

    fur.add_patterns(0, transform_fm_channel(fm_rows[0], voices, true, false));
    fur.add_patterns(1, transform_fm_channel(fm_rows[1], voices, false, false));
    if (fm3_special) {
        fur.add_patterns(2, transform_fm_channel(fm3_op_rows[0], voices, false, false));
        fur.add_patterns(3, transform_fm_channel(fm3_op_rows[1], voices, false, false));
        fur.add_patterns(4, transform_fm_channel(fm3_op_rows[2], voices, false, false));
        fur.add_patterns(5, transform_fm_channel(fm3_op_rows[3], voices, false, false));
        fur.add_patterns(6, transform_fm_channel(fm_rows[3], voices, false, false));
        fur.add_patterns(7, transform_fm_channel(fm_rows[4], voices, false, false));
    } else {
        fur.add_patterns(2, transform_fm_channel(fm_rows[2], voices, false, false));
        fur.add_patterns(3, transform_fm_channel(fm_rows[3], voices, false, false));
        fur.add_patterns(4, transform_fm_channel(fm_rows[4], voices, false, false));
    }

    // 3. DAC Samples & FM6/DAC interleaved patterns
    std::vector<uint8_t> pcm_bank;
    bool has_pcm_bank = resolve_dac_sample_bank(table_data, pcm_bank);
    int fm6_ch_idx = fm3_special ? 8 : 5;

    if (has_pcm_bank) {
        std::map<std::pair<int, int>, std::pair<int, int>> dac_map;
        int instr_no = static_cast<int>(fur.instruments.size());
        int note = kNoteC0;
        constexpr int kMapCapacity = 120;
        int sample_start = 0;
        int map_idx = 0;

        for (const auto& ds : dac_interp) {
            int len = dac_cells[ds.cell_idx].length;
            int dur = dac_cells[ds.cell_idx].duration;
            if (len <= 0 || dur <= 0) continue;
            if (ds.start < 0 || static_cast<size_t>(ds.start) >= pcm_bank.size()) continue;
            int avail = static_cast<int>(pcm_bank.size()) - ds.start;
            int clamped_len = std::min(len, avail);
            if (clamped_len <= 0) continue;

            std::pair<int, int> pos{ds.start, len};
            if (dac_map.find(pos) == dac_map.end() && fur.samples.size() < 255) {
                dac_map[pos] = {note, std::min(255, instr_no)};
                if (note < kNoteB9) {
                    note += 1;
                } else {
                    note = kNoteC0;
                    if (fur.instruments.size() < 255) {
                        fur.instruments.push_back(
                            build_ins_sample_map(sample_start, kMapCapacity, "SAMPLE_MAP_" + std::to_string(map_idx++)));
                        instr_no += 1;
                    }
                    sample_start += kMapCapacity;
                }
                std::vector<uint8_t> cut(pcm_bank.begin() + ds.start,
                                         pcm_bank.begin() + ds.start + clamped_len);
                int rate = std::max(100, static_cast<int>((static_cast<int64_t>(kSampleRate) * len) / dur));
                int sidx = static_cast<int>(fur.samples.size());
                fur.samples.push_back(build_furnace_sample(cut, rate, "SAMPLE_" + std::to_string(sidx)));
            }
        }
        if (!dac_map.empty() && note > kNoteC0 && fur.instruments.size() < 256) {
            fur.instruments.push_back(
                build_ins_sample_map(sample_start, note - kNoteC0, "SAMPLE_MAP_" + std::to_string(map_idx++)));
        }

        // Transform DAC rows and interleave with FM6
        auto fm6_entries = transform_fm_channel(fm_rows[5], voices, false, false);
        std::vector<PatternEntry> combined;
        combined.reserve(num_rows);

        int keyid_c = -1;
        int left = -1;
        int rowdur_int = std::max(1, static_cast<int>(std::round(row_duration)));

        for (size_t r = 0; r < num_rows; r++) {
            const auto& ds = dac_interp[r];
            int len = dac_cells[ds.cell_idx].length;
            int dur = dac_cells[ds.cell_idx].duration;

            PatternEntry dac_e;
            if (len == 0) {
                if (left > 0) {
                    dac_e.note = kNoteOff;
                    left = -1;
                }
            } else if (ds.keyid != keyid_c) {
                auto it = dac_map.find({ds.start, len});
                if (it != dac_map.end()) {
                    dac_e.note = it->second.first;
                    dac_e.ins  = it->second.second;
                } else {
                    dac_e.note = kNoteOff;
                }
                left = (dur + rowdur_int - 1) / rowdur_int;
                keyid_c = ds.keyid;
            } else {
                if (left > 0) {
                    left -= 1;
                } else if (left == 0) {
                    dac_e.note = kNoteOff;
                    left = -1;
                }
            }

            if (fm_rows[5][r].dac_en) {
                combined.push_back(std::move(dac_e));
            } else {
                combined.push_back(std::move(fm6_entries[r]));
            }
        }
        fur.add_patterns(fm6_ch_idx, combined);
    } else {
        fur.add_patterns(fm6_ch_idx, transform_fm_channel(fm_rows[5], voices, false, true));
    }

    return fur.build(out_fur, true);
}

bool convert_vgm_to_fur_file(const std::string& vgm_path,
                             const std::string& fur_path,
                             std::string* err_msg,
                             const ConvertOptions& opts) {
    std::vector<uint8_t> vgm_buf;
    if (!read_file_bytes(vgm_path, vgm_buf, err_msg)) {
        return false;
    }

    std::vector<uint8_t> fur_buf;
    if (!convert_vgm_to_fur_memory(vgm_buf.data(), vgm_buf.size(), fur_buf, err_msg, opts)) {
        return false;
    }

    FILE* f = std::fopen(fur_path.c_str(), "wb");
    if (!f) {
        if (err_msg) *err_msg = "Cannot write file: " + fur_path;
        return false;
    }
    size_t written = std::fwrite(fur_buf.data(), 1, fur_buf.size(), f);
    ui_fsync_file(f);
    std::fclose(f);
    auto slash = fur_path.find_last_of('/');
    if (slash != std::string::npos && slash > 0) {
        ui_sync_dir(fur_path.substr(0, slash).c_str());
    }
    if (written != fur_buf.size()) {
        if (err_msg) *err_msg = "Failed to write complete .fur file";
        return false;
    }
    return true;
}

} // namespace vgm2fur

