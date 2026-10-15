/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Questlog
 *
 * Displays the quest log and parses clicks on the quest log window.
 *
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "astonia.h"
#include "gui/gui.h"
#include "gui/gui_private.h"
#include "game/game.h"
#include "client/client.h"
#include "client/protocol.h"
#include "sdl/sdl.h"

static int havequest = 0;

struct questlog _game_questlog[] = {
    {"Lydia's Potion", 1, 2, "James", "Cameron", 15, QLF_REPEATABLE}, // 0,
    {"Find the Magic Item", 2, 3, "Gwendylon", "Cameron", 75, QLF_REPEATABLE}, // 1,
    {"The Second Skull", 3, 5, "Gwendylon", "Cameron", 150, QLF_REPEATABLE}, // 2,
    {"The Third Skull", 5, 7, "Gwendylon", "Cameron", 300, QLF_REPEATABLE}, // 3,
    {"Kill the Foul Magician", 6, 8, "Gwendylon", "Cameron", 800, QLF_REPEATABLE}, // 4,
    {"Bear Hunt", 6, 8, "Yoakin", "Cameron", 600, QLF_REPEATABLE}, // 5,
    {"A Fool's Request", 6, 8, "Nook", "Cameron", 400, 0}, // 6,
    {"Mages Gone Berserk", 6, 9, "Guiwynn", "Cameron", 800, QLF_REPEATABLE}, // 7,
    {"The Recipe for Happiness", 7, 10, "Guiwynn", "Cameron", 900, QLF_REPEATABLE}, // 8,
    {"Knightly Troubles", 7, 10, "Logain", "Cameron", 1200, QLF_REPEATABLE}, // 9,
    {"Loisan's House", 9, 12, "Seymour", "Aston", 850, 0}, // 10,
    {"The Silver Skull", 10, 13, "Seymour", "Aston", 1000, 0}, // 11,
    {"Find Loisan", 11, 15, "Seymour", "Aston", 1500, QLF_REPEATABLE}, // 12,
    {"Jeepers Creepers", 12, 18, "Kelly", "Aston", 1850, QLF_REPEATABLE}, // 13,
    {"Underground Park Shrines", 15, 20, "Kelly", "Aston", 0,
        0}, // 14, special case: exp awarded in driver, 4500 exp total
    {"In Search of Clara", 20, 27, "Kelly", "Aston", 2500, 0}, // 15,
    {"The Astronomer's Notes", 15, 20, "Gerassimo", "Aston", 5000, QLF_REPEATABLE}, // 16,
    {"The Unwanted Tenants", 9, 12, "Reskin", "Cameron", 1250, 0}, // 17,
    {"The Toughest Monster", 20, 25, "Sir Jones", "Aston", 7500, 0}, // 18,
    {"The Toughestest Monster", 20, 26, "Sir Jones", "Aston", 12000, 0}, // 19,
    {"Wanted: Occult Staff", 30, 36, "Carlos", "Aston", 40000, QLF_REPEATABLE}, // 20,
    {"Slay the Swampbeast", 23, 30, "Clara", "Swamp", 22500, 0}, // 21,
    {"Impish Bear Hunt", 20, 27, "William/Imp", "Forest", 12500, 0}, // 22,
    {"Praying Mantis Stew", 20, 27, "William", "Forest", 15000, 0}, // 23,
    {"The Spider Queen", 25, 30, "Hermit", "Forest", 25000, 0}, // 24,
    {"Earning the Lockpick", 25, 30, "Guildmaster", "Exkordon", 0,
        QLF_XREPEAT}, // 25, exp awarded in driver, amount depends on robbers killed. range: 5000 to 20000
    {"Extortion", 25, 30, "Guildmaster", "Exkordon", 0, QLF_XREPEAT}, // 26, exp awarded in driver, 5000 or 10000
    {"Price Fix Exposed", 25, 30, "Guildmaster", "Exkordon", 15000, QLF_XREPEAT}, // 27,
    {"The Golden Lockpick", 26, 33, "Guildmaster", "Exkordon", 15000, QLF_XREPEAT}, // 28,
    {"Dirty Hands", 26, 33, "Sanwyn", "Exkordon", 0, 0}, // 29, exp awarded in driver, 45000 total
    {"The Old Governor's Cross", 33, 40, "Skeleton", "Exkordon", 30000, QLF_REPEATABLE}, // 30,
    {"Spider Poison", 30, 40, "Cervik", "Exkordon", 30000, QLF_REPEATABLE}, // 31,
    {"Join the Tribe", 63, 80, "Kalanur", "Nomad Plains", 10000, 0}, // 32,
    {"Searching Sarkilar", 63, 80, "Kir Laas", "Nomad Plains", 450000, 0}, // 33,
    {"A Golden Statue", 72, 90, "Kir Garan", "Nomad Plains", 280000, 0}, // 34,
    {"Smuggler Book", 14, 17, "Imp. Commander", "Below Aston 2", 1000, QLF_REPEATABLE}, // 35,
    {"Contraband", 14, 17, "Imp. Commander", "Below Aston 2", 0, 0}, // 36, exp awarded in driver, 5000 total
    {"Smuggler Leader", 14, 17, "Imp. Commander", "Below Aston 2", 2000, QLF_REPEATABLE}, // 37,
    {"The Family Heirloom", 32, 40, "Aristocrat", "Bran. Forest", 40000, QLF_REPEATABLE}, // 38,
    {"Bear Hunt - Again", 32, 36, "Yoatin", "Bran. Forest", 40000, QLF_REPEATABLE}, // 39,
    {"The Jewels of Brannington ", 34, 40, "Count B.", "Brannington", 0,
        QLF_REPEATABLE}, // 40, exp awarded in driver, 120k total
    {"A Grolm's Spoils", 33, 42, "Brenneth", "Brannington", 15000, QLF_REPEATABLE}, // 41,
    {"A Thief's Loot ", 33, 42, "Brenneth", "Brannington", 15000, QLF_REPEATABLE}, // 42,
    {"A Necromancer's Notes", 33, 42, "Brenneth", "Brannington", 15000, QLF_REPEATABLE}, // 43,
    {"A Rest Disturbed", 36, 43, "Spirit", "Brannington", 60000, QLF_REPEATABLE}, // 44,
    {"Searching a Miner's Tool", 42, 48, "Broklin", "Brannington", 60000, QLF_REPEATABLE}, // 45,
    {"A Miner's Vengeance", 44, 50, "Broklin", "Brannington", 60000, 0}, // 46,
    {"A Miner's Misery", 85, 95, "Dwarven Chief", "Grimroot", 285000, 0}, // 47,
    {"A Miner's Bane", 95, 105, "Dwarven Chief", "Grimroot", 395000, 0}, // 48,
    {"A Miner's Anguish", 105, 115, "Dwarven Chief", "Grimroot", 525000, 0}, // 49,
    {"A Miner Lost", 115, 125, "Dwarven Chief", "Grimroot", 680000, 0}, // 50,
    {"Lizard's Teeth", 95, 105, "Dwarven Shaman", "Grimroot", 395000, 0}, // 51,
    {"Collecting Berries", 100, 110, "Dwarven Shaman", "Grimroot", 455000, 0}, // 52,
    {"Elitist Head", 105, 115, "Dwarven Shaman", "Grimroot", 525000, 0}, // 53,
    {"Looking for Caligar", 55, 65, "Kelly", "Aston", 80000, 0}, // 54,
    {"Fighting Styles", 55, 65, "Glori", "Caligar", 80000, 0}, // 55,
    {"Obelisk Hunt", 55, 65, "Glori", "Caligar", 80000, 0}, // 56,
    {"Find the Keyparts", 55, 65, "Glori", "Caligar", 80000, 0}, // 57,
    {"Assemble the Key", 55, 65, "Glori", "Caligar", 80000, 0}, // 58,
    {"Amazon Invaders", 55, 65, "Homdem", "Caligar", 80000, 0}, // 59,
    {"The Emperor's Plaque", 55, 65, "Kelly", "Aston", 160000, 0}, // 60,

