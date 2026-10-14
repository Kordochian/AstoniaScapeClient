/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Furnace, anvil and workbench window (protocol 7, workbench protocol 9): the server sends the
 * recipes of the station the player clicked (SV_CRAFTLIST, server skilling.c send_craft_list).
 * The window shows them as a grid of items: the item, its name and the bars it needs. Pick an
 * amount on the right (1, 5, 10, All), then click an item to make it; that sends
 * "/smelt <key> <count>", "/smith <key> <count>" or "/craft <key> <count>". After each make the
 * server sends the list again with new counts. Protocol 9 servers also send the window title,
 * command, skill name, tool and station name, so new stations need no client change.
 *
 * Anvil and workbench recipes are grouped by their material (bar, logs ...). Tabs above the grid
 * switch it; when the window opens it picks the best material the player carries.
 */

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <SDL3/SDL.h>

#include "astonia.h"
#include "gui/gui.h"
#include "gui/gui_private.h"
#include "game/game.h"
#include "client/client.h"
#include "client/protocol.h"
#include "sdl/sdl.h"
#include "modder/modder.h"

#define CW_COLS   5
#define CW_CELL_W 84
#define CW_CELL_H 56
#define CW_ROWS   4
#define CW_TITLE  20
#define CW_TABS   (CW_TITLE + 6) // metal tabs
#define CW_GRID   (CW_TABS + 20) // first grid row
#define CW_BTN_X  (12 + CW_COLS * CW_CELL_W + 14) // amount buttons
#define CW_BTN_W  40
#define CW_BTN_H  30
#define CW_W      (CW_BTN_X + CW_BTN_W + 12)
#define CW_H      (CW_GRID + CW_ROWS * CW_CELL_H + 36) // info line under the grid

#define MAXCRAFT 128
#define MAXMAT   32

#define CRAFT_OPEN     1
#define CRAFT_NOHAMMER 2 // no tool for this station (hammer, knife)
#define CRAFT_GROUP    4 // protocol 9: group recipes by material

struct craft_recipe {
	int nr, level, canmake, xp;
	unsigned int sprite;
	int mat, matcnt; // first material and how many of it one make uses, -1 = none
	char key[32];
	char name[40];
	char need[164];
};

struct craft_mat {
	int nr, have;
	char name[40];
	char tab[40]; // name without " bar" / " logs"
};

enum {
	C_NONE = 0,
	C_CLOSE,
	C_TITLE,
	C_AMOUNT, // + amount button (1, 5, 10, all)
	C_TAB = C_AMOUNT + 4, // + material tab
	C_CELL = C_TAB + MAXMAT, // + visible cell
};

static const int amount_cnt[4] = {1, 5, 10, 0}; // 0 = all
static const char *amount_label[4] = {"1", "5", "10", "All"};

static struct craft_recipe recipe[MAXCRAFT];
static int recipe_cnt = 0;
static struct craft_mat mat[MAXMAT];
static int mat_cnt = 0;
static int cw_station = 0; // 1 furnace, 2 anvil, 3 workbench
static int cw_flags = 0, cw_level = 0;
// station texts (sent by protocol 9 servers, else set from the station number)
static char cw_title[96], cw_cmd[16], cw_skill[40], cw_tool[40], cw_stname[40];
static int cw_open = 0;
static int cw_dx = 0, cw_dy = 0, cw_drag = 0, cw_drag_x, cw_drag_y;
static int cw_pressed = C_NONE;
static int cw_scroll = 0; // in grid rows
static int cw_amount = 0; // index into amount_cnt
static int cw_mat = -1; // material shown (server number), -1 = all recipes

static int shown[MAXCRAFT], shown_cnt = 0; // recipes in the grid

static int cw_x(void)
{
	int x = (XRES - CW_W) / 2 + cw_dx;
	return x < 0 ? 0 : (x > XRES - CW_W - 1 ? XRES - CW_W - 1 : x);
}

static int cw_y(void)
{
	int y = (YRES - CW_H) / 2 - 20 + cw_dy;
	return y < 0 ? 0 : (y > YRES - CW_H - 1 ? YRES - CW_H - 1 : y);
}

static int cw_tabs(void)
{
	return (cw_station == 2 || (cw_flags & CRAFT_GROUP)) && mat_cnt > 1;
}

static int cw_can_make(const struct craft_recipe *r)
{
	if (cw_level < r->level || r->canmake < 1) {
		return 0;
	}
	if (cw_flags & CRAFT_NOHAMMER) {
		return 0;
	}
	return 1;
}

