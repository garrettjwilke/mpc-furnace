#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>
#include <vector>

extern "C" {
#include "input.h"
#include "ui.h"
}

#include "control_surface.hpp"
#include "furnace_bridge.hpp"
#include "ui_views.hpp"
#include "vgm2fur.hpp"
#include <sys/stat.h>
#include <zlib.h>

namespace {

int count_nonzero_pixels(const uint32_t *px, uint32_t bg_color) {
  int count = 0;
  for (int i = 0; i < UI_W * UI_H; i++) {
    if (px[i] != bg_color)
      count++;
  }
  return count;
}

bool has_nonzero_audio(const int16_t *pcm, int samples) {
  for (int i = 0; i < samples; i++) {
    if (pcm[i] != 0)
      return true;
  }
  return false;
}

void write_test_wav(const std::string &path) {
  FILE *f = std::fopen(path.c_str(), "wb");
  assert(f != nullptr);

  const int num_samples = 256;
  const uint32_t data_bytes = num_samples * sizeof(int16_t);
  const uint32_t riff_size = 36 + data_bytes;
  const uint32_t fmt_size = 16;
  const uint16_t audio_format = 1;
  const uint16_t num_channels = 1;
  const uint32_t sample_rate = 22050;
  const uint32_t byte_rate = sample_rate * sizeof(int16_t);
  const uint16_t block_align = sizeof(int16_t);
  const uint16_t bits_per_sample = 16;

  std::fwrite("RIFF", 1, 4, f);
  std::fwrite(&riff_size, 4, 1, f);
  std::fwrite("WAVE", 1, 4, f);
  std::fwrite("fmt ", 1, 4, f);
  std::fwrite(&fmt_size, 4, 1, f);
  std::fwrite(&audio_format, 2, 1, f);
  std::fwrite(&num_channels, 2, 1, f);
  std::fwrite(&sample_rate, 4, 1, f);
  std::fwrite(&byte_rate, 4, 1, f);
  std::fwrite(&block_align, 2, 1, f);
  std::fwrite(&bits_per_sample, 2, 1, f);
  std::fwrite("data", 1, 4, f);
  std::fwrite(&data_bytes, 4, 1, f);

  for (int i = 0; i < num_samples; i++) {
    int16_t s = static_cast<int16_t>(std::sin(2.0 * M_PI * i / 32.0) * 16000.0);
    std::fwrite(&s, sizeof(int16_t), 1, f);
  }
  std::fclose(f);
}

} // namespace