    {"The Imperial Vault", 26, 28, "Carlos", "Aston", 20000, 0}, // 61,
    {"Tunnel Magics", 26, 28, "Rouven", "Imperial Vault", 10000, 0}, // 62,
    {"Chronicles of Seyan", 26, 28, "Rouven", "Imperial Vault", 10000, 0}, // 63,

    {"Finding Arkhata", 48, 58, "Guard", "Brannington", 60000, 0}, // 64,
    {"Rammy's Crown", 50, 60, "Rammy", "Arkhata", 70000, 0}, // 65,
    {"Ishtar's Bracelet", 50, 60, "Jaz", "Arkhata", 70000, 0}, // 66,
    {"Queen Fiona's Ring", 55, 65, "Queen Fiona", "Arkhata", 85000, 0}, // 67,
    {"A Shopkeeper's Fright", 58, 68, "Ramin", "Arkhata", 100000, 0}, // 68,
    {"The Monks' Request", 58, 68, "Johnatan", "Arkhata", 50000, 0}, // 69,
    {"The Book Eater", 58, 68, "Tracy", "Arkhata", 100000, 0}, // 70,
    {"Entrance Passes", 58, 66, "Rammy", "Arkhata", 75000, 0}, // 71,
    {"The Source", 70, 80, "Jada", "Arkhata", 175000, 0}, // 72,
    {"Ceremonial Pot", 52, 62, "Pot Maker", "Arkhata", 75000, 0}, // 73,
    {"The Lost Secrets", 52, 62, "Thai Pan", "Arkhata", 75000, 0}, // 74,
    {"A Kidnapped Student", 63, 73, "Trainer", "Arkhata", 130000, 0}, // 75,
    {"The Traitors", 60, 70, "Clerk", "Arkhata", 120000, 0}, // 76,
    {"The Blue Harpy", 58, 68, "Hunter", "Arkhata", 60000, 0}, // 77,
    {"The Mysterious Language", 60, 65, "Johnatan", "Arkhata", 60000, 0}, // 78,

    // quests added on this server (questlog.c in the server source)
    {"Forest Robbers", 20, 30, "Boreas", "Forest", 25000, QLF_REPEATABLE}, // 79,
    {"Warm Clothes", 70, 120, "Blake", "Minoa", 600000, 0}, // 80,


};
DLL_EXPORT struct questlog *game_questlog = _game_questlog;
int _game_questcount = ARRAYSIZE(_game_questlog);
DLL_EXPORT int *game_questcount = &_game_questcount;

int questonscreen[10];

static int questproz(int cnt)
{
	int n;
	float val = 100.0f;

	for (n = 0; n < cnt; n++) {
		val *= 0.825f;
	}
	return (int)val;
}

int questlist[MAXQUEST], questinit = 0;

static int questcmp(const void *a, const void *b)
{
	const int *va = (const int *)a;
	const int *vb = (const int *)b;

	if (game_questlog[*va].minlevel != game_questlog[*vb].minlevel) {
		return game_questlog[*vb].minlevel - game_questlog[*va].minlevel;
	}
	if (game_questlog[*va].maxlevel != game_questlog[*vb].maxlevel) {
		return game_questlog[*vb].maxlevel - game_questlog[*va].maxlevel;
	}
	return *va - *vb;
}

int (*do_display_random)(void) = _do_display_random;

