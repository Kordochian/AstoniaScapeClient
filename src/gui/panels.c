/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Interface panels
 *
 * The top bar and the bottom window are cut into panels (level bars, equipment, menu,
 * skills, status, chat, inventory). Each panel sits in a frame like the Options window and
 * can be dragged by its title strip. Fixed size panels show their piece of the original
 * picture. Skills, chat and inventory can be resized with the grip in their bottom right
 * corner: they then show more or fewer lines, rows or columns, at the normal text and item
 * size, on a plain background. Positions and sizes are saved in settings.ini, positions
 * anchored to the nearest screen edge so they survive window size changes.
 *
 * Everything inside a panel is positioned by dots and buttons. init_dots() lays them out
 * relative to the original pictures and then moves each one by its panel's offset
 * (pnl_dx(), pnl_dy()).
 *
 * Each panel also has its own scale. A scaled panel keeps its layout (dots, buttons, clip)
 * at normal size with its top left corner at panel.x, panel.y and is drawn enlarged from
 * there; pnl_lx()/pnl_ly() turn a screen position into the panel's layout position, and
 * pnl_mx()/pnl_my() do that for the mouse.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>

#include "astonia.h"
#include "gui/gui.h"
#include "gui/gui_private.h"
#include "game/game.h"
#include "client/client.h"
#include "sdl/sdl.h"

#define PANEL_PAD 3 // frame width left, right and below the contents
#define PANEL_GAP 0 // space between panels in the default layout
#define GRIP      10 // size of the resize grip in the bottom right corner

#define RS_W 1 // width can be changed
#define RS_H 2 // height can be changed

struct panel {
	const char *key; // settings.ini name
	const char *title;
	int bottom; // 0: piece of the top bar picture, 1: of the bottom window picture
	int bx, bw; // piece of the picture: columns bx .. bx+bw-1
	int resize; // RS_W, RS_H
	int minw, maxw, minh, maxh, steph; // content size limits, height steps from minh

	int x, y; // frame top left in GUI pixels
	int cw, ch; // content size; 0: the picture piece's size
	int saved; // position comes from the settings / a drag, not the default layout
	int ax, ox, ay, oy; // saved position: anchor (0 left/top, 1 center, 2 right/bottom) and offset
	int scale; // percent; 0 is 100
};

static struct panel panel[MAX_PANEL] = {
    {"level", "Level", 0, 0, 156},
    {"equipment", "Equipment", 0, 156, 484},
    {"menu", "Menu", 0, 640, 160},
    // skill list: 10 pixels per line, 3 to MAXSKLROWS lines
    {"skills", "Skills", 1, 0, 174, RS_H, 0, 0, 40, 10 + MAXSKLROWS * 10, 10},
    {"status", "Effects", 1, 174, 47},
    // chat: free size, at least 3 lines
    {"chat", "Chat", 1, 221, 403, RS_W | RS_H, 200, 1400, 50, 650, 1},
    // inventory: 40 pixels per row, 1 to MAXINVROWS rows
    {"inventory", "Inventory", 1, 624, 176, RS_H, 0, 0, 50, 10 + MAXINVROWS * 40, 40},
};

int panels_locked = 0;

static int drag = -1, drag_dx, drag_dy, sizing = 0;

static int bot_h(void)
{
	return (game_options & GO_SMALLBOT) ? 130 : 170;
}

static int clampi(int v, int lo, int hi)
{
	if (v > hi) {
		v = hi;
	}
	if (v < lo) {
		v = lo;
	}
	return v;
}

// content size
int pnl_cw(int p)
{
	return (panel[p].resize & RS_W) && panel[p].cw ? panel[p].cw : panel[p].bw;
}

int pnl_ch(int p)
{
	if ((panel[p].resize & RS_H) && panel[p].ch) {
		return panel[p].ch;
	}
	return panel[p].bottom ? bot_h() : 40;
}

int pnl_w(int p)
{
	return pnl_cw(p) + PANEL_PAD * 2;
}

int pnl_h(int p)
{
	return pnl_ch(p) + PANEL_TITLE + PANEL_PAD;
}

// Scale and size on screen
int pnl_scale(int p)
{
	return panel[p].scale ? panel[p].scale : 100;
}

static float pnl_s(int p)
{
	return pnl_scale(p) / 100.0f;
}

int pnl_sw(int p)
{
	return (int)(pnl_w(p) * pnl_s(p) + 0.5f);
}

int pnl_sh(int p)
{
	return (int)(pnl_h(p) * pnl_s(p) + 0.5f);
}

