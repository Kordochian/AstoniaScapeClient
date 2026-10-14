/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 */

#include <stdio.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_stdinc.h>
#include "dll.h"

#define MAXACTIONSLOT 14

struct quicks {
	map_index_t mn[9]; // 0 for invalid neighbours
	int qi[9]; // maxqick for invalid neighbours
	unsigned int mapx;
	unsigned int mapy;
	int cx;
	int cy;
};

typedef struct quicks QUICK;

extern int nocut;
extern Uint64 now;
extern int playersprite_override;
extern int mapaddx, mapaddy;
extern int mapoffx, mapoffy;
extern int mapaddx, mapaddy; // small offset to smoothen walking
extern int plrmn; // mn of player
extern map_index_t itmsel; // mn
extern map_index_t chrsel; // mn
extern map_index_t mapsel; // mn

DLL_EXPORT extern unsigned short int healthcolor, manacolor, endurancecolor, shieldcolor;
DLL_EXPORT extern unsigned short int whitecolor, lightgraycolor, graycolor, darkgraycolor, blackcolor;
DLL_EXPORT extern unsigned short int lightredcolor, redcolor, darkredcolor;
DLL_EXPORT extern unsigned short int lightgreencolor, greencolor, darkgreencolor;
DLL_EXPORT extern unsigned short int lightbluecolor, bluecolor, darkbluecolor;
DLL_EXPORT extern unsigned short int lightorangecolor, orangecolor, darkorangecolor;
DLL_EXPORT extern unsigned short int textcolor;
extern int update_skltab;

extern int teleporter;
extern int show_tutor;
extern char tutor_text[1024];
extern int show_look;

void set_v35_keytab(void);
void set_v35_actions(void);
void mtos(unsigned int mapx, unsigned int mapy, int *scrx, int *scry);
int stom(int scrx, int scry, int *mapx, int *mapy);
int gui_point_in_map(int x, int y);
int gui_map_x(int x);
int gui_map_y(int y);
void gui_layout_changed(void);
void panels_apply_chat_text(void);
void set_mapoff(int cx, int cy, int mdx, int mdy);
void set_mapadd(int addx, int addy);
void update_user_keys(void);

int main_init(void);
int main_loop(void);
void main_exit(void);
void gui_dump(FILE *fp);
void gui_sdl_keyproc(SDL_Keycode wparam);
void gui_sdl_mouseproc(float x, float y, int what);

// Options menu (options.c)
void options_load(void);
void options_save(void);
void options_exit(void);
void options_open(void);
void options_close(void);
int options_is_open(void);
int options_over(int x, int y);
int options_mouse(int what, int x, int y, int delta);
void options_init(void);
int options_over_control(int x, int y);
void options_display(void);
void options_window_changed(void);
void options_set_sfx_volume(int percent);

// Questlog window (questlog.c)
void questwin_open(void);
void questwin_close(void);
void questwin_toggle(void);
int questwin_is_open(void);
int questwin_over(int x, int y);
int questwin_over_control(int x, int y);
int questwin_mouse(int what, int x, int y, int delta);
void questwin_display(void);
void questwin_get_pos(int *x, int *y, int *w, int *h);
void questwin_set_pos(int x, int y, int w, int h);

// furnace / anvil window (craftwin.c)
void craftwin_receive(const unsigned char *buf, int len);
int craftwin_is_open(void);
void craftwin_close(void);
int craftwin_over(int x, int y);
int craftwin_over_control(int x, int y);
int craftwin_mouse(int what, int x, int y, int delta);
void craftwin_display(void);

// combat style panel (stylewin.c, protocol 11)
void stylewin_receive(int style);
int stylewin_over(int x, int y);
int stylewin_over_control(int x, int y);
int stylewin_mouse(int what, int x, int y);
void stylewin_display(void);
void stylewin_get_pos(int *x, int *y);
void stylewin_set_pos(int x, int y);

extern int (*get_skltab_sep)(int i);
DLL_EXPORT int _get_skltab_sep(int i);
extern int (*get_skltab_index)(int n);
DLL_EXPORT int _get_skltab_index(int n);
extern int (*get_skltab_show)(int i);
DLL_EXPORT int _get_skltab_show(int i);

map_index_t context_getnm(void);
int context_action_enabled(void);
int hover_capture_text(char *line);
void hover_capture_tick(void);
void hover_invalidate_inv(int slot);
void hover_invalidate_inv_delayed(int slot);
void hover_invalidate_con(int slot);

extern int (*do_display_random)(void);
DLL_EXPORT int _do_display_random(void);

void help_init(void);
void teleport_init(void);
extern int (*do_display_help)(int);
DLL_EXPORT int _do_display_help(int nr);

extern char (*action_row)[MAXACTIONSLOT];
extern char v3_action_row[2][MAXACTIONSLOT];
extern char v35_action_row[2][MAXACTIONSLOT];
extern int action_enabled;
void actions_loaded(void);

void minimap_clear(void);
void minimap_compact(void);
void minimap_areainfo(int cmd, int areaID, int server_key);

struct questlog {
	char *name;
	int minlevel, maxlevel;
	char *giver;
	char *area;
	int exp;
	unsigned int flags;
};
DLL_EXPORT extern struct questlog *game_questlog;
extern int questonscreen[10];
DLL_EXPORT extern int *game_questcount;
extern int gear_lock;
extern int last_right_click_invsel;

// Platform-specific GUI functions
void gui_sdl_draghack(void);
