#include "ui_views.hpp"
#include "furnace_bridge.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unistd.h>

enum {
  COL_BG = 0x101218,
  COL_HEADER_BG = 0x181c26,
  COL_FOOTER_BG = 0x161922,
  COL_PANEL = 0x181d28,
  COL_PANEL_ALT = 0x1e2432,
  COL_GRID_LINE = 0x283040,
  COL_BEAT_ROW = 0x18202c,
  COL_BAR_ROW = 0x202a3a,
  COL_PLAYHEAD = 0x3d2e10,
  COL_CURSOR_ROW = 0x24324a,
  COL_TEXT = 0xe8ecf2,
  COL_MUTED = 0x7c8698,
  COL_DIM = 0x485060,
  COL_HL = 0xffb000,
  COL_LOCK_HL = 0xff3860,
  COL_REC = 0xff3030,
  COL_PLAY = 0x30e070,
  COL_NOTE = 0xf4f6ff,
  COL_INS = 0x50d8ff,
  COL_VOL = 0x60e888,
  COL_FX = 0xffaa44,
  COL_FXVAL = 0xffdd66
};

TrackerViews::TrackerViews() = default;

static const char *kBrowserExts[] = {
    ".fur", ".vgm", ".vgz", ".dmf", ".mod", ".s3m", ".xm",  ".it",
    ".ftm", ".fui", ".dmp", ".tfi", ".vgi", ".opm", ".wav", nullptr};

std::string TrackerViews::get_active_save_dir() const {
  if (fb_state_.cwd[0] != '\0') {
    return fb_state_.cwd;
  }
  const auto &cur = FurnaceBridge::instance().current_path;
  if (!cur.empty()) {
    auto slash = cur.rfind('/');
    if (slash != std::string::npos && slash > 0) {
      return cur.substr(0, slash);
    }
  }
  if (!browse_dir_.empty() && browse_dir_ != "/media" && browse_dir_ != ".") {
    return browse_dir_;
  }
  if (fb_state_.external_path[0] != '\0') {
    return fb_state_.external_path;
  }
  if (fb_state_.internal_path[0] != '\0') {
    return fb_state_.internal_path;
  }
  return ".";
}

void TrackerViews::init(const std::string &initial_dir) {
  browse_dir_ = initial_dir;
  struct ui_filebrowser_config cfg{};
  cfg.initial_path = (!browse_dir_.empty() && browse_dir_ != "/media") ? browse_dir_.c_str() : nullptr;
  cfg.title = "FURNACE FILES";
  cfg.action_label = "LOAD";
  cfg.options = UI_FB_PRESET_DEFAULT;
  cfg.extensions = kBrowserExts;
  ui_filebrowser_init(&fb_state_, &cfg);
  if (fb_state_.cwd[0] != '\0') {
    browse_dir_ = fb_state_.cwd;
  }
  fb_initialized_ = true;
  vgm_import_stage_ = 0;
}

void TrackerViews::stop_usb() {
  if (ui_usb_sd_is_active(&usb_))
    ui_usb_sd_stop(&usb_);
  usb_open_ = false;
}

static struct ui_usb_sd_style furnace_usb_style() {
  struct ui_usb_sd_style st = {
      .bg = COL_BG,
      .text = COL_TEXT,
      .dim = COL_DIM,
      .hl = COL_HL,
      .box = COL_PANEL,
      .border = COL_GRID_LINE,
      .bar = COL_CURSOR_ROW,
      .active_text = COL_BG,
  };
  return st;
}

static struct ui_soft_style make_soft_style(bool shift) {
  struct ui_soft_style st{};
  st.fill = shift ? 0x2a2438 : 0x242c3a;
  st.text = COL_TEXT;
  st.active_text = 0x101014;
  st.highlight = shift ? 0xc078ff : COL_HL;
  st.gap = 4;
  st.feedback = UI_SOFT_FLASH;
  st.flash_ms = 120;
  return st;
}

void TrackerViews::draw_header(struct ui *u, const ControlSurfaceState &cs,
                               const char *k1, const char *k2, const char *k3) {
  auto &fb = FurnaceBridge::instance();
  auto &eng = fb.engine();

  ui_box(
      u, 0, 24, UI_W, 15,
      (struct ui_paint){.fill_set = 1, .fill = COL_HEADER_BG, .border_set = 0});

  int ord = fb.is_playing() ? eng.getOrder() : cs.cur_order;
  int row = fb.is_playing() ? eng.getRow() : cs.cur_row;
  int ord_len = eng.curSubSong ? eng.curSubSong->ordersLen : 1;

  char line[64];
  std::snprintf(line, sizeof(line), "O:%02X/%02X R:%02X C:%02d I:%02X O:%d",
                ord, std::max(1, ord_len) - 1, row, cs.cur_chan + 1, cs.cur_ins,
                cs.base_octave);
  ui_text(u, 4, 26, line, COL_TEXT, 1);

  // Play / Rec badge on right of header
  if (cs.recording) {
    ui_box(u, 170, 25, 24, 13,
           (struct ui_paint){.fill_set = 1, .fill = COL_REC});
    ui_text(u, 173, 26, "REC", 0xffffff, 1);
  } else if (fb.is_playing()) {
    ui_box(u, 170, 25, 24, 13,
           (struct ui_paint){.fill_set = 1, .fill = 0x1c6838});
    ui_text(u, 173, 26, "PLY", COL_PLAY, 1);
  }

  char kline[32];
  std::snprintf(kline, sizeof(kline), "%s/%s/%s", k1 ? k1 : "-", k2 ? k2 : "-",
                k3 ? k3 : "-");
  int kw = static_cast<int>(std::strlen(kline)) * 6;
  ui_text(u, std::max(198, UI_W - kw - 4), 26, kline, COL_HL, 1);
}

void TrackerViews::draw_footer(struct ui *u, const ControlSurfaceState &cs) {
  auto &fb = FurnaceBridge::instance();
  auto &eng = fb.engine();

  ui_box(
      u, 0, 224, UI_W, 16,
      (struct ui_paint){.fill_set = 1, .fill = COL_FOOTER_BG, .border_set = 0});

  char left[40];
  const char *sys_name = eng.song.systemName.empty()
                             ? "Sega Genesis"
                             : eng.song.systemName.c_str();
  std::snprintf(left, sizeof(left), "%.22s | %s", fb.status_msg.c_str(),
                sys_name);
  ui_text(u, 4, 226, left, COL_MUTED, 1);

  char right[24];
  int spd = (eng.curSubSong && eng.curSubSong->speeds.len > 0)
                ? eng.curSubSong->speeds.val[0]
                : 6;
  int hz =
      eng.curSubSong ? static_cast<int>(std::round(eng.curSubSong->hz)) : 60;
  std::snprintf(right, sizeof(right), "S:%d %dHz%s", spd, hz,
                cs.cell_edit_locked ? " [EDIT]" : "");
  int rw = static_cast<int>(std::strlen(right)) * 6;
  ui_text(u, UI_W - rw - 4, 226, right,
          cs.cell_edit_locked ? COL_LOCK_HL : COL_TEXT, 1);
}

