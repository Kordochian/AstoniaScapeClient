/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Combat style panel (protocol 11): which skill melee damage XP goes to. The server sends the
 * player's style (SV_COMBATSTYLE, server skillreg.c) at login and after every change; clicking a
 * row sends "/style <name>". Drawn like the old raise rows of the skills panel: a dot and a bar
 * under each row, blue for the selected style, red for the others. Drag it by its title (unless
 * the panels are locked); the position is saved in settings.ini as style_pos=dx,dy.
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

#define SW_W     150
#define SW_TITLE 14
#define SW_ROW   18
#define SW_ROWS  4
#define SW_H     (SW_TITLE + 6 + SW_ROWS * SW_ROW)

// same order as the server's STYLE_* numbers
static const char *sw_name[SW_ROWS] = {"Shared", "Attack", "Strength", "Defence"};
static const char *sw_cmd[SW_ROWS] = {"shared", "attack", "strength", "defence"};

static int sw_dx = 0, sw_dy = 0, sw_drag = 0, sw_drag_x, sw_drag_y;
static int sw_pressed = -1;

static int sw_x(void)
{
	int x = XRES - SW_W - 10 + sw_dx;
	return x < 0 ? 0 : (x > XRES - SW_W - 1 ? XRES - SW_W - 1 : x);
}

static int sw_y(void)
{
	int y = YRES / 3 + sw_dy;
	return y < 0 ? 0 : (y > YRES - SW_H - 1 ? YRES - SW_H - 1 : y);
}

void stylewin_receive(int style)
{
	combat_style = (style >= 0 && style < SW_ROWS) ? style : 0;
}

void stylewin_get_pos(int *x, int *y)
{
	*x = sw_dx;
	*y = sw_dy;
}

void stylewin_set_pos(int x, int y)
{
	sw_dx = x;
	sw_dy = y;
}

int stylewin_over(int x, int y)
{
	int px = sw_x(), py = sw_y();

	return combat_style >= 0 && x >= px && x <= px + SW_W && y >= py && y <= py + SW_H;
}

// -2 = title, 0..3 = row, -1 = nothing
static int sw_hit(int x, int y)
{
	int py = sw_y(), r;

	if (!stylewin_over(x, y)) {
		return -1;
	}
	if (y < py + SW_TITLE) {
		return -2;
	}
	r = (y - py - SW_TITLE - 3) / SW_ROW;
	return r >= 0 && r < SW_ROWS ? r : -1;
}

int stylewin_over_control(int x, int y)
{
	return sw_hit(x, y) >= 0;
}

// level of the skill called name, 0 if the server did not send it
static int sw_level(const char *name)
{
	int n;

	for (n = 0; n < MAXCSKILL; n++) {
		if (cskill[n].used && !strcmp(cskill[n].name, name)) {
			return cskill[n].level;
		}
	}
	return 0;
}

// Mouse input. Returns 1 if the event belongs to the panel.
int stylewin_mouse(int what, int x, int y)
{
	int h;
	char buf[64];

	if (combat_style < 0) {
		return 0;
	}

	if (what == SDL_MOUM_NONE) {
		if (sw_drag) {
			sw_dx += x - sw_drag_x;
			sw_dy += y - sw_drag_y;
			sw_drag_x = x;
			sw_drag_y = y;
		}
		return 0;
	}

	if (what == SDL_MOUM_LUP && (sw_drag || sw_pressed != -1)) {
		if (sw_drag) {
			sw_drag = 0;
			sw_dx = sw_x() - (XRES - SW_W - 10); // not past an edge
			sw_dy = sw_y() - YRES / 3;
			options_save();
		}
		h = sw_hit(x, y);
		if (h >= 0 && h == sw_pressed && h != combat_style) {
			snprintf(buf, sizeof(buf), "/style %s", sw_cmd[h]);
			cmd_text(buf);
		}
		sw_pressed = -1;
		return 1;
	}

	if (!stylewin_over(x, y)) {
		return 0;
	}

	if (what == SDL_MOUM_LDOWN) {
		h = sw_hit(x, y);
		if (h == -2 && !panels_locked) {
			sw_dx = sw_x() - (XRES - SW_W - 10);
			sw_dy = sw_y() - YRES / 3;
			sw_drag = 1;
			sw_drag_x = x;
			sw_drag_y = y;
		}
		sw_pressed = h;
	}
	return 1;
}

void stylewin_display(void)
{
	int px, py, hov, r, y, sel, lvl;
	unsigned short col, dark;

	if (combat_style < 0) {
		return;
	}

	px = sw_x();
	py = sw_y();
	hov = sw_hit(mousex, mousey);

	render_push_clip();
	render_set_clip(0, 0, XRES, YRES);

	gui_draw_panel(px, py, SW_W + 1, SW_H + 1, SW_TITLE);
	render_text(px + SW_W / 2, py + 3, IRGB(31, 22, 6), RENDER_ALIGN_CENTER, "Combat Style");

	for (r = 0; r < SW_ROWS; r++) {
		y = py + SW_TITLE + 3 + r * SW_ROW;
		sel = r == combat_style;
		col = sel ? bluecolor : redcolor;
		dark = sel ? darkbluecolor : darkredcolor;

		if (hov == r && !sel) {
			render_rect(px + 3, y, px + SW_W - 3, y + SW_ROW - 2, IRGB(8, 6, 3));
		}

		// the dot, like the old raise buttons
		render_circle_filled_alpha(px + 10, y + 7, 4, col, 255);
		render_circle_alpha(px + 10, y + 7, 4, dark, 255);

		render_text(px + 20, y + 2, sel || hov == r ? whitecolor : textcolor, RENDER_TEXT_LARGE | RENDER_TEXT_LEFT, sw_name[r]);
		if (r && (lvl = sw_level(sw_name[r])) > 0) {
			render_text_fmt(px + SW_W - 8, y + 2, sel ? whitecolor : textcolor, RENDER_TEXT_LARGE | RENDER_TEXT_RIGHT, "%d", lvl);
		}

		// the bar under the row: full blue when selected, red otherwise
		render_rect(px + 20, y + 13, px + SW_W - 8, y + 15, col);
	}

	render_pop_clip();
}