// Screen position to the panel's layout position and back
int pnl_lx(int p, int x)
{
	return panel[p].x + (int)floorf((x - panel[p].x) / pnl_s(p));
}

int pnl_ly(int p, int y)
{
	return panel[p].y + (int)floorf((y - panel[p].y) / pnl_s(p));
}

int pnl_sx(int p, int x)
{
	return panel[p].x + (int)floorf((x - panel[p].x) * pnl_s(p) + 0.5f);
}

int pnl_sy(int p, int y)
{
	return panel[p].y + (int)floorf((y - panel[p].y) * pnl_s(p) + 0.5f);
}

// The mouse in panel p's layout, or far away when the mouse is not over that panel
int pnl_mx(int p)
{
	return pnl_at(mousex, mousey) == p ? pnl_lx(p, mousex) : -10000;
}

int pnl_my(int p)
{
	return pnl_at(mousex, mousey) == p ? pnl_ly(p, mousey) : -10000;
}

int pnl_x(int p)
{
	return panel[p].x;
}

int pnl_y(int p)
{
	return panel[p].y;
}

// Offset from the original picture coordinates (picture top left at 0,0) to the screen
int pnl_dx(int p)
{
	return panel[p].x + PANEL_PAD - panel[p].bx;
}

int pnl_dy(int p)
{
	return panel[p].y + PANEL_TITLE;
}

// Height of the default top row and the top of the default bottom row. These place the map
// center, so they stay at the normal panel size: scaling a panel must not move the world.
int panels_top_row_h(void)
{
	return 40 + PANEL_TITLE + PANEL_PAD;
}

int panels_bottom_row_y(void)
{
	return YRES - (bot_h() + PANEL_TITLE + PANEL_PAD);
}

// Snap a content size to the panel's limits and steps
static void fit_size(int p, int *w, int *h)
{
	struct panel *pn = &panel[p];
	int sw = (int)(XRES / pnl_s(p)), sh = (int)(YRES / pnl_s(p)); // screen in layout pixels

	if (pn->resize & RS_W) {
		*w = clampi(*w, pn->minw, pn->maxw);
		*w = clampi(*w, pn->minw, sw - PANEL_PAD * 2);
	}
	if (pn->resize & RS_H) {
		*h = clampi(*h, pn->minh, pn->maxh);
		*h = clampi(*h, pn->minh, sh - PANEL_TITLE - PANEL_PAD);
		if (pn->steph > 1) {
			*h = pn->minh + (*h - pn->minh + pn->steph / 2) / pn->steph * pn->steph;
			while (*h > pn->maxh || *h > sh - PANEL_TITLE - PANEL_PAD) {
				*h -= pn->steph;
			}
			if (*h < pn->minh) {
				*h = pn->minh;
			}
		}
	}
}

// Default layout: the pieces side by side as in the original bars, centered
static void default_pos(int p, int *px, int *py)
{
	int i, w = 0, x;

	for (i = 0; i < MAX_PANEL; i++) {
		if (panel[i].bottom == panel[p].bottom) {
			w += pnl_sw(i) + PANEL_GAP;
		}
	}
	w -= PANEL_GAP;

	x = (XRES - w) / 2;
	for (i = 0; i < p; i++) {
		if (panel[i].bottom == panel[p].bottom) {
			x += pnl_sw(i) + PANEL_GAP;
		}
	}
	*px = x;
	*py = panel[p].bottom ? YRES - pnl_sh(p) : 0;
}

static int anchor_pos(int a, int o, int size, int screen)
{
	switch (a) {
	case 0:
		return o;
	case 1:
		return (screen - size) / 2 + o;
	default:
		return screen - size - o;
	}
}

// Remember the current position, anchored to the edge (or center) nearest to the panel
static void store_anchor(int p)
{
	int w = pnl_sw(p), h = pnl_sh(p);
	int cx = panel[p].x + w / 2, cy = panel[p].y + h / 2;

	panel[p].ax = cx < XRES / 3 ? 0 : (cx < XRES * 2 / 3 ? 1 : 2);
	panel[p].ay = cy < YRES / 3 ? 0 : (cy < YRES * 2 / 3 ? 1 : 2);
	panel[p].ox = panel[p].ax == 0 ? panel[p].x : (panel[p].ax == 1 ? panel[p].x - (XRES - w) / 2 : XRES - w - panel[p].x);
	panel[p].oy = panel[p].ay == 0 ? panel[p].y : (panel[p].ay == 1 ? panel[p].y - (YRES - h) / 2 : YRES - h - panel[p].y);
	panel[p].saved = 1;
}