static int cw_rows_total(void)
{
	return (shown_cnt + CW_COLS - 1) / CW_COLS;
}

static void cw_clamp_scroll(void)
{
	if (cw_scroll > cw_rows_total() - CW_ROWS) {
		cw_scroll = cw_rows_total() - CW_ROWS;
	}
	if (cw_scroll < 0) {
		cw_scroll = 0;
	}
}

static void cw_build(void)
{
	int i;

	shown_cnt = 0;
	for (i = 0; i < recipe_cnt; i++) {
		if (!cw_tabs() || cw_mat == -1 || recipe[i].mat == cw_mat) {
			shown[shown_cnt++] = i;
		}
	}
	cw_clamp_scroll();
}

// lowest level of the recipes made from material m
static int mat_level(int m)
{
	int i, lvl = 9999;

	for (i = 0; i < recipe_cnt; i++) {
		if (recipe[i].mat == m && recipe[i].level < lvl) {
			lvl = recipe[i].level;
		}
	}
	return lvl;
}

// the best metal the player carries bars of and can work; else the lowest one
static void cw_pick_material(void)
{
	int m, best = -1, best_lvl = -1, low = -1, low_lvl = 99999, lvl;

	for (m = 0; m < mat_cnt; m++) {
		lvl = mat_level(mat[m].nr);
		if (mat[m].have > 0 && lvl <= cw_level && lvl > best_lvl) {
			best = m;
			best_lvl = lvl;
		}
		if (lvl < low_lvl) {
			low = m;
			low_lvl = lvl;
		}
	}
	if (best == -1) {
		best = low;
	}
	cw_mat = best == -1 ? -1 : mat[best].nr;
}

static int cw_string(const unsigned char *buf, int p, int end, char *dst, int size)
{
	int len, n;

	if (p >= end) {
		dst[0] = 0;
		return end;
	}
	len = buf[p++];
	if (p + len > end) {
		len = end - p;
	}
	n = len < size - 1 ? len : size - 1;
	memcpy(dst, buf + p, (size_t)n);
	dst[n] = 0;
	return p + len;
}

// SV_CRAFTLIST: u16 total length, station, flags, level, count, the recipes, then the materials
void craftwin_receive(const unsigned char *buf, int len)
{
	int p, n, m, cnt, station, flags;
	char *s;

	station = buf[3];
	flags = buf[4];
	if (!(flags & CRAFT_OPEN) && (!cw_open || station != cw_station)) {
		return; // a refresh for a window that is not showing
	}
	if (station != cw_station) {
		cw_scroll = 0;
	}
	cw_station = station;
	cw_flags = flags;
	cw_level = buf[5];
	cnt = buf[6];

	for (p = 7, n = 0; n < cnt && p + 11 <= len; n++) {
		struct craft_recipe tmp, *r = n < MAXCRAFT ? recipe + n : &tmp;
		r->nr = buf[p];
		r->level = buf[p + 1];
		r->canmake = buf[p + 2];
		r->xp = load_u16(buf + p + 3);
		r->sprite = load_u32(buf + p + 5);
		r->mat = buf[p + 9] == 255 ? -1 : buf[p + 9];
		r->matcnt = buf[p + 10];
		p += 11;
		p = cw_string(buf, p, len, r->key, sizeof(r->key));
		p = cw_string(buf, p, len, r->name, sizeof(r->name));
		p = cw_string(buf, p, len, r->need, sizeof(r->need));
	}
	recipe_cnt = n < MAXCRAFT ? n : MAXCRAFT;

	cnt = p < len ? buf[p++] : 0;
	for (m = 0; m < cnt && p + 3 <= len; m++) {
		struct craft_mat tmp, *cm = m < MAXMAT ? mat + m : &tmp;
		cm->nr = buf[p];
		cm->have = load_u16(buf + p + 1);
		p += 3;
		p = cw_string(buf, p, len, cm->name, sizeof(cm->name));
		snprintf(cm->tab, sizeof(cm->tab), "%s", cm->name);
		if ((s = strstr(cm->tab, " bar")) || (s = strstr(cm->tab, " Bar")) || (s = strstr(cm->tab, " Logs")) ||
		    (s = strstr(cm->tab, " logs"))) {
			*s = 0;
		}
	}
	mat_cnt = m < MAXMAT ? m : MAXMAT;

	// protocol 9: title, command, skill, tool, station name
	p = cw_string(buf, p, len, cw_title, sizeof(cw_title));
	p = cw_string(buf, p, len, cw_cmd, sizeof(cw_cmd));
	p = cw_string(buf, p, len, cw_skill, sizeof(cw_skill));
	p = cw_string(buf, p, len, cw_tool, sizeof(cw_tool));
	p = cw_string(buf, p, len, cw_stname, sizeof(cw_stname));
	if (!cw_cmd[0]) { // older server: furnace or anvil
		snprintf(cw_title, sizeof(cw_title), "%s",
		    station == 1 ? "What would you like to smelt?" : "What would you like to make?");
		snprintf(cw_cmd, sizeof(cw_cmd), "%s", station == 1 ? "smelt" : "smith");
		snprintf(cw_skill, sizeof(cw_skill), "Smithing");
		snprintf(cw_tool, sizeof(cw_tool), "a hammer");
		snprintf(cw_stname, sizeof(cw_stname), "%s", station == 1 ? "furnace" : "anvil");
	}

	if (flags & CRAFT_OPEN) {
		cw_open = 1;
		cw_scroll = 0;
		cw_pick_material();
	} else {
		for (m = 0; m < mat_cnt && mat[m].nr != cw_mat; m++) {
		}
		if (m == mat_cnt) {
			cw_pick_material();
		}
	}
	cw_build();
}

