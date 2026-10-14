/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Mouse Hover
 *
 * Displays mouse-over (hover) texts.
 *
 * Add "log_char(cn,LOG_SYSTEM,0,"�c5.");" to the very end of int look_item() in tool.c!
 *
 */

#include <ctype.h>
#include <time.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_mouse.h>

#include "astonia.h"
#include "gui/gui.h"
#include "gui/gui_private.h"
#include "game/game.h"
#include "game/game_private.h"
#include "client/client.h"
#include "sdl/sdl.h"
#include "modder/modder.h"

DLL_EXPORT char hover_bless_text[120];
DLL_EXPORT char hover_freeze_text[120];
DLL_EXPORT char hover_heal_text[120];
DLL_EXPORT char hover_potion_text[120];
DLL_EXPORT char hover_rage_text[120];
DLL_EXPORT char hover_level_text[120];
DLL_EXPORT char hover_rank_text[120];
DLL_EXPORT char hover_time_text[120];

static int display_hover(void);
static void display_hover_update(void);
static void display_hover_skill(void);

void display_mouseover(void)
{
	int hide, mx, my;

	amod_update_hover_texts();

	if (mousex < 0 || mousex >= XRES || mousey < 0 || mousey >= YRES || !sdl_has_focus()) {
		return;
	}
	if (options_over(mousex, mousey) || questwin_over(mousex, mousey) || craftwin_over(mousex, mousey) ||
	    stylewin_over(mousex, mousey)) {
		return;
	}
	if (minimap_over(mousex, mousey)) {
		minimap_display_hover(mousex, mousey);
		return;
	}

	// hover texts of the panels: tested in each panel's layout, drawn at the mouse
	mx = pnl_mx(PNL_STATUS);
	my = pnl_my(PNL_STATUS);
	if (my >= doty(DOT_SSP) && my <= doty(DOT_SSP) + 53) {
		if (mx >= dotx(DOT_SSP) + 28 && mx <= dotx(DOT_SSP) + 35) {
			render_text_nl(mousex, mousey - 16, 0xffff, RENDER_TEXT_BIG | RENDER_TEXT_FRAMED | RENDER_ALIGN_CENTER,
			    hover_rage_text);
		}
		if (mx >= dotx(DOT_SSP) + 18 && mx <= dotx(DOT_SSP) + 25) {
			render_text_nl(mousex, mousey - 16, 0xffff, RENDER_TEXT_BIG | RENDER_TEXT_FRAMED | RENDER_ALIGN_CENTER,
			    hover_bless_text);
		}
		if (mx >= dotx(DOT_SSP) + 8 && mx <= dotx(DOT_SSP) + 15) {
			if (sv_ver == 35) {
				render_text_nl(mousex, mousey - 16, 0xffff, RENDER_TEXT_BIG | RENDER_TEXT_FRAMED | RENDER_ALIGN_CENTER,
				    hover_heal_text);
			} else {
				render_text_nl(mousex, mousey - 16, 0xffff, RENDER_TEXT_BIG | RENDER_TEXT_FRAMED | RENDER_ALIGN_CENTER,
				    hover_freeze_text);
			}
		}
		if (mx >= dotx(DOT_SSP) - 2 && mx <= dotx(DOT_SSP) + 5) {
			render_text_nl(mousex, mousey - 16, 0xffff, RENDER_TEXT_BIG | RENDER_TEXT_FRAMED | RENDER_ALIGN_CENTER,
			    hover_potion_text);
		}
	}

	mx = pnl_mx(PNL_LEVEL);
	my = pnl_my(PNL_LEVEL);
	if (mx >= dots_level_x() + 25 && mx <= dots_level_x() + 135) {
		if (my >= dots_level_y() + 5 && my <= dots_level_y() + 13) {
			render_text_nl(mousex + 16, mousey - 4, 0xffff, RENDER_TEXT_BIG | RENDER_TEXT_FRAMED, hover_level_text);
		}
		if (my >= dots_level_y() + 22 && my <= dots_level_y() + 30) {
			render_text_nl(mousex + 16, mousey - 4, 0xffff, RENDER_TEXT_BIG | RENDER_TEXT_FRAMED, hover_rank_text);
		}
	}

	mx = pnl_mx(PNL_MENU);
	my = pnl_my(PNL_MENU);
	if (mx >= dotx(DOT_TOP) + 728 && mx <= dotx(DOT_TOP) + 772 && my >= doty(DOT_TOP) + 7 &&
	    my <= doty(DOT_TOP) + 17) {
		render_text_nl(
		    mousex - 16, mousey - 4, 0xffff, RENDER_TEXT_BIG | RENDER_TEXT_FRAMED | RENDER_TEXT_RIGHT, hover_time_text);
	}

	display_hover_update();
	hide = display_hover();
	display_hover_skill();
	if (hide) {
		SDL_HideCursor();
	} else if (capbut == -1) {
		SDL_ShowCursor();
	}
	minimap_display_hover(mousex, mousey);
}