// Work out all panel sizes and positions for the current game area size. Called by
// init_dots().
void panels_layout(void)
{
	int p, w, h;

	for (p = 0; p < MAX_PANEL; p++) {
		if (panel[p].cw || panel[p].ch) {
			w = pnl_cw(p);
			h = pnl_ch(p);
			fit_size(p, &w, &h);
			if (panel[p].cw) {
				panel[p].cw = w;
			}
			if (panel[p].ch) {
				panel[p].ch = h;
			}
		}
	}
	for (p = 0; p < MAX_PANEL; p++) {
		if (p == drag) {
			continue; // being dragged: keep where the mouse put it
		}
		if (panel[p].saved) {
			panel[p].x = anchor_pos(panel[p].ax, panel[p].ox, pnl_sw(p), XRES);
			panel[p].y = anchor_pos(panel[p].ay, panel[p].oy, pnl_sh(p), YRES);
		} else {
			default_pos(p, &panel[p].x, &panel[p].y);
		}
		panel[p].x = clampi(panel[p].x, 0, XRES - pnl_sw(p));
		panel[p].y = clampi(panel[p].y, 0, YRES - pnl_sh(p));
	}
}

// Which panel is at this screen position? -1 for none. Later panels are drawn on top.
int pnl_at(int x, int y)
{
	int p;

	for (p = MAX_PANEL - 1; p >= 0; p--) {
		if (x >= panel[p].x && x < panel[p].x + pnl_sw(p) && y >= panel[p].y && y < panel[p].y + pnl_sh(p)) {
			return p;
		}
	}
	return -1;
}

static int on_title(int p, int x, int y)
{
	return x >= panel[p].x && x < panel[p].x + pnl_sw(p) && y >= panel[p].y &&
	       y < panel[p].y + (int)(PANEL_TITLE * pnl_s(p));
}

static int on_grip(int p, int x, int y)
{
	int ex = panel[p].x + pnl_sw(p), ey = panel[p].y + pnl_sh(p), g = (int)(GRIP * pnl_s(p));

	return panel[p].resize && x >= ex - g && x < ex && y >= ey - g && y < ey;
}

// Panels moved or resized: lay out the dots and buttons again (the map stays where it is)
static void panels_moved(void)
{
	// the help window can be dragged on its own; keep it where it is
	DOT hlp = dot[DOT_HLP], hl2 = dot[DOT_HL2];
	BUT hdrag = but[BUT_HELP_DRAG];

	init_dots();
	dot[DOT_HLP] = hlp;
	dot[DOT_HL2] = hl2;
	but[BUT_HELP_DRAG] = hdrag;

	max_invoff = max(0, ((_inventorysize - 30) / INVDX) - INVDY);
	max_conoff = max(0, (con_cnt / CONDX) - CONDY);
	max_skloff = max(0, skltab_cnt - SKLDY);
	set_invoff(0, invoff);
	set_skloff(0, skloff);
	set_conoff(0, conoff);
	set_button_flags();
}

// Put every panel back to the default layout and size
void panels_reset(void)
{
	int p;

	for (p = 0; p < MAX_PANEL; p++) {
		panel[p].saved = 0;
		panel[p].cw = panel[p].ch = 0;
		panel[p].scale = 0;
	}
	drag = -1;
	panels_moved();
	gui_layout_changed();
}

