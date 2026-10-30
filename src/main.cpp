#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>

extern "C" {
#include "app.h"
#include "input.h"
#include "ui.h"
}

#include "audio_engine.h"
#include "control_surface.hpp"
#include "furnace_bridge.hpp"
#include "ui_views.hpp"

namespace {

struct AppContext {
    struct audio_engine* ae = nullptr;
    TrackerViews views;
};

void furnace_audio_fill(void* user_ctx, int16_t* dst, int frames) {
    (void)user_ctx;
    FurnaceBridge::instance().render_frames(dst, frames);
}

int app_frame_cb(struct ui* u, const struct input* in, void* user_state) {
    auto* ctx = static_cast<AppContext*>(user_state);
    auto& cs = ControlSurface::instance();
    auto& fb = FurnaceBridge::instance();

    if (in && in->main_moved && ctx->ae) {
        audio_engine_set_volume(ctx->ae, in->main);
    }

    cs.process_frame(u, in);

    int should_quit = ctx->views.draw_frame(u, in, cs.state);

    int left_mask = 0;
    int right_mask = 0;
    if (ctx->ae) {
        audio_engine_get_meter_masks(ctx->ae, &left_mask, &right_mask);
    }

    cs.update_leds(u, in, left_mask, right_mask);

    // Keep UI ticking at ~50Hz when playing, recording, note-repeating, or meters are active
    bool pad_held = false;
    if (in) {
        for (int p = 0; p < 16; p++) {
            if (in->pad_down[p]) {
                pad_held = true;
                break;
            }
        }
    }
    if (fb.is_playing() || cs.state.recording || cs.state.note_repeat_down ||
        pad_held || left_mask != 0 || right_mask != 0) {
        ui_wake(u, 20);
    }

    if (should_quit) {
        ui_quit(u);
        return 1;
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    auto& fb = FurnaceBridge::instance();
    if (!fb.init(44100)) {
        std::fprintf(stderr, "mpc-furnace: failed to initialize Furnace engine\n");
        return 1;
    }

    std::string start_dir = ".";
    if (argc > 1 && argv[1] && argv[1][0] != '\0') {
        std::string arg = argv[1];
        // If argument looks like a tracker file, load it; otherwise treat as browse directory
        if (arg.size() > 4 && (arg.find(".fur") != std::string::npos ||
                               arg.find(".dmf") != std::string::npos ||
                               arg.find(".mod") != std::string::npos ||
                               arg.find(".s3m") != std::string::npos ||
                               arg.find(".xm") != std::string::npos ||
                               arg.find(".it") != std::string::npos ||
                               arg.find(".ftm") != std::string::npos)) {
            fb.load_song(arg);
            auto slash = arg.find_last_of('/');
            if (slash != std::string::npos) {
                start_dir = arg.substr(0, slash);
            }
        } else {
            start_dir = arg;
        }
    } else if (access("/media", R_OK) == 0) {
        start_dir = "/media";
    }

    AppContext ctx;
    ctx.views.init(start_dir);

    ctx.ae = audio_engine_create(44100, 512);
    if (ctx.ae) {
        audio_engine_set_source(ctx.ae, furnace_audio_fill, nullptr);
        audio_engine_set_active(ctx.ae, true);
        audio_engine_start(ctx.ae);
    }

    int rc = app_run(app_frame_cb, &ctx);

    ctx.views.stop_usb();

    if (ctx.ae) {
        audio_engine_stop(ctx.ae);
        audio_engine_destroy(ctx.ae);
    }

    fb.shutdown();
    return rc;
}