#define MAXVALID (TICKS * 60 * 2)
#define MAXDESC  20

struct hover_item {
	uint32_t valid_till;
	int cnt;
	int width;
	char *desc[MAXDESC];
};

static struct hover_item hi[MAX_INVENTORYSIZE + MAX_CONTAINERSIZE] = {0};

static int last_look = 0, last_invsel = -1, last_line = 0, capture = 0;
static tick_t last_tick = 0;

static int textlength(char *text)
{
	int x = 0;
	char *c, buf[4];

	for (x = 0, c = text; *c; c++) {
		if (c[0] == RENDER_TEXT_TERMINATOR) {
			if (c[1] == 'c') {
				if (isdigit(c[2])) {
					if (isdigit(c[3])) {
						c += 3;
						continue;
					}
					c += 2;
					continue;
				}
				c += 1;
				continue;
			}
			continue;
		}
		buf[0] = *c;
		buf[1] = 0;
		x += render_text_length(0, buf);
	}
	return x;
}

int hover_capture_text(char *line)
{
	while (isspace(*line)) {
		line++;
	}

	if (line[0] == RENDER_TEXT_TERMINATOR && line[1] == RENDER_TEXT_TERMINATOR && line[2] == RENDER_TEXT_TERMINATOR &&
	    strncmp(line + 3, "ITEMDESC", 8) == 0) {
		last_invsel = atoi(line + 11);
		if (last_invsel >= 1000) {
			last_invsel = last_invsel % 1000 + _inventorysize;
		}
		if (last_invsel < 0 || last_invsel > _inventorysize + _containersize) {
			last_invsel = capture = last_look = 0;
			return 1;
		}
		capture = 1;
		last_look = 20;
		last_line = 0;
		return 1;
	}

	if (line[0] == RENDER_TEXT_TERMINATOR && line[1] == 'c' && line[2] == '5' && last_look) {
		capture = 1;
	}

	if (line[0] == RENDER_TEXT_TERMINATOR && line[1] == 'c' && line[2] == '5' && line[3] == '.') {
		capture = last_look = 0;
		last_right_click_invsel = -1;
		return 1;
	}

	if (!strcmp(line, "Empty spaces...")) {
		capture = last_look = 0;
		last_right_click_invsel = -1;
		return 1;
	}

	if (capture) {
		int len = textlength(line);
		hi[last_invsel].valid_till = tick + MAXVALID;
		hi[last_invsel].desc[last_line++] = xstrdup(line, MEM_TEMP11);
		hi[last_invsel].cnt = last_line;
		hi[last_invsel].width = max(hi[last_invsel].width, len);

		if (last_invsel == last_right_click_invsel) {
			return 0;
		} else {
			last_right_click_invsel = -1;
		}
	}

	return capture;
}

void hover_capture_tick(void)
{
	if (capture) {
		capture = last_look = 0;
	}
	if (last_look > 0) {
		last_look--;
	}
}

void hover_invalidate_inv(int slot)
{
	if (slot < 0 || slot >= _inventorysize) {
		return;
	}
	hi[slot].valid_till = 0;
}