DLL_EXPORT int _do_display_random(void)
{
	int y = doty(DOT_HLP) + 15, x, n;
	unsigned int idx, bit, m;
	static short indec[10] = {0, 11, 24, 38, 43, 57, 64, 76, 83, 96};
	static short bribes[10] = {0, 15, 22, 34, 48, 54, 67, 78, 86, 93};
	static short welding[10] = {0, 18, 27, 32, 46, 52, 62, 72, 81, 98};
	static short welding2[10] = {0, 12, 25, 35, 44, 56, 65, 77, 89, 95};
	static short edge[10] = {0, 13, 26, 36, 42, 59, 66, 74, 88, 91};
	static short kindness[10] = {0, 21, 55};
	static short jobless[10] = {0, 20, 45, 61, 82, 97};
	static short security[10] = {0, 10, 29, 41, 58, 69, 75, 85, 94};

	render_text(dotx(DOT_HLP) + (10 + 204) / 2, y, whitecolor, RENDER_ALIGN_CENTER, "Random Dungeon");
	y += 24;

	render_text_fmt(dotx(DOT_HLP) + 10, y, graycolor, 0, "Continuity: %d", shrine.continuity);
	y += 12;

	x = render_text(dotx(DOT_HLP) + 10, y, graycolor, 0, "Indecisiveness: ");
	for (n = 1; n < 10; n++) {
		idx = (unsigned int)n / 32U;
		bit = 1U << ((unsigned int)n & 31U);
		if (shrine.used[idx] & bit) {
			x = render_text(x, y, graycolor, 0, "- ");
		} else {
			if (indec[n] < shrine.continuity) {
				x = render_text_fmt(x, y, graycolor, 0, "%d ", indec[n]);
			}
		}
	}
	y += 12;

	x = render_text(dotx(DOT_HLP) + 10, y, graycolor, 0, "Bribes: ");
	for (n = 1; n < 10; n++) {
		m = (unsigned int)n + 10U;
		idx = m / 32U;
		bit = 1U << (m & 31U);
		if (shrine.used[idx] & bit) {
			x = render_text(x, y, graycolor, 0, "- ");
		} else {
			if (bribes[n] < shrine.continuity) {
				x = render_text_fmt(x, y, graycolor, 0, "%d ", bribes[n]);
			}
		}
	}
	y += 12;

	x = render_text(dotx(DOT_HLP) + 10, y, graycolor, 0, "Welding: ");
	for (n = 1; n < 10; n++) {
		m = (unsigned int)n + 20U;
		idx = m / 32U;
		bit = 1U << (m & 31U);
		if (shrine.used[idx] & bit) {
			x = render_text(x, y, graycolor, 0, "- ");
		} else {
			if (welding[n] < shrine.continuity) {
				x = render_text_fmt(x, y, graycolor, 0, "%d ", welding[n]);
			}
		}
	}
	y += 12;

	if (sv_ver == 35) {
		x = render_text(dotx(DOT_HLP) + 10, y, graycolor, 0, "Welding: ");
		for (n = 1; n < 10; n++) {
			m = (unsigned int)n + 72U;
			idx = m / 32U;
			bit = 1 << (m & 31);
			if (shrine.used[idx] & bit) {
				x = render_text(x, y, graycolor, 0, "- ");
			} else {
				if (welding2[n] < shrine.continuity) {
					x = render_text_fmt(x, y, graycolor, 0, "%d ", welding2[n]);
				}
			}
		}
		y += 12;
	}

	x = render_text(dotx(DOT_HLP) + 10, y, graycolor, 0, "LOE: ");
	for (n = 1; n < 10; n++) {
		m = (unsigned int)n + 30U;
		idx = m / 32U;
		bit = 1U << (m & 31U);
		if (shrine.used[idx] & bit) {
			x = render_text(x, y, graycolor, 0, "- ");
		} else {
			if (edge[n] < shrine.continuity) {
				x = render_text_fmt(x, y, graycolor, 0, "%d ", edge[n]);
			}
		}
	}
	y += 12;

	if (sv_ver == 30) {
		x = render_text(dotx(DOT_HLP) + 10, y, graycolor, 0, "Kindness: ");
		for (n = 1; n < 3; n++) {
			m = (unsigned int)n + 40U;
			idx = m / 32U;
			bit = 1U << (m & 31U);
			if (shrine.used[idx] & bit) {
				x = render_text(x, y, graycolor, 0, "- ");
			} else {
				if (kindness[n] < shrine.continuity) {
					x = render_text_fmt(x, y, graycolor, 0, "%d ", kindness[n]);
				}
			}
		}
		y += 12;
	}

	x = render_text(dotx(DOT_HLP) + 10, y, graycolor, 0, "Security: ");
	for (n = 1; n < 9; n++) {
		m = (unsigned int)n + 53U;
		idx = m / 32U;
		bit = 1U << (m & 31U);
		if (shrine.used[idx] & bit) {
			x = render_text(x, y, graycolor, 0, "- ");
		} else {
			if (security[n] < shrine.continuity) {
				x = render_text_fmt(x, y, graycolor, 0, "%d ", security[n]);
			}
		}
	}
	y += 12;

	x = render_text(dotx(DOT_HLP) + 10, y, graycolor, 0, "Jobless: ");
	for (n = 1; n < 6; n++) {
		m = (unsigned int)n + 63U;
		idx = m / 32U;
		bit = 1U << (m & 31U);
		if (shrine.used[idx] & bit) {
			x = render_text(x, y, graycolor, 0, "- ");
		} else {
			if (jobless[n] < shrine.continuity) {
				x = render_text_fmt(x, y, graycolor, 0, "%d ", jobless[n]);
			}
		}
	}
	y += 12;

	x = render_text(dotx(DOT_HLP) + 10, y, graycolor, 0, "Vitality: ");
	if (shrine.used[50 / 32] & (1 << (50 & 31))) {
		render_text(x, y, graycolor, 0, "- ");
	} else if (30 < shrine.continuity) {
		render_text(x, y, graycolor, 0, "30");
	}
	y += 12;

	x = render_text(dotx(DOT_HLP) + 10, y, graycolor, 0, "Death: ");
	if (shrine.used[51 / 32] & (1 << (51 & 31))) {
		render_text(x, y, graycolor, 0, "- ");
	} else if (37 < shrine.continuity) {
		render_text(x, y, graycolor, 0, "37");
	}
	y += 12;

	x = render_text(dotx(DOT_HLP) + 10, y, graycolor, 0, "Braveness: ");
	if (shrine.used[52 / 32] & (1 << (52 & 31))) {
		render_text(x, y, graycolor, 0, "- ");
	} else if (51 < shrine.continuity) {
		render_text(x, y, graycolor, 0, "51");
	}
	y += 12;

	y += 12;
	y = render_text_break(dotx(DOT_HLP) + 10, y, dotx(DOT_HLP) + 204, graycolor, 0,
	    "Only shrines in dungeons you have already solved (used the continuity shrine), but not yet used, are shown. "
	    "The continuity shrine shown is the first one you haven't used yet.");

	return y;
}