// =============================================================================
// 1. PATTERN TRACKER VIEW (Full 3-Channel & Compact 8-Channel)
// =============================================================================
void TrackerViews::draw_pattern_view(struct ui *u, const struct input *in,
                                     ControlSurfaceState &cs) {
  auto &fb = FurnaceBridge::instance();
  auto &eng = fb.engine();

  const char *labels_norm[3] = {
      cs.compact_pattern_view ? "Full 3Ch" : "Compact8", "CopyPat", "PastePat"};
  const char *labels_shift[3] = {"ClonePat", "Transp+1", "ClearPat"};
  int active_soft = -1;
  int sk = ui_soft_keys(
      u, 4, 2, 312, 20, cs.shift_down ? labels_shift : labels_norm,
      &active_soft, make_soft_style(cs.shift_down), in, &soft_state_);

  if (sk == 0) {
    if (cs.shift_down)
      fb.clone_pattern(cs.cur_order, cs.cur_chan);
    else
      cs.compact_pattern_view = !cs.compact_pattern_view;
  } else if (sk == 1) {
    if (cs.shift_down)
      fb.transpose_pattern(cs.cur_order, cs.cur_chan, 1);
    else
      fb.copy_pattern(cs.cur_order, cs.cur_chan);
  } else if (sk == 2) {
    if (cs.shift_down)
      fb.clear_pattern(cs.cur_order, cs.cur_chan);
    else
      fb.paste_pattern(cs.cur_order, cs.cur_chan);
  }

  draw_header(u, cs, "Ins", "Vol", "Step");

  if (!eng.curSubSong || !eng.curOrders || !eng.curPat)
    return;
  int pat_len = std::max(1, eng.curSubSong->patLen);
  int nch = std::max(1, eng.getTotalChannelCount());

  // Handle Encoder Press -> toggle cell value edit lock
  if (in && in->encoder_press) {
    cs.cell_edit_locked = !cs.cell_edit_locked;
    fb.status_msg =
        cs.cell_edit_locked ? "Cell Edit Locked" : "Row Scroll Mode";
  }

  // Handle ERASE button on current cell/row
  if (in && (in->down & (1u << IN_ERASE))) {
    int col_map[5] = {DIV_PAT_NOTE, DIV_PAT_INS, DIV_PAT_VOL, DIV_PAT_FX(0),
                      DIV_PAT_FXVAL(0)};
    if (cs.shift_down) {
      fb.clear_row(cs.cur_order, cs.cur_chan, cs.cur_row);
      fb.status_msg = "Cleared Row";
    } else {
      fb.set_cell(cs.cur_order, cs.cur_chan, cs.cur_row,
                  col_map[cs.cur_col % 5], -1);
      fb.status_msg = "Cleared Cell";
    }
  }

  // Handle Knobs K1 (Ins), K2 (Vol), K3 (Edit Step or FX)
  int d_k1 = cs.knobs.consume(in, 0, cs.shift_down ? 8 : 3);
  int d_k2 = cs.knobs.consume(in, 1, cs.shift_down ? 8 : 3);
  int d_k3 = cs.knobs.consume(in, 2, cs.shift_down ? 8 : 3);

  if (d_k1 != 0) {
    int nins = std::max(1, static_cast<int>(eng.song.ins.size()));
    cs.cur_ins = std::max(0, std::min(nins - 1, cs.cur_ins + d_k1));
    if (cs.cell_edit_locked) {
      fb.set_cell(cs.cur_order, cs.cur_chan, cs.cur_row, DIV_PAT_INS,
                  static_cast<short>(cs.cur_ins));
    }
  }
  if (d_k2 != 0) {
    int max_v = std::max(1, eng.getMaxVolumeChan(cs.cur_chan));
    short cur_v =
        fb.get_cell(cs.cur_order, cs.cur_chan, cs.cur_row, DIV_PAT_VOL);
    if (cur_v < 0)
      cur_v = static_cast<short>(max_v);
    cur_v = static_cast<short>(
        std::max(0, std::min(max_v, static_cast<int>(cur_v) + d_k2)));
    fb.set_cell(cs.cur_order, cs.cur_chan, cs.cur_row, DIV_PAT_VOL, cur_v);
  }
  if (d_k3 != 0) {
    cs.edit_step = std::max(0, std::min(16, cs.edit_step + d_k3));
    fb.status_msg = "Edit Step: " + std::to_string(cs.edit_step);
  }

  // Handle Encoder rotation
  if (in && in->encoder != 0) {
    if (cs.cell_edit_locked) {
      int col_map[5] = {DIV_PAT_NOTE, DIV_PAT_INS, DIV_PAT_VOL, DIV_PAT_FX(0),
                        DIV_PAT_FXVAL(0)};
      int target_col = col_map[cs.cur_col % 5];
      short v = fb.get_cell(cs.cur_order, cs.cur_chan, cs.cur_row, target_col);
      if (cs.cur_col == 0) {
        int step = cs.shift_down ? 12 : 1;
        if (v < 0 || v > 179)
          v = static_cast<short>(60 + cs.base_octave * 12);
        else
          v = static_cast<short>(std::max(
              0, std::min(179, static_cast<int>(v) + in->encoder * step)));
      } else if (cs.cur_col == 1) {
        int nins = std::max(1, static_cast<int>(eng.song.ins.size()));
        if (v < 0)
          v = static_cast<short>(cs.cur_ins);
        else
          v = static_cast<short>(std::max(
              0, std::min(nins - 1, static_cast<int>(v) + in->encoder)));
      } else if (cs.cur_col == 2) {
        int max_v = std::max(1, eng.getMaxVolumeChan(cs.cur_chan));
        if (v < 0)
          v = static_cast<short>(max_v);
        else
          v = static_cast<short>(
              std::max(0, std::min(max_v, static_cast<int>(v) + in->encoder)));
      } else {
        int step = cs.shift_down ? 16 : 1;
        if (v < 0)
          v = 0;
        else
          v = static_cast<short>((v + in->encoder * step) & 0xff);
      }
      fb.set_cell(cs.cur_order, cs.cur_chan, cs.cur_row, target_col, v);
    } else {
      int step = cs.shift_down
                     ? std::max(1, static_cast<int>(eng.curSubSong->hilightA))
                     : 1;
      cs.cur_row = (cs.cur_row + in->encoder * step) % pat_len;
      if (cs.cur_row < 0)
        cs.cur_row += pat_len;
    }
  }

  int display_order = fb.is_playing() ? eng.getOrder() : cs.cur_order;
  int play_row = fb.is_playing() ? eng.getRow() : -1;
  int center_row =
      (fb.is_playing() && cs.follow_playhead) ? play_row : cs.cur_row;

  const int visible_rows = 13;
  int start_row = center_row - (visible_rows / 2);
  if (start_row + visible_rows > pat_len)
    start_row = pat_len - visible_rows;
  if (start_row < 0)
    start_row = 0;

  int vis_chans = cs.compact_pattern_view ? std::min(8, nch) : std::min(3, nch);
  int first_chan = cs.cur_chan - (vis_chans / 2);
  if (first_chan + vis_chans > nch)
    first_chan = nch - vis_chans;
  if (first_chan < 0)
    first_chan = 0;

  int gutter_w = cs.compact_pattern_view ? 18 : 22;
  int chan_w = (UI_W - gutter_w - 2) / std::max(1, vis_chans);

  // Draw Channel Headers (y = 40..54)
  ui_box(u, 0, 40, gutter_w, 14,
         (struct ui_paint){.fill_set = 1, .fill = COL_PANEL});
  ui_text(u, 2, 41, "RW", COL_MUTED, 1);

  for (int vc = 0; vc < vis_chans; vc++) {
    int ch = first_chan + vc;
    int cx = gutter_w + vc * chan_w;
    bool is_cur = (ch == cs.cur_chan);
    bool muted = eng.isChannelMuted(ch);
    int pat_id = eng.curOrders->ord[ch][display_order];

    ui_box(u, cx, 40, chan_w - 1, 14,
           (struct ui_paint){.fill_set = 1,
                             .fill = is_cur
                                         ? 0x263858u
                                         : static_cast<uint32_t>(COL_PANEL_ALT),
                             .border_set = is_cur ? 1 : 0,
                             .border = COL_HL,
                             .border_px = 1});

    char ch_hdr[24];
    const char *sname = eng.getChannelShortName(ch);
    if (!sname || !sname[0])
      sname = "CH";
    if (cs.compact_pattern_view) {
      std::snprintf(ch_hdr, sizeof(ch_hdr), "%s%s", sname, muted ? "M" : "");
    } else {
      std::snprintf(ch_hdr, sizeof(ch_hdr), "%d:%.4s P%02X%s", ch + 1, sname,
                    pat_id, muted ? " M" : "");
    }
    ui_text(u, cx + 2, 41, ch_hdr,
            muted ? COL_REC : (is_cur ? COL_HL : COL_TEXT), 1);
  }

  // Draw Tracker Rows (y = 55..221)
  int hilight_a = std::max(1, static_cast<int>(eng.curSubSong->hilightA));
  int hilight_b = std::max(1, static_cast<int>(eng.curSubSong->hilightB));

  for (int vr = 0; vr < visible_rows; vr++) {
    int r = start_row + vr;
    if (r >= pat_len)
      break;
    int ry = 55 + vr * 13;

    bool is_play = (r == play_row);
    bool is_cur_row = (r == cs.cur_row);
    uint32_t row_bg = COL_BG;
    if ((r % hilight_b) == 0)
      row_bg = COL_BAR_ROW;
    else if ((r % hilight_a) == 0)
      row_bg = COL_BEAT_ROW;
    if (is_cur_row)
      row_bg = COL_CURSOR_ROW;
    if (is_play)
      row_bg = COL_PLAYHEAD;

    // Row number gutter
    ui_box(u, 0, ry, gutter_w - 1, 12,
           (struct ui_paint){.fill_set = 1, .fill = row_bg});
    char rbuf[8];
    std::snprintf(rbuf, sizeof(rbuf), "%02X", r);
    ui_text(u, 2, ry, rbuf,
            is_play ? COL_HL : ((r % hilight_a) == 0 ? COL_TEXT : COL_MUTED),
            1);

    for (int vc = 0; vc < vis_chans; vc++) {
      int ch = first_chan + vc;
      int cx = gutter_w + vc * chan_w;
      ui_box(u, cx, ry, chan_w - 1, 12,
             (struct ui_paint){.fill_set = 1, .fill = row_bg});

      int pat_id = eng.curOrders->ord[ch][display_order];
      DivPattern *pat = eng.curPat[ch].getPattern(pat_id, false);

      short note = pat ? pat->newData[r][DIV_PAT_NOTE] : -1;
      short ins = pat ? pat->newData[r][DIV_PAT_INS] : -1;
      short vol = pat ? pat->newData[r][DIV_PAT_VOL] : -1;
      short fx = pat ? pat->newData[r][DIV_PAT_FX(0)] : -1;
      short fxv = pat ? pat->newData[r][DIV_PAT_FXVAL(0)] : -1;

      char nstr[4];
      FurnaceBridge::format_note(note, nstr);

      if (cs.compact_pattern_view) {
        ui_text(u, cx + 2, ry, nstr, (note >= 0) ? COL_NOTE : COL_DIM, 1);
        if (ins >= 0) {
          char istr[4];
          std::snprintf(istr, sizeof(istr), "%02X", ins & 0xff);
          ui_text(u, cx + 22, ry, istr, COL_INS, 1);
        }
        if (is_cur_row && ch == cs.cur_chan) {
          ui_highlight(u, cx, ry, chan_w - 1, 12,
                       cs.cell_edit_locked ? COL_LOCK_HL : COL_HL, 1);
        }
      } else {
        char istr[4], vstr[4], fxstr[4], fxvstr[4];
        if (ins >= 0)
          std::snprintf(istr, sizeof(istr), "%02X", ins & 0xff);
        else
          std::memcpy(istr, "..", 3);

        if (vol >= 0)
          std::snprintf(vstr, sizeof(vstr), "%02X", vol & 0xff);
        else
          std::memcpy(vstr, "..", 3);

        if (fx >= 0)
          std::snprintf(fxstr, sizeof(fxstr), "%02X", fx & 0xff);
        else
          std::memcpy(fxstr, "..", 3);

        if (fxv >= 0)
          std::snprintf(fxvstr, sizeof(fxvstr), "%02X", fxv & 0xff);
        else
          std::memcpy(fxvstr, "..", 3);

        ui_text(u, cx + 2, ry, nstr, (note >= 0) ? COL_NOTE : COL_DIM, 1);
        ui_text(u, cx + 24, ry, istr, (ins >= 0) ? COL_INS : COL_DIM, 1);
        ui_text(u, cx + 40, ry, vstr, (vol >= 0) ? COL_VOL : COL_DIM, 1);
        ui_text(u, cx + 56, ry, fxstr, (fx >= 0) ? COL_FX : COL_DIM, 1);
        ui_text(u, cx + 70, ry, fxvstr, (fxv >= 0) ? COL_FXVAL : COL_DIM, 1);

        if (is_cur_row && ch == cs.cur_chan) {
          static const int kColX[5] = {1, 23, 39, 55, 69};
          static const int kColW[5] = {20, 14, 14, 14, 14};
          int cidx = cs.cur_col % 5;
          ui_highlight(u, cx + kColX[cidx], ry - 1, kColW[cidx], 13,
                       cs.cell_edit_locked ? COL_LOCK_HL : COL_HL, 1);
        }
      }
    }
  }

  draw_footer(u, cs);
}