int main() {
  std::printf("[1/6] Initializing FurnaceBridge...\n");
  auto &fb = FurnaceBridge::instance();
  bool ok = fb.init(44100);
  assert(ok && "FurnaceBridge::init failed");
  assert(fb.engine().getTotalChannelCount() > 0);

  std::printf("[2/6] Testing system presets...\n");
  int preset_count = fb.get_preset_count();
  assert(preset_count >= 14);
  for (int i = 0; i < preset_count; i++) {
    fb.create_new_with_preset(i);
    assert(fb.engine().getTotalChannelCount() > 0);
  }

  // Reset to Sega Genesis preset (index 0) and demo pattern for deterministic
  // tests
  fb.create_new_with_preset(0);
  fb.create_demo_pattern();
  fb.engine().song.name = "MPC Test Song";
  fb.engine().song.author = "MPC User";

  std::printf("[3/6] Testing pattern & order editing...\n");
  fb.set_cell(0, 0, 0, 0, 96);  // C-3 on Order 0, Ch 0, Row 0
  fb.set_cell(0, 0, 0, 1, 0);   // Ins 00
  fb.set_cell(0, 0, 0, 2, 100); // Vol
  assert(fb.get_cell(0, 0, 0, 0) == 96);
  assert(fb.get_cell(0, 0, 0, 1) == 0);
  assert(fb.get_cell(0, 0, 0, 2) == 100);

  char note_str[4] = {};
  FurnaceBridge::format_note(96, note_str);
  assert(std::string(note_str) == "C-3");

  fb.transpose_pattern(0, 0, 2);
  assert(fb.get_cell(0, 0, 0, 0) == 98); // D-3

  std::printf("[4/6] Testing live pad note trigger & audio rendering...\n");
  std::vector<int16_t> pcm(2048 * 2, 0);
  int cur_row = 0;
  fb.pad_note_on(0, 0, 0, 96, 120, false, 0, &cur_row, 1);
  fb.render_frames(pcm.data(), 2048);
  fb.pad_note_off(0, 0);
  assert(has_nonzero_audio(pcm.data(), static_cast<int>(pcm.size())) &&
         "Expected non-zero audio from pad note");

  // Test playback audio
  std::fill(pcm.begin(), pcm.end(), 0);
  fb.play(0, 0);
  assert(fb.is_playing());
  fb.render_frames(pcm.data(), 2048);
  fb.stop();
  assert(!fb.is_playing());
  assert(has_nonzero_audio(pcm.data(), static_cast<int>(pcm.size())) &&
         "Expected non-zero audio during playback");

  std::printf(
      "[5/6] Testing .fur save/load and .wav sample import/export...\n");
  const std::string fur_path = "/tmp/mpc_furnace_test.fur";
  const std::string wav_in_path = "/tmp/mpc_furnace_sample_in.wav";
  const std::string wav_out_path = "/tmp/mpc_furnace_export_out.wav";

  assert(fb.save_fur(fur_path));
  assert(fb.load_song(fur_path));
  assert(std::string(fb.engine().song.name) == "MPC Test Song");
  assert(fb.get_cell(0, 0, 0, 0) == 98);

  write_test_wav(wav_in_path);
  int smp_idx = fb.import_wav_sample(wav_in_path);
  assert(smp_idx >= 0);
  DivSample *smp = fb.engine().getSample(smp_idx);
  assert(smp != nullptr && smp->samples == 256);

  assert(fb.export_wav(wav_out_path, 2));
  unlink(fur_path.c_str());
  unlink(wav_in_path.c_str());
  unlink(wav_out_path.c_str());

  std::printf("[6/7] Testing Sega Genesis VGM verification & native vgm2fur conversion...\n");
  const std::string gen_vgm_path = "/tmp/mpc_test_genesis.vgm";
  const std::string gb_vgm_path = "/tmp/mpc_test_gameboy.vgm";
  const std::string vgm_fur_out = "/tmp/mpc_test_imported.fur";

  // 1) Export a Sega Genesis VGM from preset 0 + demo pattern
  fb.create_new_with_preset(0);
  fb.create_demo_pattern();
  fb.engine().song.name = "Genesis VGM Test";
  fb.engine().song.author = "MPC Composer";
  assert(fb.export_vgm(gen_vgm_path));

  std::string vgm_err;
  assert(fb.is_genesis_vgm(gen_vgm_path, &vgm_err) && "Expected valid Sega Genesis VGM");

  // 2) Export a Game Boy VGM from preset 2 and verify is_genesis_vgm rejects it
  fb.create_new_with_preset(2); // Nintendo Game Boy
  fb.create_demo_pattern();
  assert(fb.export_vgm(gb_vgm_path));
  vgm_err.clear();
  assert(!fb.is_genesis_vgm(gb_vgm_path, &vgm_err) && "Expected Game Boy VGM to be rejected");
  assert(!vgm_err.empty());

  // 3) Convert and load the Sega Genesis VGM via fb.import_vgm()
  assert(fb.import_vgm(gen_vgm_path, vgm_fur_out));
  assert(fb.current_path == vgm_fur_out);
  assert(fb.engine().getTotalChannelCount() == 10); // 6 FM + 4 PSG
  assert(fb.engine().curSubSong->ordersLen >= 1);
  assert(fb.engine().song.insLen >= 1);

  // Render audio from the imported .fur song to verify playability
  std::fill(pcm.begin(), pcm.end(), 0);
  fb.play(0, 0);
  fb.render_frames(pcm.data(), 4096);
  fb.stop();
  assert(has_nonzero_audio(pcm.data(), static_cast<int>(pcm.size())) &&
         "Expected non-zero audio from imported Genesis VGM");

  // 4) Test compressed .vgz with YM2612 Extended Ch3, DAC data block (0x67 + 0xE0 + 0x80..0x8F), PSG, and GD3 metadata
  {
    std::vector<uint8_t> raw_vgm(0x80, 0);
    std::memcpy(raw_vgm.data(), "Vgm ", 4);
    auto put32 = [&](size_t off, uint32_t v) {
      raw_vgm[off + 0] = static_cast<uint8_t>(v & 0xFF);
      raw_vgm[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
      raw_vgm[off + 2] = static_cast<uint8_t>((v >> 16) & 0xFF);
      raw_vgm[off + 3] = static_cast<uint8_t>((v >> 24) & 0xFF);
    };
    put32(0x08, 0x00000150); // Version 1.50
    put32(0x0C, 3579545);    // SN76489 clock
    put32(0x2C, 7670454);    // YM2612 clock
    put32(0x18, 735 * 4);    // Total samples
    put32(0x24, 60);         // 60 Hz rate
    put32(0x34, 0x80 - 0x34);// VGM data offset -> 0x80

    // 0x67 data block (type 0x00 = YM2612 PCM, 64 bytes of sine/square wave)
    raw_vgm.push_back(0x67);
    raw_vgm.push_back(0x66);
    raw_vgm.push_back(0x00);
    uint32_t pcm_len = 64;
    raw_vgm.push_back(static_cast<uint8_t>(pcm_len & 0xFF));
    raw_vgm.push_back(static_cast<uint8_t>((pcm_len >> 8) & 0xFF));
    raw_vgm.push_back(static_cast<uint8_t>((pcm_len >> 16) & 0xFF));
    raw_vgm.push_back(static_cast<uint8_t>((pcm_len >> 24) & 0xFF));
    for (uint32_t i = 0; i < pcm_len; i++) {
      raw_vgm.push_back((i & 4) ? 0xC0 : 0x40);
    }

    // Enable Ch3 special mode (reg 0x27 = 0x40), enable DAC (reg 0x2B = 0x80)
    raw_vgm.insert(raw_vgm.end(), {0x52, 0x27, 0x40});
    raw_vgm.insert(raw_vgm.end(), {0x52, 0x2B, 0x80});
    // Set Ch3 per-operator frequencies and key-on Ch3 op1
    raw_vgm.insert(raw_vgm.end(), {0x52, 0xA9, 0x22, 0x52, 0xAD, 0x84});
    raw_vgm.insert(raw_vgm.end(), {0x52, 0xAA, 0x22, 0x52, 0xAE, 0x90});
    raw_vgm.insert(raw_vgm.end(), {0x52, 0xAC, 0x22, 0x52, 0xA6, 0xA0});
    raw_vgm.insert(raw_vgm.end(), {0x52, 0xA2, 0x22, 0x52, 0x28, 0xF2});
    // PSG tone 0 + noise
    raw_vgm.insert(raw_vgm.end(), {0x50, 0x8A, 0x50, 0x10, 0x50, 0x92, 0x50, 0xE4, 0x50, 0xF3});
    // Seek PCM bank to 0 and stream 32 DAC samples with 8-sample waits (0x88)
    raw_vgm.insert(raw_vgm.end(), {0xE0, 0x00, 0x00, 0x00, 0x00});
    for (int i = 0; i < 32; i++) {
      raw_vgm.push_back(0x88);
    }
    raw_vgm.push_back(0x62); // Wait 735
    raw_vgm.push_back(0x62);
    raw_vgm.push_back(0x62);
    raw_vgm.push_back(0x66); // End of sound data

    // Append GD3 tag
    uint32_t gd3_pos = static_cast<uint32_t>(raw_vgm.size());
    put32(0x14, gd3_pos - 0x14);
    const uint8_t gd3_hdr[] = {'G', 'd', '3', ' ', 0x00, 0x01, 0x00, 0x00};
    raw_vgm.insert(raw_vgm.end(), gd3_hdr, gd3_hdr + 8);
    size_t gd3_len_pos = raw_vgm.size();
    raw_vgm.insert(raw_vgm.end(), {0, 0, 0, 0});
    auto push_utf16 = [&](const char *ascii) {
      while (*ascii) {
        raw_vgm.push_back(static_cast<uint8_t>(*ascii++));
        raw_vgm.push_back(0);
      }
      raw_vgm.push_back(0);
      raw_vgm.push_back(0);
    };
    push_utf16("Green Hill Test"); // Track EN
    push_utf16("");                // Track JP
    push_utf16("Sonic Test");      // Game EN
    push_utf16("");                // Game JP
    push_utf16("Sega Mega Drive"); // System EN
    push_utf16("");                // System JP
    push_utf16("Masato Test");     // Author EN
    for (int i = 0; i < 4; i++) push_utf16("");
    uint32_t gd3_len = static_cast<uint32_t>(raw_vgm.size() - (gd3_len_pos + 4));
    put32(gd3_len_pos, gd3_len);
    put32(0x04, static_cast<uint32_t>(raw_vgm.size() - 4));

    const std::string vgz_path = "/tmp/mpc_test_ext_dac.vgz";
    gzFile gz = gzopen(vgz_path.c_str(), "wb");
    assert(gz != nullptr);
    gzwrite(gz, raw_vgm.data(), static_cast<unsigned int>(raw_vgm.size()));
    gzclose(gz);

    assert(fb.is_genesis_vgm(vgz_path, &vgm_err));
    assert(fb.import_vgm(vgz_path, vgm_fur_out));
    assert(std::string(fb.engine().song.name) == "Sonic Test - Green Hill Test");
    assert(std::string(fb.engine().song.author) == "Masato Test");
    assert(fb.engine().getTotalChannelCount() == 13); // 9 FM (Ext Ch3) + 4 PSG
    assert(fb.engine().song.sampleLen >= 1);
    unlink(vgz_path.c_str());
  }

  unlink(gen_vgm_path.c_str());
  unlink(gb_vgm_path.c_str());
  unlink(vgm_fur_out.c_str());

  std::printf("[7/7] Testing ControlSurface, 320x240 UI Views & Browser VGM Import Flow...\n");
  std::vector<uint32_t> fb_px(UI_W * UI_H, 0);
  struct ui u{};
  u.px = fb_px.data();
  u.now_ms = 1000;

  struct input in{};
  for (int i = 0; i < 16; i++) {
    in.pad_hit[i] = -1;
    in.pad_pressure[i] = -1;
  }

  auto &cs = ControlSurface::instance();
  TrackerViews views;
  views.init(".");

  const AppView all_views[] = {AppView::Pattern,    AppView::Orders,
                               AppView::Instrument, AppView::Mixer,
                               AppView::SampleWave, AppView::Browser,
                               AppView::Menu,       AppView::Help};

  for (AppView v : all_views) {
    cs.state.active_view = v;
    cs.process_frame(&u, &in);
    int rc = views.draw_frame(&u, &in, cs.state);
    assert(rc == 0);
    cs.update_leds(&u, &in, 0x07, 0x07);
    int drawn = count_nonzero_pixels(fb_px.data(), UI_BG);
    assert(drawn > 500 && "View rendered too few pixels");
  }

  // Also test compact 8-channel pattern view
  cs.state.active_view = AppView::Pattern;
  cs.state.compact_pattern_view = true;
  views.draw_frame(&u, &in, cs.state);
  assert(count_nonzero_pixels(fb_px.data(), UI_BG) > 500);

  // Test end-to-end UI File Browser VGM import workflow:
  // Create the host sandbox directory ("build/test_fb/data") with a Genesis .vgm and a Game Boy .vgm
  mkdir("build/test_fb", 0755);
  mkdir("build/test_fb/media", 0755);
  mkdir("build/test_fb/media/MPC", 0755);
  const std::string test_dir = "build/test_fb/data";
  mkdir(test_dir.c_str(), 0755);
  const std::string ui_gb_vgm = test_dir + "/01_bad_gb.vgm";
  const std::string ui_gen_vgm = test_dir + "/02_sonic_gen.vgm";
  const std::string ui_expected_fur = test_dir + "/02_sonic_gen.fur";
  unlink(ui_gb_vgm.c_str());
  unlink(ui_gen_vgm.c_str());
  unlink(ui_expected_fur.c_str());

  fb.create_new_with_preset(2); // Game Boy
  fb.create_demo_pattern();
  assert(fb.export_vgm(ui_gb_vgm));

  fb.create_new_with_preset(0); // Sega Genesis
  fb.create_demo_pattern();
  assert(fb.export_vgm(ui_gen_vgm));

  views.init(test_dir);
  cs.state.active_view = AppView::Browser;
  views.draw_frame(&u, &in, cs.state); // Initial render in test_dir (entry 0 is "..", entry 1 is "01_bad_gb.vgm", entry 2 is "02_sonic_gen.vgm")

  // Turn encoder +1 to select "01_bad_gb.vgm". Press B3 (LOAD) -> should reject non-Genesis VGM and stay in Browser
  in.encoder = 1;
  views.draw_frame(&u, &in, cs.state);
  in.encoder = 0;

  in.down = (1u << IN_B3);
  views.draw_frame(&u, &in, cs.state);
  in.down = 0;
  assert(cs.state.active_view == AppView::Browser && "Non-Genesis VGM must stay in Browser");

  // Turn encoder +1 to select "02_sonic_gen.vgm" and press B3 (LOAD) -> enters Stage 1 (Choose Save Folder)
  in.encoder = 1;
  views.draw_frame(&u, &in, cs.state);
  in.encoder = 0;

  in.down = (1u << IN_B3);
  views.draw_frame(&u, &in, cs.state);
  in.down = 0;
  assert(cs.state.active_view == AppView::Browser && "Should now be in Save Folder Selection stage");

  // Press B3 (SAVE HERE) in folder browser -> enters Stage 2 (Save .fur Filename text editor, pre-filled with 02_sonic_gen.fur)
  in.down = (1u << IN_B3);
  views.draw_frame(&u, &in, cs.state);
  in.down = 0;
  assert(cs.state.active_view == AppView::Browser && cs.state.modal_lock &&
         "Should now be in Filename Text Editor stage");

  // Press B3 (OK) in text editor -> converts VGM to 02_sonic_gen.fur, loads it, and opens Pattern view!
  in.down = (1u << IN_B3);
  views.draw_frame(&u, &in, cs.state);
  in.down = 0;
  assert(cs.state.active_view == AppView::Pattern && "Should switch to Pattern view after importing VGM");
  assert(!cs.state.modal_lock);
  assert(fb.current_path == ui_expected_fur);
  assert(access(ui_expected_fur.c_str(), F_OK) == 0 && "Converted .fur file should exist on disk");

  unlink(ui_gb_vgm.c_str());
  unlink(ui_gen_vgm.c_str());
  unlink(ui_expected_fur.c_str());

  fb.shutdown();
  std::printf("All mpc-furnace tests passed!\n");
  return 0;
}