int do_display_questlog(int nr)
{
	int y = doty(DOT_HLP) + 15, off, n, cnt, pass, m;
	char buf[256];

	if (!questinit) {
		for (n = 0; n < *game_questcount; n++) {
			questlist[n] = n;
		}
		qsort(questlist, (size_t)*game_questcount, sizeof(int), questcmp);
		questinit = 1;
	}

	if (!havequest) {
		cmd_getquestlog();
		havequest = 1;
	}

	for (n = 0; n < 10; n++) {
		questonscreen[n] = -1;
	}

	if (nr == 10) {
		return do_display_random();
	}

	off = (nr - 1) * 9;

	for (pass = cnt = 0; pass < 2; pass++) {
		for (m = 0; m < *game_questcount; m++) {
			n = questlist[m];

			if ((pass == 0 && (quest[n].flags) == QF_OPEN && quest[n].done < 10) ||
			    (pass == 1 && (quest[n].flags) == QF_DONE)) {
				if (cnt >= off) {
					if ((game_questlog[n].flags & QLF_REPEATABLE) && (quest[n].flags & QF_DONE) && quest[n].done < 10) {
						render_text(dotx(DOT_HLP) + 200, y, lightbluecolor, RENDER_TEXT_RIGHT, "Re-Open");
					}
					if ((game_questlog[n].flags & QLF_XREPEAT) && (quest[n].flags & QF_DONE)) {
						render_text(dotx(DOT_HLP) + 200, y, graycolor, RENDER_TEXT_RIGHT, "(Junk Item)");
					}
					sprintf(buf, "Quest: %s", game_questlog[n].name);
					render_text(dotx(DOT_HLP) + 10, y, whitecolor, 0, buf);
					y += 10;

					sprintf(buf, "From: %s in %s.", game_questlog[n].giver, game_questlog[n].area);
					render_text(dotx(DOT_HLP) + 10, y, graycolor, 0, buf);
					y += 10;

					if (game_questlog[n].flags & (QLF_REPEATABLE | QLF_XREPEAT)) {
						sprintf(buf, "Done: %d time%s (%d%% exp). %s.", quest[n].done, quest[n].done != 1 ? "s" : "",
						    questproz(quest[n].done), (quest[n].flags & QF_DONE) ? "Done" : "Open");
						render_text(dotx(DOT_HLP) + 10, y, graycolor, 0, buf);
						y += 10;
					} else {
						sprintf(buf, "Done: %d time. (not repeatable). %s.", quest[n].done,
						    (quest[n].flags & QF_DONE) ? "Done" : "Open");
						render_text(dotx(DOT_HLP) + 10, y, graycolor, 0, buf);
						y += 10;
					}
					y += 10;
				}
				if (cnt - off >= 0 && cnt - off < 10) {
					questonscreen[cnt - off] = n;
				}
				cnt++;
				if (cnt >= off + 9) {
					break;
				}
			}
		}
		if (cnt >= off + 9) {
			break;
		}
	}
	if (cnt == 0) {
		y += 50;
		y = render_text(dotx(DOT_HLP) + (10 + 202) / 2, y, whitecolor, RENDER_ALIGN_CENTER, "Your questlog is empty.");
	}

	return y;
}

void quest_select(int nr)
{
	if (nr < 0 || nr > 9) {
		return;
	}

	if (questonscreen[nr] != -1) {
		cmd_reopen_quest(questonscreen[nr]);
	}
}

/*
 * Questlog window
 *
 * A window in the style of the Options window that lists the quests the player
 * knows, with filters for status, area and level and a choice of sort order.
 * The second tab shows the random dungeon shrines (do_display_random()).
 * The window does not block the game: clicks outside it go to the game.
 */

#define QW 440 // window size
#define QH 330

#define QW_TITLE 18
#define QW_TABS  22
#define QW_ROW1  42 // filter rows
#define QW_ROW2  58
#define QW_HEAD  78 // column headers
#define QW_LIST  92 // first list row
#define QW_LINE  12 // list row height
#define QW_INFO  (QH - 52) // details of the selected quest

#define COL_NAME  12
#define COL_AREA  190
#define COL_LEVEL 296
#define COL_STAT  346

#define SEL_LBL_W 46
#define SEL_BOX_W 150