void hover_invalidate_inv_delayed(int slot)
{
	if (slot < 0 || slot >= _inventorysize) {
		return;
	}
	hi[slot].valid_till = tick + TICKS / 2;
}

void hover_invalidate_con(int slot)
{
	if (slot < 0 || slot >= _containersize) {
		return;
	}
	hi[slot + _inventorysize].valid_till = 0;
}

static int display_hover(void)
{
	char buf[4];
	int slot;

	if (invsel == -1) {
		if (weasel == -1) {
			if (consel == -1) {
				return 0;
			} else {
				slot = consel + _inventorysize;
			}
		} else {
			slot = weatab[weasel];
		}
	} else {
		slot = invsel;
	}

	if ((slot < _inventorysize && !item[slot]) || (slot >= _inventorysize && !container[slot - _inventorysize])) {
		return 0;
	}

	if (hi[slot].valid_till >= tick && tick - last_tick > HOVER_DELAY) {
		// do not invalidate cache if the player keeps hovering over the item
		// this would prevent idle logout
		hi[slot].valid_till = max(hi[slot].valid_till, tick + TICKS);

		int sx = mousex - hi[slot].width / 2;
		if (sx < dotx(DOT_TL)) {
			sx = dotx(DOT_TL);
		}
		if (sx > dotx(DOT_BR) - hi[slot].width - 8) {
			sx = dotx(DOT_BR) - hi[slot].width - 8;
		}

		int sy;
		if (mousey < YRES / 2) {
			sy = mousey + 16;
		} else {
			sy = mousey - hi[slot].cnt * 10 - 16;
		}
		if (sy < doty(DOT_TL)) {
			sy = doty(DOT_TL);
		}
		if (sy > doty(DOT_BR) - hi[slot].cnt * 10 - 8) {
			sy = doty(DOT_BR) - hi[slot].cnt * 10 - 8;
		}

		render_shaded_rect(sx, sy, sx + hi[slot].width + 8, sy + hi[slot].cnt * 10 + 8, 0x0000, 150);

		for (int n = 0; n < hi[slot].cnt; n++) {
			int x = sx + 4;
			unsigned short col = IRGB(24, 24, 24);

			for (int i = 0; hi[slot].desc[n][i]; i++) {
				if (hi[slot].desc[n][i] == RENDER_TEXT_TERMINATOR) {
					if (hi[slot].desc[n][i + 1] == 'c') {
						if (isdigit(hi[slot].desc[n][i + 2])) {
							if (hi[slot].desc[n][i + 2] == '5') {
								col = IRGB(31, 31, 31);
							} else {
								col = IRGB(16, 16, 16);
							}
							if (isdigit(hi[slot].desc[n][i + 3])) {
								i += 3;
								continue;
							}
							i += 2;
							continue;
						}
						i += 1;
						continue;
					}
					continue;
				}
				buf[0] = hi[slot].desc[n][i];
				buf[1] = 0;
				x = render_text(x, sy + n * 10 + 4, col, 0, buf);
			}
		}
		return 0;
	} else {
		if (!last_look && hi[slot].valid_till < tick) {
			if (slot < _inventorysize) {
				cmd_look_inv(slot);
			} else {
				cmd_look_con(slot - _inventorysize);
			}
			last_line = 0;
			last_look = 20;
			last_invsel = slot;
			for (int i = 0; i < MAXDESC; i++) {
				if (hi[slot].desc[i]) {
					xfree(hi[slot].desc[i]);
					hi[slot].desc[i] = NULL;
				}
			}
			hi[slot].width = hi[slot].cnt = 0;
		}
		return 0;
	}
}

static void display_hover_update(void)
{
	static int ivsel = -1, wsel = -1, csel = -1, ssel = -1, soff = 0, ioff = 0, coff = 0;

	if (ivsel != invsel || wsel != weasel || csel != consel || ssel != sklsel2 || soff != skloff || ioff != invoff ||
	    coff != conoff) {
		ivsel = invsel;
		wsel = weasel;
		csel = consel;
		ssel = sklsel2;
		soff = skloff;
		ioff = invoff;
		coff = conoff;
		last_tick = tick;
	}
}