// =============================================================================
// 2. ORDER MATRIX & SONG VIEW
// =============================================================================
void TrackerViews::draw_orders_view(struct ui *u, const struct input *in,
                                    ControlSurfaceState &cs) {
  auto &fb = FurnaceBridge::instance();
  auto &eng = fb.engine();

  const char *labels_norm[3] = {"Add Ord", "Dup Ord", "Del Ord"};
  const char *labels_shift[3] = {
      "DeepCln", "Move Up", eng.getRepeatPattern() ? "Loop:ON" : "Loop:OFF"};
  int active_soft = -1;
  int sk = ui_soft_keys(
      u, 4, 2, 312, 20, cs.shift_down ? labels_shift : labels_norm,
      &active_soft, make_soft_style(cs.shift_down), in, &soft_state_);

  if (eng.curSubSong) {
    if (sk == 0) {
      if (cs.shift_down) {
        eng.deepCloneOrder(cs.cur_order, true);
        fb.status_msg = "Deep Cloned Order";
      } else {
        eng.addOrder(cs.cur_order, false, true);
        fb.status_msg = "Inserted Order";
      }
      fb.dirty = true;
    } else if (sk == 1) {
      if (cs.shift_down) {
        eng.moveOrderUp(cs.cur_order);
        fb.status_msg = "Moved Order Up";
      } else {
        eng.addOrder(cs.cur_order, true, true);
        fb.status_msg = "Duplicated Order";
      }
      fb.dirty = true;
    } else if (sk == 2) {
      if (cs.shift_down) {
        eng.setRepeatPattern(!eng.getRepeatPattern());
        fb.status_msg =
            eng.getRepeatPattern() ? "Pattern Loop ON" : "Pattern Loop OFF";
      } else if (eng.curSubSong->ordersLen > 1) {
        eng.deleteOrder(cs.cur_order);
        if (cs.cur_order >= eng.curSubSong->ordersLen)
          cs.cur_order = eng.curSubSong->ordersLen - 1;
        fb.dirty = true;
        fb.status_msg = "Deleted Order";
      }
    }
  }

  draw_header(u, cs, "PatID", "Speed", "PatLen");

  if (!eng.curSubSong || !eng.curOrders)
    return;
  int ord_len = std::max(1, eng.curSubSong->ordersLen);
  int nch = std::max(1, eng.getTotalChannelCount());

  if (in && in->encoder_press) {
    cs.cell_edit_locked = !cs.cell_edit_locked;
  }

  int d_k1 = cs.knobs.consume(in, 0, 3);
  int d_k2 = cs.knobs.consume(in, 1, 3);
  int d_k3 = cs.knobs.consume(in, 2, 3);

  if (d_k1 != 0) {
    int cur_p = eng.curOrders->ord[cs.cur_chan][cs.cur_order];
    cur_p = std::max(0, std::min(DIV_MAX_PATTERNS - 1, cur_p + d_k1));
    eng.curOrders->ord[cs.cur_chan][cs.cur_order] =
        static_cast<unsigned char>(cur_p);
    fb.dirty = true;
  }
  if (d_k2 != 0) {
    if (cs.shift_down) {
      float hz = std::max(10.0f, std::min(240.0f, eng.curSubSong->hz + d_k2));
      eng.setSongRate(hz);
    } else {
      int s = static_cast<int>(eng.curSubSong->speeds.val[0]) + d_k2;
      eng.curSubSong->speeds.val[0] =
          static_cast<unsigned short>(std::max(1, std::min(64, s)));
    }
    fb.dirty = true;
  }
  if (d_k3 != 0) {
    int step = cs.shift_down ? 1 : 4;
    eng.curSubSong->patLen =
        std::max(4, std::min(256, eng.curSubSong->patLen + d_k3 * step));
    fb.dirty = true;
  }

  if (in && in->encoder != 0) {
    if (cs.cell_edit_locked) {
      int cur_p = eng.curOrders->ord[cs.cur_chan][cs.cur_order];
      cur_p = std::max(0, std::min(DIV_MAX_PATTERNS - 1, cur_p + in->encoder));
      eng.curOrders->ord[cs.cur_chan][cs.cur_order] =
          static_cast<unsigned char>(cur_p);
      fb.dirty = true;
    } else {
      cs.cur_order = (cs.cur_order + in->encoder) % ord_len;
      if (cs.cur_order < 0)
        cs.cur_order += ord_len;
    }
  }

  // Left Matrix (x = 4..206, y = 42..220)
  int vis_ch = std::min(7, nch);
  int first_ch = cs.cur_chan - (vis_ch / 2);
  if (first_ch + vis_ch > nch)
    first_ch = nch - vis_ch;
  if (first_ch < 0)
    first_ch = 0;

  ui_box(u, 4, 42, 204, 178,
         (struct ui_paint){.fill_set = 1, .fill = COL_PANEL});
  ui_text(u, 8, 44, "OR", COL_MUTED, 1);
  for (int vc = 0; vc < vis_ch; vc++) {
    int ch = first_ch + vc;
    char ch_lbl[8];
    std::snprintf(ch_lbl, sizeof(ch_lbl), "C%d", ch + 1);
    ui_text(u, 28 + vc * 25, 44, ch_lbl,
            (ch == cs.cur_chan) ? COL_HL : COL_MUTED, 1);
  }

  const int vis_ords = 12;
  int start_ord = cs.cur_order - (vis_ords / 2);
  if (start_ord + vis_ords > ord_len)
    start_ord = ord_len - vis_ords;
  if (start_ord < 0)
    start_ord = 0;

  int play_ord = fb.is_playing() ? eng.getOrder() : -1;

  for (int vo = 0; vo < vis_ords; vo++) {
    int o = start_ord + vo;
    if (o >= ord_len)
      break;
    int oy = 58 + vo * 13;

    uint32_t row_bg = (o == play_ord) ? COL_PLAYHEAD
                                      : ((o == cs.cur_order) ? COL_CURSOR_ROW
                                                             : COL_PANEL_ALT);
    ui_box(u, 6, oy, 200, 12, (struct ui_paint){.fill_set = 1, .fill = row_bg});

    char obuf[8];
    std::snprintf(obuf, sizeof(obuf), "%02X", o);
    ui_text(u, 8, oy, obuf, (o == play_ord) ? COL_HL : COL_MUTED, 1);

    for (int vc = 0; vc < vis_ch; vc++) {
      int ch = first_ch + vc;
      int cx = 26 + vc * 25;
      int pid = eng.curOrders->ord[ch][o];
      char pbuf[8];
      std::snprintf(pbuf, sizeof(pbuf), "%02X", pid);
      ui_text(u, cx + 2, oy, pbuf,
              (ch == cs.cur_chan && o == cs.cur_order) ? COL_HL : COL_TEXT, 1);
      if (ch == cs.cur_chan && o == cs.cur_order) {
        ui_highlight(u, cx, oy - 1, 18, 13,
                     cs.cell_edit_locked ? COL_LOCK_HL : COL_HL, 1);
      }
    }
  }

  // Right Song Info Panel (x = 212..316, y = 42..220)
  ui_box(u, 212, 42, 104, 178,
         (struct ui_paint){.fill_set = 1,
                           .fill = COL_PANEL,
                           .border_set = 1,
                           .border = COL_GRID_LINE,
                           .border_px = 1});
  ui_text(u, 218, 46, "SONG TIMING", COL_HL, 1);

  char info[32];
  std::snprintf(info, sizeof(info), "Orders: %d", ord_len);
  ui_text(u, 218, 64, info, COL_TEXT, 1);

  std::snprintf(info, sizeof(info), "PatLen: %d", eng.curSubSong->patLen);
  ui_text(u, 218, 80, info, COL_TEXT, 1);

  std::snprintf(info, sizeof(info), "Speed1: %d",
                eng.curSubSong->speeds.val[0]);
  ui_text(u, 218, 96, info, COL_TEXT, 1);

  std::snprintf(info, sizeof(info), "RateHz: %.0f", eng.curSubSong->hz);
  ui_text(u, 218, 112, info, COL_TEXT, 1);

  std::snprintf(info, sizeof(info), "VTempo: %d/%d",
                eng.curSubSong->virtualTempoN, eng.curSubSong->virtualTempoD);
  ui_text(u, 218, 128, info, COL_TEXT, 1);

  std::snprintf(info, sizeof(info), "Chans : %d", nch);
  ui_text(u, 218, 144, info, COL_TEXT, 1);

  std::snprintf(info, sizeof(info), "Chips : %d", eng.song.systemLen);
  ui_text(u, 218, 160, info, COL_TEXT, 1);

  ui_text(u, 218, 184, "Press SEQ for", COL_MUTED, 1);
  ui_text(u, 218, 198, "Pattern Grid", COL_MUTED, 1);

  draw_footer(u, cs);
}

