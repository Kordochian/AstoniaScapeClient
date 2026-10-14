/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Minimap
 *
 * The round minimap can be dragged to any spot, resized by its grip (bottom right) and zoomed with
 * the mouse wheel. A click on it, the Map action key (M) or Ctrl+Shift+M opens the big map: a
 * window showing everything explored in the current area, which can be dragged, resized, zoomed
 * and panned. Positions and sizes are kept in settings.ini.
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <SDL3/SDL.h>

#include "astonia.h"
#include "gui/gui.h"
#include "gui/gui_private.h"
#include "client/client.h"
#include "game/game.h"
#include "sdl/sdl.h"
#include "lib/cjson/cJSON.h"

#define MINIMAP           40 // default radius of the round minimap, in GUI pixels
#define MM_MIN_R          30 // radius limits when resizing
#define MM_MAX_R          200
#define MM_MAX_ZOOM       4 // GUI pixels per tile on the minimap: 1 to 4
#define MMTEX             (MM_MAX_R * 2) // minimap texture size in tiles
#define MAXMAP            1024 // map positions kept per area (server areas are up to 1024x1024)
#define OLDMAP            256 // size of the old areas
#define IRGBA(r, g, b, a) (((uint32_t)(a) << 24) | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | ((uint32_t)(b) << 0))

#define MAPPIX_UNKNOWN 0
#define MAPPIX_BLOCK   1
#define MAPPIX_FSPRITE 2
#define MAPPIX_CHAR    3
#define MAPPIX_EMPTY   4
#define MAPPIX_USE     5

static int update1, update2, update3, orx, ory, rewrite_cnt;

static unsigned char _mmap[MAXMAP * MAXMAP];
static unsigned short map_poi_idx[MAXMAP * MAXMAP];
static unsigned char load_buf[MAXMAP * MAXMAP], load_buf2[MAXMAP * MAXMAP]; // file buffers (too big for the stack)

// big map: the whole area is kept in one texture (maptex1); update1 = rebuild all of it,
// otherwise only the changed rectangle is sent
static uint32_t mapix1[MAXMAP * MAXMAP];
static int dirty_x0 = MAXMAP, dirty_y0 = MAXMAP, dirty_x1 = -1, dirty_y1 = -1;
static int known_w, known_h; // highest known position + 1, decides between old 256 and big map handling
static uint32_t mapix2[MMTEX * MMTEX];

#define MAXSAVEMAP 100
static int mapnr = -1;

static int map_managed = 0; // map managed 0 = we're guessing. map managed 1 = the server will send us area changes.
static int map_area = 0;
static int map_server = 0;

static int map_poi_load(void);