int craftwin_is_open(void)
{
	return cw_open;
}

void craftwin_close(void)
{
	cw_open = 0;
	cw_drag = 0;
	cw_pressed = C_NONE;
}

int craftwin_over(int x, int y)
{
	int px = cw_x(), py = cw_y();

	return cw_open && x >= px && x <= px + CW_W && y >= py && y <= py + CW_H;
}

static int cw_in(int x, int y, int sx, int sy, int ex, int ey)
{
	return x >= sx && x <= ex && y >= sy && y <= ey;
}

static void cw_tab_pos(int m, int *sx, int *ex)
{
	int w = (CW_COLS * CW_CELL_W) / (mat_cnt > 0 ? mat_cnt : 1);

	if (w > 90) {
		w = 90;
	}
	*sx = cw_x() + 12 + m * w;
	*ex = *sx + w - 4;
}

static void cw_amount_pos(int b, int *sx, int *sy)
{
	*sx = cw_x() + CW_BTN_X;
	*sy = cw_y() + CW_GRID + 4 + b * (CW_BTN_H + 8);
}

static int cw_hit(int x, int y)
{
	int px = cw_x(), py = cw_y(), b, bx, by, sx, ex, c, r;

	if (!craftwin_over(x, y)) {
		return C_NONE;
	}
	if (cw_in(x, y, px + CW_W - 20, py + 2, px + CW_W - 4, py + 17)) {
		return C_CLOSE;
	}
	if (y < py + CW_TITLE) {
		return C_TITLE;
	}
	for (b = 0; b < 4; b++) {
		cw_amount_pos(b, &bx, &by);
		if (cw_in(x, y, bx, by, bx + CW_BTN_W, by + CW_BTN_H)) {
			return C_AMOUNT + b;
		}
	}
	if (cw_tabs() && y >= py + CW_TABS && y <= py + CW_TABS + 14) {
		for (b = 0; b < mat_cnt; b++) {
			cw_tab_pos(b, &sx, &ex);
			if (x >= sx && x <= ex) {
				return C_TAB + b;
			}
		}
	}
	if (x >= px + 12 && x < px + 12 + CW_COLS * CW_CELL_W && y >= py + CW_GRID &&
	    y < py + CW_GRID + CW_ROWS * CW_CELL_H) {
		c = (x - px - 12) / CW_CELL_W;
		r = (y - py - CW_GRID) / CW_CELL_H;
		if ((cw_scroll + r) * CW_COLS + c < shown_cnt) {
			return C_CELL + r * CW_COLS + c;
		}
	}
	return C_NONE;
}

int craftwin_over_control(int x, int y)
{
	int h = cw_hit(x, y);
	return h != C_NONE && h != C_TITLE;
}

static void cw_make(int i)
{
	char buf[128];

	if (!cw_can_make(recipe + i)) {
		return;
	}
	if (amount_cnt[cw_amount]) {
		snprintf(buf, sizeof(buf), "/%s %s %d", cw_cmd, recipe[i].key, amount_cnt[cw_amount]);
	} else {
		snprintf(buf, sizeof(buf), "/%s %s all", cw_cmd, recipe[i].key);
	}
	cmd_text(buf);
}

