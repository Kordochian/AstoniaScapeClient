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
#define PH 198

#define ROW_SFX   40
#define ROW_MUSIC 58
#define ROW_MODE  104
#define ROW_RES   122
#define ROW_NOTE  146

#define CTRL_X 112 // left edge of the sliders and selectors
#define SLD_W  130 // slider width
#define SEL_W  170 // selector width

#define BTN_W 64
#define BTN_H 16

enum {
	H_NONE,
	H_CLOSE,
	H_SFX,
	H_MUSIC,
	H_MODE_L,
	H_MODE_R,
	H_RES_L,
	H_RES_R,
	H_APPLY,
	H_DONE,
};

static int is_open = 0;
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

// Read settings.ini. Called before the window is created, so the display mode and
// size saved here replace what -w / -h / -o asked for.
void options_load(void)
{
	char filename[MAX_PATH], line[256], *val, *end;
	FILE *fp;
	int mode = -1, w = 0, h = 0;

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
		}
	}
	fclose(fp);

	if (mode != -1) {
		have_gfx = 1;
		cfg_mode = mode;
		cfg_w = w;
		cfg_h = h;

		sdl_display_mode = mode;
		if (mode == DISPMODE_BORDERLESS) {
			want_width = want_height = 0;
		} else if (w >= 640 && h >= 360) {
			want_width = w;
			want_height = h;
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

	fprintf(fp, "# Astonia client settings, changed with the in-game Options menu\n");
	fprintf(fp, "sfx_volume=%d\n", sfx_percent());
	fprintf(fp, "music_volume=%d\n", music_volume);
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

static int panel_x(void)
{
	return (XRES - PW) / 2;
}

static int panel_y(void)
{
	int y = (YRES - PH) / 2 - 20;
	return y < 4 ? 4 : y;
}

int options_over(int x, int y)
{
	int px = panel_x(), py = panel_y();

	return is_open && x >= px && x < px + PW && y >= py && y < py + PH;
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
	int px = panel_x(), py = panel_y();
	int cx = px + CTRL_X;

	if (!options_over(x, y)) {
		return H_NONE;
	}
	if (in_box(x, y, px + PW - 18, py + 2, px + PW - 4, py + 15)) {
		return H_CLOSE;
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
	if (sel_mode != DISPMODE_BORDERLESS) {
		if (in_box(x, y, cx, py + ROW_RES - 2, cx + 14, py + ROW_RES + 11)) {
			return H_RES_L;
		}
		if (in_box(x, y, cx + SEL_W - 14, py + ROW_RES - 2, cx + SEL_W, py + ROW_RES + 11)) {
			return H_RES_R;
		}
	}
	if (in_box(x, y, px + PW / 2 - BTN_W - 8, py + PH - BTN_H - 10, px + PW / 2 - 8, py + PH - 10)) {
		return changes_pending() ? H_APPLY : H_NONE;
	}
	if (in_box(x, y, px + PW / 2 + 8, py + PH - BTN_H - 10, px + PW / 2 + 8 + BTN_W, py + PH - 10)) {
		return H_DONE;
	}
	return H_NONE;
}

int options_over_control(int x, int y)
{
	return hit_test(x, y) != H_NONE;
}

static void slider_set(int which, int x)
{
	int cx = panel_x() + CTRL_X;
	int v = clampi(((x - cx) * 100 + SLD_W / 2) / SLD_W, 0, 100);

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
	case H_APPLY:
		apply_graphics();
		break;
	default:
		break;
	}
}

// Mouse input while the panel is open. Returns 1 if the event was used here, which
// is always the case while the panel is open: the game underneath gets no clicks.
int options_mouse(int what, int x, int y, int delta)
{
	static int pressed = H_NONE;
	int h;

	if (!is_open) {
		return 0;
	}

	switch (what) {
	case SDL_MOUM_NONE:
		if (dragging != H_NONE) {
			slider_set(dragging, x);
			return 1;
		}
		return 0;

	case SDL_MOUM_LDOWN:
		if (!options_over(x, y)) {
			options_close();
			return 1;
		}
		h = hit_test(x, y);
		if (h == H_SFX || h == H_MUSIC) {
			dragging = h;
			slider_set(h, x);
		}
		pressed = h;
		return 1;

	case SDL_MOUM_LUP:
		if (dragging != H_NONE) {
			dragging = H_NONE;
			if (dirty) {
				options_save();
			}
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
		}
		return 1;

	default:
		return 1;
	}
}

void gui_draw_box(int sx, int sy, int ex, int ey, unsigned short fill, unsigned short border)
{
	render_rect(sx, sy, ex, ey, fill);
	render_line(sx, sy, ex, sy, border);
	render_line(sx, ey, ex, ey, border);
	render_line(sx, sy, sx, ey, border);
	render_line(ex, sy, ex, ey + 1, border);
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
	int px, py, hov, pending, best, dw, dh;
	char buf[64];
	unsigned short gold = IRGB(31, 24, 10);

	if (!is_open) {
		return;
	}

	px = panel_x();
	py = panel_y();
	hov = dragging != H_NONE ? dragging : hit_test(mousex, mousey);
	pending = changes_pending();

	render_push_clip();
	render_set_clip(0, 0, XRES, YRES);

	gui_draw_panel(px, py, PW + 1, PH + 1, 18);

	render_text(px + PW / 2, py + 4, whitecolor, RENDER_ALIGN_CENTER, "Options");
	render_text(px + PW - 11, py + 4, hov == H_CLOSE ? whitecolor : lightgraycolor, RENDER_ALIGN_CENTER, "X");

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

	// note
	best = sdl_best_scale();
	if (pending) {
		render_text(
		    px + 16, py + ROW_NOTE, lightgraycolor, RENDER_TEXT_SMALL, "Press Apply to use the new display settings.");
	} else if (best != sdl_scale) {
		render_text(px + 16, py + ROW_NOTE, IRGB(31, 24, 10), RENDER_TEXT_SMALL,
		    "Restart the client to fit the graphics to this size.");
	} else if (sdl_display_mode == DISPMODE_WINDOWED) {
		render_text(px + 16, py + ROW_NOTE, lightgraycolor, RENDER_TEXT_SMALL, "Drag the window's edges to resize it.");
	}
	render_text_fmt(px + 16, py + ROW_NOTE + 12, graycolor, RENDER_TEXT_SMALL, "Graphics scale: %dx", sdl_scale);

	draw_button(px + PW / 2 - BTN_W - 8, py + PH - BTN_H - 10, "Apply", pending, hov == H_APPLY);
	draw_button(px + PW / 2 + 8, py + PH - BTN_H - 10, "Close", 1, hov == H_DONE);

	render_pop_clip();
}