enum {
	Q_NONE,
	Q_CLOSE,
	Q_TITLE,
	Q_TAB_QUESTS,
	Q_TAB_RANDOM,
	Q_SEL_L, // + selector * 3
	Q_SEL_R = Q_SEL_L + 4,
	Q_SEL_M = Q_SEL_R + 4,
	Q_SCROLL_UP = Q_SEL_M + 4,
	Q_SCROLL_DOWN,
	Q_REOPEN,
	Q_ROW, // + visible row
};

enum { QS_STATUS, QS_AREA, QS_LEVEL, QS_SORT };

static const char *qs_label[] = {"Status", "Area", "Level", "Sort"};
static const char *status_name[] = {"All quests", "Open", "Completed", "Can be redone"};
static const char *level_name[] = {"All levels", "My level", "Up to my level", "Above my level"};
static const char *sort_name[] = {"Open first", "Level", "Area", "Name"};

static int qw_open = 0, qw_tab = 0;
static int qw_dx = 0, qw_dy = 0; // offset from the centered position, after dragging
static int qw_drag = 0, qw_drag_x, qw_drag_y;
static int qw_pressed = Q_NONE;
static int qw_scroll = 0, qw_sel = -1;
static int f_status = 0, f_level = 0, f_sort = 0;
static char f_area[64] = ""; // empty: all areas

static int qw_list[MAXQUEST], qw_cnt = 0;
static const char *qw_areas[MAXQUEST];
static int qw_area_cnt = 0;

static int qw_x(void)
{
	int x = (XRES - QW) / 2 + qw_dx;
	return x < 0 ? 0 : (x > XRES - QW - 1 ? XRES - QW - 1 : x);
}

static int qw_y(void)
{
	int y = (YRES - QH) / 2 - 10 + qw_dy;
	return y < 0 ? 0 : (y > YRES - QH - 1 ? YRES - QH - 1 : y);
}

static int qw_rows(void)
{
	return (QW_INFO - 6 - QW_LIST) / QW_LINE;
}

static int quest_known(int n)
{
	return (quest[n].flags == QF_OPEN && quest[n].done < 10) || quest[n].flags == QF_DONE;
}

static int quest_redo(int n)
{
	return (game_questlog[n].flags & QLF_REPEATABLE) && (quest[n].flags & QF_DONE) && quest[n].done < 10;
}

static int player_level(void)
{
	return exp2level((int)experience);
}

static int qw_cmp(const void *a, const void *b)
{
	int va = *(const int *)a, vb = *(const int *)b, d;
	const struct questlog *qa = &game_questlog[va], *qb = &game_questlog[vb];

	switch (f_sort) {
	case 0: // open first, then highest level first (the old questlog order)
		d = (quest[va].flags == QF_DONE) - (quest[vb].flags == QF_DONE);
		if (d) {
			return d;
		}
		if (qa->minlevel != qb->minlevel) {
			return qb->minlevel - qa->minlevel;
		}
		break;
	case 1:
		if (qa->minlevel != qb->minlevel) {
			return qa->minlevel - qb->minlevel;
		}
		if (qa->maxlevel != qb->maxlevel) {
			return qa->maxlevel - qb->maxlevel;
		}
		break;
	case 2:
		if ((d = strcmp(qa->area, qb->area)) != 0) {
			return d;
		}
		if (qa->minlevel != qb->minlevel) {
			return qa->minlevel - qb->minlevel;
		}
		break;
	case 3:
		if ((d = strcmp(qa->name, qb->name)) != 0) {
			return d;
		}
		break;
	}
	return va - vb;
}

static int area_cmp(const void *a, const void *b)
{
	return strcmp(*(const char *const *)a, *(const char *const *)b);
}

// Build the filtered and sorted list, and the list of areas to filter by
static void qw_build(void)
{
	int n, i, lvl = player_level();

	qw_area_cnt = 0;
	qw_cnt = 0;

	for (n = 0; n < *game_questcount && n < MAXQUEST; n++) {
		if (!quest_known(n)) {
			continue;
		}

		for (i = 0; i < qw_area_cnt; i++) {
			if (!strcmp(qw_areas[i], game_questlog[n].area)) {
				break;
			}
		}
		if (i == qw_area_cnt) {
			qw_areas[qw_area_cnt++] = game_questlog[n].area;
		}

		if (f_status == 1 && quest[n].flags != QF_OPEN) {
			continue;
		}
		if (f_status == 2 && quest[n].flags != QF_DONE) {
			continue;
		}
		if (f_status == 3 && !quest_redo(n)) {
			continue;
		}
		if (f_area[0] && strcmp(f_area, game_questlog[n].area)) {
			continue;
		}
		if (f_level == 1 && (lvl < game_questlog[n].minlevel || lvl > game_questlog[n].maxlevel)) {
			continue;
		}
		if (f_level == 2 && game_questlog[n].minlevel > lvl) {
			continue;
		}
		if (f_level == 3 && game_questlog[n].minlevel <= lvl) {
			continue;
		}
		qw_list[qw_cnt++] = n;
	}

	qsort(qw_areas, (size_t)qw_area_cnt, sizeof(qw_areas[0]), area_cmp);
	qsort(qw_list, (size_t)qw_cnt, sizeof(qw_list[0]), qw_cmp);

	if (qw_scroll > qw_cnt - qw_rows()) {
		qw_scroll = qw_cnt - qw_rows();
	}
	if (qw_scroll < 0) {
		qw_scroll = 0;
	}
}

static const char *selector_value(int s, char *buf, size_t size)
{
	switch (s) {
	case QS_STATUS:
		return status_name[f_status];
	case QS_AREA:
		return f_area[0] ? f_area : "All areas";
	case QS_LEVEL:
		if (f_level == 0) {
			return level_name[0];
		}
		snprintf(buf, size, "%s (%d)", level_name[f_level], player_level());
		return buf;
	default:
		return sort_name[f_sort];
	}
}