// Mouse input for dragging panels by their title strip and resizing them by their grip.
// Returns 1 if the event was used.
int panels_mouse(int what, int x, int y)
{
	int p, w, h;

	switch (what) {
	case SDL_MOUM_NONE:
		if (drag == -1) {
			return 0;
		}
		if (sizing) {
			w = (int)((x - drag_dx - panel[drag].x) / pnl_s(drag)) - PANEL_PAD * 2;
			h = (int)((y - drag_dy - panel[drag].y) / pnl_s(drag)) - PANEL_TITLE - PANEL_PAD;
			fit_size(drag, &w, &h);
			if (panel[drag].resize & RS_W) {
				panel[drag].cw = w;
			}
			if (panel[drag].resize & RS_H) {
				panel[drag].ch = h;
			}
			panel[drag].x = clampi(panel[drag].x, 0, XRES - pnl_sw(drag));
			panel[drag].y = clampi(panel[drag].y, 0, YRES - pnl_sh(drag));
		} else {
			panel[drag].x = clampi(x - drag_dx, 0, XRES - pnl_sw(drag));
			panel[drag].y = clampi(y - drag_dy, 0, YRES - pnl_sh(drag));
		}
		panels_moved();
		return 1;

	case SDL_MOUM_LDOWN:
		if (panels_locked || csprite) {
			return 0;
		}
		p = pnl_at(x, y);
		if (p == -1) {
			return 0;
		}
		if (on_grip(p, x, y)) {
			sizing = 1;
			// distance from the mouse to the frame's bottom right corner
			drag_dx = x - (panel[p].x + pnl_sw(p));
			drag_dy = y - (panel[p].y + pnl_sh(p));
		} else if (on_title(p, x, y)) {
			sizing = 0;
			drag_dx = x - panel[p].x;
			drag_dy = y - panel[p].y;
		} else {
			return 0;
		}
		drag = p;
		return 1;

	case SDL_MOUM_LUP:
		if (drag == -1) {
			return 0;
		}
		store_anchor(drag);
		drag = -1;
		sizing = 0;
		panels_moved();
		options_save();
		return 1;

	default:
		return drag != -1;
	}
}

int panels_dragging(void)
{
	return drag != -1;
}

// settings.ini: panel_<key>=ax,ox,ay,oy[,w,h] and pscale_<key>=percent
int panels_load_setting(const char *key, const char *val)
{
	int p, ax, ox, ay, oy, w = 0, h = 0, n;

	if (!strncmp(key, "pscale_", 7)) {
		for (p = 0; p < MAX_PANEL; p++) {
			if (!strcmp(key + 7, panel[p].key)) {
				n = atoi(val);
				panel[p].scale = n == 100 ? 0 : clampi(n, 50, 300);
				return 1;
			}
		}
		return 0;
	}
	if (strncmp(key, "panel_", 6)) {
		return 0;
	}
	for (p = 0; p < MAX_PANEL; p++) {
		if (!strcmp(key + 6, panel[p].key)) {
			n = sscanf(val, "%d,%d,%d,%d,%d,%d", &ax, &ox, &ay, &oy, &w, &h);
			if (n >= 4) {
				panel[p].ax = clampi(ax, 0, 2);
				panel[p].ox = ox;
				panel[p].ay = clampi(ay, 0, 2);
				panel[p].oy = oy;
				panel[p].saved = 1;
			}
			if (n >= 6) {
				panel[p].cw = (panel[p].resize & RS_W) ? w : 0;
				panel[p].ch = (panel[p].resize & RS_H) ? h : 0;
			}
			return 1;
		}
	}
	return 0;
}

void panels_save_settings(void *vfp)
{
	FILE *fp = vfp;
	int p;

	fprintf(fp, "ui_locked=%d\n", panels_locked);
	for (p = 0; p < MAX_PANEL; p++) {
		if (panel[p].saved) {
			fprintf(fp, "panel_%s=%d,%d,%d,%d,%d,%d\n", panel[p].key, panel[p].ax, panel[p].ox, panel[p].ay, panel[p].oy,
			    panel[p].cw, panel[p].ch);
		}
		if (panel[p].scale) {
			fprintf(fp, "pscale_%s=%d\n", panel[p].key, panel[p].scale);
		}
	}
}

// A scroll bar groove between the up and down arrow buttons
static void draw_groove(int bup, int bdw)
{
	int x = but[bup].x, y1 = but[bup].y + 6, y2 = but[bdw].y - 6;

	if (y2 > y1) {
		gui_draw_box(x - 3, y1, x + 3, y2, IRGB(4, 3, 2), IRGB(12, 10, 6));
	}
}

// Start drawing panel p: its scale and offset, clipped to its layout
void panels_pass(int p)
{
	sdl_panel_pass(pnl_s(p), panel[p].x, panel[p].y);
	render_set_clip(panel[p].x, panel[p].y, panel[p].x + pnl_w(p), panel[p].y + pnl_h(p));
}