static void cw_click(int h)
{
	if (h == C_CLOSE) {
		craftwin_close();
	} else if (h >= C_AMOUNT && h < C_AMOUNT + 4) {
		cw_amount = h - C_AMOUNT;
	} else if (h >= C_TAB && h < C_TAB + MAXMAT) {
		if (h - C_TAB < mat_cnt) {
			cw_mat = mat[h - C_TAB].nr;
			cw_scroll = 0;
			cw_build();
		}
	} else if (h >= C_CELL) {
		int i = cw_scroll * CW_COLS + h - C_CELL;
		if (i < shown_cnt) {
			cw_make(shown[i]);
		}
	}
}

// Mouse input. Returns 1 if the event belongs to the window. Clicks outside it go to the game.
int craftwin_mouse(int what, int x, int y, int delta)
{
	int h;

	if (!cw_open) {
		return 0;
	}

	if (what == SDL_MOUM_NONE) {
		if (cw_drag) {
			cw_dx += x - cw_drag_x;
			cw_dy += y - cw_drag_y;
			cw_drag_x = x;
			cw_drag_y = y;
		}
		return 0;
	}

	if (what == SDL_MOUM_LUP && (cw_drag || cw_pressed != C_NONE)) {
		if (cw_drag) {
			cw_drag = 0;
			cw_dx = cw_x() - (XRES - CW_W) / 2; // not past an edge
			cw_dy = cw_y() - ((YRES - CW_H) / 2 - 20);
		}
		h = cw_hit(x, y);
		if (h != C_NONE && h == cw_pressed) {
			cw_click(h);
		}
		cw_pressed = C_NONE;
		return 1;
	}

	if (!craftwin_over(x, y)) {
		return 0;
	}

	switch (what) {
	case SDL_MOUM_LDOWN:
		h = cw_hit(x, y);
		if (h == C_TITLE) {
			cw_dx = cw_x() - (XRES - CW_W) / 2;
			cw_dy = cw_y() - ((YRES - CW_H) / 2 - 20);
			cw_drag = 1;
			cw_drag_x = x;
			cw_drag_y = y;
		}
		cw_pressed = h;
		return 1;
	case SDL_MOUM_WHEEL:
		cw_scroll -= delta;
		cw_clamp_scroll();
		return 1;
	default:
		return 1;
	}
}

static void cw_icon(unsigned int sprite, int x, int y, int dim)
{
	unsigned short c1, c2, c3, shine;
	unsigned char scale, cr, cg, cb, light, sat;
	RenderFX fx;

	if (!sprite) {
		return;
	}
	bzero(&fx, sizeof(fx));
	fx.sprite = trans_asprite(0, sprite, tick, &scale, &cr, &cg, &cb, &light, &sat, &c1, &c2, &c3, &shine);
	fx.shine = shine;
	fx.c1 = c1;
	fx.c2 = c2;
	fx.c3 = c3;
	fx.cr = (char)cr;
	fx.cg = (char)cg;
	fx.cb = (char)cb;
	fx.clight = (char)light;
	fx.sat = (char)sat;
	fx.scale = scale;
	fx.align = RENDER_ALIGN_CENTER;
	fx.ml = fx.ll = fx.rl = fx.ul = fx.dl = dim ? RENDERFX_NORMAL_LIGHT : FX_ITEMLIGHT;
	render_sprite_fx(&fx, x, y);
}

static const char *cw_mat_name(int m)
{
	int i;

	for (i = 0; i < mat_cnt; i++) {
		if (mat[i].nr == m) {
			return mat[i].tab;
		}
	}
	return "";
}

static const char *cw_mat_fullname(int m)
{
	int i;

	for (i = 0; i < mat_cnt; i++) {
		if (mat[i].nr == m) {
			return mat[i].name;
		}
	}
	return "";
}

static int cw_mat_have(int m)
{
	int i;

	for (i = 0; i < mat_cnt; i++) {
		if (mat[i].nr == m) {
			return mat[i].have;
		}
	}
	return 0;
}

// name in a grid cell: without the metal when the tab already says it ("Bronze Dagger" -> "Dagger")
static const char *cw_short_name(const struct craft_recipe *rc)
{
	const char *m;
	size_t n;

	if (!cw_tabs() || rc->mat == -1) {
		return rc->name;
	}
	m = cw_mat_name(rc->mat);
	n = strlen(m);
	if (n && !SDL_strncasecmp(rc->name, m, n) && rc->name[n] == ' ' && rc->name[n + 1]) {
		return rc->name + n + 1;
	}
	return rc->name;
}