static void selector_step(int s, int dir)
{
	int i;

	switch (s) {
	case QS_STATUS:
		f_status = (f_status + 4 + dir) % 4;
		break;
	case QS_LEVEL:
		f_level = (f_level + 4 + dir) % 4;
		break;
	case QS_SORT:
		f_sort = (f_sort + 4 + dir) % 4;
		break;
	case QS_AREA:
		// "All areas" is position -1, then the areas in alphabetical order
		for (i = 0; i < qw_area_cnt && strcmp(qw_areas[i], f_area); i++) {
			;
		}
		if (!f_area[0]) {
			i = -1;
		}
		i += dir;
		if (i < -1) {
			i = qw_area_cnt - 1;
		}
		if (i >= qw_area_cnt) {
			i = -1;
		}
		snprintf(f_area, sizeof(f_area), "%s", i < 0 ? "" : qw_areas[i]);
		break;
	}
	qw_scroll = 0;
	qw_sel = -1;
}

// position of selector s: label at (x, y), box from bx to bx + SEL_BOX_W
static void selector_pos(int s, int *x, int *y, int *bx)
{
	*x = qw_x() + (s & 1 ? 226 : 10);
	*y = qw_y() + (s & 2 ? QW_ROW2 : QW_ROW1);
	*bx = *x + SEL_LBL_W;
}

int questwin_is_open(void)
{
	return qw_open;
}

void questwin_open(void)
{
	if (!havequest) {
		cmd_getquestlog();
		havequest = 1;
	}
	options_close();
	display_help = 0;
	display_quest = 0;
	qw_open = 1;
}

void questwin_close(void)
{
	qw_open = 0;
	qw_drag = 0;
	qw_pressed = Q_NONE;
}

void questwin_toggle(void)
{
	if (qw_open) {
		questwin_close();
	} else {
		questwin_open();
	}
}

int questwin_over(int x, int y)
{
	int px = qw_x(), py = qw_y();

	return qw_open && x >= px && x <= px + QW && y >= py && y <= py + QH;
}

static int qw_in(int x, int y, int sx, int sy, int ex, int ey)
{
	return x >= sx && x <= ex && y >= sy && y <= ey;
}

static int qw_hit(int x, int y)
{
	int px = qw_x(), py = qw_y(), s, lx, ly, bx, r;

	if (!questwin_over(x, y)) {
		return Q_NONE;
	}
	if (qw_in(x, y, px + QW - 18, py + 2, px + QW - 4, py + 15)) {
		return Q_CLOSE;
	}
	if (y < py + QW_TITLE) {
		return Q_TITLE;
	}
	if (qw_in(x, y, px + 10, py + QW_TABS - 2, px + 80, py + QW_TABS + 11)) {
		return Q_TAB_QUESTS;
	}
	if (qw_in(x, y, px + 84, py + QW_TABS - 2, px + 184, py + QW_TABS + 11)) {
		return Q_TAB_RANDOM;
	}
	if (qw_tab != 0) {
		return Q_NONE;
	}

	for (s = 0; s < 4; s++) {
		selector_pos(s, &lx, &ly, &bx);
		if (y >= ly - 2 && y <= ly + 11) {
			if (x >= bx && x <= bx + 14) {
				return Q_SEL_L + s;
			}
			if (x >= bx + SEL_BOX_W - 14 && x <= bx + SEL_BOX_W) {
				return Q_SEL_R + s;
			}
			if (x > bx + 14 && x < bx + SEL_BOX_W - 14) {
				return Q_SEL_M + s;
			}
		}
	}

	if (x >= px + QW - 14 && x <= px + QW - 4) {
		if (qw_in(x, y, px + QW - 14, py + QW_LIST, px + QW - 4, py + QW_LIST + 8)) {
			return Q_SCROLL_UP;
		}
		if (qw_in(x, y, px + QW - 14, py + QW_LIST + qw_rows() * QW_LINE - 9, px + QW - 4,
		        py + QW_LIST + qw_rows() * QW_LINE)) {
			return Q_SCROLL_DOWN;
		}
		return Q_NONE;
	}

	if (y >= py + QW_LIST && y < py + QW_LIST + qw_rows() * QW_LINE && x >= px + 6) {
		r = (y - py - QW_LIST) / QW_LINE;
		if (qw_scroll + r < qw_cnt) {
			return Q_ROW + r;
		}
	}

	if (qw_sel != -1 && quest_redo(qw_sel) && qw_in(x, y, px + QW - 84, py + QH - 26, px + QW - 12, py + QH - 10)) {
		return Q_REOPEN;
	}
	return Q_NONE;
}

int questwin_over_control(int x, int y)
{
	int h = qw_hit(x, y);
	return h != Q_NONE && h != Q_TITLE;
}

static void qw_click(int h)
{
	if (h == Q_CLOSE) {
		questwin_close();
	} else if (h == Q_TAB_QUESTS) {
		qw_tab = 0;
	} else if (h == Q_TAB_RANDOM) {
		qw_tab = 1;
	} else if (h >= Q_SEL_L && h < Q_SEL_L + 4) {
		selector_step(h - Q_SEL_L, -1);
	} else if (h >= Q_SEL_R && h < Q_SEL_R + 4) {
		selector_step(h - Q_SEL_R, 1);
	} else if (h >= Q_SEL_M && h < Q_SEL_M + 4) {
		selector_step(h - Q_SEL_M, 1);
	} else if (h == Q_SCROLL_UP) {
		qw_scroll -= qw_rows() - 1;
	} else if (h == Q_SCROLL_DOWN) {
		qw_scroll += qw_rows() - 1;
	} else if (h == Q_REOPEN) {
		if (qw_sel != -1 && quest_redo(qw_sel)) {
			cmd_reopen_quest(qw_sel);
		}
	} else if (h >= Q_ROW) {
		int i = qw_scroll + h - Q_ROW;
		if (i < qw_cnt) {
			qw_sel = (qw_sel == qw_list[i]) ? -1 : qw_list[i];
		}
	}
}