int16_t tactics2melee(int val)
{
	return (int16_t)((float)val * 0.375f);
}

int16_t tactics2immune(int val)
{
	return (int16_t)((float)val * 0.125f);
}

int16_t tactics2spell(int val)
{
	return (int16_t)((float)val * 0.125f);
}

static char *v3_basename(int v)
{
	switch (v) {
	case V3_WIS:
		return "WIS";
	case V3_INT:
		return "INT";
	case V3_AGI:
		return "AGI";
	case V3_STR:
		return "STR";
	default:
		return "err";
	}
}

static char *nicenumber(int n)
{
	static char nicebuf[256];

	if (n > 1000000) {
		sprintf(nicebuf, "%d,%03d,%03d", n / 1000000, (n / 1000) % 1000, n % 1000);
	} else if (n > 1000) {
		sprintf(nicebuf, "%d,%03d", n / 1000, n % 1000);
	} else {
		sprintf(nicebuf, "%d", n);
	}

	return nicebuf;
}

static void display_hover_skill_v35(void)
{
	if (skltab && sklsel2 != -1 && tick - last_tick > HOVER_DELAY) {
		int height = 0, width = 200;

		int v = skltab[skl_row(sklsel2)].v;
		if (v < 0 || v >= *game_v_max) {
			return;
		}

		height += render_text_break_length(0, 0, width - 12, 0xffff, 0, game_skilldesc[v]);

		int sx = mousex + 8;
		if (sx < dotx(DOT_TL)) {
			sx = dotx(DOT_TL);
		}
		if (sx > dotx(DOT_BR) - width - 8) {
			sx = dotx(DOT_BR) - width - 8;
		}

		int sy = mousey - height / 2 - 4;
		if (sy < doty(DOT_TL)) {
			sy = doty(DOT_TL);
		}
		if (sy > doty(DOT_BR) - height - 8) {
			sy = doty(DOT_BR) - height - 8;
		}

		render_shaded_rect(sx, sy, sx + width + 8, sy + height + 8, 0x0000, 150);

		sy = render_text_break(sx + 4, sy + 4, sx + width - 8, 0xffff, 0, game_skilldesc[v]) + 10;
	}
}

