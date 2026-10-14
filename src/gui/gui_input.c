/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Graphical User Interface - Input handling (keyboard and mouse)
 *
 */

#include <inttypes.h>
#include <time.h>
#include <ctype.h>
#include <math.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_stdinc.h>

#include "astonia.h"
#include "gui/gui.h"
#include "gui/gui_private.h"
#include "client/client.h"
#include "game/game.h"
#include "sdl/sdl.h"
#include "modder/modder.h"

void gui_sdl_keyproc(SDL_Keycode wparam)
{
	int i;

	if (wparam != SDLK_ESCAPE && wparam != SDLK_F12 && amod_keydown(wparam)) {
		return;
	}

	switch (wparam) {
	case SDLK_ESCAPE:
		if (options_is_open()) {
			options_close();
			return;
		}
		if (craftwin_is_open()) {
			craftwin_close();
			return;
		}
		questwin_close();
		cmd_stop();
		context_stop();
		show_look = 0;
		display_gfx = 0;
		teleporter = 0;
		show_tutor = 0;
		display_help = 0;
		display_quest = 0;
		show_color = 0;
		context_key_reset();
		action_ovr = ACTION_NONE;
		minimap_hide();
		if (context_key_enabled()) {
			cmd_reset();
		}
		context_key_set(0);
		return;
	case SDLK_F1:
		if (fkeyitem[0]) {
			exec_cmd(CMD_USE_FKEYITEM, 0);
		}
		return;
	case SDLK_F2:
		if (fkeyitem[1]) {
			exec_cmd(CMD_USE_FKEYITEM, 1);
		}
		return;
	case SDLK_F3:
		if (fkeyitem[2]) {
			exec_cmd(CMD_USE_FKEYITEM, 2);
		}
		return;
	case SDLK_F4:
		if (fkeyitem[3]) {
			exec_cmd(CMD_USE_FKEYITEM, 3);
		}
		return;

	case SDLK_F5:
		cmd_speed(1);
		return;
	case SDLK_F6:
		cmd_speed(0);
		return;
	case SDLK_F7:
		cmd_speed(2);
		return;

	case SDLK_F8:
		nocut ^= 1;
		return;

	case SDLK_F9:
		questwin_toggle();
		return;

	case SDLK_F10:
		display_vc ^= 1;
		list_mem();
		render_list_text();
		return;

	case SDLK_F11:
		if (display_help) {
			display_help = 0;
		} else {
			display_quest = 0;
			questwin_close();
			display_help = 1;
		}
		return;
	case SDLK_F12:
		quit = 1;
		return;

	case SDLK_RETURN:
	case SDLK_KP_ENTER:
		cmd_proc(CMD_RETURN);
		return;
	case SDLK_DELETE:
		cmd_proc(CMD_DELETE);
		return;
	case SDLK_BACKSPACE:
		cmd_proc(CMD_BACK);
		return;
	case SDLK_LEFT:
		cmd_proc(CMD_LEFT);
		return;
	case SDLK_RIGHT:
		cmd_proc(CMD_RIGHT);
		return;
	case SDLK_HOME:
		cmd_proc(CMD_HOME);
		return;
	case SDLK_END:
		cmd_proc(CMD_END);
		return;
	case SDLK_UP:
		cmd_proc(CMD_UP);
		return;
	case SDLK_DOWN:
		cmd_proc(CMD_DOWN);
		return;
	case SDLK_TAB:
		cmd_proc(9);
		return;

	case SDLK_KP_0:
		wparam = '0';
		goto spellbindkey;
	case SDLK_KP_1:
		wparam = '1';
		goto spellbindkey;
	case SDLK_KP_2:
		wparam = '2';
		goto spellbindkey;
	case SDLK_KP_3:
		wparam = '3';
		goto spellbindkey;
	case SDLK_KP_4:
		wparam = '4';
		goto spellbindkey;
	case SDLK_KP_5:
		wparam = '5';
		goto spellbindkey;
	case SDLK_KP_6:
		wparam = '6';
		goto spellbindkey;
	case SDLK_KP_7:
		wparam = '7';
		goto spellbindkey;
	case SDLK_KP_8:
		wparam = '8';
		goto spellbindkey;
	case SDLK_KP_9:
		wparam = '9';
		goto spellbindkey;

	case 'm':
		if (vk_shift && vk_control && !context_key_enabled()) {
			minimap_toggle();
		} else {
			goto spellbindkey;
		}
		return;

	case '0':
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':
	case '8':
	case '9':
	case 'a':
	case 'b':
	case 'c':
	case 'd':
	case 'e':
	case 'f':
	case 'g':
	case 'h':
	case 'i':
	case 'j':
	case 'k':
	case 'l':
	case 'n':
	case 'o':
	case 'p':
	case 'q':
	case 'r':
	case 's':
	case 't':
	case 'u':
	case 'v':
	case 'w':
	case 'x':
	case 'y':
	case 'z':
	spellbindkey:
		if (!vk_item && !vk_char && !vk_spell) {
			context_keydown(wparam);
			return;
		}

		// This rules out numbers, we already got here so a-z garaunteed.
		// toupper for unsigned (aka SDL_Keycode)
		if (wparam >= 97) {
			wparam -= 32;
		}

		for (i = 0; i < max_keytab; i++) {
			if (keytab[i].keycode != wparam && keytab[i].userdef != wparam) {
				continue;
			}

			if ((keytab[i].vk_item && !vk_item) || (!keytab[i].vk_item && vk_item)) {
				continue;
			}
			if ((keytab[i].vk_char && !vk_char) || (!keytab[i].vk_char && vk_char)) {
				continue;
			}
			if ((keytab[i].vk_spell && !vk_spell) || (!keytab[i].vk_spell && vk_spell)) {
				continue;
			}

			if (keytab[i].cl_spell) {
				if (keytab[i].tgt == TGT_MAP) {
					exec_cmd(CMD_MAP_CAST_K, keytab[i].cl_spell);
				} else if (keytab[i].tgt == TGT_CHR) {
					exec_cmd(CMD_CHR_CAST_K, keytab[i].cl_spell);
				} else if (keytab[i].tgt == TGT_SLF) {
					exec_cmd(CMD_SLF_CAST_K, keytab[i].cl_spell);
				} else {
					return; // hu ?
				}
				keytab[i].usetime = now;
				return;
			}
			return;
		}
		return;

	case SDLK_PAGEUP:
		render_text_pageup();
		break;
	case SDLK_PAGEDOWN:
		render_text_pagedown();
		break;

	case '+':
	case '=':
		if (!context_key_isset()) {
			context_action_enable(1);
		}
		break;
	case '-':
		if (!context_key_isset()) {
			context_action_enable(0);
		}
		break;

		// case '<':               render_sceweup(); break;

	case SDLK_INSERT:
		if (vk_shift && !vk_control && !vk_alt) {
			gui_insert();
		}
		break;
	}
}

