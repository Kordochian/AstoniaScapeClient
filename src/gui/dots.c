/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Dots and Buttons
 *
 * Dots are used to position GUI elements. Positioning can be changed by
 * changing init_dots() or individual elements of the dots and/or button
 * array.
 */

#include <assert.h>
#include <stdint.h>
#include <stddef.h>

#include "astonia.h"
#include "gui/gui.h"
#include "gui/gui_private.h"

extern int __textdisplay_sy, __textdisplay_sx;

static DOT dot_storage[MAX_DOT];
DOT *dot = dot_storage;
static BUT but_storage[MAX_BUT];
BUT *but = but_storage;

// dot and but helpers
static void set_dot(int didx, int x, int y, int flags);
static void set_but(int bidx, int x, int y, int hitrad, int flags);

static void set_dot(int didx, int x, int y, int flags)
{
	assert(didx >= 0 && didx < MAX_DOT && "set_dot: ill didx");

	dot[didx].flags = flags;
	dot[didx].x = x;
	dot[didx].y = y;
}

DLL_EXPORT int dotx(int didx)
{
	return dot[didx].x;
}

DLL_EXPORT int doty(int didx)
{
	if (dot[didx].flags & DOTF_TOPOFF) {
		return dot[didx].y + gui_topoff;
	}
	return dot[didx].y;
}

static void set_but(int bidx, int x, int y, int hitrad, int flags)
{
	assert(bidx >= 0 && bidx < MAX_BUT && "set_but: ill bidx");

	but[bidx].flags = flags;

	but[bidx].x = x;
	but[bidx].y = y;

	but[bidx].sqhitrad = hitrad * hitrad;
}

DLL_EXPORT int butx(int bidx)
{
	return but[bidx].x;
}

DLL_EXPORT int buty(int bidx)
{
	if (but[bidx].flags & BUTF_TOPOFF) {
		return but[bidx].y + gui_topoff;
	}
	return but[bidx].y;
}

// The top bar no longer slides out of the way (GO_SMALLTOP): its pieces are panels the
// player can move instead.
#define stop 0
#define sbot (game_options & GO_SMALLBOT)

void dots_update(void)
{
	set_dot(DOT_TUT, (XRES - 410) / 2, panels_bottom_row_y() - 100 - (context_action_enabled() ? 30 : 0), 0);
}

// Which panel each dot and button inside the top bar and bottom window belongs to
static const struct {
	int idx, pnl;
} dot_panel[] = {
    {DOT_TOP, PNL_MENU},
    {DOT_WEA, PNL_EQUIPMENT},
    {DOT_BOT, PNL_CHAT},
    {DOT_BO2, PNL_INVENTORY},
    {DOT_INV, PNL_INVENTORY},
    {DOT_IN1, PNL_INVENTORY},
    {DOT_IN2, PNL_INVENTORY},
    {DOT_SCR, PNL_INVENTORY},
    {DOT_CON, PNL_SKILLS},
    {DOT_SCL, PNL_SKILLS},
    {DOT_SCU, PNL_SKILLS},
    {DOT_SCD, PNL_SKILLS},
    {DOT_SKL, PNL_SKILLS},
    {DOT_SK2, PNL_SKILLS},
    {DOT_SSP, PNL_STATUS},
    {DOT_GLD, PNL_STATUS},
    {DOT_MOD, PNL_STATUS},
    {DOT_TXT, PNL_CHAT},
    {DOT_TX2, PNL_CHAT},
    {DOT_JNK, PNL_CHAT},
};

int dots_but_panel(int b)
{
	if (b >= BUT_WEA_BEG && b <= BUT_WEA_END) {
		return PNL_EQUIPMENT;
	}
	if ((b >= BUT_INV_BEG && b <= BUT_INV_END) || (b >= BUT_SCR_UP && b <= BUT_SCR_DW)) {
		return PNL_INVENTORY;
	}
	if ((b >= BUT_CON_BEG && b <= BUT_CON_END) || (b >= BUT_SKL_BEG && b <= BUT_SKL_END) ||
	    (b >= BUT_SCL_UP && b <= BUT_SCL_DW)) {
		return PNL_SKILLS;
	}
	if (b == BUT_GLD || (b >= BUT_MOD_WALK0 && b <= BUT_MOD_WALK2)) {
		return PNL_STATUS;
	}
	if (b == BUT_JNK) {
		return PNL_CHAT;
	}
	if (b == BUT_WEA_LCK) {
		return PNL_MENU;
	}
	return -1;
}