static void cw_display_grid(int px, int py, int hov)
{
	int i, c, r, x, y, cx;
	unsigned short ok_col = IRGB(8, 30, 8), no_col = IRGB(31, 14, 4), low_col = IRGB(31, 10, 8);

	for (i = cw_scroll * CW_COLS; i < shown_cnt && i < (cw_scroll + CW_ROWS) * CW_COLS; i++) {
		const struct craft_recipe *rc = recipe + shown[i];
		int ok = cw_can_make(rc), lowlvl = cw_level < rc->level;

		c = i % CW_COLS;
		r = i / CW_COLS - cw_scroll;
		x = px + 12 + c * CW_CELL_W;
		y = py + CW_GRID + r * CW_CELL_H;
		cx = x + CW_CELL_W / 2;

		// each item sits in its own box; the hovered one lights up
		if (hov == C_CELL + r * CW_COLS + c) {
			gui_draw_box(x + 2, y + 1, x + CW_CELL_W - 3, y + CW_CELL_H - 3, ok ? IRGB(14, 10, 5) : IRGB(9, 7, 4),
			    ok ? IRGB(28, 22, 10) : IRGB(16, 12, 6));
		} else {
			gui_draw_box(x + 2, y + 1, x + CW_CELL_W - 3, y + CW_CELL_H - 3, IRGB(5, 4, 2), IRGB(14, 11, 6));
		}

		cw_icon(rc->sprite, cx, y + 17, !ok);

		// long names ("Two-Handed Sword") use the small font to stay inside the box
		render_text(cx, y + 34, lowlvl ? low_col : (ok ? whitecolor : lightgraycolor),
		    RENDER_ALIGN_CENTER | (render_text_len(0, cw_short_name(rc), -1) > CW_CELL_W - 10 ? RENDER_TEXT_SMALL : 0),
		    cw_short_name(rc));
		if (cw_station == 2 && rc->mat != -1) {
			render_text_fmt(cx, y + 44, rc->canmake ? ok_col : no_col, RENDER_ALIGN_CENTER, "%d bar%s", rc->matcnt,
			    rc->matcnt == 1 ? "" : "s");
		} else {
			render_text_fmt(cx, y + 44, rc->canmake ? ok_col : no_col, RENDER_ALIGN_CENTER, "Level %d", rc->level);
		}
	}

	if (shown_cnt == 0) {
		render_text(px + 12 + CW_COLS * CW_CELL_W / 2, py + CW_GRID + 40, lightgraycolor, RENDER_ALIGN_CENTER,
		    "Nothing can be made here.");
	}

	// scrollbar when there are more rows than fit
	if (cw_rows_total() > CW_ROWS) {
		int top = py + CW_GRID, h = CW_ROWS * CW_CELL_H, sx = px + 12 + CW_COLS * CW_CELL_W + 3;
		int th = h * CW_ROWS / cw_rows_total(), ty = top + (h - th) * cw_scroll / (cw_rows_total() - CW_ROWS);
		render_rect_alpha(sx, top, sx + 5, top + h, IRGB(6, 5, 3), 200);
		gui_draw_box(sx, ty, sx + 5, ty + th, IRGB(18, 14, 8), IRGB(8, 6, 3));
	}
}