static int clampi(int v, int lo, int hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

// size of the area the big map can show: 256 for the old areas
static int map_ext(void)
{
	if (known_w > OLDMAP || known_h > OLDMAP || originx >= OLDMAP || originy >= OLDMAP) {
		return MAXMAP;
	}
	return OLDMAP;
}

static void map_known_reset(void)
{
	int i;

	known_w = known_h = 0;
	for (i = 0; i < MAXMAP * MAXMAP; i++) {
		if (_mmap[i]) {
			if (i % MAXMAP + 1 > known_w) {
				known_w = i % MAXMAP + 1;
			}
			if (i / MAXMAP + 1 > known_h) {
				known_h = i / MAXMAP + 1;
			}
		}
	}
}

static void map_known_add(int x, int y)
{
	if (x + 1 > known_w) {
		known_w = x + 1;
	}
	if (y + 1 > known_h) {
		known_h = y + 1;
	}
}

// map files: 256x256 (old areas, same format as before) or MAXMAP x MAXMAP
static int map_read_file(const char *filename, unsigned char *dst)
{
	FILE *fp;
	long size;
	int y;

	fp = fopen(filename, "rb");
	if (!fp) {
		return 0;
	}
	fseek(fp, 0, SEEK_END);
	size = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	memset(dst, 0, MAXMAP * MAXMAP);
	if (size == (long)MAXMAP * MAXMAP) {
		fread(dst, MAXMAP * MAXMAP, 1, fp);
	} else if (size == OLDMAP * OLDMAP) {
		for (y = 0; y < OLDMAP; y++) {
			fread(dst + y * MAXMAP, OLDMAP, 1, fp);
		}
	} else {
		fclose(fp);
		return 0;
	}
	fclose(fp);

	return 1;
}

static void map_write_file(const char *filename, const unsigned char *src)
{
	FILE *fp;
	int i, y, big = 0;

	for (i = 0; i < MAXMAP * MAXMAP; i++) {
		if (src[i] && (i % MAXMAP >= OLDMAP || i / MAXMAP >= OLDMAP)) {
			big = 1;
			break;
		}
	}

	fp = fopen(filename, "wb");
	if (!fp) {
		return;
	}
	if (big) {
		fwrite(src, MAXMAP * MAXMAP, 1, fp);
	} else {
		for (y = 0; y < OLDMAP; y++) {
			fwrite(src + y * MAXMAP, OLDMAP, 1, fp);
		}
	}
	fclose(fp);
}
static void map_update_poi(void);
static uint32_t map_poi_col(int x, int y);

SDL_Texture *maptex1 = NULL, *maptex2 = NULL;

// ----- layout and settings -----

// round minimap: radius and zoom (GUI pixels per tile); position saved as distance from the
// right edge and from the top (-1: default spot at the top right of the map)
static int mm_shown = 1, mm_r = MINIMAP, mm_zoom = 1, mm_rx = -1, mm_ty = -1;
static int mx, my; // top left of the minimap's square on screen
static int mm_press = 0, mm_drag = 0, mm_size = 0, mm_px, mm_py, mm_dx, mm_dy, mm_ox, mm_oy;

// big map window: offset from the centered spot, size, zoom and view center (in tiles)
#define BW_TITLE 18
#define BW_FOOT  14
#define BW_MIN_W 200
#define BW_MIN_H 160
static const float bw_zooms[] = {0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 6.0f, 8.0f};
#define BW_ZOOMS ((int)(sizeof(bw_zooms) / sizeof(bw_zooms[0])))
static int bw_open = 0, bw_dx = 0, bw_dy = 0, bw_w = 540, bw_h = 440, bw_zi = 4;
static float bw_cx, bw_cy;
static int bw_follow = 1; // view follows the player until the map is dragged
static int bw_drag = 0, bw_size = 0, bw_pan = 0, bw_pressed = 0, bw_ax, bw_ay, bw_ox, bw_oy;

enum { B_NONE, B_CLOSE, B_TITLE, B_GRIP, B_MAP, B_BODY };

static int mm_d(void)
{
	return mm_r * 2;
}

// tiles from the center to the edge of the minimap
static int mm_tiles(void)
{
	return max(8, mm_r / mm_zoom);
}

static void mm_place(void)
{
	if (mm_rx < 0 || mm_ty < 0) {
		mx = dotx(DOT_MBR) - mm_d() - 6;
		my = doty(DOT_MTL) + 6;
	} else {
		mx = XRES - mm_rx - mm_d();
		my = mm_ty;
	}
	mx = clampi(mx, 0, max(0, XRES - mm_d() - 1));
	my = clampi(my, 0, max(0, YRES - mm_d() - 1));
}

// remember the spot as distance from the right edge, so it stays there when the window grows
static void mm_store(void)
{
	mm_rx = XRES - mx - mm_d();
	mm_ty = my;
}

int minimap_load_setting(const char *key, const char *val)
{
	if (!strcmp(key, "minimap")) {
		int s = 1, rx = -1, ty = -1, r = MINIMAP, z = 1;
		sscanf(val, "%d,%d,%d,%d,%d", &s, &rx, &ty, &r, &z);
		mm_shown = s ? 1 : 0;
		mm_rx = rx;
		mm_ty = ty;
		mm_r = clampi(r, MM_MIN_R, MM_MAX_R);
		mm_zoom = clampi(z, 1, MM_MAX_ZOOM);
		return 1;
	}
	if (!strcmp(key, "bigmap")) {
		int dx = 0, dy = 0, w = 540, h = 440, z = 4;
		sscanf(val, "%d,%d,%d,%d,%d", &dx, &dy, &w, &h, &z);
		bw_dx = dx;
		bw_dy = dy;
		bw_w = max(BW_MIN_W, w);
		bw_h = max(BW_MIN_H, h);
		bw_zi = clampi(z, 0, BW_ZOOMS - 1);
		return 1;
	}
	return 0;
}

void minimap_save_settings(void *f)
{
	FILE *fp = f;

	fprintf(fp, "minimap=%d,%d,%d,%d,%d\n", mm_shown, mm_rx, mm_ty, mm_r, mm_zoom);
	fprintf(fp, "bigmap=%d,%d,%d,%d,%d\n", bw_dx, bw_dy, bw_w, bw_h, bw_zi);
}

int minimap_shown(void)
{
	return mm_shown;
}

void minimap_set_shown(int onoff)
{
	mm_shown = onoff ? 1 : 0;
}

// back to the default spot, size and zoom (Settings: Panel layout Reset)
void minimap_reset_layout(void)
{
	mm_rx = mm_ty = -1;
	mm_r = MINIMAP;
	mm_zoom = 1;
	bw_dx = bw_dy = 0;
	bw_w = 540;
	bw_h = 440;
	update2 = 1;
}

// the window was laid out again: the minimap keeps its distance from the right edge
void minimap_layout(void)
{
	mm_place();
}

void minimap_init(void)
{
	if (game_options & GO_NOMAP) {
		return;
	}

	minimap_layout();

	memset(_mmap, 0, sizeof(_mmap));
	known_w = known_h = 0;
	update1 = update2 = update3 = 1;

	maptex1 = sdl_create_texture(MAXMAP, MAXMAP);
	maptex2 = sdl_create_texture(MMTEX, MMTEX);

	SDL_SetTextureBlendMode(maptex1, SDL_BLENDMODE_BLEND);
	SDL_SetTextureBlendMode(maptex2, SDL_BLENDMODE_BLEND);

	SDL_SetTextureScaleMode(maptex1, SDL_SCALEMODE_NEAREST);
	SDL_SetTextureScaleMode(maptex2, SDL_SCALEMODE_NEAREST);
}

// a map position changed: the big map sends this part again (POIs around it change color too)
static void mark_dirty(int x, int y)
{
	int r = map_poi_idx[x + y * MAXMAP] ? 2 : 0;

	dirty_x0 = min(dirty_x0, max(0, x - r));
	dirty_y0 = min(dirty_y0, max(0, y - r));
	dirty_x1 = max(dirty_x1, min(MAXMAP - 1, x + r));
	dirty_y1 = max(dirty_y1, min(MAXMAP - 1, y + r));
}

static void set_pix(int x, int y, unsigned char val)
{
	unsigned char val2;

	if ((val2 = _mmap[x + y * MAXMAP]) != val) {
		// count how much of the map has changed permanently (not counting characters
		// and formerly unknown tiles or swapping between sightblocks and fsprites)
		if (val2 != MAPPIX_UNKNOWN && val2 != MAPPIX_CHAR && val != MAPPIX_CHAR &&
		    !((val == MAPPIX_BLOCK && val2 == MAPPIX_FSPRITE) || (val == MAPPIX_FSPRITE && val2 == MAPPIX_BLOCK))) {
			// note("changed: %d to %d (%d,%d)",_mmap[x+y*MAXMAP],val,x,y);
			rewrite_cnt++;
		}

		_mmap[x + y * MAXMAP] = val;
		map_known_add(x, y);
		mark_dirty(x, y);
		update2 = update3 = 1;
	}
}

static void map_save(void);
static int map_load(void);

void minimap_update(void)
{
	int x, y, xs, xe, ox, oy;

	if (game_options & GO_NOMAP) {
		return;
	}

	ox = (int)originx - (int)DIST;
	oy = (int)originy - (int)DIST;

	rewrite_cnt = 0;
	for (y = 1; y < (int)DIST * 2; y++) {
		if (y + oy < 0) {
			continue;
		}
		if (y + oy >= MAXMAP) {
			continue;
		}

		if (y < (int)DIST) {
			xs = (int)DIST - y;
			xe = (int)DIST + y;
		} else {
			xs = y - (int)DIST;
			xe = (int)DIST * 3 - y;
		}

		for (x = xs + 1; x < xe; x++) {
			if (x + ox < 0) {
				continue;
			}
			if (x + ox >= MAXMAP) {
				continue;
			}
			map_index_t mn = mapmn((unsigned int)x, (unsigned int)y);
			if (!(map[mn].flags & CMF_VISIBLE)) {
				continue;
			}

			if (map[mn].mmf & MMF_SIGHTBLOCK) {
				if (map[mn].flags & CMF_USE) {
					set_pix(ox + x, oy + y, MAPPIX_USE);
				} else {
					set_pix(ox + x, oy + y, MAPPIX_BLOCK);
				}
			} else if (map[mn].fsprite) {
				set_pix(ox + x, oy + y, MAPPIX_FSPRITE);
			} else if (map[mn].csprite && mn != (unsigned int)plrmn) {
				set_pix(ox + x, oy + y, MAPPIX_CHAR);
			} else {
				set_pix(ox + x, oy + y, MAPPIX_EMPTY);
			}
		}
	}
	if (rewrite_cnt > 8 && !map_managed) {
		memset(_mmap, 0, sizeof(_mmap));
		known_w = known_h = 0;
		update1 = update2 = 1;
		note("MAP CHANGED: %d", rewrite_cnt);
	}
	if (mapnr == -1 && update3) {
		update3 = 0;
		if (!map_managed && (game_options & GO_MAPSAVE)) {
			mapnr = map_load();
		}
	}
}

static uint32_t pix_col(int x, int y)
{
	uint32_t c;

	switch (_mmap[x + y * MAXMAP]) {
	case MAPPIX_BLOCK:
		return IRGBA(180, 180, 180, 255);
	case MAPPIX_FSPRITE:
		return IRGBA(140, 140, 220, 255);
	case MAPPIX_CHAR:
		return IRGBA(60, 220, 60, 255);
	case MAPPIX_EMPTY:
		if ((c = map_poi_col(x, y))) {
			return c;
		}
		return IRGBA(60, 60, 60, 255);
	case MAPPIX_USE:
		return IRGBA(120, 80, 80, 255);
	case MAPPIX_UNKNOWN:
	default:
		if ((c = map_poi_col(x, y))) {
			return c;
		}
		return IRGBA(25, 25, 25, 255);
	}
}

static void draw_center(int x, int y)
{
	render_pixel(x, y, IRGB(31, 8, 8));
	render_pixel(x + 1, y, IRGB(31, 8, 8));
	render_pixel(x, y + 1, IRGB(31, 8, 8));
	render_pixel(x - 1, y, IRGB(31, 8, 8));
	render_pixel(x, y - 1, IRGB(31, 8, 8));
}

static void draw_center2(int x, int y)
{
	int i;

	render_pixel(x, y, IRGB(31, 8, 8));

	for (i = 0; i < 3; i++) {
		render_pixel(x + i, y, IRGB(31, 8, 8));
		render_pixel(x, y + i, IRGB(31, 8, 8));
		render_pixel(x - i, y, IRGB(31, 8, 8));
		render_pixel(x, y - i, IRGB(31, 8, 8));
	}
}

static void draw_grip(int x, int y, unsigned short col)
{
	for (int i = 0; i < 3; i++) {
		render_line(x - 5 - i * 3, y - 3, x - 3, y - 5 - i * 3, col);
	}
}

// ----- big map window -----

static int bw_W(void)
{
	return min(bw_w, XRES - 1);
}

static int bw_H(void)
{
	return min(bw_h, YRES - 1);
}

static int bw_x(void)
{
	return clampi((XRES - bw_W()) / 2 + bw_dx, 0, XRES - bw_W() - 1);
}

static int bw_y(void)
{
	return clampi((YRES - bw_H()) / 2 - 10 + bw_dy, 0, YRES - bw_H() - 1);
}

// keep the window where it is on screen after a drag or a size change
static void bw_store(int x, int y)
{
	bw_dx = x - (XRES - bw_W()) / 2;
	bw_dy = y - ((YRES - bw_H()) / 2 - 10);
}

// the map inside the window
static void bw_inner(int *ix, int *iy, int *iw, int *ih)
{
	*ix = bw_x() + 4;
	*iy = bw_y() + BW_TITLE + 2;
	*iw = bw_W() - 8;
	*ih = bw_H() - BW_TITLE - BW_FOOT - 4;
}

static float bw_z(void)
{
	return bw_zooms[clampi(bw_zi, 0, BW_ZOOMS - 1)];
}

// top left tile of the view (may be outside the area)
static void bw_view(float *left, float *top)
{
	int ix, iy, iw, ih;
	float z = bw_z();

	if (bw_follow) {
		bw_cx = (float)originx + 0.5f;
		bw_cy = (float)originy + 0.5f;
	}
	bw_inner(&ix, &iy, &iw, &ih);
	*left = bw_cx - (float)iw / (2.0f * z);
	*top = bw_cy - (float)ih / (2.0f * z);
}

static int bw_over(int x, int y)
{
	int px = bw_x(), py = bw_y();

	return bw_open && x >= px && x <= px + bw_W() && y >= py && y <= py + bw_H();
}

static int bw_hit(int x, int y)
{
	int px = bw_x(), py = bw_y(), ix, iy, iw, ih;

	if (!bw_over(x, y)) {
		return B_NONE;
	}
	if (x >= px + bw_W() - 18 && x <= px + bw_W() - 4 && y >= py + 2 && y <= py + 15) {
		return B_CLOSE;
	}
	if (y < py + BW_TITLE) {
		return B_TITLE;
	}
	if (x >= px + bw_W() - 10 && y >= py + bw_H() - 10) {
		return B_GRIP;
	}
	bw_inner(&ix, &iy, &iw, &ih);
	if (x >= ix && x < ix + iw && y >= iy && y < iy + ih) {
		return B_MAP;
	}
	return B_BODY;
}

// send the changed part of the area to the big map texture
static void bigmap_upload(void)
{
	int x, y, x0, y0, x1, y1, w;
	SDL_Rect r;

	if (update1) {
		x0 = y0 = 0;
		x1 = y1 = MAXMAP - 1;
		update1 = 0;
	} else if (dirty_x1 < 0) {
		return;
	} else {
		x0 = dirty_x0;
		y0 = dirty_y0;
		x1 = dirty_x1;
		y1 = dirty_y1;
	}
	dirty_x0 = dirty_y0 = MAXMAP;
	dirty_x1 = dirty_y1 = -1;

	w = x1 - x0 + 1;
	for (y = y0; y <= y1; y++) {
		for (x = x0; x <= x1; x++) {
			mapix1[(x - x0) + (y - y0) * w] = pix_col(x, y);
		}
	}
	r.x = x0;
	r.y = y0;
	r.w = w;
	r.h = y1 - y0 + 1;
	SDL_UpdateTexture(maptex1, &r, mapix1, w * (int)sizeof(uint32_t));
}

static void bigmap_display(void)
{
	int px, py, W, H, ix, iy, iw, ih, hov, ext;
	float z, left, top, sx0, sy0, sx1, sy1;
	SDL_FRect dr, sr;

	px = bw_x();
	py = bw_y();
	W = bw_W();
	H = bw_H();
	hov = (bw_drag || bw_size || bw_pan) ? B_NONE : bw_hit(mousex, mousey);

	render_push_clip();
	render_set_clip(0, 0, XRES, YRES);

	gui_draw_panel(px, py, W + 1, H + 1, BW_TITLE);
	render_text(px + W / 2, py + 5, whitecolor, RENDER_ALIGN_CENTER, "Map");
	render_text(px + W - 11, py + 5, hov == B_CLOSE ? whitecolor : lightgraycolor, RENDER_ALIGN_CENTER, "X");
	draw_grip(px + W, py + H, hov == B_GRIP || bw_size ? whitecolor : IRGB(20, 16, 8));

	bigmap_upload();

	bw_inner(&ix, &iy, &iw, &ih);
	z = bw_z();
	bw_view(&left, &top);
	ext = map_ext();

	render_rect(ix, iy, ix + iw, iy + ih, IRGB(1, 1, 1));

	// the part of the area inside the view, cut at the area's edges
	sx0 = max(left, 0.0f);
	sy0 = max(top, 0.0f);
	sx1 = min(left + (float)iw / z, (float)ext);
	sy1 = min(top + (float)ih / z, (float)ext);
	if (sx1 > sx0 && sy1 > sy0) {
		sr.x = sx0;
		sr.y = sy0;
		sr.w = sx1 - sx0;
		sr.h = sy1 - sy0;
		dr.x = ((float)ix + (sx0 - left) * z + (float)x_offset) * (float)sdl_scale;
		dr.y = ((float)iy + (sy0 - top) * z + (float)y_offset) * (float)sdl_scale;
		dr.w = sr.w * z * (float)sdl_scale;
		dr.h = sr.h * z * (float)sdl_scale;
		sdl_render_copy(maptex1, &sr, &dr);
	}

	render_set_clip(ix, iy, ix + iw, iy + ih);
	{
		int cx = ix + (int)(((float)originx + 0.5f - left) * z);
		int cy = iy + (int)(((float)originy + 0.5f - top) * z);
		if (cx >= ix && cx < ix + iw && cy >= iy && cy < iy + ih) {
			draw_center2(cx, cy);
		}
	}
	render_text(ix + 4, iy + 3, whitecolor, RENDER_TEXT_FRAMED, "N");
	render_set_clip(0, 0, XRES, YRES);

	render_text(ix, py + H - BW_FOOT + 1, graycolor, RENDER_TEXT_SMALL,
	    "Wheel: zoom   Drag: move   Right-click: center on you");
	render_text_fmt(ix + iw, py + H - BW_FOOT + 1, graycolor, RENDER_TEXT_SMALL | RENDER_TEXT_RIGHT, "%d%%",
	    (int)(z * 100.0f + 0.5f));

	render_pop_clip();
}

// zoom the big map one step, keeping the tile under (x, y) where it is
static void bw_zoom_at(int x, int y, int step)
{
	int ix, iy, iw, ih, nz = clampi(bw_zi + step, 0, BW_ZOOMS - 1);
	float left, top, tx, ty, z0, z1;

	if (nz == bw_zi) {
		return;
	}
	bw_view(&left, &top);
	bw_inner(&ix, &iy, &iw, &ih);
	z0 = bw_z();
	z1 = bw_zooms[nz];
	if (!bw_follow) {
		tx = left + (float)(x - ix) / z0;
		ty = top + (float)(y - iy) / z0;
		bw_cx = tx - (float)(x - ix) / z1 + (float)iw / (2.0f * z1);
		bw_cy = ty - (float)(y - iy) / z1 + (float)ih / (2.0f * z1);
	}
	bw_zi = nz;
}

static int bigmap_mouse(int what, int x, int y, int delta)
{
	int h;

	if (!bw_open) {
		return 0;
	}

	switch (what) {
	case SDL_MOUM_NONE:
		if (bw_size) {
			// the top left corner stays where it is while the bottom right follows the mouse
			bw_w = clampi(x - bw_ax - bw_ox, BW_MIN_W, XRES - 1 - bw_ox);
			bw_h = clampi(y - bw_ay - bw_oy, BW_MIN_H, YRES - 1 - bw_oy);
			bw_store(bw_ox, bw_oy);
			return 1;
		}
		if (bw_drag) {
			bw_dx += x - bw_ax;
			bw_dy += y - bw_ay;
			bw_ax = x;
			bw_ay = y;
			return 1;
		}
		if (bw_pan) {
			if (bw_pan == 1 && abs(x - bw_ax) + abs(y - bw_ay) < 3) {
				return 1;
			}
			bw_pan = 2;
			bw_follow = 0;
			bw_cx -= (float)(x - bw_ax) / bw_z();
			bw_cy -= (float)(y - bw_ay) / bw_z();
			bw_cx = SDL_clamp(bw_cx, 0.0f, (float)map_ext());
			bw_cy = SDL_clamp(bw_cy, 0.0f, (float)map_ext());
			bw_ax = x;
			bw_ay = y;
			return 1;
		}
		return 0;

	case SDL_MOUM_LUP:
		if (bw_size || bw_drag) {
			bw_store(bw_x(), bw_y());
			bw_w = bw_W();
			bw_h = bw_H();
			bw_size = bw_drag = 0;
			options_save();
			return 1;
		}
		if (bw_pan) {
			bw_pan = 0;
			return 1;
		}
		if (bw_pressed) {
			if (bw_pressed == B_CLOSE && bw_hit(x, y) == B_CLOSE) {
				bw_open = 0;
			}
			bw_pressed = 0;
			return 1;
		}
		return bw_over(x, y);

	case SDL_MOUM_LDOWN:
		h = bw_hit(x, y);
		if (h == B_NONE) {
			return 0;
		}
		if (h == B_GRIP) {
			bw_ox = bw_x();
			bw_oy = bw_y();
			bw_store(bw_ox, bw_oy);
			bw_size = 1;
			bw_ax = x - (bw_x() + bw_W());
			bw_ay = y - (bw_y() + bw_H());
		} else if (h == B_TITLE) {
			bw_store(bw_x(), bw_y());
			bw_drag = 1;
			bw_ax = x;
			bw_ay = y;
		} else if (h == B_MAP) {
			bw_pan = 1;
			bw_ax = x;
			bw_ay = y;
		} else {
			bw_pressed = h;
		}
		return 1;

	case SDL_MOUM_RUP:
		if (bw_hit(x, y) == B_MAP) {
			bw_follow = 1;
		}
		return bw_over(x, y);

	case SDL_MOUM_WHEEL:
		if (!bw_over(x, y)) {
			return 0;
		}
		bw_zoom_at(x, y, delta > 0 ? 1 : (delta < 0 ? -1 : 0));
		options_save();
		return 1;

	default:
		return bw_over(x, y);
	}
}

// ----- round minimap -----

static int mm_over_circle(int x, int y)
{
	int dx = x - (mx + mm_r), dy = y - (my + mm_r);

	return dx * dx + dy * dy <= mm_r * mm_r;
}

static int mm_over_grip(int x, int y)
{
	return x >= mx + mm_d() - 10 && x <= mx + mm_d() + 2 && y >= my + mm_d() - 10 && y <= my + mm_d() + 2;
}

static int mm_active(void)
{
	return mm_shown && !(game_options & GO_NOMAP);
}

static int mm_over(int x, int y)
{
	if (!mm_active()) {
		return 0;
	}
	mm_place();
	return mm_over_circle(x, y) || (!panels_locked && mm_over_grip(x, y));
}

static void minimap_round_display(void)
{
	int x, y, ix, iy, i, R, d, hov;
	SDL_FRect dr, sr;
	SDL_Rect ur;

	mm_place();

	if (orx != originx || ory != originy) {
		update2 = 1;
		orx = originx;
		ory = originy;
	}

	R = mm_tiles();
	d = R * 2;
	if (update2) {
		bzero(mapix2, sizeof(uint32_t) * (size_t)(d * d));
		for (iy = -R; iy < R; iy++) {
			for (ix = -R; ix < R; ix++) {
				if (ix * ix + iy * iy > R * R) {
					continue;
				}

				x = originx + ix;
				y = originy + iy;

				if (x < 0 || x >= MAXMAP || y < 0 || y >= MAXMAP) {
					mapix2[(R + ix) + (R + iy) * d] = IRGBA(25, 25, 25, 255);
				} else {
					mapix2[(R + ix) + (R + iy) * d] = pix_col(x, y);
				}
			}
		}
		ur.x = ur.y = 0;
		ur.w = ur.h = d;
		SDL_UpdateTexture(maptex2, &ur, mapix2, d * (int)sizeof(uint32_t));
		update2 = 0;
	}

	dr.x = (float)((mx + x_offset) * sdl_scale);
	dr.w = (float)(mm_d() * sdl_scale);
	dr.y = (float)((my + y_offset) * sdl_scale);
	dr.h = (float)(mm_d() * sdl_scale);

	sr.x = 0.0f;
	sr.w = (float)d;
	sr.y = 0.0f;
	sr.h = (float)d;

	sdl_render_copy_ex(maptex2, &sr, &dr, 45.0);
	draw_center(mx + mm_r, my + mm_r);

	hov = mm_drag || mm_size || (!bw_over(mousex, mousey) && mm_over(mousex, mousey));
	for (i = 0; i < sdl_scale; i++) {
		sdl_render_circle((mx + mm_r + x_offset) * sdl_scale, (my + mm_r + y_offset) * sdl_scale,
		    mm_r * sdl_scale + i, 0xffffffff);
	}
	render_text(mx + mm_r, my + 4, 0xffff, 0, "N");
	if (hov && !panels_locked) {
		draw_grip(mx + mm_d(), my + mm_d(), mm_size || mm_over_grip(mousex, mousey) ? whitecolor : lightgraycolor);
	}
}

void display_minimap(void)
{
	if (game_options & GO_NOMAP) {
		return;
	}

	if (mm_shown) {
		minimap_round_display();
	}
	if (bw_open) {
		bigmap_display();
	}
}

// Mouse input for the big map window and the round minimap. Returns 1 if the event was used.
// The minimap is dragged by any point inside it and resized by its grip, unless the panels
// are locked; a click on it opens the big map, the wheel zooms it.
int minimap_mouse(int what, int x, int y, int delta)
{
	if (game_options & GO_NOMAP) {
		return 0;
	}

	if (bigmap_mouse(what, x, y, delta)) {
		return 1;
	}
	if (!mm_active()) {
		mm_press = mm_drag = mm_size = 0;
		return 0;
	}
	mm_place();

	switch (what) {
	case SDL_MOUM_NONE:
		if (mm_size) {
			// the top left corner stays where it is while the grip follows the mouse
			int r = max(x - mm_ox - mm_dx, y - mm_oy - mm_dy) / 2;
			mm_r = clampi(r, MM_MIN_R, MM_MAX_R);
			mx = mm_ox;
			my = mm_oy;
			mm_store();
			mm_place();
			update2 = 1;
			return 1;
		}
		if (mm_press && !mm_drag && !panels_locked && abs(x - mm_px) + abs(y - mm_py) >= 3) {
			mm_drag = 1;
		}
		if (mm_drag) {
			mx = x - mm_dx;
			my = y - mm_dy;
			mm_store();
			mm_place();
			return 1;
		}
		return 0;

	case SDL_MOUM_LDOWN:
		if (csprite) {
			return 0; // an item on the mouse goes to the world below
		}
		if (!panels_locked && mm_over_grip(x, y) && !mm_over_circle(x, y)) {
			mm_size = 1;
			mm_ox = mx;
			mm_oy = my;
			mm_dx = x - (mx + mm_d());
			mm_dy = y - (my + mm_d());
			return 1;
		}
		if (mm_over_circle(x, y)) {
			mm_press = 1;
			mm_px = x;
			mm_py = y;
			mm_dx = x - mx;
			mm_dy = y - my;
			return 1;
		}
		return 0;

	case SDL_MOUM_LUP:
		if (mm_size || mm_drag) {
			mm_place();
			mm_store();
			mm_size = mm_drag = mm_press = 0;
			options_save();
			return 1;
		}
		if (mm_press) {
			mm_press = 0;
			if (mm_over_circle(x, y)) {
				minimap_toggle();
			}
			return 1;
		}
		return 0;

	case SDL_MOUM_WHEEL:
		if (!mm_over_circle(x, y)) {
			return 0;
		}
		mm_zoom = clampi(mm_zoom + (delta > 0 ? 1 : (delta < 0 ? -1 : 0)), 1, MM_MAX_ZOOM);
		update2 = 1;
		options_save();
		return 1;

	default:
		return mm_over_circle(x, y);
	}
}

// the mouse is over the minimap or the big map: the world below does not react to it
int minimap_over(int x, int y)
{
	if (game_options & GO_NOMAP) {
		return 0;
	}
	return bw_over(x, y) || mm_over(x, y) || mm_drag || mm_size || bw_drag || bw_size || bw_pan;
}

static void minimap_clearonly(void)
{
	memset(_mmap, 0, sizeof(_mmap));
	known_w = known_h = 0;
	update1 = update2 = update3 = 1;
}

void minimap_clear(void)
{
	if (game_options & GO_MAPSAVE) {
		map_save();
	}
	mapnr = -1;
	map_area = 0;
	memset(_mmap, 0, sizeof(_mmap));
	known_w = known_h = 0;
	update1 = update2 = update3 = 1;
}

static void minimap_reveal(int x, int y)
{
	if (x < 0 || x >= MAXMAP || y < 0 || y >= MAXMAP) {
		return;
	}

	_mmap[x + y * MAXMAP] = MAPPIX_EMPTY;
	map_known_add(x, y);
	mark_dirty(x, y);
	update2 = update3 = 1;
}

// open or close the big map (Map action key, Ctrl+Shift+M, click on the minimap)
void minimap_toggle(void)
{
	if (game_options & GO_NOMAP) {
		return;
	}
	bw_open = !bw_open;
	bw_drag = bw_size = bw_pan = bw_pressed = 0;
	if (bw_open) {
		bw_follow = 1;
	}
}

// Escape: close the big map
void minimap_hide(void)
{
	bw_open = 0;
	bw_drag = bw_size = bw_pan = bw_pressed = 0;
}

static char *mapname(int i)
{
	static char filename[MAX_PATH];

	if (map_managed) {
		if (localdata) {
			sprintf(filename, "%smMap%d_%d.dat", localdata, map_server, map_area);
		} else {
			sprintf(filename, "bin/data/mMap%d_%d.dat", map_server, map_area);
		}
	} else {
		if (localdata) {
			sprintf(filename, "%smap%03d.dat", localdata, i);
		} else {
			sprintf(filename, "bin/data/map%03d.dat", i);
		}
	}

	return filename;
}

static void map_save_unmanaged(void)
{
	FILE *fp;
	int i, cnt;
	char *filename;

	for (i = cnt = 0; i < MAXMAP * MAXMAP; i++) {
		if (_mmap[i]) {
			cnt++;
		}
	}
	if (cnt < 250) {
		return;
	}

	// check if another client wrote the same map
	// in the meantime
	mapnr = map_load();

	// new map, find a save-slot
	if (mapnr == -1) {
		for (i = 0; i < MAXSAVEMAP; i++) {
			filename = mapname(i);
			fp = fopen(filename, "rb");
			if (!fp) {
				break;
			}
			fclose(fp);
		}
		if (i == MAXSAVEMAP) {
			warn("Area map storage full! Please use /compactmap to merge duplicate maps.");
			return;
		}
		mapnr = i;
	}

	filename = mapname(mapnr);
	// note("saving area map to %s",filename);
	map_write_file(filename, _mmap);
}

static void map_save_managed(void)
{
	char *filename;

	if (!map_area) {
		return;
	}

	filename = mapname(42);
	note("saving area map to %s", filename);
	map_write_file(filename, _mmap);
}

static void map_save(void)
{
	if (map_managed) {
		map_save_managed();
	} else {
		map_save_unmanaged();
	}
}

static int map_compare(const unsigned char *tmap, const unsigned char *xmap)
{
	int i, hit, miss;

	for (i = hit = miss = 0; i < MAXMAP * MAXMAP; i++) {
		// sightblock, fsprite or usable sightblock
		if (tmap[i] == 1 || tmap[i] == 2 || tmap[i] == 5) {
			if (xmap[i] == 1 || xmap[i] == 2 || xmap[i] == 5) {
				hit++;
			} else if (xmap[i] != 0) {
				miss++;
			}
		}
		// empty or csprite
		if (tmap[i] == 3 || tmap[i] == 4) {
			if (xmap[i] == 3 || xmap[i] == 4) {
				hit++;
			} else if (xmap[i] != 0) {
				miss++;
			}
		}
	}
	if (hit < 200) {
		return 0;
	}
	if (miss > hit / 100) {
		return 0;
	}

	return hit;
}

static void map_merge(unsigned char *xmap, const unsigned char *tmap)
{
	int i;

	// only overwrite empty parts of the map with loaded data.
	for (i = 0; i < MAXMAP * MAXMAP; i++) {
		if (!xmap[i]) {
			if (tmap[i] == 3) {
				xmap[i] = 4; // do not load csprites, they move too much
			} else {
				xmap[i] = tmap[i];
			}
		}
	}
}

static int map_load_unmanaged(void)
{
	int i, hit, besti = -1, besthit = 0;
	char *filename;

	for (i = 0; i < MAXSAVEMAP; i++) {
		filename = mapname(i);
		if (!map_read_file(filename, load_buf)) {
			continue;
		}

		if (!(hit = map_compare(load_buf, _mmap))) {
			continue;
		}

		if (hit > besthit) {
			besti = i;
			besthit = hit;
		}
	}
	if (besti != -1) {
		filename = mapname(besti);
		// note("loading area map from %s (%d hits)",filename,besthit);
		if (!map_read_file(filename, load_buf)) {
			return -1;
		}

		map_merge(_mmap, load_buf);
		map_known_reset();
		update1 = update2 = 1;

		return besti;
	}

	return -1;
}

static int map_load_managed(void)
{
	char *filename;

	if (!map_area) {
		return 0;
	}

	filename = mapname(42);
	note("loading area map from %s", filename);

	if (map_read_file(filename, load_buf)) {
		memcpy(_mmap, load_buf, sizeof(_mmap));
		map_known_reset();
		update1 = update2 = 1;
	}

	return 1;
}

static int map_load(void)
{
	if (map_managed) {
		return map_load_managed();
	} else {
		return map_load_unmanaged();
	}
}

void minimap_compact(void)
{
	int i, j;
	char *filename;

	if (game_options & GO_NOMAP) {
		return;
	}

	for (i = 0; i < MAXSAVEMAP; i++) {
		filename = mapname(i);
		if (!map_read_file(filename, load_buf)) {
			continue;
		}

		for (j = i + 1; j < MAXSAVEMAP; j++) {
			filename = mapname(j);
			if (!map_read_file(filename, load_buf2)) {
				continue;
			}

			if (map_compare(load_buf, load_buf2)) {
				map_merge(load_buf, load_buf2);
				filename = mapname(i);
				map_write_file(filename, load_buf);

				filename = mapname(j);
				remove(filename);
				note("merged map %d into map %d", j, i);
			}
		}
	}
}

#define AIC_SETID  0
#define AIC_CLEAR  1
#define AIC_REVEAL 2

void minimap_areainfo(int cmd, int opt1, int opt2)
{
	int cnt;

	map_managed = 1;

	switch (cmd) {
	case AIC_SETID:
		if (map_area) {
			map_save();
		}

		map_area = opt1;
		map_server = opt2;

		map_load();
		cnt = map_poi_load();
		bzero(map_poi_idx, sizeof(map_poi_idx));
		if (cnt) {
			map_update_poi();
		}
		update1 = update2 = 1;
		break;
	case AIC_CLEAR:
		minimap_clearonly();
		break;
	case AIC_REVEAL:
		minimap_reveal(opt1, opt2);
		break;
	}
}

typedef struct {
	int x, y;
	int type;
	char *desc;
} MAP_POI;

MAP_POI *map_poi = NULL;
int map_poi_cnt = 0, map_poi_max = 0;

static int map_poi_parse(const char *json_str, const char *source_name)
{
	cJSON *root = cJSON_Parse(json_str);
	if (!root) {
		warn("map_poi: Failed to parse %s: %s", source_name, cJSON_GetErrorPtr());
		return -1;
	}

	cJSON *coords_arr = cJSON_GetObjectItem(root, "coords");
	if (!coords_arr || !cJSON_IsArray(coords_arr)) {
		warn("map_poi: Missing coords array in %s", source_name);
		cJSON_Delete(root);
		return -1;
	}

	int count = cJSON_GetArraySize(coords_arr);
	int i;
	for (i = 0; i < count; i++) {
		cJSON *item = cJSON_GetArrayItem(coords_arr, i);
		if (!item || !cJSON_IsObject(item)) {
			continue;
		}

		cJSON *x = cJSON_GetObjectItem(item, "x");
		cJSON *y = cJSON_GetObjectItem(item, "y");
		cJSON *type = cJSON_GetObjectItem(item, "type");
		cJSON *desc = cJSON_GetObjectItem(item, "desc");
		if (!x || !y || !type || !desc || !cJSON_IsNumber(x) || !cJSON_IsNumber(y) || !cJSON_IsNumber(type) ||
		    !cJSON_IsString(desc)) {
			continue;
		}

		if (map_poi_cnt >= map_poi_max) {
			map_poi_max += 16;
			map_poi = xrealloc(map_poi, sizeof(MAP_POI) * (size_t)map_poi_max, MEM_GUI);
			if (!map_poi) {
				return -1;
			}
		}

		map_poi[map_poi_cnt].x = max(0, min(MAXMAP - 1, x->valueint));
		map_poi[map_poi_cnt].y = max(0, min(MAXMAP - 1, y->valueint));
		map_poi[map_poi_cnt].type = type->valueint;
		map_poi[map_poi_cnt].desc = xstrdup(desc->valuestring, MEM_GUI);
		map_poi_cnt++;

		if (map_poi_cnt > 60000) {
			break; // yeah. no one's gonna do that. right? right??
		}
	}

	cJSON_Delete(root);
	return map_poi_cnt;
}

static char *poimapname(void)
{
	static char filename[MAX_PATH];

	sprintf(filename, "res/config/map_poi%d_%d.json", map_server, map_area);

	return filename;
}

static int map_poi_load(void)
{
	char *path = poimapname();
	int loaded;

	for (int i = 1; i < map_poi_cnt; i++) {
		xfree(map_poi[i].desc);
	}
	map_poi_cnt = 1;

	if (!map_managed) {
		return 0;
	}

	char *json = load_ascii_file(path, MEM_TEMP);
	if (!json) {
		return 0;
	}

	loaded = map_poi_parse(json, "map_poi.json");
	note("loaded %d map POIs", loaded);

	return loaded;
}

static void map_update_poi(void)
{
	int i;
	int x, y, xoff, yoff;

	for (i = 1; i < map_poi_cnt; i++) {
		for (yoff = -2; yoff < 3; yoff++) {
			y = yoff + map_poi[i].y;
			if (y < 0 || y >= MAXMAP) {
				continue;
			}

			for (xoff = -2; xoff < 3; xoff++) {
				if (xoff == -2 && yoff == -2) {
					continue;
				}
				if (xoff == 2 && yoff == 2) {
					continue;
				}
				if (xoff == 2 && yoff == -2) {
					continue;
				}
				if (xoff == -2 && yoff == 2) {
					continue;
				}

				x = xoff + map_poi[i].x;
				if (x < 0 || x >= MAXMAP) {
					continue;
				}

				map_poi_idx[x + y * MAXMAP] = (unsigned short)i;
			}
		}
	}
}

static uint32_t map_poi_col(int x, int y)
{
	int i;

	if (x < 0 || y < 0 || x >= MAXMAP || y >= MAXMAP) {
		return 0;
	}

	if (!(i = map_poi_idx[x + y * MAXMAP])) {
		return 0;
	}

	if (map_poi[i].type == 2) {
		if (_mmap[map_poi[i].x + map_poi[i].y * MAXMAP] == MAPPIX_UNKNOWN) {
			return 0;
		}
	}

	if (_mmap[x + y * MAXMAP] != MAPPIX_UNKNOWN) {
		return IRGBA(64, 192, 64, 255);
	} else {
		return IRGBA(64, 128, 64, 255);
	}
}


void minimap_display_hover(int hx, int hy)
{
	int x, y, i;

	if (game_options & GO_NOMAP) {
		return;
	}

	if (bw_over(hx, hy)) { // big map window
		int ix, iy, iw, ih;
		float left, top;

		if (bw_hit(hx, hy) != B_MAP || bw_pan == 2) {
			return;
		}
		bw_view(&left, &top);
		bw_inner(&ix, &iy, &iw, &ih);
		x = (int)floorf(left + (float)(hx - ix) / bw_z());
		y = (int)floorf(top + (float)(hy - iy) / bw_z());
	} else if (mm_active() && !mm_drag && !mm_size && (mm_place(), mm_over_circle(hx, hy))) { // small, round map
		double sq = 0.70710678118654752440;
		int tmp;

		x = hx - (mx + mm_r);
		y = hy - (my + mm_r);

		// Apply clockwise rotation matrix
		tmp = (int)(round((x * sq + y * sq) / mm_zoom));
		y = (int)(round((-x * sq + y * sq) / mm_zoom));
		x = tmp;

		x += originx;
		y += originy;
	} else {
		return;
	}

	if (x < 0 || x >= MAXMAP) {
		return;
	}
	if (y < 0 || y >= MAXMAP) {
		return;
	}

	for (i = 1; i < map_poi_cnt; i++) {
		if (abs(map_poi[i].x - x) < 5 && abs(map_poi[i].y - y) < 5) {
			if (map_poi[i].type == 2) {
				if (_mmap[map_poi[i].x + map_poi[i].y * MAXMAP] == MAPPIX_UNKNOWN) {
					continue;
				}
			}
			int width = 100;
			int height;

			width = render_text_length(0, map_poi[i].desc);
			if (width > 100) {
				width = 100;
				height = render_text_break_length(0, 0, width, 0xffff, 0, map_poi[i].desc) + 8;
			} else {
				height = 18;
			}

			if (hx + width >= dotx(DOT_BR) - 4) {
				hx = dotx(DOT_BR) - width - 4;
				hy += 8;
			}

			render_shaded_rect(hx, hy, hx + width + 8, hy + height, 0x0000, 150);
			render_text_break(hx + 4, hy + 4, hx + width + 4, 0xffff, 0, map_poi[i].desc);
			break;
		}
	}
}