static int clampi(int v, int lo, int hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

static void hide_but(int b)
{
	but[b].x = but[b].y = -10000;
	but[b].sqhitrad = 0;
}

// Origin of the original top bar picture as seen from the level panel. The level and
// rank bars are drawn relative to this.
int dots_level_x(void)
{
	return pnl_dx(PNL_LEVEL);
}

int dots_level_y(void)
{
	return pnl_dy(PNL_LEVEL);
}

void init_dots(void)
{
	int i, x, y, xc, yc, gx = gui_dx;
	int bh, sch, ich, cch, ccw;

	panels_layout();
	bh = !sbot ? 170 : 130;
	sch = pnl_ch(PNL_SKILLS);
	ich = pnl_ch(PNL_INVENTORY);
	cch = pnl_ch(PNL_CHAT);
	ccw = pnl_cw(PNL_CHAT);

	// top left, bottom right of screen
	set_dot(DOT_TL, 0, 0, 0);
	set_dot(DOT_BR, XRES, YRES, 0);

	// Everything in the top bar and the bottom window is first laid out inside the original
	// 800 pixel pictures (each picture's top left at 0,0), then moved with its panel.

	// top and bottom window
	set_dot(DOT_TOP, 0, 0, 0);
	set_dot(DOT_BOT, 0, 0, 0);
	set_dot(DOT_BO2, XRES0, bh, 0);

	// equipment, inventory, container. center of first displayed item.
	set_dot(DOT_WEA, 180, 20, 0);
	set_dot(DOT_INV, 660, doty(DOT_BOT) + 27, 0);
	set_dot(DOT_CON, 20, doty(DOT_BOT) + 27, 0);

	// inventory top left and bottom right; 40 pixels per row
	set_dot(DOT_IN1, 645, doty(DOT_BOT) + 2, 0);
	set_dot(DOT_IN2, 795, ich - 2, 0);
	__invdy = clampi((ich - 10) / 40, 1, MAXINVROWS);

	// scroll bars
	set_dot(DOT_SCL, 160 + 5, 0, 0);
	set_dot(DOT_SCR, XRES0 - 160 - 5, 0, 0);
	set_dot(DOT_SCU, 0, doty(DOT_BOT) + 15, 0);
	set_dot(DOT_SCD, 0, sch - 10, 0);

	// self spell bars (bless, potion, rage, ...)
	if (!sbot) {
		set_dot(DOT_SSP, dotx(DOT_BOT) + 179, doty(DOT_BOT) + 68, 0);
	} else {
		set_dot(DOT_SSP, dotx(DOT_BOT) + 179, doty(DOT_BOT) + 52, 0);
	}

	// chat text, as large as the chat panel
	set_dot(DOT_TXT, 230, doty(DOT_BOT) + 8, 0);
	set_dot(DOT_TX2, 221 + ccw, cch - 12, 0);
	__textdisplay_sy = cch - 23; // keep clear of the boxed input line below
	__textdisplay_sx = ccw - 17; // slider on the right

	// skill list, 10 pixels per line; a container shown instead uses 40 pixel rows
	set_dot(DOT_SKL, 8, doty(DOT_BOT) + 12, 0);
	set_dot(DOT_SK2, 156, sch - 2, 0);
	__skldy = clampi((sch - 10) / 10, 1, MAXSKLROWS);
	__condy = clampi((sch - 10) / 40, 1, MAXINVROWS);

	// gold
	set_dot(DOT_GLD, 195, doty(DOT_BO2) - 22, 0);

	// trashcan, bottom right of the chat
	set_dot(DOT_JNK, 221 + ccw - 14, cch - 22, 0);

	// speed options: stealth/normal/fast
	set_dot(DOT_MOD, 181, doty(DOT_BOT) + 24, 0);

	// note to self: do not use dotx(),doty() here because the moving top bar logic is built into the
	// button flags as well
	for (i = 0; i < 12; i++) {
		set_but(BUT_WEA_BEG + i, dot[DOT_WEA].x + i * FDX, dot[DOT_WEA].y + 0, 40, 0);
	}
	for (x = 0; x < 4; x++) {
		for (y = 0; y < MAXINVROWS; y++) {
			set_but(BUT_INV_BEG + x + y * 4, dot[DOT_INV].x + x * FDX, dot[DOT_INV].y + y * FDX, 40, 0);
		}
	}
	for (x = 0; x < 4; x++) {
		for (y = 0; y < MAXINVROWS; y++) {
			set_but(BUT_CON_BEG + x + y * 4, dot[DOT_CON].x + x * FDX, dot[DOT_CON].y + y * FDX, 40, 0);
		}
	}
	for (i = 0; i < MAXSKLROWS; i++) {
		set_but(BUT_SKL_BEG + i, dot[DOT_SKL].x, dot[DOT_SKL].y + i * LINEHEIGHT, 10, 0);
	}

	set_but(BUT_WEA_LCK, dot[DOT_WEA].x + 12 * FDX - 12, dot[DOT_WEA].y + 7, 18, 0);

	set_but(BUT_SCL_UP, dot[DOT_SCL].x + 0, dot[DOT_SCU].y + 0, 30, 0);
	set_but(BUT_SCL_TR, dot[DOT_SCL].x + 0, dot[DOT_SCU].y + 10, 40, BUTF_CAPTURE | BUTF_MOVEEXEC);
	set_but(BUT_SCL_DW, dot[DOT_SCL].x + 0, sch - 10, 30, 0);

	set_but(BUT_SCR_UP, dot[DOT_SCR].x + 0, dot[DOT_SCU].y + 0, 30, 0);
	set_but(BUT_SCR_TR, dot[DOT_SCR].x + 0, dot[DOT_SCU].y + 10, 40, BUTF_CAPTURE | BUTF_MOVEEXEC);
	set_but(BUT_SCR_DW, dot[DOT_SCR].x + 0, ich - 10, 30, 0);

	set_but(BUT_GLD, dot[DOT_GLD].x + 0, dot[DOT_GLD].y + 10, 30, BUTF_CAPTURE);

	set_but(BUT_JNK, dot[DOT_JNK].x + 0, dot[DOT_JNK].y + 0, 30, 0);

	set_but(BUT_MOD_WALK0, dot[DOT_MOD].x + 1 * 14, dot[DOT_MOD].y + 0 * 30, 30, 0);
	set_but(BUT_MOD_WALK1, dot[DOT_MOD].x + 0 * 14, dot[DOT_MOD].y + 0 * 30, 30, 0);
	set_but(BUT_MOD_WALK2, dot[DOT_MOD].x + 2 * 14, dot[DOT_MOD].y + 0 * 30, 30, 0);

	// move everything to its panel. The bottom window's pieces use the bottom panels' offsets,
	// the top bar's the top panels'.
	for (i = 0; i < (int)(sizeof(dot_panel) / sizeof(dot_panel[0])); i++) {
		dot[dot_panel[i].idx].x += pnl_dx(dot_panel[i].pnl);
		dot[dot_panel[i].idx].y += pnl_dy(dot_panel[i].pnl);
	}
	for (i = 0; i < MAX_BUT; i++) {
		int p = dots_but_panel(i);
		if (p != -1) {
			but[i].x += pnl_dx(p);
			but[i].y += pnl_dy(p);
		}
	}

	// rows the panels are too small for: off screen, so they are never drawn or hit
	for (i = 0; i < 4 * MAXINVROWS; i++) {
		if (i / 4 >= __invdy) {
			hide_but(BUT_INV_BEG + i);
		}
		if (i / 4 >= __condy) {
			hide_but(BUT_CON_BEG + i);
		}
	}
	for (i = __skldy; i < MAXSKLROWS; i++) {
		hide_but(BUT_SKL_BEG + i);
	}

	// map top left, bottom right, center: the space between the default top and bottom rows.
	// The map is drawn on the whole screen; this is where the player stands.
	set_dot(DOT_MTL, 0, panels_top_row_h(), 0);
	set_dot(DOT_MBR, XRES, panels_bottom_row_y() + 4, 0);
	x = dotx(DOT_MBR) - dotx(DOT_MTL);
	y = doty(DOT_MBR) - doty(DOT_MTL) + (!stop ? 0 : 40);
	xc = x / 2;
	if (y < 430) {
		yc = y / 2 + 20;
	} else if (y < 450) {
		yc = y / 2 + 20 - y + 430;
	} else {
		yc = y / 2;
	}
	set_dot(DOT_MCT, dotx(DOT_MTL) + xc, doty(DOT_MTL) - (!stop ? 0 : 40) + yc, 0);

	// help and quest window
	set_dot(DOT_HLP, 0, panels_top_row_h(), 0);
	set_dot(DOT_HL2, 222, panels_top_row_h() + 394, 0);

	// teleporter window
	set_dot(DOT_TEL, (XRES - 520) / 2, (doty(DOT_MBR) - doty(DOT_MTL) - 320 - (!stop ? 0 : 40)) / 2 + doty(DOT_MTL), 0);

	// look at window
	set_dot(DOT_LOK, gx + 150, panels_top_row_h() + 10, 0);

	// color picker window
	set_dot(DOT_COL, gx + 340, 210, 0);

	// action bar
	set_dot(DOT_ACT, XRES - MAXACTIONSLOT * 40 - (XRES - MAXACTIONSLOT * 40) / 2, panels_bottom_row_y() - 12, 0);

	// tutor window
	dots_update();

	set_but(BUT_MAP, XRES / 2, YRES / 2, 0, BUTF_NOHIT);

	for (i = 0; i < MAXACTIONSLOT; i++) {
		set_but(BUT_ACT_BEG + i, dot[DOT_ACT].x + i * 40, dot[DOT_ACT].y, 18, 0);
	}
	set_but(BUT_ACT_LCK, dot[DOT_ACT].x - 40, dot[DOT_ACT].y, 18, 0);
	set_but(BUT_ACT_OPN, dot[DOT_ACT].x + MAXACTIONSLOT * 40, dot[DOT_ACT].y, 18, 0);

	set_but(BUT_HELP_DRAG, (dotx(DOT_HLP) + dotx(DOT_HL2)) / 2, doty(DOT_HLP) + 6, 0, BUTF_CAPTURE | BUTF_MOVEEXEC);
	set_but(BUT_OPTIONS, 0, 0, 0, BUTF_NOHIT); // hit test in detect_hover_target()
}