// =============================================================================
// 3. INSTRUMENT & SYNTH EDITOR VIEW (FM 4-Op, Macros, Instrument List)
// =============================================================================
void TrackerViews::draw_instrument_view(struct ui *u, const struct input *in,
                                        ControlSurfaceState &cs) {
  auto &fb = FurnaceBridge::instance();
  auto &eng = fb.engine();

  static const char *kTabNames[3] = {"Page:Macro", "Page:List", "Page:Synth"};
  const char *b3_norm = (ins_sub_tab_ == 0)
                            ? "Next Op"
                            : ((ins_sub_tab_ == 1) ? "NextMacro" : "Dup Ins");
  const char *labels_norm[3] = {"New Ins", kTabNames[ins_sub_tab_ % 3],
                                b3_norm};
  const char *labels_shift[3] = {"Dup Ins", "Rename", "Del Ins"};

  int active_soft = -1;
  int sk = ui_soft_keys(
      u, 4, 2, 312, 20, cs.shift_down ? labels_shift : labels_norm,
      &active_soft, make_soft_style(cs.shift_down), in, &soft_state_);

  if (sk == 0) {
    if (cs.shift_down && !eng.song.ins.empty()) {
      auto *copy = new DivInstrument(*eng.getIns(cs.cur_ins));
      copy->name += " Copy";
      cs.cur_ins = std::max(0, eng.addInstrumentPtr(copy) - 1);
      fb.dirty = true;
    } else {
      int idx = eng.addInstrument(cs.cur_chan);
      if (idx >= 0)
        cs.cur_ins = idx;
      fb.dirty = true;
    }
  } else if (sk == 1) {
    if (cs.shift_down && !eng.song.ins.empty()) {
      DivInstrument *ins = eng.getIns(cs.cur_ins);
      std::snprintf(text_buf_, sizeof(text_buf_), "%s", ins->name.c_str());
      ui_text_editor_init(&text_ed_, text_buf_, sizeof(text_buf_),
                          "Rename Instrument");
      text_edit_target_ = 3;
      text_edit_active_ = true;
      cs.modal_lock = true;
      cs.prev_view = cs.active_view;
      cs.active_view = AppView::Menu;
      return;
    } else {
      ins_sub_tab_ = (ins_sub_tab_ + 1) % 3;
      cs.knobs.reset();
    }
  } else if (sk == 2) {
    if (cs.shift_down) {
      if (eng.song.ins.size() > 1) {
        eng.delInstrument(cs.cur_ins);
        if (cs.cur_ins >= static_cast<int>(eng.song.ins.size()))
          cs.cur_ins = static_cast<int>(eng.song.ins.size()) - 1;
        fb.dirty = true;
      }
    } else if (ins_sub_tab_ == 0) {
      fm_selected_op_ = (fm_selected_op_ + 1) & 3;
    } else if (ins_sub_tab_ == 1) {
      macro_target_ = (macro_target_ + 1) % 5;
      macro_cursor_ = 0;
    } else if (!eng.song.ins.empty()) {
      auto *copy = new DivInstrument(*eng.getIns(cs.cur_ins));
      copy->name += " Copy";
      cs.cur_ins = std::max(0, eng.addInstrumentPtr(copy) - 1);
      fb.dirty = true;
    }
  }

  if (eng.song.ins.empty()) {
    eng.addInstrument(0);
  }
  int nins = static_cast<int>(eng.song.ins.size());
  if (cs.cur_ins >= nins)
    cs.cur_ins = nins - 1;
  if (cs.cur_ins < 0)
    cs.cur_ins = 0;
  DivInstrument *ins = eng.getIns(cs.cur_ins);

  if (ins_sub_tab_ == 0) {
    // ---------------------------------------------------------------------
    // Sub-tab 0: FM & Synth Operator Editor
    // ---------------------------------------------------------------------
    static const char *kRowK1[4] = {"ALG", "TL", "D2R", "MUL"};
    static const char *kRowK2[4] = {"FB", "AR", "SL", "DT"};
    static const char *kRowK3[4] = {"FMS", "DR", "RR", "SSG"};
    draw_header(u, cs, kRowK1[fm_param_row_], kRowK2[fm_param_row_],
                kRowK3[fm_param_row_]);

    if (in && in->encoder != 0) {
      if (cs.shift_down) {
        cs.cur_ins = (cs.cur_ins + in->encoder + nins) % nins;
        ins = eng.getIns(cs.cur_ins);
      } else {
        fm_param_row_ = (fm_param_row_ + in->encoder + 4) % 4;
      }
    }
    if (in && in->encoder_press) {
      fm_selected_op_ = (fm_selected_op_ + 1) & 3;
    }

    int d1 = cs.knobs.consume(in, 0, cs.shift_down ? 6 : 2);
    int d2 = cs.knobs.consume(in, 1, cs.shift_down ? 6 : 2);
    int d3 = cs.knobs.consume(in, 2, cs.shift_down ? 6 : 2);

    auto &fm = ins->fm;
    auto &op = fm.op[fm_selected_op_ & 3];

    if (d1 != 0 || d2 != 0 || d3 != 0) {
      if (fm_param_row_ == 0) {
        fm.alg = static_cast<unsigned char>(
            std::max(0, std::min(7, static_cast<int>(fm.alg) + d1)));
        fm.fb = static_cast<unsigned char>(
            std::max(0, std::min(7, static_cast<int>(fm.fb) + d2)));
        fm.fms = static_cast<unsigned char>(
            std::max(0, std::min(7, static_cast<int>(fm.fms) + d3)));
      } else if (fm_param_row_ == 1) {
        op.tl = static_cast<unsigned char>(
            std::max(0, std::min(127, static_cast<int>(op.tl) + d1)));
        op.ar = static_cast<unsigned char>(
            std::max(0, std::min(31, static_cast<int>(op.ar) + d2)));
        op.dr = static_cast<unsigned char>(
            std::max(0, std::min(31, static_cast<int>(op.dr) + d3)));
      } else if (fm_param_row_ == 2) {
        op.d2r = static_cast<unsigned char>(
            std::max(0, std::min(31, static_cast<int>(op.d2r) + d1)));
        op.sl = static_cast<unsigned char>(
            std::max(0, std::min(15, static_cast<int>(op.sl) + d2)));
        op.rr = static_cast<unsigned char>(
            std::max(0, std::min(15, static_cast<int>(op.rr) + d3)));
      } else {
        op.mult = static_cast<unsigned char>(
            std::max(0, std::min(15, static_cast<int>(op.mult) + d1)));
        op.dt = static_cast<unsigned char>(
            std::max(0, std::min(7, static_cast<int>(op.dt) + d2)));
        op.ssgEnv = static_cast<unsigned char>(
            std::max(0, std::min(15, static_cast<int>(op.ssgEnv) + d3)));
      }
      eng.notifyInsChange(cs.cur_ins);
      fb.dirty = true;
    }

    // Top Instrument Card & Operator Tabs
    ui_box(u, 4, 42, 312, 34,
           (struct ui_paint){.fill_set = 1, .fill = COL_PANEL});
    char title[48];
    std::snprintf(title, sizeof(title), "INS %02X: %.20s", cs.cur_ins,
                  ins->name.c_str());
    ui_text(u, 8, 45, title, COL_INS, 1);

    // Carrier mask per algorithm (0..7)
    static const uint8_t kCarrierMask[8] = {0x08, 0x08, 0x08, 0x08,
                                            0x0A, 0x0E, 0x0E, 0x0F};
    uint8_t cmask = kCarrierMask[fm.alg & 7];

    for (int o = 0; o < 4; o++) {
      bool is_sel = (o == fm_selected_op_);
      bool is_car = ((cmask >> o) & 1) != 0;
      int ox = 8 + o * 76;
      ui_box(u, ox, 59, 72, 15,
             (struct ui_paint){.fill_set = 1,
                               .fill = is_sel ? 0x2c3e5au : 0x1c2230u,
                               .border_set = is_sel ? 1 : 0,
                               .border = COL_HL,
                               .border_px = 1});
      char olbl[16];
      std::snprintf(olbl, sizeof(olbl), "OP%d %s", o + 1,
                    is_car ? "CAR" : "MOD");
      ui_text(u, ox + 6, 61, olbl, is_car ? 0xff7858 : COL_INS, 1);
    }

    // 4 Rows of 3 Parameter Boxes (y = 80..220)
    const char *pnames[4][3] = {{"ALG", "FB", "FMS"},
                                {"TL", "AR", "DR"},
                                {"D2R", "SL", "RR"},
                                {"MUL", "DT", "SSG"}};
    int pvals[4][3] = {{fm.alg, fm.fb, fm.fms},
                       {op.tl, op.ar, op.dr},
                       {op.d2r, op.sl, op.rr},
                       {op.mult, op.dt, op.ssgEnv}};
    int pmax[4][3] = {{7, 7, 7}, {127, 31, 31}, {31, 15, 15}, {15, 7, 15}};

    for (int r = 0; r < 4; r++) {
      int ry = 80 + r * 35;
      bool row_focused = (r == fm_param_row_);
      for (int c = 0; c < 3; c++) {
        int cx = 4 + c * 105;
        ui_box(u, cx, ry, 101, 32,
               (struct ui_paint){.fill_set = 1,
                                 .fill = row_focused
                                             ? 0x222c3eu
                                             : static_cast<uint32_t>(COL_PANEL),
                                 .border_set = 1,
                                 .border = row_focused ? COL_HL : COL_GRID_LINE,
                                 .border_px = 1});
        char plbl[24];
        std::snprintf(plbl, sizeof(plbl), "%s: %d", pnames[r][c], pvals[r][c]);
        ui_text(u, cx + 6, ry + 4, plbl, row_focused ? COL_TEXT : COL_MUTED, 1);

        // Bar meter
        int bar_w = (pvals[r][c] * 89) / std::max(1, pmax[r][c]);
        ui_box(u, cx + 6, ry + 20, 89, 6,
               (struct ui_paint){.fill_set = 1, .fill = 0x10141c});
        if (bar_w > 0) {
          ui_box(u, cx + 6, ry + 20, bar_w, 6,
                 (struct ui_paint){.fill_set = 1,
                                   .fill = row_focused ? COL_HL : COL_INS});
        }
      }
    }
  } else if (ins_sub_tab_ == 1) {
    // ---------------------------------------------------------------------
    // Sub-tab 1: Macro Bar Graph Editor
    // ---------------------------------------------------------------------
    draw_header(u, cs, "Val", "Len", "Loop");

    static const char *kMacroNames[5] = {"Volume", "Arpeggio", "Duty/Noise",
                                         "Wavetable", "Pitch"};
    DivInstrumentMacro *mac = &ins->std.volMacro;
    int max_val = 64;
    if (macro_target_ == 1) {
      mac = &ins->std.arpMacro;
      max_val = 48;
    } else if (macro_target_ == 2) {
      mac = &ins->std.dutyMacro;
      max_val = 15;
    } else if (macro_target_ == 3) {
      mac = &ins->std.waveMacro;
      max_val = 31;
    } else if (macro_target_ == 4) {
      mac = &ins->std.pitchMacro;
      max_val = 64;
    }

    int mlen = std::max<int>(1, mac->len);
    if (in && in->encoder != 0) {
      macro_cursor_ = (macro_cursor_ + in->encoder + mlen) % mlen;
    }

    int d1 = cs.knobs.consume(in, 0, 2);
    int d2 = cs.knobs.consume(in, 1, 3);
    int d3 = cs.knobs.consume(in, 2, 3);

    if (d1 != 0) {
      if (mac->len == 0)
        mac->len = 1;
      mac->val[macro_cursor_] =
          std::max(0, std::min(max_val, mac->val[macro_cursor_] + d1));
      eng.notifyInsChange(cs.cur_ins);
      fb.dirty = true;
    }
    if (d2 != 0) {
      mac->len = static_cast<unsigned char>(
          std::max(0, std::min(64, static_cast<int>(mac->len) + d2)));
      if (macro_cursor_ >= mac->len && mac->len > 0)
        macro_cursor_ = mac->len - 1;
      eng.notifyInsChange(cs.cur_ins);
      fb.dirty = true;
    }
    if (d3 != 0) {
      int lp = (mac->loop >= 255) ? -1 : static_cast<int>(mac->loop);
      lp = std::max(-1, std::min(static_cast<int>(mac->len) - 1, lp + d3));
      mac->loop = (lp < 0) ? 255 : static_cast<unsigned char>(lp);
      eng.notifyInsChange(cs.cur_ins);
      fb.dirty = true;
    }

    ui_box(u, 4, 42, 312, 178,
           (struct ui_paint){.fill_set = 1, .fill = COL_PANEL});
    char hdr[64];
    std::snprintf(hdr, sizeof(hdr), "Macro: %s  Len:%d  Loop:%s  Step[%02d]=%d",
                  kMacroNames[macro_target_], mac->len,
                  (mac->loop < mac->len) ? std::to_string(mac->loop).c_str()
                                         : "Off",
                  macro_cursor_, (mac->len > 0) ? mac->val[macro_cursor_] : 0);
    ui_text(u, 8, 46, hdr, COL_HL, 1);

    // Draw 16 visible macro bars
    int start_s = (macro_cursor_ / 16) * 16;
    int graph_x = 12, graph_y = 66, graph_w = 296, graph_h = 136;
    ui_box(u, graph_x, graph_y, graph_w, graph_h,
           (struct ui_paint){.fill_set = 1,
                             .fill = 0x10141c,
                             .border_set = 1,
                             .border = COL_GRID_LINE,
                             .border_px = 1});

    int bar_step = graph_w / 16;
    for (int i = 0; i < 16; i++) {
      int idx = start_s + i;
      if (idx >= mac->len)
        break;
      int v = std::max(0, std::min(max_val, mac->val[idx]));
      int bh = (v * (graph_h - 12)) / std::max(1, max_val);
      int bx = graph_x + 4 + i * bar_step;
      int by = graph_y + graph_h - 4 - bh;
      bool is_cur = (idx == macro_cursor_);
      bool is_loop = (idx == mac->loop);
      if (bh > 0) {
        ui_box(u, bx, by, bar_step - 4, bh,
               (struct ui_paint){
                   .fill_set = 1,
                   .fill = is_cur ? COL_HL : (is_loop ? COL_PLAY : COL_INS)});
      }
      if (is_cur) {
        ui_highlight(u, bx - 1, graph_y + 2, bar_step - 2, graph_h - 4, COL_HL,
                     1);
      }
    }
  } else {
    // ---------------------------------------------------------------------
    // Sub-tab 2: Instrument List
    // ---------------------------------------------------------------------
    draw_header(u, cs, "Select", "-", "-");
    if (in && in->encoder != 0) {
      cs.cur_ins = (cs.cur_ins + in->encoder + nins) % nins;
    }
    ui_box(u, 4, 42, 312, 178,
           (struct ui_paint){.fill_set = 1, .fill = COL_PANEL});

    int start_i = std::max(0, cs.cur_ins - 5);
    for (int row = 0; row < 11; row++) {
      int idx = start_i + row;
      if (idx >= nins)
        break;
      DivInstrument *item = eng.getIns(idx);
      int ry = 46 + row * 15;
      bool sel = (idx == cs.cur_ins);
      ui_box(
          u, 8, ry, 304, 14,
          (struct ui_paint){.fill_set = 1,
                            .fill = sel ? COL_CURSOR_ROW
                                        : static_cast<uint32_t>(COL_PANEL_ALT),
                            .border_set = sel ? 1 : 0,
                            .border = COL_HL,
                            .border_px = 1});
      char line[56];
      std::snprintf(line, sizeof(line), "%02X: %-28s [T:%d]", idx,
                    item->name.c_str(), static_cast<int>(item->type));
      ui_text(u, 12, ry + 1, line, sel ? COL_HL : COL_TEXT, 1);
    }
  }

  draw_footer(u, cs);
}