// Mouse input. Returns 1 if the event belongs to the questlog window. Clicks
// outside the window are left to the game.
int questwin_mouse(int what, int x, int y, int delta)
{
	int h;

	if (!qw_open) {
		return 0;
	}

	if (what == SDL_MOUM_NONE) {
		if (qw_drag) {
			qw_dx += x - qw_drag_x;
			qw_dy += y - qw_drag_y;
			qw_drag_x = x;
			qw_drag_y = y;
		}
		return 0;
	}

	if (what == SDL_MOUM_LUP && (qw_drag || qw_pressed != Q_NONE)) {
		qw_drag = 0;
		h = qw_hit(x, y);
		if (h != Q_NONE && h == qw_pressed) {
			qw_click(h);
		}
		qw_pressed = Q_NONE;
		return 1;
	}

	if (!questwin_over(x, y)) {
		return 0;
	}

	switch (what) {
	case SDL_MOUM_LDOWN:
		h = qw_hit(x, y);
		if (h == Q_TITLE) {
			// keep the window where it is on screen, then follow the mouse
			qw_dx = qw_x() - (XRES - QW) / 2;
			qw_dy = qw_y() - ((YRES - QH) / 2 - 10);
			qw_drag = 1;
			qw_drag_x = x;
			qw_drag_y = y;
		}
		qw_pressed = h;
		return 1;
	case SDL_MOUM_WHEEL:
		if (qw_tab == 0) {
			qw_scroll -= delta * 3;
		}
		return 1;
	default:
		return 1;
	}
}

static void qw_selector(int s, int hov)
{
	int lx, ly, bx;
	char buf[64];
	unsigned short edge = IRGB(14, 11, 6);

	selector_pos(s, &lx, &ly, &bx);
	render_text(lx, ly, textcolor, 0, qs_label[s]);

	gui_draw_box(bx, ly - 2, bx + SEL_BOX_W, ly + 11, IRGB(5, 4, 3), edge);
	gui_draw_box(bx, ly - 2, bx + 14, ly + 11, hov == Q_SEL_L + s ? IRGB(16, 12, 6) : IRGB(10, 8, 4), edge);
	gui_draw_box(bx + SEL_BOX_W - 14, ly - 2, bx + SEL_BOX_W, ly + 11,
	    hov == Q_SEL_R + s ? IRGB(16, 12, 6) : IRGB(10, 8, 4), edge);
	render_text(bx + 7, ly, whitecolor, RENDER_ALIGN_CENTER, "<");
	render_text(bx + SEL_BOX_W - 7, ly, whitecolor, RENDER_ALIGN_CENTER, ">");
	render_text(bx + SEL_BOX_W / 2, ly, hov == Q_SEL_M + s ? whitecolor : lightgraycolor, RENDER_ALIGN_CENTER,
	    selector_value(s, buf, sizeof(buf)));
}

static void qw_tab_button(int sx, int ex, int y, const char *text, int active, int hover)
{
	unsigned short fill = active ? IRGB(18, 13, 6) : (hover ? IRGB(12, 9, 4) : IRGB(7, 5, 3));

	gui_draw_box(sx, y - 2, ex, y + 11, fill, IRGB(20, 16, 8));
	render_text((sx + ex) / 2, y, active || hover ? whitecolor : lightgraycolor, RENDER_ALIGN_CENTER, text);
}

static void qw_display_list(int px, int py, int hov)
{
	int r, n, y, rows = qw_rows(), lvl = player_level();
	unsigned short col, line = IRGB(10, 8, 4);
	char buf[80];

	render_text(px + COL_NAME, py + QW_HEAD, IRGB(31, 24, 10), 0, "Quest");
	render_text(px + COL_AREA, py + QW_HEAD, IRGB(31, 24, 10), 0, "Area");
	render_text(px + COL_LEVEL, py + QW_HEAD, IRGB(31, 24, 10), 0, "Level");
	render_text(px + COL_STAT, py + QW_HEAD, IRGB(31, 24, 10), 0, "Status");
	render_line(px + 8, py + QW_HEAD + 11, px + QW - 8, py + QW_HEAD + 11, line);

	for (r = 0; r < rows && qw_scroll + r < qw_cnt; r++) {
		n = qw_list[qw_scroll + r];
		y = py + QW_LIST + r * QW_LINE;

		if (n == qw_sel) {
			render_rect_alpha(px + 6, y - 1, px + QW - 16, y + QW_LINE - 1, IRGB(18, 13, 6), 110);
		} else if (hov == Q_ROW + r) {
			render_rect_alpha(px + 6, y - 1, px + QW - 16, y + QW_LINE - 1, IRGB(12, 9, 4), 90);
		}

		render_text(px + COL_NAME, y, whitecolor, 0, game_questlog[n].name);
		render_text(px + COL_AREA, y, lightgraycolor, 0, game_questlog[n].area);

		// level range: white if it suits the player, red if too high, gray if below
		if (lvl < game_questlog[n].minlevel) {
			col = IRGB(31, 12, 10);
		} else if (lvl > game_questlog[n].maxlevel) {
			col = graycolor;
		} else {
			col = whitecolor;
		}
		render_text_fmt(px + COL_LEVEL, y, col, 0, "%d-%d", game_questlog[n].minlevel, game_questlog[n].maxlevel);

		if (quest[n].flags == QF_OPEN) {
			render_text(px + COL_STAT, y, IRGB(31, 26, 8), 0, "Open");
		} else if (quest_redo(n)) {
			snprintf(buf, sizeof(buf), "Done (%dx), redo", quest[n].done);
			render_text(px + COL_STAT, y, lightbluecolor, 0, buf);
		} else {
			snprintf(buf, sizeof(buf), quest[n].done > 1 ? "Done (%dx)" : "Done", quest[n].done);
			render_text(px + COL_STAT, y, IRGB(12, 28, 12), 0, buf);
		}
	}

	if (qw_cnt == 0) {
		render_text(px + QW / 2, py + QW_LIST + 30, lightgraycolor, RENDER_ALIGN_CENTER,
		    havequest ? "No quests match these filters." : "Your questlog is empty.");
	}

	// scrollbar
	if (qw_cnt > rows) {
		int top = py + QW_LIST, h = rows * QW_LINE;
		int th = h * rows / qw_cnt, ty = top + (h - th) * qw_scroll / (qw_cnt - rows);
		if (th < 8) {
			th = 8;
			ty = top + (h - th) * qw_scroll / (qw_cnt - rows);
		}
		render_rect_alpha(px + QW - 12, top, px + QW - 6, top + h, IRGB(6, 5, 3), 200);
		gui_draw_box(px + QW - 12, ty, px + QW - 7, ty + th, IRGB(18, 14, 8), IRGB(8, 6, 3));
	}
}