// line under the grid: about the item under the mouse, or a hint
static void cw_display_info(int px, int py, int hov)
{
	int y = py + CW_GRID + CW_ROWS * CW_CELL_H + 6;
	unsigned short red = IRGB(31, 10, 8);

	render_line(px + 8, y - 3, px + CW_W - 8, y - 3, IRGB(10, 8, 4));

	if (hov >= C_CELL && cw_scroll * CW_COLS + hov - C_CELL < shown_cnt) {
		const struct craft_recipe *rc = recipe + shown[cw_scroll * CW_COLS + hov - C_CELL];

		render_text_fmt(
		    px + 12, y, whitecolor, 0, "%s: level %d, %d XP each, needs %s.", rc->name, rc->level, rc->xp, rc->need);
		if (cw_level < rc->level) {
			render_text_fmt(px + 12, y + 12, red, 0, "You need %s level %d.", cw_skill, rc->level);
		} else if (cw_flags & CRAFT_NOHAMMER) {
			render_text_fmt(px + 12, y + 12, red, 0, "You need %s.", cw_tool);
		} else if (rc->canmake < 1) {
			render_text(px + 12, y + 12, red, 0, "You do not have the materials.");
		} else {
			render_text_fmt(px + 12, y + 12, lightgraycolor, 0, "You have materials for %d. Click to make %s.",
			    rc->canmake, amount_cnt[cw_amount] ? amount_label[cw_amount] : "all");
		}
		return;
	}

	if (cw_flags & CRAFT_NOHAMMER) {
		render_text_fmt(px + 12, y, red, 0, "You need %s to use the %s.", cw_tool, cw_stname);
	} else if (cw_tabs() && cw_mat != -1 && cw_station == 2) {
		render_text_fmt(px + 12, y, lightgraycolor, 0, "You carry %d %s bar%s. Pick an amount, then click an item.",
		    cw_mat_have(cw_mat), cw_mat_name(cw_mat), cw_mat_have(cw_mat) == 1 ? "" : "s");
	} else if (cw_tabs() && cw_mat != -1) {
		render_text_fmt(px + 12, y, lightgraycolor, 0, "You carry %d %s. Pick an amount, then click an item.",
		    cw_mat_have(cw_mat), cw_mat_fullname(cw_mat));
	} else {
		render_text(px + 12, y, lightgraycolor, 0, "Pick an amount, then click an item.");
	}
	render_text_fmt(px + 12, y + 12, graycolor, 0, "Your %s level: %d", cw_skill, cw_level);
}

void craftwin_display(void)
{
	int px, py, hov, b, bx, by, sx, ex;
	unsigned short edge = IRGB(20, 16, 8);

	if (!cw_open) {
		return;
	}

	px = cw_x();
	py = cw_y();
	hov = cw_hit(mousex, mousey);

	render_push_clip();
	render_set_clip(0, 0, XRES, YRES);

	gui_draw_panel(px, py, CW_W + 1, CW_H + 1, CW_TITLE);
	render_text(px + CW_W / 2, py + 6, IRGB(31, 22, 6), RENDER_ALIGN_CENTER, cw_title);
	gui_draw_box(
	    px + CW_W - 20, py + 2, px + CW_W - 4, py + 17, hov == C_CLOSE ? IRGB(16, 12, 6) : IRGB(8, 6, 3), edge);
	render_text(px + CW_W - 12, py + 6, hov == C_CLOSE ? whitecolor : lightgraycolor, RENDER_ALIGN_CENTER, "X");

	render_set_clip(px + 1, py + 1, px + CW_W, py + CW_H);

	// material tabs (anvil metals, workbench logs ...)
	if (cw_tabs()) {
		for (b = 0; b < mat_cnt; b++) {
			int active = mat[b].nr == cw_mat, font = 0;
			char text[64];
			cw_tab_pos(b, &sx, &ex);
			gui_draw_box(sx, py + CW_TABS, ex, py + CW_TABS + 14,
			    active ? IRGB(18, 13, 6) : (hov == C_TAB + b ? IRGB(12, 9, 4) : IRGB(7, 5, 3)), edge);
			// narrow tabs: small font, then without the count
			snprintf(text, sizeof(text), "%s (%d)", mat[b].tab, mat[b].have);
			if (render_text_len(0, text, -1) > ex - sx - 4) {
				font = RENDER_TEXT_SMALL;
			}
			if (font && render_text_len(font, text, -1) > ex - sx - 4) {
				snprintf(text, sizeof(text), "%s", mat[b].tab);
			}
			render_text((sx + ex) / 2, py + CW_TABS + 3 + (font ? 1 : 0),
			    active || hov == C_TAB + b ? whitecolor : lightgraycolor, RENDER_ALIGN_CENTER | font, text);
		}
	}

	cw_display_grid(px, py, hov);

	// amount buttons
	for (b = 0; b < 4; b++) {
		unsigned short fill;
		cw_amount_pos(b, &bx, &by);
		if (b == cw_amount) {
			fill = IRGB(20, 15, 6);
		} else if (hov == C_AMOUNT + b) {
			fill = IRGB(13, 10, 5);
		} else {
			fill = IRGB(8, 6, 3);
		}
		gui_draw_box(bx, by, bx + CW_BTN_W, by + CW_BTN_H, fill, edge);
		render_text(bx + CW_BTN_W / 2, by + CW_BTN_H / 2 - 4, b == cw_amount ? whitecolor : lightgraycolor,
		    RENDER_ALIGN_CENTER, amount_label[b]);
	}

	cw_display_info(px, py, hov);

	render_pop_clip();
}