// =============================================================================
// 4. CHANNEL MIXER & OSCILLOSCOPE VIEW
// =============================================================================
void TrackerViews::draw_mixer_view(struct ui *u, const struct input *in,
                                   ControlSurfaceState &cs) {
  auto &fb = FurnaceBridge::instance();
  auto &eng = fb.engine();

  const char *labels_norm[3] = {"Mute Ch", "Solo Ch", "UnmuteAll"};
  const char *labels_shift[3] = {osc_per_channel_ ? "Osc:Master" : "Osc:Chan",
                                 "ResetVol", "ResetPan"};
  int active_soft = -1;
  int sk = ui_soft_keys(
      u, 4, 2, 312, 20, cs.shift_down ? labels_shift : labels_norm,
      &active_soft, make_soft_style(cs.shift_down), in, &soft_state_);

  if (sk == 0) {
    if (cs.shift_down)
      osc_per_channel_ = !osc_per_channel_;
    else
      eng.toggleMute(cs.cur_chan);
  } else if (sk == 1) {
    if (cs.shift_down) {
      int chip = eng.song.dispatchOfChan[cs.cur_chan];
      if (chip >= 0 && chip < eng.song.systemLen)
        eng.song.systemVol[chip] = 1.0f;
    } else {
      eng.toggleSolo(cs.cur_chan);
    }
  } else if (sk == 2) {
    if (cs.shift_down) {
      int chip = eng.song.dispatchOfChan[cs.cur_chan];
      if (chip >= 0 && chip < eng.song.systemLen)
        eng.song.systemPan[chip] = 0.0f;
    } else {
      eng.unmuteAll();
    }
  }

  draw_header(u, cs, "ChipVol", "ChipPan", "OscZoom");

  int nch = std::max(1, eng.getTotalChannelCount());
  if (in && in->encoder != 0) {
    cs.cur_chan = (cs.cur_chan + in->encoder + nch) % nch;
  }
  if (in && in->encoder_press) {
    eng.toggleMute(cs.cur_chan);
  }

  int d1 = cs.knobs.consume(in, 0, 2);
  int d2 = cs.knobs.consume(in, 1, 2);
  int d3 = cs.knobs.consume(in, 2, 4);

  int cur_chip = eng.song.dispatchOfChan[cs.cur_chan];
  if (d1 != 0 && cur_chip >= 0 && cur_chip < eng.song.systemLen) {
    eng.song.systemVol[cur_chip] = std::max(
        0.0f, std::min(2.0f, eng.song.systemVol[cur_chip] + d1 * 0.05f));
    fb.dirty = true;
  }
  if (d2 != 0 && cur_chip >= 0 && cur_chip < eng.song.systemLen) {
    eng.song.systemPan[cur_chip] = std::max(
        -1.0f, std::min(1.0f, eng.song.systemPan[cur_chip] + d2 * 0.05f));
    fb.dirty = true;
  }
  if (d3 != 0) {
    osc_zoom_ = std::max(1, std::min(8, osc_zoom_ + d3));
  }

  // Top Oscilloscope Box (x = 4..316, y = 42..102, h = 60)
  int ox = 4, oy = 42, ow = 312, oh = 60;
  ui_box(u, ox, oy, ow, oh,
         (struct ui_paint){.fill_set = 1,
                           .fill = 0x0c1016,
                           .border_set = 1,
                           .border = COL_GRID_LINE,
                           .border_px = 1});
  int mid_y = oy + oh / 2;
  ui_box(u, ox + 1, mid_y, ow - 2, 1,
         (struct ui_paint){.fill_set = 1, .fill = 0x202838});

  if (osc_per_channel_) {
    DivDispatchOscBuffer *cb = eng.getOscBuffer(cs.cur_chan);
    if (cb) {
      unsigned short needle = cb->needle;
      for (int x = 0; x < ow - 4; x++) {
        unsigned short idx =
            static_cast<unsigned short>(needle - (ow - 4 - x) * osc_zoom_);
        short s = cb->data[idx];
        int y = mid_y - (s * (oh / 2 - 4)) / 32768;
        y = std::max(oy + 2, std::min(oy + oh - 3, y));
        ui_box(u, ox + 2 + x, y, 2, 2,
               (struct ui_paint){.fill_set = 1, .fill = COL_INS});
      }
    }
  } else if (eng.oscBuf[0] && eng.oscBuf[1]) {
    int wp = eng.oscWritePos;
    int prev_y = mid_y;
    for (int x = 0; x < ow - 4; x++) {
      int idx = (wp - (ow - 4 - x) * osc_zoom_) & 0x7fff;
      float s = 0.5f * (eng.oscBuf[0][idx] + eng.oscBuf[1][idx]);
      int y = mid_y - static_cast<int>(s * (oh / 2 - 4));
      y = std::max(oy + 2, std::min(oy + oh - 3, y));
      int y0 = std::min(prev_y, y);
      int yh = std::max(1, std::abs(y - prev_y) + 1);
      ui_box(u, ox + 2 + x, y0, 1, yh,
             (struct ui_paint){.fill_set = 1, .fill = COL_PLAY});
      prev_y = y;
    }
  }
  ui_text(u, ox + 4, oy + 2,
          osc_per_channel_ ? "CHAN OSC" : "MASTER OSC (Pads 1-16 = Mute)",
          COL_MUTED, 1);

  // Channel Mixer Strips (y = 105..221)
  int vis_rows = 8;
  int start_ch = std::max(
      0, std::min(std::max(0, nch - vis_rows), cs.cur_chan - vis_rows / 2));

  for (int i = 0; i < vis_rows; i++) {
    int ch = start_ch + i;
    if (ch >= nch)
      break;
    int ry = 105 + i * 14;
    bool is_cur = (ch == cs.cur_chan);
    bool muted = eng.isChannelMuted(ch);
    DivChannelState *cst = eng.getChanState(ch);

    ui_box(u, 4, ry, 312, 13,
           (struct ui_paint){.fill_set = 1,
                             .fill = is_cur ? COL_CURSOR_ROW
                                            : static_cast<uint32_t>(COL_PANEL),
                             .border_set = is_cur ? 1 : 0,
                             .border = COL_HL,
                             .border_px = 1});

    char nstr[4];
    FurnaceBridge::format_note(
        (cst && cst->keyOn) ? static_cast<short>(cst->note) : -1, nstr);

    int chip = eng.song.dispatchOfChan[ch];
    float cvol = (chip >= 0 && chip < eng.song.systemLen)
                     ? eng.song.systemVol[chip]
                     : 1.0f;

    char row_txt[56];
    std::snprintf(row_txt, sizeof(row_txt),
                  "%02d %-10.10s %s I:%02X V:%.0f%% %s", ch + 1,
                  eng.getChannelName(ch), nstr,
                  (cst && cst->lastIns >= 0) ? (cst->lastIns & 0xff) : 0,
                  cvol * 100.0f, muted ? "[MUTE]" : "[ON]  ");
    ui_text(u, 8, ry + 1, row_txt,
            muted ? COL_REC : (is_cur ? COL_HL : COL_TEXT), 1);
  }
  // Clear keyHit flags after visual readout
  for (int c = 0; c < nch; c++)
    eng.keyHit[c] = false;

  draw_footer(u, cs);
}