void gui_sdl_mouseproc(float x, float y, int what)
{
	int delta, tmp, mx, my;
	static int mdown = 0;
	static int chat_click = 0;

	// SDL3 provides sub-pixel mouse precision via floats, but we work with discrete UI elements
	// and tile-based positioning, so we convert to int immediately. Precision loss is negligible
	// for user interaction in this context.
	int local_x = (int)x;
	int local_y = (int)y;

	// The settings window takes the clicks inside it
	if (what != SDL_MOUM_NONE && options_mouse(what, mousex, mousey, local_y)) {
		if (what == SDL_MOUM_LUP) {
			vk_lbut = 0;
		} else if (what == SDL_MOUM_RUP) {
			vk_rbut = 0;
		}
		return;
	}
	// the furnace / anvil window, for clicks inside it
	if (what != SDL_MOUM_NONE && craftwin_mouse(what, mousex, mousey, local_y)) {
		if (what == SDL_MOUM_LUP) {
			vk_lbut = 0;
		} else if (what == SDL_MOUM_RUP) {
			vk_rbut = 0;
		}
		return;
	}
	// so does the questlog window, for clicks inside it
	if (what != SDL_MOUM_NONE && questwin_mouse(what, mousex, mousey, local_y)) {
		if (what == SDL_MOUM_LUP) {
			vk_lbut = 0;
		} else if (what == SDL_MOUM_RUP) {
			vk_rbut = 0;
		}
		return;
	}

	// the combat style panel
	if (what != SDL_MOUM_NONE && stylewin_mouse(what, mousex, mousey)) {
		if (what == SDL_MOUM_LUP) {
			vk_lbut = 0;
		} else if (what == SDL_MOUM_RUP) {
			vk_rbut = 0;
		}
		return;
	}

	// the big map window and the minimap (dragged, resized, zoomed with the wheel)
	if (what != SDL_MOUM_NONE && minimap_mouse(what, mousex, mousey, local_y)) {
		if (what == SDL_MOUM_LUP) {
			vk_lbut = 0;
		} else if (what == SDL_MOUM_RUP) {
			vk_rbut = 0;
		}
		return;
	}

	// interface panels are dragged by their title strip
	if (what != SDL_MOUM_NONE && panels_mouse(what, mousex, mousey)) {
		if (what == SDL_MOUM_LUP) {
			vk_lbut = 0;
		}
		return;
	}

	// the chat slider
	if ((what == SDL_MOUM_LDOWN || what == SDL_MOUM_LUP) && chat_slider_mouse(what)) {
		vk_lbut = what == SDL_MOUM_LDOWN;
		return;
	}

	switch (what) {
	case SDL_MOUM_NONE:
		mousex = local_x;
		mousey = local_y;

		if (capbut != -1) {
			if (mousex != XRES / 2 || mousey != YRES / 2) {
				mousedx += (mousex - (XRES / 2)) / sdl_scale;
				mousedy += (mousey - (YRES / 2)) / sdl_scale;
				sdl_set_cursor_pos(XRES / 2, YRES / 2);
			}
		}

		// window position to GUI pixels (graphics scale, then interface size)
		mousex = (int)floorf(x / ((float)sdl_scale * sdl_ui_scale));
		mousey = (int)floorf(y / ((float)sdl_scale * sdl_ui_scale));
		mousex -= render_offset_x();
		mousey -= render_offset_y();

		if (panels_mouse(SDL_MOUM_NONE, mousex, mousey)) {
			break;
		}
		if (minimap_mouse(SDL_MOUM_NONE, mousex, mousey, 0)) {
			break;
		}

		options_mouse(SDL_MOUM_NONE, mousex, mousey, 0);
		questwin_mouse(SDL_MOUM_NONE, mousex, mousey, 0);
		craftwin_mouse(SDL_MOUM_NONE, mousex, mousey, 0);
		stylewin_mouse(SDL_MOUM_NONE, mousex, mousey);

		if (chat_slider_mouse(SDL_MOUM_NONE)) {
			break;
		}

		if (butsel != -1 && vk_lbut && (but[butsel].flags & BUTF_MOVEEXEC)) {
			exec_cmd(lcmd, 0);
		}

		amod_mouse_move(mousex, mousey);
		break;

	case SDL_MOUM_LDOWN:
		vk_lbut = 1;

		if (amod_mouse_click(mousex, mousey, what)) {
			break;
		}

		// the chat "jump to latest" button (only there while scrolled up)
		if (pnl_at(mousex, mousey) == PNL_CHAT && chat_latest_hit(pnl_mx(PNL_CHAT), pnl_my(PNL_CHAT))) {
			render_text_bottom();
			chat_click = 1;
			break;
		}

		// a click on the chat input box starts typing (the release is not passed on)
		if (context_key_enabled() && !context_key_isset() && pnl_at(mousex, mousey) == PNL_CHAT &&
		    cmd_box_hit(pnl_mx(PNL_CHAT), pnl_my(PNL_CHAT))) {
			context_key_set(1);
			chat_click = 1;
			break;
		}

		if (butsel != -1 && capbut == -1 && (but[butsel].flags & BUTF_CAPTURE)) {
			amod_mouse_capture(1);
			SDL_HideCursor();
			sdl_capture_mouse(1);
			mousedx = 0;
			mousedy = 0;
			sdl_set_cursor_pos(XRES / 2, YRES / 2);
			capbut = butsel;
		}
		break;


	case SDL_MOUM_MUP:
		shift_override = 0;
		control_override = 0;
		mdown = 0;
		if ((game_options & GO_WHEEL) && special_tab[vk_special].spell) {
			if (special_tab[vk_special].target == TGT_MAP) {
				exec_cmd(CMD_MAP_CAST_K, special_tab[vk_special].spell);
			} else if (special_tab[vk_special].target == TGT_CHR) {
				exec_cmd(CMD_CHR_CAST_K, special_tab[vk_special].spell);
			} else if (special_tab[vk_special].target == TGT_SLF) {
				exec_cmd(CMD_SLF_CAST_K, special_tab[vk_special].spell);
			}
			break;
		}
		// fall through intended
	case SDL_MOUM_LUP:
		vk_lbut = 0;

		if (chat_click) {
			chat_click = 0;
			break;
		}

		if (amod_mouse_click(mousex, mousey, what)) {
			break;
		}
		if (context_click(mousex, mousey)) {
			break;
		}

		if (capbut != -1) {
			int cbx = but[capbut].x, cby = but[capbut].y, cbp = dots_but_panel(capbut);
			if (cbp != -1) {
				cbx = pnl_sx(cbp, cbx);
				cby = pnl_sy(cbp, cby);
			}
			sdl_set_cursor_pos((int)((float)(cbx + render_offset_x()) * (float)sdl_scale * sdl_ui_scale),
			    (int)((float)(cby + render_offset_y()) * (float)sdl_scale * sdl_ui_scale));
			sdl_capture_mouse(0);
			SDL_ShowCursor();
			amod_mouse_capture(0);
			if (!(but[capbut].flags & BUTF_MOVEEXEC)) {
				exec_cmd(lcmd, 0);
			}
			capbut = -1;
		} else {
			if ((tmp = context_key_click()) != CMD_NONE) {
				exec_cmd(tmp, 0);
			} else {
				exec_cmd(lcmd, 0);
			}
		}
		break;

	case SDL_MOUM_RDOWN:
		vk_rbut = 1;
		if (amod_mouse_click(mousex, mousey, what)) {
			break;
		}
		context_stop();
		break;

	case SDL_MOUM_RUP:
		vk_rbut = 0;
		if (amod_mouse_click(mousex, mousey, what)) {
			break;
		}
		if (rcmd == CMD_MAP_LOOK && context_open(mousex, mousey)) {
			break;
		}
		context_stop();
		exec_cmd(rcmd, 0);
		break;

	case SDL_MOUM_WHEEL:
		delta = local_y;

		if (amod_mouse_click(0, delta, what)) {
			break;
		}

		mx = pnl_mx(PNL_SKILLS);
		my = pnl_my(PNL_SKILLS);
		if (mx >= dotx(DOT_SKL) && mx < dotx(DOT_SK2) && my >= doty(DOT_SKL) &&
		    my < doty(DOT_SK2)) { // skill / depot / merchant
			while (delta > 0) {
				if (!con_cnt) {
					set_skloff(0, skloff - 1);
				} else {
					set_conoff(0, conoff - 1);
				}
				delta--;
			}
			while (delta < 0) {
				if (!con_cnt) {
					set_skloff(0, skloff + 1);
				} else {
					set_conoff(0, conoff + 1);
				}
				delta++;
			}
			break;
		}

		if (pnl_at(mousex, mousey) == PNL_CHAT) { // anywhere over the chat panel: only the chat scrolls
			while (delta > 0) {
				render_text_lineup();
				render_text_lineup();
				render_text_lineup();
				delta--;
			}
			while (delta < 0) {
				render_text_linedown();
				render_text_linedown();
				render_text_linedown();
				delta++;
			}
			break;
		}

		mx = pnl_mx(PNL_INVENTORY);
		my = pnl_my(PNL_INVENTORY);
		if (mx >= dotx(DOT_IN1) && mx < dotx(DOT_IN2) && my >= doty(DOT_IN1) && my < doty(DOT_IN2)) { // inventory
			while (delta > 0) {
				set_invoff(0, invoff - 1);
				delta--;
			}
			while (delta < 0) {
				set_invoff(0, invoff + 1);
				delta++;
			}
			break;
		}

		// over the world: zoom it in (wheel up) or out, in the Options window's 5% steps
		if (gui_point_in_map(mousex, mousey)) {
			int z = sdl_world_zoom, zmin = sdl_world_min_zoom();

			// below the server's view range the world can't zoom out any further
			if (zmin < WORLD_ZOOM_MIN) {
				zmin = WORLD_ZOOM_MIN;
			}
			if (z < zmin) {
				z = zmin;
			}
			while (delta > 0) {
				z += 5;
				delta--;
			}
			while (delta < 0) {
				z -= 5;
				delta++;
			}
			if (z < zmin) {
				z = zmin;
			}
			if (z > WORLD_ZOOM_MAX) {
				z = WORLD_ZOOM_MAX;
			}
			if (z != sdl_world_zoom) {
				sdl_set_world_zoom(z);
				options_save();
			}
			break;
		}

		if (game_options & GO_WHEELSPEED) {
			// Mousewheel toggles movement speed mode
			// pspeed: 0=normal, 1=fast, 2=stealth
			// cmd_speed: 0=normal, 1=fast, 2=stealth
			while (delta > 0) {
				// Scroll up: cycle through normal->fast->stealth->normal
				if (pspeed == 0) {
					cmd_speed(1); // normal to fast
				} else if (pspeed == 1) {
					cmd_speed(2); // fast to stealth
				} else if (pspeed == 2) {
					cmd_speed(0); // stealth to normal
				}
				delta--;
			}
			while (delta < 0) {
				// Scroll down: cycle through normal->stealth->fast->normal
				if (pspeed == 0) {
					cmd_speed(2); // normal to stealth
				} else if (pspeed == 2) {
					cmd_speed(1); // stealth to fast
				} else if (pspeed == 1) {
					cmd_speed(0); // fast to normal
				}
				delta++;
			}
			break;
		}

		if (game_options & GO_WHEEL) {
			while (delta > 0) {
				vk_special_inc();
				delta--;
			}
			while (delta < 0) {
				vk_special_dec();
				delta++;
			}
			vk_special_time = now;

			if (mdown) {
				shift_override = special_tab[vk_special].shift_over;
				control_override = special_tab[vk_special].control_over;
			}
		}
		break;

	case SDL_MOUM_MDOWN:
		if (game_options & GO_WHEEL) {
			shift_override = special_tab[vk_special].shift_over;
			control_override = special_tab[vk_special].control_over;
		} else {
			shift_override = 1;
		}
		mdown = 1;
		break;
	}
}
