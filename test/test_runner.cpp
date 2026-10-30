#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <unistd.h>

extern "C" {
#include "input.h"
#include "ui.h"
}

#include "control_surface.hpp"
#include "furnace_bridge.hpp"
#include "ui_views.hpp"

namespace {

int count_nonzero_pixels(const uint32_t* px, uint32_t bg_color) {
    int count = 0;
    for (int i = 0; i < UI_W * UI_H; i++) {
        if (px[i] != bg_color) count++;
    }
    return count;
}

bool has_nonzero_audio(const int16_t* pcm, int samples) {
    for (int i = 0; i < samples; i++) {
        if (pcm[i] != 0) return true;
    }
    return false;
}

void write_test_wav(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "wb");
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
    auto& fb = FurnaceBridge::instance();
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

    // Reset to Sega Genesis preset (index 0) and demo pattern for deterministic tests
    fb.create_new_with_preset(0);
    fb.create_demo_pattern();
    fb.engine().song.name = "MPC Test Song";
    fb.engine().song.author = "MPC User";

    std::printf("[3/6] Testing pattern & order editing...\n");
    fb.set_cell(0, 0, 0, 0, 96); // C-3 on Order 0, Ch 0, Row 0
    fb.set_cell(0, 0, 0, 1, 0);  // Ins 00
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
    assert(has_nonzero_audio(pcm.data(), static_cast<int>(pcm.size())) && "Expected non-zero audio from pad note");

    // Test playback audio
    std::fill(pcm.begin(), pcm.end(), 0);
    fb.play(0, 0);
    assert(fb.is_playing());
    fb.render_frames(pcm.data(), 2048);
    fb.stop();
    assert(!fb.is_playing());
    assert(has_nonzero_audio(pcm.data(), static_cast<int>(pcm.size())) && "Expected non-zero audio during playback");

    std::printf("[5/6] Testing .fur save/load and .wav sample import/export...\n");
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
    DivSample* smp = fb.engine().getSample(smp_idx);
    assert(smp != nullptr && smp->samples == 256);

    assert(fb.export_wav(wav_out_path, 2));
    unlink(fur_path.c_str());
    unlink(wav_in_path.c_str());
    unlink(wav_out_path.c_str());

    std::printf("[6/6] Testing ControlSurface and 320x240 UI Views...\n");
    std::vector<uint32_t> fb_px(UI_W * UI_H, 0);
    struct ui u{};
    u.px = fb_px.data();
    u.now_ms = 1000;

    struct input in{};
    for (int i = 0; i < 16; i++) {
        in.pad_hit[i] = -1;
        in.pad_pressure[i] = -1;
    }

    auto& cs = ControlSurface::instance();
    TrackerViews views;
    views.init(".");

    const AppView all_views[] = {
        AppView::Pattern,
        AppView::Orders,
        AppView::Instrument,
        AppView::Mixer,
        AppView::SampleWave,
        AppView::Browser,
        AppView::Menu,
        AppView::Help
    };

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

    // USB SD: menu row opens the toolkit screen, eject returns to the menu.
    ui_usb_sd_set_mock(1, 0);
    cs.state.active_view = AppView::Menu;
    cs.state.modal_lock = false;
    in.encoder = 6;
    in.encoder_press = 1;
    cs.process_frame(&u, &in);
    assert(views.draw_frame(&u, &in, cs.state) == 0);
    assert(cs.state.modal_lock);
    assert(cs.state.active_view == AppView::Menu);

    in.encoder = 0;
    in.encoder_press = 0;
    in.down = 1u << IN_SEQ;
    cs.process_frame(&u, &in);
    assert(views.draw_frame(&u, &in, cs.state) == 0);
    assert(cs.state.modal_lock);
    assert(cs.state.active_view == AppView::Menu);

    in.down = 0;
    in.encoder_press = 1;
    cs.process_frame(&u, &in);
    assert(views.draw_frame(&u, &in, cs.state) == 0);
    assert(cs.state.modal_lock);

    in.encoder_press = 0;
    cs.process_frame(&u, &in);
    assert(views.draw_frame(&u, &in, cs.state) == 0);
    assert(!cs.state.modal_lock);
    assert(cs.state.active_view == AppView::Menu);
    assert(count_nonzero_pixels(fb_px.data(), UI_BG) > 500);
    views.stop_usb();

    fb.shutdown();
    std::printf("All mpc-furnace tests passed!\n");
    return 0;
}