// =============================================================================
// 5. SAMPLE & WAVETABLE VIEW
// =============================================================================
void TrackerViews::draw_sample_wave_view(struct ui *u, const struct input *in,
                                         ControlSurfaceState &cs) {
  auto &fb = FurnaceBridge::instance();
  auto &eng = fb.engine();

  const char *labels_norm[3] = {wave_mode_ ? "Mode:Sample" : "Mode:Wave",
                                wave_mode_ ? "New Wave" : "Gen Synth",
                                wave_mode_ ? "PresetShape" : "LoopToggle"};
  const char *labels_shift[3] = {"ImportWAV", "Clear", "Audition"};

  int active_soft = -1;
  int sk = ui_soft_keys(
      u, 4, 2, 312, 20, cs.shift_down ? labels_shift : labels_norm,
      &active_soft, make_soft_style(cs.shift_down), in, &soft_state_);

  if (sk == 0) {
    if (cs.shift_down) {
      cs.prev_view = cs.active_view;
      cs.active_view = AppView::Browser;
      return;
    }
    wave_mode_ = !wave_mode_;
    cs.knobs.reset();
  } else if (sk == 1) {
    if (wave_mode_) {
      int idx = eng.addWave();
      if (idx >= 0)
        cs.cur_wave = idx;
      fb.dirty = true;
    } else {
      // Generate a rich synth/drum PCM sample so the user can test sample
      // playback right away
      auto *s = new DivSample;
      s->name = "MPC Synth Hit";
      s->legacyRate = 22050;
      s->centerRate = 22050;
      s->depth = DIV_SAMPLE_DEPTH_16BIT;
      s->init(4096);
      for (int i = 0; i < 4096; i++) {
        float env = 1.0f - (static_cast<float>(i) / 4096.0f);
        float phase = std::sin(i * 0.08f + std::sin(i * 0.02f) * 2.0f);
        s->data16[i] = static_cast<short>(phase * env * 26000.0f);
      }
      cs.cur_sample = eng.addSamplePtr(s);
      fb.dirty = true;
      fb.status_msg = "Generated MPC Synth Sample";
    }
  } else if (sk == 2) {
    if (wave_mode_) {
      if (eng.song.wave.empty())
        eng.addWave();
      DivWavetable *w = eng.getWave(cs.cur_wave);
      wave_preset_idx_ = (wave_preset_idx_ + 1) % 4;
      for (int i = 0; i < w->len; i++) {
        float t = static_cast<float>(i) / std::max(1, w->len);
        if (wave_preset_idx_ == 0)
          w->data[i] =
              static_cast<int>((0.5f + 0.5f * std::sin(t * 6.28318f)) * w->max);
        else if (wave_preset_idx_ == 1)
          w->data[i] = static_cast<int>(t * w->max);
        else if (wave_preset_idx_ == 2)
          w->data[i] = (i < w->len / 2) ? w->max : 0;
        else
          w->data[i] = (i < w->len / 2) ? (i * 2 * w->max / w->len)
                                        : ((w->len - i) * 2 * w->max / w->len);
      }
      eng.notifyWaveChange(cs.cur_wave);
      fb.dirty = true;
    } else if (!eng.song.sample.empty()) {
      DivSample *s = eng.getSample(cs.cur_sample);
      s->loop = !s->loop;
      if (s->loop && s->loopEnd <= s->loopStart) {
        s->loopStart = 0;
        s->loopEnd = s->samples;
        s->loopMode = DIV_SAMPLE_LOOP_FORWARD;
      }
      eng.renderSamplesP(cs.cur_sample);
      fb.dirty = true;
    }
  }

  if (wave_mode_) {
    draw_header(u, cs, "StepVal", "WaveLen", "WaveMax");
    if (eng.song.wave.empty())
      eng.addWave();
    int nwaves = static_cast<int>(eng.song.wave.size());
    if (cs.cur_wave >= nwaves)
      cs.cur_wave = nwaves - 1;
    DivWavetable *w = eng.getWave(cs.cur_wave);
    int wlen = std::max(1, w->len);

    if (in && in->encoder != 0) {
      if (cs.shift_down)
        cs.cur_wave = (cs.cur_wave + in->encoder + nwaves) % nwaves;
      else
        wave_cursor_ = (wave_cursor_ + in->encoder + wlen) % wlen;
    }

    int d1 = cs.knobs.consume(in, 0, 2);
    int d2 = cs.knobs.consume(in, 1, 4);
    int d3 = cs.knobs.consume(in, 2, 2);

    if (d1 != 0) {
      w->data[wave_cursor_] =
          std::max(0, std::min(w->max, w->data[wave_cursor_] + d1));
      eng.notifyWaveChange(cs.cur_wave);
      fb.dirty = true;
    }
    if (d2 != 0) {
      w->len = std::max(4, std::min(64, w->len + d2 * 4));
      if (wave_cursor_ >= w->len)
        wave_cursor_ = w->len - 1;
      eng.notifyWaveChange(cs.cur_wave);
      fb.dirty = true;
    }
    if (d3 != 0) {
      w->max = std::max(1, std::min(255, w->max + d3));
      eng.notifyWaveChange(cs.cur_wave);
      fb.dirty = true;
    }

    ui_box(u, 4, 42, 312, 178,
           (struct ui_paint){.fill_set = 1, .fill = COL_PANEL});
    char hdr[64];
    std::snprintf(hdr, sizeof(hdr),
                  "WAVE %02X/%02X  Len:%d  Max:%d  Pos[%02d]=%d", cs.cur_wave,
                  nwaves - 1, w->len, w->max, wave_cursor_,
                  w->data[wave_cursor_]);
    ui_text(u, 8, 46, hdr, COL_HL, 1);

    int gx = 12, gy = 66, gw = 296, gh = 140;
    ui_box(u, gx, gy, gw, gh,
           (struct ui_paint){.fill_set = 1,
                             .fill = 0x0c1016,
                             .border_set = 1,
                             .border = COL_GRID_LINE,
                             .border_px = 1});
    for (int i = 0; i < w->len; i++) {
      int bx = gx + 2 + (i * (gw - 4)) / w->len;
      int bw = std::max(2, (gw - 4) / w->len - 1);
      int bh = (w->data[i] * (gh - 8)) / std::max(1, w->max);
      int by = gy + gh - 4 - bh;
      ui_box(u, bx, by, bw, std::max(2, bh),
             (struct ui_paint){.fill_set = 1,
                               .fill = (i == wave_cursor_) ? COL_HL : COL_INS});
    }
  } else {
    draw_header(u, cs, "Rate", "LoopSt", "LoopEnd");
    int nsamp = static_cast<int>(eng.song.sample.size());
    if (cs.cur_sample >= nsamp)
      cs.cur_sample = std::max(0, nsamp - 1);

    if (in && in->encoder != 0 && nsamp > 0) {
      cs.cur_sample = (cs.cur_sample + in->encoder + nsamp) % nsamp;
    }

    ui_box(u, 4, 42, 312, 178,
           (struct ui_paint){.fill_set = 1, .fill = COL_PANEL});

    if (nsamp == 0) {
      ui_text(u, 16, 80, "No samples in song.", COL_TEXT, 1);
      ui_text(u, 16, 100, "Press B2 (Gen Synth) to create a sample,", COL_MUTED,
              1);
      ui_text(u, 16, 116, "or Shift+B1 to import a .wav file.", COL_MUTED, 1);
    } else {
      DivSample *s = eng.getSample(cs.cur_sample);
      int d1 = cs.knobs.consume(in, 0, 2);
      int d2 = cs.knobs.consume(in, 1, 2);
      int d3 = cs.knobs.consume(in, 2, 2);

      if (d1 != 0) {
        s->centerRate =
            std::max(1000, std::min(96000, s->centerRate + d1 * 250));
        s->legacyRate = s->centerRate;
        eng.renderSamplesP(cs.cur_sample);
        fb.dirty = true;
      }
      if (d2 != 0 && s->samples > 0) {
        int step = std::max(1, static_cast<int>(s->samples) / 64);
        s->loopStart = std::max(0, std::min(static_cast<int>(s->samples) - 1,
                                            s->loopStart + d2 * step));
        eng.renderSamplesP(cs.cur_sample);
        fb.dirty = true;
      }
      if (d3 != 0 && s->samples > 0) {
        int step = std::max(1, static_cast<int>(s->samples) / 64);
        s->loopEnd =
            std::max(s->loopStart + 1, std::min(static_cast<int>(s->samples),
                                                s->loopEnd + d3 * step));
        eng.renderSamplesP(cs.cur_sample);
        fb.dirty = true;
      }

      char hdr[64];
      std::snprintf(hdr, sizeof(hdr), "SAMP %02X: %.16s  %dHz  %s",
                    cs.cur_sample, s->name.c_str(), s->centerRate,
                    s->loop ? "LOOP" : "1SHOT");
      ui_text(u, 8, 46, hdr, COL_HL, 1);

      int gx = 12, gy = 66, gw = 296, gh = 136;
      ui_box(u, gx, gy, gw, gh,
             (struct ui_paint){.fill_set = 1,
                               .fill = 0x0c1016,
                               .border_set = 1,
                               .border = COL_GRID_LINE,
                               .border_px = 1});
      int mid_y = gy + gh / 2;
      ui_box(u, gx + 1, mid_y, gw - 2, 1,
             (struct ui_paint){.fill_set = 1, .fill = 0x202838});

      if (s->data16 && s->samples > 0) {
        for (int x = 0; x < gw - 4; x++) {
          unsigned int sidx =
              (static_cast<unsigned int>(x) * s->samples) / (gw - 4);
          short v = s->data16[sidx];
          int h = std::abs(v) * (gh / 2 - 4) / 32768;
          ui_box(u, gx + 2 + x, mid_y - h, 1, std::max(1, h * 2),
                 (struct ui_paint){.fill_set = 1, .fill = COL_PLAY});
        }
        if (s->loop) {
          int lx0 =
              gx + 2 + (s->loopStart * (gw - 4)) / static_cast<int>(s->samples);
          int lx1 =
              gx + 2 + (s->loopEnd * (gw - 4)) / static_cast<int>(s->samples);
          ui_box(u, lx0, gy + 2, 2, gh - 4,
                 (struct ui_paint){.fill_set = 1, .fill = COL_HL});
          ui_box(u, lx1, gy + 2, 2, gh - 4,
                 (struct ui_paint){.fill_set = 1, .fill = COL_REC});
        }
      }
    }
  }

  draw_footer(u, cs);
}