static void display_hover_skill_v3(void)
{
	if (skltab && sklsel2 != -1 && tick - last_tick > HOVER_DELAY) {
		int height = 0, width = 200;
		int base = 0, cap = 0, raisecost = 0, unused = -1;
		int equip_bonus = 0, next_raise_cost = 0;
		float exp_percentage = 0.0f;
		int16_t offense = 0, defense = 0, speed = 0, armor = 0, weapon = 0;
		int16_t immune = 0, spells = 0, tactics = 0, athlete = 0;

		int v = skltab[skl_row(sklsel2)].v;
		if (v < 0 || v >= *game_v_max) {
			return;
		}

		int v1 = game_skill[v].base1;
		int v2 = game_skill[v].base2;
		int v3 = game_skill[v].base3;

		// First calculate base attribute modifier
		if (v1 != -1 && v2 != -1 && v3 != -3 && v != V3_DEMON) {
			base = (value[0][v1] + value[0][v2] + value[0][v3]) / 5;
			if (base > max(15, value[1][v] * 2)) {
				cap = max(15, value[1][v] * 2);
			}
			height += 10;
		}

		// Calculate equipment bonus (current - base - attribute modifier)
		// If base is capped, use the capped value
		if (cap) {
			equip_bonus = value[0][v] - value[1][v] - cap;
		} else {
			equip_bonus = value[0][v] - value[1][v] - base;
		}
		if (equip_bonus != 0) {
			height += 10; // Add line for equipment bonus
		}

		if (game_skill[v].cost && v != V3_DEMON) {
			raisecost = raise_cost(v, value[1][v]);
			next_raise_cost = raise_cost(v, value[1][v] + 1);
			height += 10;
			if (experience >= experience_used) {
				unused = (int)(experience - experience_used);
				height += 10;
				// Calculate percentage to next raise
				if (next_raise_cost > 0 && unused < next_raise_cost) {
					exp_percentage = (float)unused / (float)next_raise_cost * 100.0f;
					height += 10; // Add line for percentage
				}
			}
		}

		if (v == V3_DAGGER || v == V3_SWORD || v == V3_TWOHAND || v == V3_STAFF || v == V3_HAND) {
			offense = value[0][v];
			defense = value[0][v];
			height += 20;
		} else if (v == V3_ATTACK) {
			offense = (int16_t)(value[0][v] * 2);
			height += 10;
		} else if (v == V3_PARRY) {
			defense = (int16_t)(value[0][v] * 2);
			height += 10;
		} else if (v == V3_TACTICS) {
			offense = tactics2melee(value[0][v]);
			defense = tactics2melee(value[0][v]);
			immune = tactics2immune(value[0][v] + 14);
			if (value[0][V3_BLESS]) {
				spells = tactics2spell(value[0][v]);
				height += 10;
			}
			height += 30;
		} else if (v == V3_SPEEDSKILL) {
			speed = (int16_t)(value[0][v] / 2);
			height += 10;
		} else if (v == V3_BODYCONTROL) {
			armor = (int16_t)(value[0][v] * 5);
			weapon = (int16_t)(value[0][v] / 4);
			height += 20;
		} else if (value[0][V3_TACTICS] && (v == V3_PULSE || v == V3_WARCRY || v == V3_HEAL || v == V3_FREEZE ||
		                                       v == V3_FLASH || v == V3_FIREBALL)) {
			tactics = tactics2spell(value[0][V3_TACTICS]);
			height += 10;
		} else if (value[0][V3_TACTICS] && v == V3_IMMUNITY) {
			tactics = tactics2immune(value[0][V3_TACTICS] + 14);
			height += 10;
		} else if (v == V3_SPEED) {
			if (value[0][V3_SPEEDSKILL]) {
				height += 10;
			}
			if (value[1][V_PROFBASE]) {
				athlete = value[1][V_PROFBASE] * 3;
				height += 10;
			}
		}

		if (!value[0][V3_BODYCONTROL]) {
			switch (v) {
			case V3_BLESS:
			case V3_HEAL:
			case V3_FREEZE:
			case V3_MAGICSHIELD:
			case V3_FLASH:
			case V3_FIREBALL:
			case V3_PULSE:
				armor = (int16_t)((float)value[0][v] / 8.0f * 17.5f);
				height += 10;
				break;
			}
		}

		if (height) {
			height += 10; // add a free line if there are more lines to display
		}

		height += render_text_break_length(0, 0, width - 12, 0xffff, 0, game_skilldesc[v]);

		int sx = mousex + 8;
		if (sx < dotx(DOT_TL)) {
			sx = dotx(DOT_TL);
		}
		if (sx > dotx(DOT_BR) - width - 8) {
			sx = dotx(DOT_BR) - width - 8;
		}

		int sy = mousey - height / 2 - 4;
		if (sy < doty(DOT_TL)) {
			sy = doty(DOT_TL);
		}
		if (sy > doty(DOT_BR) - height - 8) {
			sy = doty(DOT_BR) - height - 8;
		}

		render_shaded_rect(sx, sy, sx + width + 8, sy + height + 8, 0x0000, 150);

		sy = render_text_break(sx + 4, sy + 4, sx + width - 8, 0xffff, 0, game_skilldesc[v]) + 10;

		// Show base and current values with equipment bonus
		if (equip_bonus != 0) {
			if (equip_bonus > 0) {
				render_text_fmt(sx + 4, sy, 0xffff, 0, "Base: %d, Current: %d (+%d from equipment)", value[1][v],
				    value[0][v], equip_bonus);
			} else {
				render_text_fmt(sx + 4, sy, 0xffff, 0, "Base: %d, Current: %d (%d from equipment)", value[1][v],
				    value[0][v], equip_bonus);
			}
			sy += 10;
		}

		if (base) {
			if (cap && v != V3_SPEED) {
				render_text_fmt(sx + 4, sy, 0xffff, 0, "Gets +%d from (%s+%s+%s) (capped at %d)", base, v3_basename(v1),
				    v3_basename(v2), v3_basename(v3), cap);
			} else {
				render_text_fmt(sx + 4, sy, 0xffff, 0, "Gets +%d from (%s+%s+%s)", base, v3_basename(v1),
				    v3_basename(v2), v3_basename(v3));
			}
			sy += 10;
		}
		if (v == V3_SPEED && value[0][V3_SPEEDSKILL]) {
			render_text_fmt(sx + 4, sy, 0xffff, 0, "Gets +%d from Speedskill", value[0][V3_SPEEDSKILL] / 2);
			sy += 10;
		}
		if (athlete) {
			render_text_fmt(sx + 4, sy, 0xffff, 0, "Gets +%d from Athlete", athlete);
			sy += 10;
		}
		if (tactics) {
			render_text_fmt(sx + 4, sy, 0xffff, 0, "Gets +%d hidden bonus from tactics", tactics);
			sy += 10;
		}
		if (offense) {
			render_text_fmt(sx + 4, sy, 0xffff, 0, "Gives +%d to offense", offense);
			sy += 10;
		}
		if (defense) {
			render_text_fmt(sx + 4, sy, 0xffff, 0, "Gives +%d to defense", defense);
			sy += 10;
		}
		if (immune) {
			render_text_fmt(sx + 4, sy, 0xffff, 0, "Gives +%d hidden bonus to immunity", immune);
			sy += 10;
		}
		if (spells) {
			render_text_fmt(sx + 4, sy, 0xffff, 0, "Gives +%d hidden bonus to spell power", spells);
			sy += 10;
		}
		if (speed) {
			render_text_fmt(sx + 4, sy, 0xffff, 0, "Gives +%d to speed", speed);
			sy += 10;
		}
		if (armor) {
			render_text_fmt(sx + 4, sy, 0xffff, 0, "Gives +%.2f to armor value", (double)((float)armor / 20.0f));
			sy += 10;
		}
		if (weapon) {
			render_text_fmt(sx + 4, sy, 0xffff, 0, "Gives +%d to weapon value", weapon);
			sy += 10;
		}
		if (raisecost) {
			render_text_fmt(sx + 4, sy, 0xffff, 0, "%s exp to raise", nicenumber(raisecost));
			sy += 10;
			if (unused >= 0) {
				render_text_fmt(sx + 4, sy, 0xffff, 0, "You have %s unused exp", nicenumber(unused));
				sy += 10;
				// Show percentage progress to next raise
				if (exp_percentage > 0.0f) {
					render_text_fmt(sx + 4, sy, 0xffff, 0, "Progress to next raise: %.1f%%", (double)exp_percentage);
					sy += 10;
				}
			}
		}
	}
}

