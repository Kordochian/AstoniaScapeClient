/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Graphical User Interface - Options menu
 *
 * In-game window for the audio volumes and the display mode / resolution.
 * Settings are kept in settings.ini next to moac35.dat (bin/data or the
 * user's application data folder).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>

#include "astonia.h"
#include "gui/gui.h"
#include "gui/gui_private.h"
#include "client/client.h"
#include "game/game.h"
#include "sdl/sdl.h"

extern int want_width, want_height;

// panel size and layout, in game coordinates
#define PW 300
#define PH 522

#define ROW_SFX   40
#define ROW_MUSIC 58
#define ROW_MODE  104
#define ROW_RES   122
#define ROW_ZOOM  140
#define ROW_WZOOM 158
#define ROW_UI    184
#define ROW_PANELS 210
#define ROW_PSCALE 228 // one row per panel, ZROW_H apart
#define ROW_CHAT   410
#define ROW_MINIMAP 428
#define ROW_RESET  446
#define ROW_NOTE  470
#define ZROW_H     26 // zoom sliders have "Zoom out" / "Zoom in" under their ends

#define CTRL_X 112 // left edge of the sliders and selectors
#define SLD_W  130 // slider width
#define SEL_W  170 // selector width

#define BTN_W 64
#define BTN_H 16

enum {
	H_NONE,
	H_CLOSE,
	H_TITLE,
	H_GRIP,
	H_SFX,
	H_MUSIC,
	H_MODE_L,
	H_MODE_R,
	H_RES_L,
	H_RES_R,
	H_ZOOM_L,
	H_ZOOM_R,
	H_WZOOM,
	H_UI,
	H_PANELS_L,
	H_PANELS_R,
	H_PSCALE,
	H_CHAT_L,
	H_CHAT_R,
	H_MMAP_L,
	H_MMAP_R,
	H_RESET,
	H_APPLY,
	H_DONE,
};

static int is_open = 0;
// The window can be dragged by its title bar (never locked): offset from the centered spot
static int win_dx = 0, win_dy = 0, win_drag = 0, win_drag_x, win_drag_y;
// It can also be made shorter with the grip in its bottom right corner; the settings keep their
// size and scroll inside it.
#define WIN_TITLE 18
#define WIN_FOOT  32 // Apply and Close buttons
#define CONTENT_H (PH - WIN_TITLE - WIN_FOOT)
#define WIN_MIN_H 120
static int win_h = PH, win_scroll = 0, win_size = 0, win_size_dy;
static int dragging = H_NONE;

static int sel_mode = DISPMODE_WINDOWED;
static int sel_res = 0;
static int res_cnt = 0;
static int res_w[MAX_RESOLUTIONS + 1], res_h[MAX_RESOLUTIONS + 1];

// graphics settings from settings.ini, only written once the player chose them in game
static int have_gfx = 0;
static int cfg_mode = DISPMODE_WINDOWED;
static int cfg_w = 0, cfg_h = 0;

// window size at startup, and the scale we last told the player about
static int base_w = 0, base_h = 0;
static int hint_scale = 0;
static int dirty = 0;

static const char *mode_name[] = {"Windowed", "Windowed fullscreen", "Fullscreen"};
static const char *mode_key[] = {"windowed", "borderless", "fullscreen"};
static const char *zoom_name[] = {"Auto (largest)", "1x (see the most)", "2x", "3x", "4x"};

// Zoom sliders: world zoom, interface size and the panel scales, in percent, 5% steps.
// Left is zoom out (smaller), right is zoom in (larger).
#define UI_MIN     50
#define UI_MAX     200
#define PSCALE_MIN 75
#define PSCALE_MAX 200

// panel scale rows: the panel under the mouse, and the one whose slider is dragged
static int hit_panel = 0, drag_panel = 0;
// the interface size slider only applies on release (the window itself changes size with it)
static int drag_ui = 0;

static void current_selection(void);

static int clampi(int v, int lo, int hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

static void settings_filename(char *buf, size_t size)
{
	if (localdata) {
		snprintf(buf, size, "%s%s", localdata, "settings.ini");
	} else {
		snprintf(buf, size, "%s", "bin/data/settings.ini");
	}
}

static int sfx_percent(void)
{
	return clampi((sound_volume * 100 + 64) / 128, 0, 100);
}

void options_set_sfx_volume(int percent)
{
	percent = clampi(percent, 0, 100);
	sound_volume = (percent * 128 + 50) / 100;
	dirty = 1;
}

static void set_music_volume(int percent)
{
	music_volume = clampi(percent, 0, 100);
	dirty = 1;
}

// Read settings.ini. Called before the window is created. The display mode and size
// saved here are used unless the command line asked for some (-w, -h, -s or the
// fullscreen bit of -o): the launcher passes those when the player set them there.
void options_load(void)
{
	char filename[MAX_PATH], line[256], *val, *end;
	FILE *fp;
	int mode = -1, w = 0, h = 0, cmdline;

	cmdline = want_width || want_height || sdl_display_mode != -1 ||
	          (!(game_options & GO_NOTSET) && (game_options & GO_FULL));

	settings_filename(filename, sizeof(filename));
	fp = fopen(filename, "r");
	if (!fp) {
		return;
	}

	while (fgets(line, sizeof(line), fp)) {
		if (line[0] == '#' || line[0] == ';' || !(val = strchr(line, '='))) {
			continue;
		}
		*val++ = 0;
		for (end = val + strlen(val); end > val && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == ' '); end--) {
			end[-1] = 0;
		}

		if (!strcmp(line, "sfx_volume")) {
			options_set_sfx_volume(atoi(val));
		} else if (!strcmp(line, "music_volume")) {
			set_music_volume(atoi(val));
		} else if (!strcmp(line, "display_mode")) {
			for (int i = 0; i < 3; i++) {
				if (!strcmp(val, mode_key[i])) {
					mode = i;
				}
			}
		} else if (!strcmp(line, "width")) {
			w = atoi(val);
		} else if (!strcmp(line, "height")) {
			h = atoi(val);
		} else if (!strcmp(line, "zoom")) {
			sdl_zoom = clampi(atoi(val), 0, 4);
		} else if (!strcmp(line, "world_zoom")) {
			sdl_set_world_zoom(atoi(val));
		} else if (!strcmp(line, "ui_size")) {
			sdl_ui_size = clampi(atoi(val), 25, 400);
		} else if (!strcmp(line, "options_pos")) {
			sscanf(val, "%d,%d,%d", &win_dx, &win_dy, &win_h);
		} else if (!strcmp(line, "questlog_pos")) {
			int qx = 0, qy = 0, qw = 0, qh = 0;
			if (sscanf(val, "%d,%d,%d,%d", &qx, &qy, &qw, &qh) >= 2) {
				questwin_set_pos(qx, qy, qw, qh);
			}
		} else if (!strcmp(line, "style_pos")) {
			int sx = 0, sy = 0;
			if (sscanf(val, "%d,%d", &sx, &sy) == 2) {
				stylewin_set_pos(sx, sy);
			}
		} else if (!strcmp(line, "chat_text")) {
			pnl_chat_text = clampi(atoi(val), -1, 1);
		} else if (!strcmp(line, "ui_locked")) {
			panels_locked = atoi(val) ? 1 : 0;
		} else if (!minimap_load_setting(line, val)) {
			panels_load_setting(line, val);
		}
	}
	fclose(fp);

	if (mode != -1) {
		have_gfx = 1;
		cfg_mode = mode;
		cfg_w = w;
		cfg_h = h;

		if (cmdline) {
			note("Display settings from the command line replace those in %s", filename);
		} else {
			sdl_display_mode = mode;
			if (mode == DISPMODE_BORDERLESS) {
				want_width = want_height = 0;
			} else if (w >= 640 && h >= 360) {
				want_width = w;
				want_height = h;
			}
		}
	}
	dirty = 0;
}

void options_save(void)
{
	char filename[MAX_PATH];
	FILE *fp;

	settings_filename(filename, sizeof(filename));
	fp = fopen(filename, "w");
	if (!fp) {
		warn("Could not write %s", filename);
		return;
	}

	fprintf(fp, "# Astonia client settings, changed with the in-game Settings window\n");
	fprintf(fp, "sfx_volume=%d\n", sfx_percent());
	fprintf(fp, "music_volume=%d\n", music_volume);
	fprintf(fp, "zoom=%d\n", sdl_zoom);
	fprintf(fp, "world_zoom=%d\n", sdl_world_zoom);
	fprintf(fp, "ui_size=%d\n", sdl_ui_size);
	fprintf(fp, "options_pos=%d,%d,%d\n", win_dx, win_dy, win_h);
	{
		int qx, qy, qw, qh;
		questwin_get_pos(&qx, &qy, &qw, &qh);
		fprintf(fp, "questlog_pos=%d,%d,%d,%d\n", qx, qy, qw, qh);
	}
	{
		int sx, sy;
		stylewin_get_pos(&sx, &sy);
		fprintf(fp, "style_pos=%d,%d\n", sx, sy);
	}
	if (pnl_chat_text >= 0) {
		fprintf(fp, "chat_text=%d\n", pnl_chat_text);
	}
	panels_save_settings(fp);
	minimap_save_settings(fp);
	if (have_gfx) {
		fprintf(fp, "display_mode=%s\n", mode_key[clampi(cfg_mode, 0, 2)]);
		if (cfg_mode != DISPMODE_BORDERLESS) {
			fprintf(fp, "width=%d\n", cfg_w);
			fprintf(fp, "height=%d\n", cfg_h);
		}
	}
	fclose(fp);
	dirty = 0;
}

// Called at exit: keep a window size the player dragged to
void options_exit(void)
{
	if (dirty) {
		options_save();
	}
}

// Called once the window exists
void options_init(void)
{
	sdl_get_mode_size(&base_w, &base_h);
	hint_scale = sdl_scale;
}

// Called by the SDL layer whenever the window's size in pixels changed
void options_window_changed(void)
{
	int w, h, best;

	if (sdl_display_mode == DISPMODE_WINDOWED) {
		sdl_get_mode_size(&w, &h);
		if (w > 0 && h > 0 && (w != base_w || h != base_h)) {
			// remember the window size for the next start, as most programs do
			base_w = w;
			base_h = h;
			have_gfx = 1;
			cfg_mode = DISPMODE_WINDOWED;
			cfg_w = w;
			cfg_h = h;
			dirty = 1;
		}
	}

	if (is_open) {
		current_selection();
	}

	best = sdl_best_scale();
	if (best != sdl_scale && best != hint_scale) {
		addline("Restart the client to get %s graphics at this window size.", best > sdl_scale ? "larger" : "smaller");
	}
	hint_scale = best;
}

static void current_selection(void)
{
	int w, h, i;

	sel_mode = clampi(sdl_display_mode, 0, 2);
	res_cnt = sdl_get_resolutions(res_w, res_h, MAX_RESOLUTIONS);

	sdl_get_mode_size(&w, &h);
	for (i = 0; i < res_cnt; i++) {
		if (res_w[i] == w && res_h[i] == h) {
			break;
		}
	}
	if (i == res_cnt && w > 0 && h > 0) {
		// the window has a size of its own (it was resized), offer it too
		for (i = 0; i < res_cnt && (res_w[i] < w || (res_w[i] == w && res_h[i] < h)); i++) {
			;
		}
		memmove(&res_w[i + 1], &res_w[i], sizeof(int) * (size_t)(res_cnt - i));
		memmove(&res_h[i + 1], &res_h[i], sizeof(int) * (size_t)(res_cnt - i));
		res_w[i] = w;
		res_h[i] = h;
		res_cnt++;
	}
	sel_res = clampi(i, 0, res_cnt > 0 ? res_cnt - 1 : 0);
}

void options_open(void)
{
	current_selection();
	dragging = H_NONE;
	is_open = 1;
}

void options_close(void)
{
	if (!is_open) {
		return;
	}
	is_open = 0;
	dragging = H_NONE;
	if (dirty) {
		options_save();
	}
}

int options_is_open(void)
{
	return is_open;
}

static int base_x(void)
{
	return (XRES - PW) / 2;
}

static int wh(void)
{
	return clampi(win_h, WIN_MIN_H, min(PH, YRES - 1));
}

static int base_y(void)
{
	int y = (YRES - wh()) / 2 - 20;
	return y < 4 ? 4 : y;
}

static int panel_x(void)
{
	return clampi(base_x() + win_dx, 0, XRES - PW - 1);
}

// top of the window
static int win_y(void)
{
	return clampi(base_y() + win_dy, 0, YRES - wh() - 1);
}

static int max_scroll(void)
{
	return max(0, CONTENT_H - (wh() - WIN_TITLE - WIN_FOOT));
}

// top of the settings, which scroll inside the window
static int panel_y(void)
{
	win_scroll = clampi(win_scroll, 0, max_scroll());
	return win_y() - win_scroll;
}

int options_over(int x, int y)
{
	int px = panel_x(), wy = win_y();

	return is_open && x >= px && x < px + PW && y >= wy && y < wy + wh();
}

static int in_box(int x, int y, int sx, int sy, int ex, int ey)
{
	return x >= sx && x <= ex && y >= sy && y <= ey;
}

static int changes_pending(void)
{
	int w, h;

	if (sel_mode != sdl_display_mode) {
		return 1;
	}
	if (sel_mode == DISPMODE_BORDERLESS || res_cnt == 0) {
		return 0;
	}
	sdl_get_mode_size(&w, &h);
	return res_w[sel_res] != w || res_h[sel_res] != h;
}

static int hit_test(int x, int y)
{
	int px = panel_x(), py = panel_y(), wy = win_y(), h = wh();
	int cx = px + CTRL_X;

	if (!options_over(x, y)) {
		return H_NONE;
	}
	if (in_box(x, y, px + PW - 18, wy + 2, px + PW - 4, wy + 15)) {
		return H_CLOSE;
	}
	if (y < wy + WIN_TITLE) {
		return H_TITLE;
	}
	if (x >= px + PW - 10 && y >= wy + h - 10) {
		return H_GRIP;
	}
	if (in_box(x, y, px + PW / 2 - BTN_W - 8, wy + h - BTN_H - 10, px + PW / 2 - 8, wy + h - 10)) {
		return changes_pending() ? H_APPLY : H_NONE;
	}
	if (in_box(x, y, px + PW / 2 + 8, wy + h - BTN_H - 10, px + PW / 2 + 8 + BTN_W, wy + h - 10)) {
		return H_DONE;
	}
	if (y >= wy + h - WIN_FOOT) {
		return H_NONE; // settings scrolled out of view
	}
	if (in_box(x, y, cx - 4, py + ROW_SFX - 3, cx + SLD_W + 4, py + ROW_SFX + 12)) {
		return H_SFX;
	}
	if (in_box(x, y, cx - 4, py + ROW_MUSIC - 3, cx + SLD_W + 4, py + ROW_MUSIC + 12)) {
		return H_MUSIC;
	}
	if (in_box(x, y, cx, py + ROW_MODE - 2, cx + 14, py + ROW_MODE + 11)) {
		return H_MODE_L;
	}
	if (in_box(x, y, cx + SEL_W - 14, py + ROW_MODE - 2, cx + SEL_W, py + ROW_MODE + 11)) {
		return H_MODE_R;
	}
	if (in_box(x, y, cx, py + ROW_ZOOM - 2, cx + 14, py + ROW_ZOOM + 11)) {
		return H_ZOOM_L;
	}
	if (in_box(x, y, cx + SEL_W - 14, py + ROW_ZOOM - 2, cx + SEL_W, py + ROW_ZOOM + 11)) {
		return H_ZOOM_R;
	}
	if (in_box(x, y, cx - 4, py + ROW_WZOOM - 3, cx + SLD_W + 4, py + ROW_WZOOM + 12)) {
		return H_WZOOM;
	}
	if (in_box(x, y, cx - 4, py + ROW_UI - 3, cx + SLD_W + 4, py + ROW_UI + 12)) {
		return H_UI;
	}
	if (in_box(x, y, cx, py + ROW_PANELS - 2, cx + 14, py + ROW_PANELS + 11)) {
		return H_PANELS_L;
	}
	if (in_box(x, y, cx + SEL_W - 14, py + ROW_PANELS - 2, cx + SEL_W, py + ROW_PANELS + 11)) {
		return H_PANELS_R;
	}
	for (int p = 0; p < MAX_PANEL; p++) {
		int r = ROW_PSCALE + p * ZROW_H;
		if (in_box(x, y, cx - 4, py + r - 3, cx + SLD_W + 4, py + r + 12)) {
			hit_panel = p;
			return H_PSCALE;
		}
	}
	if (in_box(x, y, cx, py + ROW_CHAT - 2, cx + 14, py + ROW_CHAT + 11)) {
		return H_CHAT_L;
	}
	if (in_box(x, y, cx + SEL_W - 14, py + ROW_CHAT - 2, cx + SEL_W, py + ROW_CHAT + 11)) {
		return H_CHAT_R;
	}
	if (!(game_options & GO_NOMAP)) {
		if (in_box(x, y, cx, py + ROW_MINIMAP - 2, cx + 14, py + ROW_MINIMAP + 11)) {
			return H_MMAP_L;
		}
		if (in_box(x, y, cx + SEL_W - 14, py + ROW_MINIMAP - 2, cx + SEL_W, py + ROW_MINIMAP + 11)) {
			return H_MMAP_R;
		}
	}
	if (in_box(x, y, cx, py + ROW_RESET - 3, cx + BTN_W, py + ROW_RESET - 3 + BTN_H)) {
		return H_RESET;
	}
	if (sel_mode != DISPMODE_BORDERLESS) {
		if (in_box(x, y, cx, py + ROW_RES - 2, cx + 14, py + ROW_RES + 11)) {
			return H_RES_L;
		}
		if (in_box(x, y, cx + SEL_W - 14, py + ROW_RES - 2, cx + SEL_W, py + ROW_RES + 11)) {
			return H_RES_R;
		}
	}
	return H_NONE;
}

int options_over_control(int x, int y)
{
	int h = hit_test(x, y);
	return h != H_NONE && h != H_TITLE && h != H_GRIP;
}

// Value of a zoom slider at mouse x, in 5% steps
static int zoom_at(int x, int lo, int hi)
{
	int cx = panel_x() + CTRL_X;
	int v = lo + ((x - cx) * (hi - lo) + SLD_W / 2) / SLD_W;

	return clampi((v + 2) / 5 * 5, lo, hi);
}

static int is_zoom_slider(int h)
{
	return h == H_WZOOM || h == H_UI || h == H_PSCALE;
}

// Change a zoom slider to v percent
static void zoom_set(int which, int v)
{
	if (which == H_WZOOM) {
		// only the world is enlarged, right away; the interface keeps its size
		sdl_set_world_zoom(clampi(v, WORLD_ZOOM_MIN, WORLD_ZOOM_MAX));
	} else if (which == H_UI) {
		// the interface size changes right away; the panel moves with the new layout
		sdl_set_ui_size(clampi(v, UI_MIN, UI_MAX));
	} else if (which == H_PSCALE) {
		// the panel grows or shrinks right away, text and pictures with it
		panels_set_scale(drag_panel, clampi(v, PSCALE_MIN, PSCALE_MAX));
	}
}

static void slider_set(int which, int x)
{
	int cx = panel_x() + CTRL_X;
	int v = clampi(((x - cx) * 100 + SLD_W / 2) / SLD_W, 0, 100);

	if (which == H_WZOOM) {
		zoom_set(which, zoom_at(x, WORLD_ZOOM_MIN, WORLD_ZOOM_MAX));
		return;
	}
	if (which == H_UI) {
		drag_ui = zoom_at(x, UI_MIN, UI_MAX); // applied on release
		return;
	}
	if (which == H_PSCALE) {
		zoom_set(which, zoom_at(x, PSCALE_MIN, PSCALE_MAX));
		return;
	}
	if (which == H_SFX) {
		options_set_sfx_volume(v);
	} else {
		set_music_volume(v);
	}
}

static void apply_graphics(void)
{
	int w = 0, h = 0;

	if (res_cnt > 0) {
		w = res_w[sel_res];
		h = res_h[sel_res];
	}
	if (!sdl_set_display_mode(sel_mode, w, h)) {
		return;
	}

	sdl_get_mode_size(&w, &h);
	base_w = w;
	base_h = h;
	have_gfx = 1;
	cfg_mode = sel_mode;
	cfg_w = w;
	cfg_h = h;
	options_save();

	current_selection();
	options_window_changed();
}

static void click(int what)
{
	switch (what) {
	case H_CLOSE:
	case H_DONE:
		options_close();
		break;
	case H_MODE_L:
		sel_mode = (sel_mode + 2) % 3;
		break;
	case H_MODE_R:
		sel_mode = (sel_mode + 1) % 3;
		break;
	case H_RES_L:
		if (sel_res > 0) {
			sel_res--;
		}
		break;
	case H_RES_R:
		if (sel_res < res_cnt - 1) {
			sel_res++;
		}
		break;
	case H_ZOOM_L:
	case H_ZOOM_R:
		// graphics scale is picked at start, so this is saved now and used next start
		sdl_zoom = (sdl_zoom + (what == H_ZOOM_L ? 4 : 1)) % 5;
		options_save();
		break;
	case H_PANELS_L:
	case H_PANELS_R:
		panels_locked = !panels_locked;
		options_save();
		break;
	case H_CHAT_L:
	case H_CHAT_R:
		panels_set_chat_text(!panels_chat_text());
		options_save();
		break;
	case H_MMAP_L:
	case H_MMAP_R:
		minimap_set_shown(!minimap_shown());
		options_save();
		break;
	case H_RESET:
		panels_reset();
		minimap_reset_layout();
		options_save();
		break;
	case H_APPLY:
		apply_graphics();
		break;
	default:
		break;
	}
}

// Mouse input while the window is open. Returns 1 if the event was used here. The window
// stays open while the game is played: clicks outside it go to the game.
int options_mouse(int what, int x, int y, int delta)
{
	static int pressed = H_NONE;
	int h;

	if (!is_open) {
		return 0;
	}

	if (what != SDL_MOUM_NONE && what != SDL_MOUM_LUP && !options_over(x, y)) {
		return 0;
	}
	if (what == SDL_MOUM_LUP && !win_drag && !win_size && dragging == H_NONE && pressed == H_NONE) {
		return 0; // a click that started in the game
	}

	switch (what) {
	case SDL_MOUM_NONE:
		if (win_size) {
			win_h = clampi(y - win_size_dy - win_y(), WIN_MIN_H, min(PH, YRES - 1));
			return 1;
		}
		if (win_drag) {
			win_dx += x - win_drag_x;
			win_dy += y - win_drag_y;
			win_drag_x = x;
			win_drag_y = y;
			return 1;
		}
		if (dragging != H_NONE) {
			slider_set(dragging, x);
			return 1;
		}
		return 0;

	case SDL_MOUM_LDOWN:
		h = hit_test(x, y);
		if (h == H_GRIP) {
			// keep the top where it is on screen while the bottom follows the mouse
			win_dx = panel_x() - base_x();
			win_dy = win_y() - base_y();
			win_size = 1;
			win_size_dy = y - (win_y() + wh());
			pressed = H_NONE;
			return 1;
		}
		if (h == H_SFX || h == H_MUSIC || is_zoom_slider(h)) {
			dragging = h;
			drag_panel = hit_panel;
			drag_ui = sdl_ui_size;
			slider_set(h, x);
		}
		if (h == H_TITLE) {
			// start from where the window is on screen, then follow the mouse
			win_dx = panel_x() - base_x();
			win_dy = win_y() - base_y();
			win_drag = 1;
			win_drag_x = x;
			win_drag_y = y;
		}
		pressed = h;
		return 1;

	case SDL_MOUM_LUP:
		if (win_size) {
			// base_y() moves with the height: keep the top where it is
			int top = win_y();
			win_size = 0;
			win_h = wh();
			win_dy = top - base_y();
			pressed = H_NONE;
			options_save();
			return 1;
		}
		if (win_drag) {
			win_drag = 0;
			pressed = H_NONE;
			options_save();
			return 1;
		}
		if (dragging != H_NONE) {
			if (dragging == H_UI) {
				zoom_set(H_UI, drag_ui);
			}
			if (dirty || is_zoom_slider(dragging)) {
				options_save();
			}
			dragging = H_NONE;
			pressed = H_NONE;
			return 1;
		}
		h = hit_test(x, y);
		if (h != H_NONE && h == pressed) {
			click(h);
		}
		pressed = H_NONE;
		return 1;

	case SDL_MOUM_WHEEL:
		h = hit_test(x, y);
		if (h == H_SFX) {
			options_set_sfx_volume(sfx_percent() + (delta > 0 ? 5 : -5));
		} else if (h == H_MUSIC) {
			set_music_volume(music_volume + (delta > 0 ? 5 : -5));
		} else if (h == H_WZOOM) {
			zoom_set(h, sdl_world_zoom + (delta > 0 ? 5 : -5));
			options_save();
		} else if (h == H_UI) {
			zoom_set(h, sdl_ui_size + (delta > 0 ? 5 : -5));
			options_save();
		} else if (h == H_PSCALE) {
			drag_panel = hit_panel;
			zoom_set(h, pnl_scale(hit_panel) + (delta > 0 ? 5 : -5));
			options_save();
		} else {
			win_scroll = clampi(win_scroll - delta * 18, 0, max_scroll());
		}
		return 1;

	default:
		return 1;
	}
}

// The border is drawn as 1 pixel rectangles: scaled lines can lose an edge at some
// interface and panel sizes.
void gui_draw_box_outline(int sx, int sy, int ex, int ey, unsigned short border)
{
	render_rect(sx, sy, ex + 1, sy + 1, border);
	render_rect(sx, ey, ex + 1, ey + 1, border);
	render_rect(sx, sy, sx + 1, ey + 1, border);
	render_rect(ex, sy, ex + 1, ey + 1, border);
}

void gui_draw_box(int sx, int sy, int ex, int ey, unsigned short fill, unsigned short border)
{
	render_rect(sx, sy, ex, ey, fill);
	gui_draw_box_outline(sx, sy, ex, ey, border);
}

static void draw_slider(int row, int value, int hover, const char *label)
{
	int px = panel_x(), py = panel_y();
	int cx = px + CTRL_X, y = py + row;
	int kx = cx + value * SLD_W / 100;
	unsigned short gold = IRGB(26, 20, 8);

	render_text(px + 16, y, textcolor, 0, label);

	render_rect(cx, y + 4, cx + SLD_W, y + 7, IRGB(6, 5, 3));
	render_rect(cx, y + 4, kx, y + 7, gold);
	gui_draw_box(kx - 3, y - 1, kx + 3, y + 11, hover ? whitecolor : IRGB(22, 20, 16), IRGB(4, 3, 2));

	render_text_fmt(cx + SLD_W + 12, y, textcolor, 0, "%d%%", value);
}

// A zoom slider: lo (zoom out) on the left, hi (zoom in) on the right, value v in percent.
// note (may be NULL) is shown small between the two end captions.
static void draw_zoom_slider(int row, int v, int lo, int hi, int hover, const char *label, const char *note)
{
	int px = panel_x(), py = panel_y();
	int cx = px + CTRL_X, y = py + row;
	int kx = cx + clampi(v - lo, 0, hi - lo) * SLD_W / (hi - lo);
	unsigned short gold = IRGB(26, 20, 8);

	render_text(px + 16, y, textcolor, 0, label);

	render_rect(cx, y + 4, cx + SLD_W, y + 7, IRGB(6, 5, 3));
	render_rect(cx, y + 4, kx, y + 7, gold);
	gui_draw_box(kx - 3, y - 1, kx + 3, y + 11, hover ? whitecolor : IRGB(22, 20, 16), IRGB(4, 3, 2));

	render_text_fmt(cx + SLD_W + 12, y, textcolor, 0, "%d%%", v);

	render_text(cx, y + 13, graycolor, RENDER_TEXT_SMALL, "Zoom out");
	render_text(cx + SLD_W, y + 13, graycolor, RENDER_TEXT_SMALL | RENDER_TEXT_RIGHT, "Zoom in");
	if (note) {
		render_text(cx + SLD_W / 2, y + 13, IRGB(31, 24, 10), RENDER_TEXT_SMALL | RENDER_ALIGN_CENTER, note);
	}
}

static void draw_selector(int row, const char *label, const char *value, int enabled, int hover_l, int hover_r)
{
	int px = panel_x(), py = panel_y();
	int cx = px + CTRL_X, y = py + row;
	unsigned short col = enabled ? whitecolor : graycolor;

	render_text(px + 16, y, enabled ? textcolor : graycolor, 0, label);

	gui_draw_box(cx, y - 2, cx + SEL_W, y + 11, IRGB(5, 4, 3), IRGB(14, 11, 6));
	if (enabled) {
		gui_draw_box(cx, y - 2, cx + 14, y + 11, hover_l ? IRGB(16, 12, 6) : IRGB(10, 8, 4), IRGB(14, 11, 6));
		gui_draw_box(
		    cx + SEL_W - 14, y - 2, cx + SEL_W, y + 11, hover_r ? IRGB(16, 12, 6) : IRGB(10, 8, 4), IRGB(14, 11, 6));
		render_text(cx + 7, y, whitecolor, RENDER_ALIGN_CENTER, "<");
		render_text(cx + SEL_W - 7, y, whitecolor, RENDER_ALIGN_CENTER, ">");
	}
	render_text(cx + SEL_W / 2, y, col, RENDER_ALIGN_CENTER, value);
}

static void draw_button(int sx, int sy, const char *text, int enabled, int hover)
{
	unsigned short fill = !enabled ? IRGB(6, 5, 4) : (hover ? IRGB(18, 13, 6) : IRGB(11, 8, 4));

	gui_draw_box(sx, sy, sx + BTN_W, sy + BTN_H, fill, IRGB(20, 16, 8));
	render_text(sx + BTN_W / 2, sy + 3, enabled ? whitecolor : graycolor, RENDER_ALIGN_CENTER, text);
}

#define RADIUS 10 // corner radius of the window

// How far row i of a rounded rectangle h rows high is indented at its corners
static int corner_inset(int i, int h)
{
	int j = i < h - 1 - i ? i : h - 1 - i;
	float dy;

	if (j >= RADIUS) {
		return 0;
	}
	dy = (float)RADIUS - (float)j - 0.5f;
	return (int)((float)RADIUS - SDL_sqrtf((float)(RADIUS * RADIUS) - dy * dy) + 0.5f);
}

// Window with rounded corners, a see-through body and a title bar of title_h rows.
// Drawn one row at a time so no pixel is blended twice.
void gui_draw_panel(int x, int y, int w, int h, int title_h)
{
	unsigned short border = IRGB(20, 16, 8);
	int i, in, edge;

	// shadow, 4 pixels down and right, only where the window does not cover it
	for (i = 0; i < h; i++) {
		int sx = x + 4 + corner_inset(i, h);
		if (i + 4 < h && sx < x + w - corner_inset(i + 4, h)) {
			sx = x + w - corner_inset(i + 4, h);
		}
		render_rect_alpha(sx, y + 4 + i, x + 4 + w - corner_inset(i, h), y + 5 + i, blackcolor, 90);
	}

	for (i = 0; i < h; i++) {
		in = corner_inset(i, h);
		if (i < title_h) {
			render_rect_alpha(x + in, y + i, x + w - in, y + i + 1, IRGB(9, 7, 4), 225);
		} else {
			render_rect_alpha(x + in, y + i, x + w - in, y + i + 1, IRGB(2, 2, 2), 190);
		}

		// outline: top, bottom and title separator rows are solid, the others get their
		// edge pixels, widened where the corner curve steps by more than one pixel
		if (i == 0 || i == h - 1 || i == title_h - 1) {
			render_rect(x + in, y + i, x + w - in, y + i + 1, border);
			continue;
		}
		edge = corner_inset(i < h / 2 ? i - 1 : i + 1, h);
		if (edge < in + 1) {
			edge = in + 1;
		}
		render_rect(x + in, y + i, x + edge, y + i + 1, border);
		render_rect(x + w - edge, y + i, x + w - in, y + i + 1, border);
	}
}

void options_display(void)
{
	int px, py, wy, h, hov, pending, best, dw, dh;
	char buf[64];
	unsigned short gold = IRGB(31, 24, 10);

	if (!is_open) {
		return;
	}

	px = panel_x();
	py = panel_y();
	wy = win_y();
	h = wh();
	hov = dragging != H_NONE ? dragging : hit_test(mousex, mousey);
	pending = changes_pending();

	render_push_clip();
	render_set_clip(0, 0, XRES, YRES);

	gui_draw_panel(px, wy, PW + 1, h + 1, WIN_TITLE);

	render_text(px + PW / 2, wy + 5, whitecolor, RENDER_ALIGN_CENTER, "Settings");
	render_text(px + PW - 11, wy + 5, hov == H_CLOSE ? whitecolor : lightgraycolor, RENDER_ALIGN_CENTER, "X");

	draw_button(px + PW / 2 - BTN_W - 8, wy + h - BTN_H - 10, "Apply", pending, hov == H_APPLY);
	draw_button(px + PW / 2 + 8, wy + h - BTN_H - 10, "Close", 1, hov == H_DONE);

	// resize grip: three short diagonal lines
	for (int i = 0; i < 3; i++) {
		render_line(px + PW - 5 - i * 3, wy + h - 3, px + PW - 3, wy + h - 5 - i * 3,
		    hov == H_GRIP || win_size ? whitecolor : IRGB(20, 16, 8));
	}

	// scroll position, when the window is too short for all settings
	if (max_scroll() > 0) {
		int ch = h - WIN_TITLE - WIN_FOOT, bar = max(10, ch * ch / CONTENT_H);
		int by = wy + WIN_TITLE + (ch - bar) * win_scroll / max_scroll();
		render_rect(px + PW - 5, by, px + PW - 2, by + bar, IRGB(16, 12, 6));
	}

	// the settings, clipped to the space between title and buttons
	render_set_clip(px, wy + WIN_TITLE, px + PW, wy + h - WIN_FOOT);

	// Audio
	render_text(px + 10, py + 24, gold, 0, "Audio");
	render_line(px + 10, py + 35, px + PW - 10, py + 35, IRGB(10, 8, 4));
	draw_slider(ROW_SFX, sfx_percent(), hov == H_SFX, "Sound effects");
	draw_slider(ROW_MUSIC, music_volume, hov == H_MUSIC, "Music");

	// Graphics
	render_text(px + 10, py + 84, gold, 0, "Graphics");
	render_line(px + 10, py + 95, px + PW - 10, py + 95, IRGB(10, 8, 4));
	draw_selector(ROW_MODE, "Display mode", mode_name[sel_mode], 1, hov == H_MODE_L, hov == H_MODE_R);

	if (sel_mode == DISPMODE_BORDERLESS) {
		sdl_get_display_size(&dw, &dh);
		snprintf(buf, sizeof(buf), "%d x %d (desktop)", dw, dh);
		draw_selector(ROW_RES, "Resolution", buf, 0, 0, 0);
	} else if (res_cnt > 0) {
		snprintf(buf, sizeof(buf), "%d x %d", res_w[sel_res], res_h[sel_res]);
		draw_selector(ROW_RES, sel_mode == DISPMODE_WINDOWED ? "Window size" : "Resolution", buf, 1, hov == H_RES_L,
		    hov == H_RES_R);
	}

	draw_selector(ROW_ZOOM, "Graphics size", zoom_name[clampi(sdl_zoom, 0, 4)], 1, hov == H_ZOOM_L, hov == H_ZOOM_R);
	if (sdl_world_zoom < sdl_world_min_zoom()) {
		snprintf(buf, sizeof(buf), "(min %d%%)", sdl_world_min_zoom());
	}
	draw_zoom_slider(ROW_WZOOM, sdl_world_zoom, WORLD_ZOOM_MIN, WORLD_ZOOM_MAX, hov == H_WZOOM, "World zoom",
	    sdl_world_zoom < sdl_world_min_zoom() ? buf : NULL);

	{
		int v = dragging == H_UI ? drag_ui : sdl_ui_size;
		int capped = (int)(sdl_ui_scale * 100.0f + 0.5f) < v;

		snprintf(buf, sizeof(buf), "(max %d%%)", (int)(sdl_ui_scale * 100.0f + 0.5f));
		draw_zoom_slider(ROW_UI, v, UI_MIN, UI_MAX, hov == H_UI, "Interface size", capped ? buf : NULL);
	}
	draw_selector(ROW_PANELS, "Panels", panels_locked ? "Locked" : "Movable (drag title)", 1, hov == H_PANELS_L,
	    hov == H_PANELS_R);
	for (int p = 0; p < MAX_PANEL; p++) {
		char label[32];
		snprintf(label, sizeof(label), "%s scale", pnl_title(p));
		draw_zoom_slider(ROW_PSCALE + p * ZROW_H, pnl_scale(p), PSCALE_MIN, PSCALE_MAX,
		    hov == H_PSCALE && (dragging == H_PSCALE ? drag_panel : hit_panel) == p, label, NULL);
	}
	draw_selector(ROW_CHAT, "Chat text", panels_chat_text() ? "Large" : "Normal", 1, hov == H_CHAT_L, hov == H_CHAT_R);
	draw_selector(ROW_MINIMAP, "Minimap",
	    (game_options & GO_NOMAP) ? "Off (start option)" : (minimap_shown() ? "Shown (drag to move)" : "Hidden"),
	    !(game_options & GO_NOMAP), hov == H_MMAP_L, hov == H_MMAP_R);
	render_text(px + 16, py + ROW_RESET, textcolor, 0, "Panel layout");
	draw_button(px + CTRL_X, py + ROW_RESET - 3, "Reset", 1, hov == H_RESET);

	// note
	best = sdl_best_scale();
	if (pending) {
		render_text(
		    px + 16, py + ROW_NOTE, lightgraycolor, RENDER_TEXT_SMALL, "Press Apply to use the new display settings.");
	} else if (best != sdl_scale) {
		render_text(px + 16, py + ROW_NOTE, IRGB(31, 24, 10), RENDER_TEXT_SMALL,
		    "Restart the client to use the new graphics size.");
	} else if (sdl_display_mode == DISPMODE_WINDOWED) {
		render_text(px + 16, py + ROW_NOTE, lightgraycolor, RENDER_TEXT_SMALL, "Drag the window's edges to resize it.");
	}
	render_text_fmt(px + 16, py + ROW_NOTE + 12, graycolor, RENDER_TEXT_SMALL, "Graphics scale: %dx", sdl_scale);

	render_pop_clip();
}