// =============================================================================
// 6. FILE BROWSER VIEW
// =============================================================================
void TrackerViews::draw_browser_view(struct ui *u, const struct input *in,
                                     ControlSurfaceState &cs) {
  if (!fb_initialized_) {
    init(browse_dir_);
  }

  auto &fb = FurnaceBridge::instance();

  // Stage 3 of VGM import: enter destination .fur filename
  if (vgm_import_stage_ == 3) {
    cs.modal_lock = true;
    int res = ui_text_editor(u, 0, 0, UI_W, UI_H, &text_ed_,
                             ui_text_editor_style_default(), in);

    // Render the locked .fur extension immediately following the name
    int ty = 58;
    int ext_x = 16 + text_ed_.len * 6;
    if (ext_x + 24 < UI_W - 16) {
      ui_text(u, ext_x, ty, ".fur", COL_INS, 1);
    }

    if (res == UI_TEXT_CANCEL) {
      cs.modal_lock = false;
      vgm_import_stage_ = 2;
    } else if (res == UI_TEXT_OK) {
      cs.modal_lock = false;
      vgm_import_stage_ = 0;

      std::string fn = text_buf_;
      while (!fn.empty() && (fn.front() == ' ' || fn.front() == '\t'))
        fn.erase(fn.begin());
      while (!fn.empty() && (fn.back() == ' ' || fn.back() == '\t'))
        fn.pop_back();

      // If user typed .fur at the end, strip it so we always append .fur
      // cleanly
      if (fn.size() >= 4) {
        std::string tail = fn.substr(fn.size() - 4);
        for (char &c : tail)
          c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (tail == ".fur") {
          fn = fn.substr(0, fn.size() - 4);
        }
      }
      if (fn.empty())
        fn = "imported";
      fn += ".fur";

      std::string dst_path;
      if (!fn.empty() && fn[0] == '/') {
        dst_path = fn;
      } else if (vgm_import_dst_dir_.empty()) {
        dst_path = get_active_save_dir() + "/" + fn;
      } else if (vgm_import_dst_dir_.back() == '/') {
        dst_path = vgm_import_dst_dir_ + fn;
      } else {
        dst_path = vgm_import_dst_dir_ + "/" + fn;
      }

      if (!vgm_import_dst_dir_.empty()) {
        browse_dir_ = vgm_import_dst_dir_;
        ui_filebrowser_set_cwd(&fb_state_, browse_dir_.c_str());
      }
      bool ok = fb.import_vgm(vgm_import_src_path_, dst_path);
      ui_filebrowser_refresh(&fb_state_);
      if (ok) {
        cs.cur_order = 0;
        cs.cur_row = 0;
        cs.cur_chan = 0;
        cs.cur_ins = 0;
        cs.active_view = AppView::Pattern;
      } else {
        std::snprintf(fb_state_.status_msg, sizeof(fb_state_.status_msg), "%s",
                      fb.status_msg.c_str());
        fb_state_.status_is_error = 1;
      }
    }
    return;
  }

  // Stage 2 of VGM import: choose destination directory
  if (vgm_import_stage_ == 2) {
    int status = ui_filebrowser(u, 0, 0, UI_W, UI_H, &vgm_save_fb_,
                                ui_filebrowser_style_default(), in);
    if (status == UI_FB_CANCEL) {
      vgm_import_stage_ = 0;
      cs.modal_lock = false;
    } else if (status == UI_FB_SELECT) {
      vgm_import_dst_dir_ = vgm_save_fb_.selected_path[0]
                                ? vgm_save_fb_.selected_path
                                : vgm_save_fb_.cwd;
      ui_text_editor_init(&text_ed_, text_buf_, sizeof(text_buf_),
                          "SAVE .FUR FILENAME");
      cs.modal_lock = true;
      vgm_import_stage_ = 3;
    }
    return;
  }

  // Stage 1 of VGM import: confirmation popup ("Convert VGM to Furnace?")
  if (vgm_import_stage_ == 1) {
    cs.modal_lock = true;
    ui_filebrowser(u, 0, 0, UI_W, UI_H, &fb_state_,
                   ui_filebrowser_style_default(), nullptr);

    const char *q_labels[3] = {"Cancel", "", "Yes"};
    int active_s = -1;
    int sk = ui_soft_keys(u, 4, 2, 312, 20, q_labels, &active_s,
                          make_soft_style(false), in, &soft_state_);

    ui_box(u, 24, 60, 272, 110,
           (struct ui_paint){.fill_set = 1,
                             .fill = COL_PANEL,
                             .border_set = 1,
                             .border = COL_HL,
                             .border_px = 2});

    ui_text(u, 38, 76, "Convert VGM to Furnace?", COL_HL, 1);

    std::string base_name = vgm_import_src_path_;
    auto last_slash = base_name.rfind('/');
    if (last_slash != std::string::npos)
      base_name = base_name.substr(last_slash + 1);
    char file_label[64];
    std::snprintf(file_label, sizeof(file_label), "File: %.28s",
                  base_name.c_str());
    ui_text(u, 38, 98, file_label, COL_TEXT, 1);

    ui_text(u, 38, 124, "B1: Cancel", COL_MUTED, 1);
    ui_text(u, 38, 140, "B3 / Press: Yes (Choose save folder)", COL_PLAY, 1);

    bool cancel = (sk == 0) ||
                  (in && (input_down(in, IN_B1) || input_down(in, IN_ERASE)));
    bool yes =
        (sk == 2) || (in && (input_down(in, IN_B3) || in->encoder_press));

    if (cancel) {
      vgm_import_stage_ = 0;
      cs.modal_lock = false;
      return;
    }
    if (yes) {
      cs.modal_lock = false;
      struct ui_filebrowser_config cfg{};
      cfg.initial_path = fb_state_.cwd[0] ? fb_state_.cwd : browse_dir_.c_str();
      cfg.internal_path =
          fb_state_.internal_path[0] ? fb_state_.internal_path : nullptr;
      cfg.external_path =
          fb_state_.external_path[0] ? fb_state_.external_path : nullptr;
      cfg.forced_root =
          fb_state_.forced_root[0] ? fb_state_.forced_root : nullptr;
      cfg.title = "SAVE .FUR FOLDER";
      cfg.action_label = "SAVE HERE";
      cfg.options = UI_FB_OPT_DIRS_ONLY | UI_FB_OPT_SELECT_DIR |
                    UI_FB_OPT_MKDIR | UI_FB_OPT_HIDE_DOTFILES |
                    UI_FB_OPT_TOGGLE_DOTFILES;
      ui_filebrowser_init(&vgm_save_fb_, &cfg);
      std::snprintf(vgm_save_fb_.status_msg, sizeof(vgm_save_fb_.status_msg),
                    "Choose folder & press B3 (SAVE HERE)");
      vgm_save_fb_.status_is_error = 0;
      vgm_import_stage_ = 2;
      return;
    }
    return;
  }

  int status = ui_filebrowser(u, 0, 0, UI_W, UI_H, &fb_state_,
                              ui_filebrowser_style_default(), in);
  if (fb_state_.cwd[0] != '\0') {
    browse_dir_ = fb_state_.cwd;
  }
  if (status == UI_FB_CANCEL) {
    cs.active_view =
        (cs.prev_view == AppView::Browser) ? AppView::Pattern : cs.prev_view;
  } else if (status == UI_FB_SELECT) {
    std::string path = fb_state_.selected_path;

    // Check extension
    std::string ext;
    auto dot = path.rfind('.');
    if (dot != std::string::npos) {
      ext = path.substr(dot);
      for (char &c : ext) {
        if (c >= 'A' && c <= 'Z')
          c = static_cast<char>(c - 'A' + 'a');
      }
    }

    if (ext == ".vgm" || ext == ".vgz") {
      std::string err;
      if (!fb.is_genesis_vgm(path, &err)) {
        fb.status_msg = err.empty() ? "Not a Sega Genesis VGM" : err;
        std::snprintf(fb_state_.status_msg, sizeof(fb_state_.status_msg), "%s",
                      fb.status_msg.c_str());
        fb_state_.status_is_error = 1;
        return;
      }

      vgm_import_src_path_ = path;

      // Default .fur filename based on the VGM file's stem
      std::string base = path;
      auto slash = base.rfind('/');
      if (slash != std::string::npos)
        base = base.substr(slash + 1);
      auto bdot = base.rfind('.');
      if (bdot != std::string::npos && bdot > 0)
        base = base.substr(0, bdot);
      while (!base.empty() && (base.front() == ' ' || base.front() == '\t'))
        base.erase(base.begin());
      while (!base.empty() && (base.back() == ' ' || base.back() == '\t'))
        base.pop_back();
      if (base.empty())
        base = "imported";
      std::snprintf(text_buf_, sizeof(text_buf_), "%s", base.c_str());

      vgm_import_stage_ = 1;
      cs.modal_lock = true;
    } else if (ext == ".wav") {
      int idx = fb.import_wav_sample(path);
      if (idx >= 0)
        cs.cur_sample = idx;
      cs.active_view = AppView::SampleWave;
    } else if (ext == ".fui" || ext == ".dmp" || ext == ".tfi" ||
               ext == ".vgi" || ext == ".opm") {
      int idx = fb.load_instrument_file(path);
      if (idx >= 0)
        cs.cur_ins = idx;
      cs.active_view = AppView::Instrument;
    } else {
      if (fb.load_song(path)) {
        cs.cur_order = 0;
        cs.cur_row = 0;
        cs.cur_chan = 0;
        cs.cur_ins = 0;
        cs.active_view = AppView::Pattern;
      }
    }
  }
}

