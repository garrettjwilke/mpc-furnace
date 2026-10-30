#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace vgm2fur {

struct ConvertOptions {
    int pattern_length = 128;
    double row_duration = 0.0;   // 0.0 = auto from VGM playback rate (default 735.0 / 60Hz)
    double playback_rate = 0.0;  // 0.0 = auto from VGM header (default 60.0)
    int skip_samples = 0;
    float ym2612_volume = 1.0f;
    float sn76489_volume = 1.0f;
    bool use_latch = true;
};

// Check whether a file on disk (.vgm or .vgz) is a valid Sega Genesis / Mega Drive VGM.
// If false and err_msg is non-null, *err_msg is populated with a human-readable reason.
bool is_genesis_vgm_file(const std::string& path, std::string* err_msg = nullptr);

// Check whether an in-memory buffer (raw VGM or gzip/zlib compressed) is a valid
// Sega Genesis / Mega Drive VGM.
bool is_genesis_vgm_memory(const uint8_t* data, size_t size, std::string* err_msg = nullptr);

// Convert a Sega Genesis / Mega Drive VGM buffer in memory to a Furnace (.fur) binary module.
bool convert_vgm_to_fur_memory(const uint8_t* vgm_data, size_t vgm_size,
                               std::vector<uint8_t>& out_fur,
                               std::string* err_msg = nullptr,
                               const ConvertOptions& opts = ConvertOptions{});

// Convert a Sega Genesis / Mega Drive VGM file on disk to a Furnace (.fur) file on disk.
bool convert_vgm_to_fur_file(const std::string& vgm_path,
                             const std::string& fur_path,
                             std::string* err_msg = nullptr,
                             const ConvertOptions& opts = ConvertOptions{});

} // namespace vgm2fur