// use-based skills (protocol 5): description, level and XP to the next level
static void display_hover_skill_xp(const SKLTAB *st)
{
	const struct cskill *cs = cskill + st->skill_id;
	const char *desc;
	int height = 0, width = 200, lines = 2;
	uint32_t need = 0, done = 0;

	if (cs->desc[0]) {
		desc = cs->desc; // the server's text (skills.cfg)
	} else if (cs->value >= 0 && cs->value < *game_v_max && game_skilldesc[cs->value]) {
		desc = game_skilldesc[cs->value];
	} else {
		desc = "";
	}

	if (cs->xp_next > cs->xp_level) {
		need = cs->xp_next - cs->xp_level;
		done = cs->xp > cs->xp_level ? min(cs->xp - cs->xp_level, need) : 0;
		lines += 2;
	}
	if (st->v >= 0 && st->curr != st->base) {
		lines++;
	}

	if (desc[0]) {
		height += render_text_break_length(0, 0, width - 12, 0xffff, 0, desc) + 4;
	}
	height += lines * 10;

	int sx = mousex + 8;
	if (sx < dotx(DOT_TL)) {
		sx = dotx(DOT_TL);
	}
	if (sx > dotx(DOT_BR) - width - 8) {
		sx = dotx(DOT_BR) - width - 8;
	}

	int sy = mousey - height / 2 - 4;
	if (sy < doty(DOT_TL)) {
		sy = doty(DOT_TL);
	}
	if (sy > doty(DOT_BR) - height - 8) {
		sy = doty(DOT_BR) - height - 8;
	}

	render_shaded_rect(sx, sy, sx + width + 8, sy + height + 8, 0x0000, 150);

	sy += 4;
	if (desc[0]) {
		sy = render_text_break(sx + 4, sy, sx + width - 8, 0xffff, 0, desc) + 4;
	}
	if (cs->xp_next > cs->xp_level) {
		render_text_fmt(sx + 4, sy, 0xffff, 0, "%s level %d of %d", cs->name, cs->level, cs->max);
	} else {
		render_text_fmt(sx + 4, sy, 0xffff, 0, "%s level %d (max)", cs->name, cs->level);
	}
	sy += 10;
	if (st->v >= 0 && st->curr != st->base) {
		render_text_fmt(sx + 4, sy, 0xffff, 0, "With equipment and effects: %d", st->curr);
		sy += 10;
	}
	render_text_fmt(sx + 4, sy, 0xffff, 0, "XP gained: %s", nicenumber((int)min(cs->xp, 2000000000u)));
	sy += 10;
	if (need) {
		render_text_fmt(sx + 4, sy, 0xffff, 0, "Next level at %s XP", nicenumber((int)min(cs->xp_next, 2000000000u)));
		sy += 10;
		render_text_fmt(sx + 4, sy, 0xffff, 0, "%s XP to go (%.1f%% done)", nicenumber((int)(need - done)),
		    100.0 * done / need);
	}
}