static void qw_display_info(int px, int py, int hov)
{
	int n = qw_sel, y = py + QW_INFO;
	char buf[160];

	render_line(px + 8, y - 4, px + QW - 8, y - 4, IRGB(10, 8, 4));

	if (n == -1 || !quest_known(n)) {
		snprintf(buf, sizeof(buf), "%d quest%s shown. Click a quest for details.", qw_cnt, qw_cnt == 1 ? "" : "s");
		render_text(px + 12, y + 4, lightgraycolor, 0, buf);
		return;
	}

	render_text(px + 12, y, whitecolor, 0, game_questlog[n].name);
	snprintf(buf, sizeof(buf), "From %s in %s, for levels %d to %d.", game_questlog[n].giver, game_questlog[n].area,
	    game_questlog[n].minlevel, game_questlog[n].maxlevel);
	render_text(px + 12, y + 11, lightgraycolor, 0, buf);

	if (game_questlog[n].flags & (QLF_REPEATABLE | QLF_XREPEAT)) {
		snprintf(buf, sizeof(buf), "Done %d time%s, next time gives %d%% exp. %s.", quest[n].done,
		    quest[n].done != 1 ? "s" : "", questproz(quest[n].done), (quest[n].flags & QF_DONE) ? "Done" : "Open");
	} else {
		snprintf(buf, sizeof(buf), "Not repeatable. %s.", (quest[n].flags & QF_DONE) ? "Done" : "Open");
	}
	render_text(px + 12, y + 22, lightgraycolor, 0, buf);
	if ((game_questlog[n].flags & QLF_XREPEAT) && (quest[n].flags & QF_DONE)) {
		render_text(px + 12, y + 33, graycolor, 0, "Can be done again with a junk item.");
	}

	if (quest_redo(n)) {
		unsigned short fill = hov == Q_REOPEN ? IRGB(18, 13, 6) : IRGB(11, 8, 4);
		gui_draw_box(px + QW - 84, py + QH - 26, px + QW - 12, py + QH - 10, fill, IRGB(20, 16, 8));
		render_text(px + QW - 48, py + QH - 23, whitecolor, RENDER_ALIGN_CENTER, "Re-open");
	}
}

void questwin_display(void)
{
	int px, py, hov, ox, oy;

	if (!qw_open) {
		return;
	}
#ifdef QUESTWIN_DEMO
	for (int i = 0; i < 40; i++) {
		quest[i].flags = (i % 3) ? QF_DONE : QF_OPEN;
		quest[i].done = (i % 3) ? (unsigned char)(i % 4 + 1) : 0;
	}
#endif

	qw_build();

	px = qw_x();
	py = qw_y();
	hov = qw_hit(mousex, mousey);

	render_push_clip();
	render_set_clip(0, 0, XRES, YRES);

	gui_draw_panel(px, py, QW + 1, QH + 1, QW_TITLE);
	render_text(px + QW / 2, py + 4, whitecolor, RENDER_ALIGN_CENTER, "Questlog");
	render_text(px + QW - 11, py + 4, hov == Q_CLOSE ? whitecolor : lightgraycolor, RENDER_ALIGN_CENTER, "X");

	qw_tab_button(px + 10, px + 80, py + QW_TABS, "Quests", qw_tab == 0, hov == Q_TAB_QUESTS);
	qw_tab_button(px + 84, px + 184, py + QW_TABS, "Random dungeon", qw_tab == 1, hov == Q_TAB_RANDOM);

	if (qw_tab == 0) {
		for (int s = 0; s < 4; s++) {
			qw_selector(s, hov);
		}
		qw_display_list(px, py, hov);
		qw_display_info(px, py, hov);
	} else {
		// do_display_random() draws relative to the help window; point that at our window for a moment
		ox = dot[DOT_HLP].x;
		oy = dot[DOT_HLP].y;
		dot[DOT_HLP].x = px + (QW - 214) / 2;
		dot[DOT_HLP].y = py + QW_TABS + 6;
		do_display_random();
		dot[DOT_HLP].x = ox;
		dot[DOT_HLP].y = oy;
	}

	render_pop_clip();
}