// Frame, title strip and background of panel p, inside panels_pass(p). The panel contents
// (items, skills, chat, ...) are drawn on top by the display_*() functions.
void panels_display_frame(int p)
{
	int sx, sy, hov, i, ex, ey;
	unsigned int spr;

	hov = (!panels_locked && drag == -1) ? pnl_at(mousex, mousey) : -1;
	if (hov != -1 && !on_title(hov, mousex, mousey) && !on_grip(hov, mousex, mousey)) {
		hov = -1;
	}

	render_push_clip();
	gui_draw_panel(panel[p].x, panel[p].y, pnl_w(p), pnl_h(p), PANEL_TITLE);
	if ((p == drag && !sizing) || (p == hov && on_title(p, mousex, mousey))) {
		render_rect_alpha(panel[p].x + 2, panel[p].y + 1, panel[p].x + pnl_w(p) - 2, panel[p].y + PANEL_TITLE - 1,
		    whitecolor, 24);
	}
	if (pnl_w(p) >= 60) {
		char buf[100];
		const char *title = panel[p].title;

		// while a body or shop is open the Skills panel shows it, so it takes its name
		if (p == PNL_SKILLS && con_cnt) {
			if (con_type == 1) {
				snprintf(buf, sizeof(buf), "%s", con_name);
			} else {
				snprintf(buf, sizeof(buf), "%s's Shop", con_name);
			}
			title = buf;
		}
		// y + 3 centres the small font between the top border and the title separator
		render_text(panel[p].x + pnl_w(p) / 2, panel[p].y + 3, p == drag ? whitecolor : IRGB(31, 24, 10),
		    RENDER_ALIGN_CENTER | RENDER_TEXT_SMALL, title);
	}

	sx = panel[p].x + PANEL_PAD;
	sy = panel[p].y + PANEL_TITLE;

	if (panel[p].resize) {
		// resizable panels have a plain background; the pictures can't stretch
		if (p == PNL_SKILLS) {
			draw_groove(BUT_SCL_UP, BUT_SCL_DW);
		} else if (p == PNL_INVENTORY) {
			draw_groove(BUT_SCR_UP, BUT_SCR_DW);
		}

		// resize grip: three short diagonal lines
		if (!panels_locked) {
			ex = panel[p].x + pnl_w(p) - 3;
			ey = panel[p].y + pnl_h(p) - 3;
			for (i = 0; i < 3; i++) {
				render_line(ex - 2 - i * 3, ey, ex, ey - 2 - i * 3,
				    (p == hov && on_grip(p, mousex, mousey)) || (p == drag && sizing) ? whitecolor : IRGB(20, 16, 8));
			}
		}
	} else {
		spr = panel[p].bottom ? opt_sprite((game_options & GO_SMALLBOT) ? 991 : 998) : opt_sprite(999);
		render_more_clip(sx, sy, sx + panel[p].bw, sy + pnl_ch(p));
		render_sprite(spr, sx - panel[p].bx, sy, RENDERFX_NORMAL_LIGHT, RENDER_ALIGN_NORMAL);
	}
	render_pop_clip();
}

// Panel names for the Options window
const char *pnl_title(int p)
{
	return panel[p].title;
}

// Change a panel's scale. The panel keeps its anchor; the map is centered again.
void panels_set_scale(int p, int percent)
{
	if (p < 0 || p >= MAX_PANEL) {
		return;
	}
	int i, ow, oh, nx, ny;

	// The other panels stay exactly where they are: panels still at their default spot are
	// pinned there first (the default layout would re-center the whole row around the new size)
	for (i = 0; i < MAX_PANEL; i++) {
		if (!panel[i].saved) {
			store_anchor(i);
		}
	}

	// This panel keeps the corner it is anchored to: top left normally, the bottom edge for
	// panels at the bottom of the screen, the right edge for panels at the right
	ow = pnl_sw(p);
	oh = pnl_sh(p);
	panel[p].scale = percent == 100 ? 0 : clampi(percent, 50, 300);
	nx = panel[p].ax == 2 ? panel[p].x + ow - pnl_sw(p) : panel[p].x;
	ny = panel[p].ay == 2 ? panel[p].y + oh - pnl_sh(p) : panel[p].y;
	panel[p].x = clampi(nx, 0, XRES - pnl_sw(p));
	panel[p].y = clampi(ny, 0, YRES - pnl_sh(p));
	store_anchor(p);

	gui_layout_changed();
}

// Chat text size: -1 as the "large font" game option, 0 normal, 1 large
void render_set_textfont(int nr);
int pnl_chat_text = -1;

int panels_chat_text(void)
{
	return pnl_chat_text >= 0 ? pnl_chat_text : ((game_options & GO_LARGE) ? 1 : 0);
}

// Changing the font clears the chat, as its lines are wrapped for one font
void panels_set_chat_text(int t)
{
	pnl_chat_text = t ? 1 : 0;
	render_set_textfont(pnl_chat_text);
}

void panels_apply_chat_text(void)
{
	if (pnl_chat_text >= 0) {
		render_set_textfont(pnl_chat_text);
	}
}