// effects tab line (protocol 10): the server's explanation
static void display_hover_effect(const struct effline *ce)
{
	int height = 10, width = 200;

	if (ce->desc[0]) {
		height += render_text_break_length(0, 0, width - 12, 0xffff, 0, ce->desc) + 4;
	}

	int sx = mousex + 8;
	if (sx < dotx(DOT_TL)) {
		sx = dotx(DOT_TL);
	}
	if (sx > dotx(DOT_BR) - width - 8) {
		sx = dotx(DOT_BR) - width - 8;
	}

	int sy = mousey - height / 2 - 4;
	if (sy < doty(DOT_TL)) {
		sy = doty(DOT_TL);
	}
	if (sy > doty(DOT_BR) - height - 8) {
		sy = doty(DOT_BR) - height - 8;
	}

	render_shaded_rect(sx, sy, sx + width + 8, sy + height + 8, 0x0000, 150);

	sy += 4;
	render_text_fmt(sx + 4, sy, 0xffff, 0, "%s: %s", ce->label, ce->value);
	sy += 14;
	if (ce->desc[0]) {
		render_text_break(sx + 4, sy, sx + width - 8, 0xffff, 0, ce->desc);
	}
}

static void display_hover_skill(void)
{
	if (capbut != -1) {
		return; // dont display hover when dragging scrollthumb
	}

	if (skltab && sklsel2 != -1 && skl_row(sklsel2) < skltab_cnt && skltab[skl_row(sklsel2)].v < 0 &&
	    skltab[skl_row(sklsel2)].skill_id < 0) {
		// tabs, headers and effect lines: only effect lines have a hover
		const SKLTAB *st = &skltab[skl_row(sklsel2)];

		if (st->v == STV_EFFECT && st->curr >= 0 && st->curr < effline_cnt && tick - last_tick > HOVER_DELAY) {
			display_hover_effect(effline + st->curr);
		}
		return;
	}

	if (skltab && sklsel2 != -1 && skl_row(sklsel2) < skltab_cnt && skltab[skl_row(sklsel2)].skill_id >= 0 &&
	    skltab[skl_row(sklsel2)].skill_id < MAXCSKILL) {
		if (tick - last_tick > HOVER_DELAY) {
			display_hover_skill_xp(&skltab[skl_row(sklsel2)]);
		}
		return;
	}

	if (sv_ver == 35) {
		display_hover_skill_v35();
	} else {
		display_hover_skill_v3();
	}
}