// =============================================================================
// 7. PROJECT & SYSTEM MENU VIEW (New Preset, Save .fur, Metadata, Export, USB
// SD, Quit)
// =============================================================================
int TrackerViews::draw_menu_view(struct ui *u, const struct input *in,
                                 ControlSurfaceState &cs) {
  auto &fb = FurnaceBridge::instance();
  auto &eng = fb.engine();

  if (text_edit_active_) {
    cs.modal_lock = true;
    int res = ui_text_editor(u, 0, 0, UI_W, UI_H, &text_ed_,
                             ui_text_editor_style_default(), in);
    if (res == UI_TEXT_OK) {
      text_edit_active_ = false;
      cs.modal_lock = false;
      if (text_edit_target_ == 0) {
        std::string fn = text_buf_;
        if (fn.empty())
          fn = "song.fur";
        if (fn.find(".fur") == std::string::npos)
          fn += ".fur";
        std::string active_dir = get_active_save_dir();
        std::string full = (fn[0] == '/') ? fn : (active_dir + "/" + fn);
        fb.save_fur(full);
        browse_dir_ = active_dir;
        ui_filebrowser_set_cwd(&fb_state_, active_dir.c_str());
        ui_filebrowser_refresh(&fb_state_);
      } else if (text_edit_target_ == 1) {
        eng.song.name = text_buf_;
        fb.dirty = true;
      } else if (text_edit_target_ == 2) {
        eng.song.author = text_buf_;
        fb.dirty = true;
      } else if (text_edit_target_ == 3) {
        eng.getIns(cs.cur_ins)->name = text_buf_;
        fb.dirty = true;
        cs.active_view = AppView::Instrument;
      }
    } else if (res == UI_TEXT_CANCEL) {
      text_edit_active_ = false;
      cs.modal_lock = false;
      if (text_edit_target_ == 3)
        cs.active_view = AppView::Instrument;
    }
    return 0;
  }

  if (confirm_quit_) {
    cs.modal_lock = true;
    const char *q_labels[3] = {"Cancel", "Save&Quit", "QuitNow"};
    int active_s = -1;
    int sk = ui_soft_keys(u, 4, 2, 312, 20, q_labels, &active_s,
                          make_soft_style(false), in, &soft_state_);
    ui_box(u, 24, 60, 272, 110,
           (struct ui_paint){.fill_set = 1,
                             .fill = COL_PANEL,
                             .border_set = 1,
                             .border = COL_REC,
                             .border_px = 2});
    ui_text(u, 40, 80, "Song has unsaved edits!", COL_HL, 1);
    ui_text(u, 40, 104, "B1: Cancel and stay", COL_TEXT, 1);
    ui_text(u, 40, 120, "B2: Save to song.fur & Quit", COL_PLAY, 1);
    ui_text(u, 40, 136, "B3: Quit without saving", COL_REC, 1);

    if (sk == 0) {
      confirm_quit_ = false;
      cs.modal_lock = false;
    } else if (sk == 1) {
      fb.save_fur(get_active_save_dir() + "/autosave.fur");
      ui_quit(u);
      return 1;
    } else if (sk == 2) {
      ui_quit(u);
      return 1;
    }
    return 0;
  }

  const char *labels[3] = {"Back", "Apply/Edit", "Quit App"};
  int active_soft = -1;
  int sk = ui_soft_keys(u, 4, 2, 312, 20, labels, &active_soft,
                        make_soft_style(false), in, &soft_state_);

  if (sk == 0) {
    cs.active_view =
        (cs.prev_view == AppView::Menu) ? AppView::Pattern : cs.prev_view;
    return 0;
  }
  if (sk == 2) {
    if (fb.dirty) {
      confirm_quit_ = true;
      cs.modal_lock = true;
      return 0;
    }
    ui_quit(u);
    return 1;
  }

  draw_header(u, cs, "Preset", "-", "-");

  const int menu_items = 8;
  if (in && in->encoder != 0) {
    menu_cursor_ = (menu_cursor_ + in->encoder + menu_items) % menu_items;
  }

  int d1 = cs.knobs.consume(in, 0, 3);
  if (d1 != 0 || (in && (in->down & (1u << IN_MINUS))) ||
      (in && (in->down & (1u << IN_PLUS)))) {
    int step = d1;
    if (in && (in->down & (1u << IN_MINUS)))
      step = -1;
    if (in && (in->down & (1u << IN_PLUS)))
      step = 1;
    int np = fb.get_preset_count();
    selected_preset_ = (selected_preset_ + step + np) % np;
  }

  bool activate = (sk == 1) || (in && in->encoder_press);
  if (activate) {
    if (menu_cursor_ == 0) {
      fb.create_new_with_preset(selected_preset_);
      cs.cur_order = 0;
      cs.cur_row = 0;
      cs.cur_chan = 0;
      cs.cur_ins = 0;
      cs.active_view = AppView::Pattern;
      return 0;
    } else if (menu_cursor_ == 1) {
      std::snprintf(text_buf_, sizeof(text_buf_), "song.fur");
      ui_text_editor_init(&text_ed_, text_buf_, sizeof(text_buf_),
                          "Save .fur Filename");
      text_edit_target_ = 0;
      text_edit_active_ = true;
      cs.modal_lock = true;
      return 0;
    } else if (menu_cursor_ == 2) {
      std::snprintf(text_buf_, sizeof(text_buf_), "%s", eng.song.name.c_str());
      ui_text_editor_init(&text_ed_, text_buf_, sizeof(text_buf_),
                          "Song Title");
      text_edit_target_ = 1;
      text_edit_active_ = true;
      cs.modal_lock = true;
      return 0;
    } else if (menu_cursor_ == 3) {
      std::snprintf(text_buf_, sizeof(text_buf_), "%s",
                    eng.song.author.c_str());
      ui_text_editor_init(&text_ed_, text_buf_, sizeof(text_buf_),
                          "Author Name");
      text_edit_target_ = 2;
      text_edit_active_ = true;
      cs.modal_lock = true;
      return 0;
    } else if (menu_cursor_ == 4) {
      fb.export_vgm(get_active_save_dir() + "/export.vgm");
    } else if (menu_cursor_ == 5) {
      fb.export_wav(get_active_save_dir() + "/export.wav", 30);
    } else if (menu_cursor_ == 6) {
      ui_usb_sd_init(&usb_);
      usb_open_ = true;
      cs.modal_lock = true;
      ui_usb_sd(u, &usb_, furnace_usb_style(), nullptr);
      return 0;
    } else if (menu_cursor_ == 7) {
      if (fb.dirty) {
        confirm_quit_ = true;
        cs.modal_lock = true;
        return 0;
      }
      ui_quit(u);
      return 1;
    }
  }

  ui_box(u, 4, 42, 312, 178,
         (struct ui_paint){.fill_set = 1, .fill = COL_PANEL});

  char rows[8][56];
  std::snprintf(rows[0], sizeof(rows[0]), "New Song Preset: < %s >",
                fb.get_preset(selected_preset_).name);
  std::snprintf(rows[1], sizeof(rows[1]), "Save Song (.fur)...");
  std::snprintf(rows[2], sizeof(rows[2]), "Song Title : %.28s",
                eng.song.name.c_str());
  std::snprintf(rows[3], sizeof(rows[3]), "Author Name: %.28s",
                eng.song.author.c_str());
  std::snprintf(rows[4], sizeof(rows[4]),
                "Export VGM Register Dump (export.vgm)");
  std::snprintf(rows[5], sizeof(rows[5]), "Export Audio WAV (export.wav)");
  std::snprintf(rows[6], sizeof(rows[6]), "%s", UI_USB_SD_LABEL);
  std::snprintf(rows[7], sizeof(rows[7]), "Quit mpc-furnace");

  for (int i = 0; i < menu_items; i++) {
    int ry = 48 + i * 20;
    bool sel = (i == menu_cursor_);
    ui_box(u, 8, ry, 304, 18,
           (struct ui_paint){.fill_set = 1,
                             .fill = sel ? COL_CURSOR_ROW
                                         : static_cast<uint32_t>(COL_PANEL_ALT),
                             .border_set = sel ? 1 : 0,
                             .border = COL_HL,
                             .border_px = 1});
    ui_text(u, 14, ry + 3, rows[i], sel ? COL_HL : COL_TEXT, 1);
  }

  draw_footer(u, cs);
  return 0;
}

// =============================================================================
// 8. HARDWARE HELP OVERLAY VIEW (Shift + SAMPLE REC)
// =============================================================================
void TrackerViews::draw_help_view(struct ui *u, const struct input *in,
                                  ControlSurfaceState &cs) {
  const char *labels[3] = {"Close", "", ""};
  int active_soft = -1;
  int sk = ui_soft_keys(u, 4, 2, 312, 20, labels, &active_soft,
                        make_soft_style(false), in, &soft_state_);
  if (sk == 0 || (in && in->encoder_press)) {
    cs.active_view =
        (cs.prev_view == AppView::Help) ? AppView::Pattern : cs.prev_view;
    return;
  }

  ui_box(u, 4, 26, 312, 210,
         (struct ui_paint){.fill_set = 1,
                           .fill = COL_PANEL,
                           .border_set = 1,
                           .border = COL_HL,
                           .border_px = 1});

  static const char *kHelpLines[] = {
      "MPC-FURNACE HARDWARE SHORTCUTS",
      "SEQ          : Pattern Grid (Press again: 3Ch/8Ch)",
      "LOOP         : Order Matrix & Song Timing",
      "PAD FX       : Instrument FM / Macro / Patch Editor",
      "MUTE/KNOB FX : Channel Mixer & Oscilloscope",
      "SAMPLE/CHOP  : Sample & Wavetable Editor",
      "SAMPLE SEL   : File Browser (.fur/.vgm/.dmf/.mod/.wav)",
      "SHIFT+SMP SEL: Project Menu (Save / USB SD / Quit)",
      "PADS 1-16    : Chromatic Synth / Mixer Channel Mutes",
      "PAD BANK     : Octave 1..6 (Shift: Next Instrument)",
      "16 LEVELS    : Velocity / Pitch Interval Pad Modes",
      "NOTE REPEAT  : Retrigger Held Pad (Shift: Speed)",
      "SEQ REC      : Step/Live Record (Shift: Metronome)",
      "ERASE + PAD  : Enter Note Off === (Shift: Note Rel ~~~)"};
  for (int i = 0; i < 14; i++) {
    ui_text(u, 10, 30 + i * 14, kHelpLines[i], (i == 0) ? COL_HL : COL_TEXT, 1);
  }
}

// =============================================================================
// TOP-LEVEL FRAME DISPATCH
// =============================================================================
int TrackerViews::draw_frame(struct ui *u, const struct input *in,
                             ControlSurfaceState &cs) {
  if (usb_open_) {
    int rc = ui_usb_sd(u, &usb_, furnace_usb_style(), in);
    if (rc == UI_USB_SD_ACTIVE)
      return 0;
    usb_open_ = false;
    cs.modal_lock = false;
    struct ui_filebrowser_config cfg{};
    cfg.initial_path = (!browse_dir_.empty() && browse_dir_ != "/media") ? browse_dir_.c_str() : nullptr;
    cfg.title = "FURNACE FILES";
    cfg.action_label = "LOAD";
    cfg.options = UI_FB_PRESET_DEFAULT;
    cfg.extensions = kBrowserExts;
    ui_filebrowser_init(&fb_state_, &cfg);
    if (fb_state_.cwd[0] != '\0') {
      browse_dir_ = fb_state_.cwd;
    }
  }

  ui_clear(u, COL_BG);

  switch (cs.active_view) {
  case AppView::Pattern:
    draw_pattern_view(u, in, cs);
    break;
  case AppView::Orders:
    draw_orders_view(u, in, cs);
    break;
  case AppView::Instrument:
    draw_instrument_view(u, in, cs);
    break;
  case AppView::Mixer:
    draw_mixer_view(u, in, cs);
    break;
  case AppView::SampleWave:
    draw_sample_wave_view(u, in, cs);
    break;
  case AppView::Browser:
    draw_browser_view(u, in, cs);
    break;
  case AppView::Menu:
    return draw_menu_view(u, in, cs);
  case AppView::Help:
    draw_help_view(u, in, cs);
    break;
  }
  return 0;
}
