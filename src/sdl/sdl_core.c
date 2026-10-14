/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * SDL - Core Module
 *
 * Initialization, lifecycle management, window management, cursor handling,
 * event loop, and background prefetching system.
 */

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <zip.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_timer.h>
#include <SDL3_mixer/SDL_mixer.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_render.h>

#include "dll.h"
#include "astonia.h"
#include "sdl/sdl.h"
#include "sdl/sdl_private.h"
#include "gui/gui.h"

// SDL window and renderer
SDL_Window *sdlwnd = NULL;
SDL_Renderer *sdlren = NULL;

// Cursors
static SDL_Cursor *curs[20];

// Zip archives for graphics
zip_t *sdl_zip1 = NULL;
zip_t *sdl_zip2 = NULL;
zip_t *sdl_zip1p = NULL;
zip_t *sdl_zip2p = NULL;
zip_t *sdl_zip1m = NULL;
zip_t *sdl_zip2m = NULL;

// Prefetch threading (shared with sdl_texture.c)
SDL_Semaphore *prework = NULL;
SDL_Mutex *premutex = NULL;

// SDL3_mixer globals
MIX_Mixer *sdl_mixer = NULL;
MIX_Track *sdl_tracks[MAX_SOUND_CHANNELS] = {NULL};

// Scale and resolution settings
DLL_EXPORT int sdl_scale = 1;
DLL_EXPORT int sdl_frames = 0;
DLL_EXPORT int sdl_multi = 4;
DLL_EXPORT int sdl_cache_size = 8000;
DLL_EXPORT int __xres = XRES0;
DLL_EXPORT int __yres = YRES0;
DLL_EXPORT int gui_dx = 0;

// Zoom chosen in the Options window: 0 = automatic (largest graphics that fit), 1-4 = at
// most this graphics scale. Smaller graphics show more of the world.
int sdl_zoom = 0;

// Interface size chosen in the Options window, in percent. The top bar and the bottom window
// are drawn this much larger or smaller than the world. sdl_ui_scale is the factor in use; it
// can be lower than asked when the server's view range leaves no room for a larger interface.
int sdl_ui_size = 100;
float sdl_ui_scale = 1.0f;

// The world is drawn in map pixels (GUI pixels * sdl_ui_scale), the interface in GUI pixels
// (XRES x YRES). These are the size of the world picture and the centering offsets of both.
int sdl_map_xres = XRES0, sdl_map_yres = YRES0;
static int map_offx = 0, map_offy = 0, gui_offx = 0, gui_offy = 0;

// World zoom chosen in the Options window, in percent. Only the world is drawn larger; the
// interface keeps its size. sdl_world_scale is the factor; the world then has
// sdl_map_xres / sdl_world_scale x sdl_map_yres / sdl_world_scale world pixels.
int sdl_world_zoom = 100;
float sdl_world_scale = 1.0f;

// Display mode (DISPMODE_*). -1 means "decide from -w/-h/-o" as older clients did.
int sdl_display_mode = -1;

static void sdl_update_presentation(void);

// Worker thread management

struct zip_handles;
struct zip_handles *worker_zips = NULL;
SDL_AtomicInt worker_quit;
SDL_Thread **worker_threads = NULL;

// Image loading state machine (shared with sdl_image.c)
static int sdli_state_storage[MAXSPRITE];
int *sdli_state = sdli_state_storage;

void sdl_dump(FILE *fp)
{
	fprintf(fp, "SDL datadump:\n");

	fprintf(fp, "XRES: %d\n", XRES);
	fprintf(fp, "YRES: %d\n", YRES);

	fprintf(fp, "sdl_scale: %d\n", sdl_scale);
	fprintf(fp, "sdl_frames: %d\n", sdl_frames);
	fprintf(fp, "sdl_multi: %d\n", sdl_multi);
	fprintf(fp, "sdl_cache_size: %d (max=%d)\n", sdl_cache_size, MAX_TEXCACHE);

	fprintf(fp, "mem_png: %lld\n", (long long)__atomic_load_n(&mem_png, __ATOMIC_RELAXED));
	fprintf(fp, "mem_tex: %lld\n", (long long)__atomic_load_n(&mem_tex, __ATOMIC_RELAXED));
	fprintf(fp, "texc_hit: %lld\n", texc_hit);
	fprintf(fp, "texc_miss: %lld\n", texc_miss);
	fprintf(fp, "texc_pre: %lld\n", texc_pre);

	fprintf(fp, "\n");
}

#define GO_DEFAULTS (GO_CONTEXT | GO_ACTION | GO_BIGBAR | GO_PREDICT | GO_SHORT | GO_MAPSAVE | GO_SMOOTHCAM)

// #define GO_DEFAULTS (GO_CONTEXT|GO_ACTION|GO_BIGBAR|GO_PREDICT|GO_SHORT|GO_MAPSAVE|GO_NOMAP)

int sdl_init(int width, int height, char *title, int monitor)
{
	int i;
	int num_displays;
	SDL_DisplayID *displays;
	SDL_DisplayID display_id;

	if (!SDL_Init(SDL_INIT_VIDEO | ((game_options & GO_SOUND) ? SDL_INIT_AUDIO : 0))) {
		fail("SDL_Init Error: %s", SDL_GetError());
		return 0;
	}

	SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");

	// Get all available displays
	displays = SDL_GetDisplays(&num_displays);
	if (!displays || num_displays == 0) {
		fail("SDL_GetDisplays Error: %s", SDL_GetError());
		SDL_Quit();
		return 0;
	}

	// Validate monitor number and select display
	if (monitor < 0 || monitor >= num_displays) {
		note("Invalid monitor %d, using default (0). Available monitors: %d", monitor, num_displays);
		monitor = 0;
	} else if (monitor > 0) {
		note("Using monitor %d of %d available monitors", monitor, num_displays);
	}
	display_id = displays[monitor];
	SDL_free(displays);

	const SDL_DisplayMode *DM = SDL_GetCurrentDisplayMode(display_id);

	if (!DM) {
		fail("SDL_GetCurrentDisplayMode Error: %s", SDL_GetError());
		SDL_Quit();
		return 0;
	}

	// Unless the player forced a rate with -k, default to the monitor refresh
	// rate so smooth camera has sub-tick frames to interpolate (vsync caps us
	// there anyway). Fall back to 60 if the refresh rate is unknown or not above
	// the tick rate.
	if (frames_per_second_auto) {
		int hz = (int)(DM->refresh_rate + 0.5f);
		frames_per_second = (hz > TICKS) ? hz : 60;
	}

	if (!width || !height) {
		width = DM->w;
		height = DM->h;
	}

	if (sdl_display_mode < DISPMODE_WINDOWED || sdl_display_mode > DISPMODE_FULLSCREEN) {
		if (game_options & GO_FULL) {
			sdl_display_mode = DISPMODE_FULLSCREEN;
		} else if (width >= DM->w && height >= DM->h) {
			sdl_display_mode = DISPMODE_BORDERLESS;
		} else {
			sdl_display_mode = DISPMODE_WINDOWED;
		}
	}

	SDL_DisplayMode fullscreen_mode;
	int have_fullscreen_mode = 0;

	if (sdl_display_mode == DISPMODE_BORDERLESS) {
		width = DM->w;
		height = DM->h;
	} else if (sdl_display_mode == DISPMODE_FULLSCREEN) {
		if (SDL_GetClosestFullscreenDisplayMode(display_id, width, height, 0.0f, false, &fullscreen_mode)) {
			width = fullscreen_mode.w;
			height = fullscreen_mode.h;
			have_fullscreen_mode = 1;
		} else {
			width = DM->w;
			height = DM->h;
		}
	}

	// Create window and position on selected monitor
	sdlwnd = SDL_CreateWindow(title, width, height, SDL_WINDOW_RESIZABLE);
	if (sdlwnd) {
		SDL_SetWindowMinimumSize(sdlwnd, 640, 360);
	}
	if (sdlwnd && monitor > 0) {
		// Position window on the selected monitor
		SDL_Rect display_bounds;
		if (SDL_GetDisplayBounds(display_id, &display_bounds)) {
			int x_pos = display_bounds.x + (DM->w - width) / 2;
			int y_pos = display_bounds.y + (DM->h - height) / 2;
			SDL_SetWindowPosition(sdlwnd, x_pos, y_pos);
		}
	}
	if (!sdlwnd) {
		fail("SDL_Init Error: %s", SDL_GetError());
		SDL_Quit();
		return 0;
	}

	// Use display mode's logical size for scaling calculations
	// SDL3 automatically handles pixel_density scaling to physical pixels
	// Fall back to supplied width and height if less than logical, as this is required for windowed.
	if (sdl_display_mode != DISPMODE_FULLSCREEN && (width > DM->w || height > DM->h)) {
		width = DM->w;
		height = DM->h;
	}

	if (sdl_display_mode == DISPMODE_FULLSCREEN) {
		// Exclusive fullscreen mode
		SDL_SetWindowFullscreenMode(sdlwnd, have_fullscreen_mode ? &fullscreen_mode : NULL);
		SDL_SetWindowFullscreen(sdlwnd, true);
		// Wait for fullscreen transition to complete (prevents resize events on macOS)
		SDL_SyncWindow(sdlwnd);
	} else if (sdl_display_mode == DISPMODE_BORDERLESS) {
		// Borderless fullscreen desktop
		SDL_SetWindowFullscreenMode(sdlwnd, NULL);
		SDL_SetWindowFullscreen(sdlwnd, true);
		// Wait for fullscreen transition to complete (prevents resize events on macOS)
		SDL_SyncWindow(sdlwnd);
	}

	// Prefer native GPU APIs for best performance and compatibility:
	// - Windows: Direct3D avoids SDL3 OpenGL gamma issues
	// - Linux: Vulkan for modern driver support
	// - macOS: Metal required (OpenGL deprecated since 10.14)
#ifdef _WIN32
	const char *renderers_to_try[] = {"direct3d11", "direct3d12", NULL};
#elif __linux__
	const char *renderers_to_try[] = {"vulkan", NULL};
#else
	const char *renderers_to_try[] = {"metal", NULL};
#endif

	for (int i = 0; renderers_to_try[i] != NULL; i++) {
		SDL_PropertiesID renderer_props_create = SDL_CreateProperties();
		if (renderer_props_create != 0) {
			SDL_SetPointerProperty(renderer_props_create, SDL_PROP_RENDERER_CREATE_WINDOW_POINTER, sdlwnd);
			SDL_SetStringProperty(renderer_props_create, SDL_PROP_RENDERER_CREATE_NAME_STRING, renderers_to_try[i]);
			SDL_SetNumberProperty(
			    renderer_props_create, SDL_PROP_RENDERER_CREATE_OUTPUT_COLORSPACE_NUMBER, (Sint64)SDL_COLORSPACE_SRGB);
			sdlren = SDL_CreateRendererWithProperties(renderer_props_create);
			SDL_DestroyProperties(renderer_props_create);
			if (sdlren) {
				break; // Success, use this renderer
			}
		}
	}
	if (!sdlren) {
		// Fallback to default if all options fail.
		sdlren = SDL_CreateRenderer(sdlwnd, NULL);
	}

	if (!sdlren) {
		// Even the fallback didn't work, so we need to exit.
		SDL_DestroyWindow(sdlwnd);
		fail("SDL_Init Error: %s", SDL_GetError());
		SDL_Quit();
		return 0;
	}

	SDL_SetRenderVSync(sdlren, 1);

	// Initialize hash table (statically allocated)
	for (i = 0; i < MAX_TEXHASH; i++) {
		sdlt_cache[i] = STX_NONE;
	}

	// Initialize texture cache (statically allocated)
	for (i = 0; i < MAX_TEXCACHE; i++) {
		// Initialize flags atomically
		uint16_t *flags_ptr = (uint16_t *)&sdlt[i].flags;
		__atomic_store_n(flags_ptr, 0, __ATOMIC_RELAXED);
		sdlt[i].prev = i - 1;
		sdlt[i].next = i + 1;
		sdlt[i].hnext = STX_NONE;
		sdlt[i].hprev = STX_NONE;
		// Initialize new fields:
		// Generation starts at 1 (0 is reserved for "never valid for jobs")
		sdlt[i].generation = 1;
		sdlt[i].work_state = TX_WORK_IDLE;
	}
	sdlt[0].prev = STX_NONE;
	sdlt[MAX_TEXCACHE - 1].next = STX_NONE;
	sdlt_best = 0;
	sdlt_last = MAX_TEXCACHE - 1;

	// Initialize the new texture job queue
	tex_jobs_init();

	SDL_RaiseWindow(sdlwnd);

	// We want SDL to translate scan codes to ASCII / Unicode
	// but we don't really want the SDL line editing stuff.
	// I hope just keeping it enabled all the time doesn't break
	// anything.
	SDL_StartTextInput(sdlwnd);

#ifdef __APPLE__
	// On macOS, use actual renderer output size for scaling calculations.
	// SDL_GetCurrentDisplayMode() includes reserved space (menu bar, dock, notch),
	// but the Metal renderer only renders to the usable area. Using display mode
	// dimensions causes clipping at the bottom of the window.
	// On Linux/Windows, SDL_GetRenderOutputSize() can return HiDPI-scaled dimensions
	// (e.g., 2x physical pixels), which breaks scaling calculations.
	int render_output_w = 0, render_output_h = 0;
	if (SDL_GetRenderOutputSize(sdlren, &render_output_w, &render_output_h)) {
		// Fallback to window pixel size if renderer output size fails
		SDL_GetWindowSizeInPixels(sdlwnd, &render_output_w, &render_output_h);
	}
	if (render_output_w > 0 && render_output_h > 0) {
		width = render_output_w;
		height = render_output_h;
	}
#endif

	// decide on screen format: the largest graphics scale whose 800x450 minimum fits, then a
	// game area as large as the screen at that scale (up to XRESMAX x YRESMAX). The GUI keeps
	// its size and the map shows more of the world.
	sdl_scale = sdl_scale_for_size(width, height);
	if (sdl_zoom >= 1 && sdl_zoom < sdl_scale) {
		sdl_scale = sdl_zoom;
	}
	sdl_set_game_size(width / sdl_scale, height / sdl_scale);

	if (game_options & GO_NOTSET) {
		if (YRES >= 620) {
			game_options = GO_DEFAULTS;
		} else if (YRES >= 580) {
			game_options = GO_DEFAULTS | GO_SMALLBOT;
		} else {
			game_options = GO_DEFAULTS | GO_SMALLBOT | GO_SMALLTOP;
		}
	}
	note("SDL using %dx%d scale %d, options=%" PRIu64, XRES, YRES, sdl_scale, game_options);

	// The game's sdl_scale and render_set_offset() handle scaling and centering for the
	// size we start with. If the window size changes later, SDL's logical presentation
	// scales the picture to fit (see sdl_update_presentation()).
	sdl_update_presentation();

	sdl_create_cursors();

	sdl_zip1 = zip_open("res/gx1.zip", ZIP_RDONLY, NULL);
	sdl_zip1p = zip_open("res/gx1_patch.zip", ZIP_RDONLY, NULL);
	sdl_zip1m = zip_open("res/gx1_mod.zip", ZIP_RDONLY, NULL);

	switch (sdl_scale) {
	case 2:
		sdl_zip2 = zip_open("res/gx2.zip", ZIP_RDONLY, NULL);
		sdl_zip2p = zip_open("res/gx2_patch.zip", ZIP_RDONLY, NULL);
		sdl_zip2m = zip_open("res/gx2_mod.zip", ZIP_RDONLY, NULL);
		break;
	case 3:
		sdl_zip2 = zip_open("res/gx3.zip", ZIP_RDONLY, NULL);
		sdl_zip2p = zip_open("res/gx3_patch.zip", ZIP_RDONLY, NULL);
		sdl_zip2m = zip_open("res/gx3_mod.zip", ZIP_RDONLY, NULL);
		break;
	case 4:
		sdl_zip2 = zip_open("res/gx4.zip", ZIP_RDONLY, NULL);
		sdl_zip2p = zip_open("res/gx4_patch.zip", ZIP_RDONLY, NULL);
		sdl_zip2m = zip_open("res/gx4_mod.zip", ZIP_RDONLY, NULL);
		break;
	default:
		break;
	}

	if (game_options & GO_SOUND) {
		if (!MIX_Init()) {
			warn("MIX_Init failed: %s", SDL_GetError());
			game_options &= ~GO_SOUND;
		} else {
			// Create mixer device (NULL spec means use reasonable defaults)
			sdl_mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, NULL);
			if (!sdl_mixer) {
				warn("MIX_CreateMixerDevice failed: %s", SDL_GetError());
				game_options &= ~GO_SOUND;
				MIX_Quit();
			} else {
				// Create tracks (SDL3_mixer doesn't auto-allocate channels like SDL2_mixer)
				for (int i = 0; i < MAX_SOUND_CHANNELS; i++) {
					sdl_tracks[i] = MIX_CreateTrack(sdl_mixer);
					if (!sdl_tracks[i]) {
						warn("MIX_CreateTrack failed for track %d: %s", i, SDL_GetError());
					}
				}
				note("Created %d sound tracks", MAX_SOUND_CHANNELS);
			}
		}
	}

	// Initialize mutex unconditionally (needed for job queue even in single-threaded mode)
	premutex = SDL_CreateMutex();
	if (!premutex) {
		fail("Failed to create premutex");
		return 0;
	}

	// Initialize semaphore for waking background workers (starts at 0)
	prework = SDL_CreateSemaphore(0);
	if (!prework) {
		fail("Failed to create prework semaphore");
		return 0;
	}

	SDL_SetAtomicInt(&worker_quit, 0);

	if (sdl_multi) {
		char buf[80];
		int n;

		// Allocate worker zip handles
		worker_zips = xmalloc((size_t)sdl_multi * sizeof(struct zip_handles), MEM_SDL_BASE);
		if (!worker_zips) {
			fail("Out of memory for worker zip handles");
			sdl_multi = 0;
		} else {
			// Open zip handles for each worker
			for (n = 0; n < sdl_multi; n++) {
				worker_zips[n].zip1 = zip_open("res/gx1.zip", ZIP_RDONLY, NULL);
				worker_zips[n].zip1p = zip_open("res/gx1_patch.zip", ZIP_RDONLY, NULL);
				worker_zips[n].zip1m = zip_open("res/gx1_mod.zip", ZIP_RDONLY, NULL);

				switch (sdl_scale) {
				case 2:
					worker_zips[n].zip2 = zip_open("res/gx2.zip", ZIP_RDONLY, NULL);
					worker_zips[n].zip2p = zip_open("res/gx2_patch.zip", ZIP_RDONLY, NULL);
					worker_zips[n].zip2m = zip_open("res/gx2_mod.zip", ZIP_RDONLY, NULL);
					break;
				case 3:
					worker_zips[n].zip2 = zip_open("res/gx3.zip", ZIP_RDONLY, NULL);
					worker_zips[n].zip2p = zip_open("res/gx3_patch.zip", ZIP_RDONLY, NULL);
					worker_zips[n].zip2m = zip_open("res/gx3_mod.zip", ZIP_RDONLY, NULL);
					break;
				case 4:
					worker_zips[n].zip2 = zip_open("res/gx4.zip", ZIP_RDONLY, NULL);
					worker_zips[n].zip2p = zip_open("res/gx4_patch.zip", ZIP_RDONLY, NULL);
					worker_zips[n].zip2m = zip_open("res/gx4_mod.zip", ZIP_RDONLY, NULL);
					break;
				default:
					break;
				}

				if (!worker_zips[n].zip1) {
					warn("Worker %d: Failed to open res/gx1.zip - aborting initialization", n);
					// Clean up already opened zips
					for (int i = 0; i < n; i++) {
						if (worker_zips[i].zip1) {
							zip_close(worker_zips[i].zip1);
						}
						if (worker_zips[i].zip1p) {
							zip_close(worker_zips[i].zip1p);
						}
						if (worker_zips[i].zip1m) {
							zip_close(worker_zips[i].zip1m);
						}
						if (worker_zips[i].zip2) {
							zip_close(worker_zips[i].zip2);
						}
						if (worker_zips[i].zip2p) {
							zip_close(worker_zips[i].zip2p);
						}
						if (worker_zips[i].zip2m) {
							zip_close(worker_zips[i].zip2m);
						}
					}
					xfree(worker_zips);
					worker_zips = NULL;
					sdl_multi = 0;
					break;
				}
			}

			// Only create threads if all zip handles succeeded
			if (sdl_multi > 0) {
				worker_threads = xmalloc((size_t)sdl_multi * sizeof(SDL_Thread *), MEM_SDL_BASE);
				if (!worker_threads) {
					fail("Out of memory for thread handles");
					// Clean up zip handles
					for (n = 0; n < sdl_multi; n++) {
						if (worker_zips[n].zip1) {
							zip_close(worker_zips[n].zip1);
						}
						if (worker_zips[n].zip1p) {
							zip_close(worker_zips[n].zip1p);
						}
						if (worker_zips[n].zip1m) {
							zip_close(worker_zips[n].zip1m);
						}
						if (worker_zips[n].zip2) {
							zip_close(worker_zips[n].zip2);
						}
						if (worker_zips[n].zip2p) {
							zip_close(worker_zips[n].zip2p);
						}
						if (worker_zips[n].zip2m) {
							zip_close(worker_zips[n].zip2m);
						}
					}
					xfree(worker_zips);
					worker_zips = NULL;
					sdl_multi = 0;
				} else {
					// Create all threads
					for (n = 0; n < sdl_multi; n++) {
						sprintf(buf, "moac background worker %d", n);
						worker_threads[n] = SDL_CreateThread(sdl_pre_backgnd, buf, (void *)(long long)n);
						if (!worker_threads[n]) {
							warn("Failed to create worker thread %d", n);
							// Signal quit and join already created threads
							SDL_SetAtomicInt(&worker_quit, 1);
							for (int i = 0; i < n; i++) {
								if (worker_threads[i]) {
									SDL_WaitThread(worker_threads[i], NULL);
								}
							}
							// Clean up
							xfree(worker_threads);
							worker_threads = NULL;
							for (int i = 0; i < sdl_multi; i++) {
								if (worker_zips[i].zip1) {
									zip_close(worker_zips[i].zip1);
								}
								if (worker_zips[i].zip1p) {
									zip_close(worker_zips[i].zip1p);
								}
								if (worker_zips[i].zip1m) {
									zip_close(worker_zips[i].zip1m);
								}
								if (worker_zips[i].zip2) {
									zip_close(worker_zips[i].zip2);
								}
								if (worker_zips[i].zip2p) {
									zip_close(worker_zips[i].zip2p);
								}
								if (worker_zips[i].zip2m) {
									zip_close(worker_zips[i].zip2m);
								}
							}
							xfree(worker_zips);
							worker_zips = NULL;
							sdl_multi = 0;
							break;
						}
					}
				}
			}
		}
	}

	return 1;
}

int sdl_clear(void)
{
	// SDL_SetRenderDrawColor(sdlren,255,63,63,255);     // clear with bright red to spot broken sprites
	SDL_SetRenderDrawColor(sdlren, 0, 0, 0, 255);
	SDL_RenderClear(sdlren);
	// note("mem: %.2fM PNG, %.2fM Tex, Hit: %ld, Miss: %ld, Max:
	// %d\n",mem_png/(1024.0*1024.0),mem_tex/(1024.0*1024.0),texc_hit,texc_miss,maxpanic);
	maxpanic = 0;

	// Reset blend mode to default at start of each frame to prevent mods from
	// accidentally leaving non-default blend modes that affect subsequent rendering
	sdl_reset_blend_mode();

	return 1;
}

int sdl_render(void)
{
	SDL_RenderPresent(sdlren);
	sdl_frames++;
	return 1;
}

void sdl_exit(void)
{
	// Signal workers to quit and join them
	if (sdl_multi && worker_threads) {
		SDL_SetAtomicInt(&worker_quit, 1);

		// Wake all workers so they can see the quit signal
		// (they're blocked on SDL_WaitSemaphore)
		if (prework) {
			for (int n = 0; n < sdl_multi; n++) {
				SDL_SignalSemaphore(prework);
			}
		}

		for (int n = 0; n < sdl_multi; n++) {
			if (worker_threads[n]) {
				SDL_WaitThread(worker_threads[n], NULL);
			}
		}
		xfree(worker_threads);
		worker_threads = NULL;
	}

	// Close worker zip handles
	if (worker_zips) {
		for (int n = 0; n < sdl_multi; n++) {
			if (worker_zips[n].zip1) {
				zip_close(worker_zips[n].zip1);
			}
			if (worker_zips[n].zip1p) {
				zip_close(worker_zips[n].zip1p);
			}
			if (worker_zips[n].zip1m) {
				zip_close(worker_zips[n].zip1m);
			}
			if (worker_zips[n].zip2) {
				zip_close(worker_zips[n].zip2);
			}
			if (worker_zips[n].zip2p) {
				zip_close(worker_zips[n].zip2p);
			}
			if (worker_zips[n].zip2m) {
				zip_close(worker_zips[n].zip2m);
			}
		}
		xfree(worker_zips);
		worker_zips = NULL;
	}

	if (sdl_zip1) {
		zip_close(sdl_zip1);
	}
	if (sdl_zip1m) {
		zip_close(sdl_zip1m);
	}
	if (sdl_zip1p) {
		zip_close(sdl_zip1p);
	}
	if (sdl_zip2) {
		zip_close(sdl_zip2);
	}
	if (sdl_zip2m) {
		zip_close(sdl_zip2m);
	}
	if (sdl_zip2p) {
		zip_close(sdl_zip2p);
	}

	if (prework) {
		SDL_DestroySemaphore(prework);
		prework = NULL;
	}

	if (premutex) {
		SDL_DestroyMutex(premutex);
		premutex = NULL;
	}

	// Shutdown the new texture job queue
	tex_jobs_shutdown();

	if (game_options & GO_SOUND) {
		MIX_Quit();
	}

	// Clean up mod textures (gated behind DEVELOPER for address sanitizer)
	sdl_cleanup_mod_textures();

#ifdef DEVELOPER
	sdl_dump_spritecache();
#endif
}

void cmd_proc(int key);
void context_keyup(SDL_Keycode key);

void sdl_loop(void)
{
	SDL_Event event;

	while (SDL_PollEvent(&event)) {
		switch (event.type) {
		case SDL_EVENT_MOUSE_MOTION:
		case SDL_EVENT_MOUSE_BUTTON_DOWN:
		case SDL_EVENT_MOUSE_BUTTON_UP:
			// Window coordinates to the game's pixel coordinates (they differ once the window was resized)
			SDL_ConvertEventToRenderCoordinates(sdlren, &event);
			break;
		default:
			break;
		}

		switch (event.type) {
		case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
			sdl_update_presentation();
			options_window_changed();
			break;
		case SDL_EVENT_QUIT:
			quit = 1;
			break;
		case SDL_EVENT_KEY_DOWN:
			gui_sdl_keyproc(event.key.key);
			break;
		case SDL_EVENT_KEY_UP:
			context_keyup(event.key.key);
			break;
		case SDL_EVENT_TEXT_INPUT:
			cmd_proc(event.text.text[0]);
			break;
		case SDL_EVENT_MOUSE_MOTION:
			gui_sdl_mouseproc(event.motion.x, event.motion.y, SDL_MOUM_NONE);
			break;
		case SDL_EVENT_MOUSE_BUTTON_DOWN:
			if (event.button.button == SDL_BUTTON_LEFT) {
				gui_sdl_mouseproc(event.button.x, event.button.y, SDL_MOUM_LDOWN);
			}
			if (event.button.button == SDL_BUTTON_MIDDLE) {
				gui_sdl_mouseproc(event.button.x, event.button.y, SDL_MOUM_MDOWN);
			}
			if (event.button.button == SDL_BUTTON_RIGHT) {
				gui_sdl_mouseproc(event.button.x, event.button.y, SDL_MOUM_RDOWN);
			}
			break;
		case SDL_EVENT_MOUSE_BUTTON_UP:
			if (event.button.button == SDL_BUTTON_LEFT) {
				gui_sdl_mouseproc(event.button.x, event.button.y, SDL_MOUM_LUP);
			}
			if (event.button.button == SDL_BUTTON_MIDDLE) {
				gui_sdl_mouseproc(event.button.x, event.button.y, SDL_MOUM_MUP);
			}
			if (event.button.button == SDL_BUTTON_RIGHT) {
				gui_sdl_mouseproc(event.button.x, event.button.y, SDL_MOUM_RUP);
			}
			break;
		case SDL_EVENT_MOUSE_WHEEL:
			gui_sdl_mouseproc(event.wheel.x, event.wheel.y, SDL_MOUM_WHEEL);
			break;
		case SDL_EVENT_WINDOW_FOCUS_GAINED:
#ifdef ENABLE_DRAGHACK
			float x, y;
			Uint32 mouseState = SDL_GetMouseState(&x, &y);
			if (mouseState & SDL_BUTTON_MASK(SDL_BUTTON_LEFT)) {
				gui_sdl_draghack();
			}
#endif
			break;
		default:
			break;
		}
	}
}

void sdl_set_cursor_pos(int x, int y)
{
	float wx = (float)x, wy = (float)y;

	if (sdlren && !SDL_RenderCoordinatesToWindow(sdlren, (float)x, (float)y, &wx, &wy)) {
		wx = (float)x;
		wy = (float)y;
	}
	SDL_WarpMouseInWindow(sdlwnd, wx, wy);
}

// Largest graphics scale (1-4) that fits the given size in pixels. This is the scale
// sdl_init() picks for that size.
int sdl_scale_for_size(int width, int height)
{
	int scale;

	for (scale = 4; scale > 1; scale--) {
		if (width / XRES0 >= scale && height / YRESMIN >= scale) {
			break;
		}
	}
	return scale;
}

// Scale the client would pick for the current window size after a restart
int sdl_best_scale(void)
{
	int w = 0, h = 0, scale;

	if (!sdlren || !SDL_GetRenderOutputSize(sdlren, &w, &h) || w <= 0 || h <= 0) {
		return sdl_scale;
	}
	scale = sdl_scale_for_size(w, h);
	if (sdl_zoom >= 1 && sdl_zoom < scale) {
		scale = sdl_zoom;
	}
	return scale;
}

// The game area limits changed (view range from the server): fit it to the window again
void sdl_refresh_layout(void)
{
	sdl_update_presentation();
}

// Interface factor that fits: the GUI's minimum size (800x450) times the factor must fit in
// the window (nx x ny map pixels) and must not be larger than the largest world picture.
// Asking for more gives the largest size that fits, so the world is never scaled down to make
// room for the interface.
static float sdl_fit_ui_scale(int nx, int ny)
{
	float u = (float)sdl_ui_size / 100.0f, m;

	if (nx > 0 && ny > 0) {
		m = (float)nx / XRES0;
		if (u > m) {
			u = m;
		}
		m = (float)ny / YRESMIN;
		if (u > m) {
			u = m;
		}
	}
	if (u < 0.25f) {
		u = 0.25f;
	}
	m = (float)XRESMAX / XRES0;
	if (u > m) {
		u = m;
	}
	m = (float)YRESMAX / YRESMIN;
	if (u > m) {
		u = m;
	}
	return u;
}

// exact change test for scale values (a != b without -Wfloat-equal)
static int float_differs(float a, float b)
{
	return a < b || a > b;
}

// World scale for the chosen world zoom. Zooming out is limited so the world picture never
// gets bigger than what the server sends (XRESMAX x YRESMAX), past that it would only be black.
static float sdl_world_scale_for(int percent)
{
	float s = (float)percent / 100.0f, m;

	m = (float)sdl_map_xres / (float)XRESMAX;
	if (s < m) {
		s = m;
	}
	m = (float)sdl_map_yres / (float)YRESMAX;
	if (s < m) {
		s = m;
	}
	return s;
}

// Smallest world zoom that is possible right now, in percent
int sdl_world_min_zoom(void)
{
	int m = (int)(sdl_world_scale_for(1) * 100.0f + 0.999f);

	return m > WORLD_ZOOM_MIN ? m : WORLD_ZOOM_MIN;
}

// Set the size of the world picture in map pixels, clamped to what the GUI needs and what the
// server sends, and derive the GUI's size (XRES x YRES) from it using interface factor u.
// Returns 1 if anything changed.
static int sdl_set_game_size_u(int w, int h, float u)
{
	int ox = XRES, oy = YRES, omx = sdl_map_xres, omy = sdl_map_yres;
	float ou = sdl_ui_scale, os = sdl_world_scale;
	int minx, miny;

	sdl_ui_scale = u;
	minx = (int)(XRES0 * u + 0.01f);
	miny = (int)(YRESMIN * u + 0.01f);

	sdl_map_xres = w < minx ? minx : (w > XRESMAX ? XRESMAX : w);
	sdl_map_yres = h < miny ? miny : (h > YRESMAX ? YRESMAX : h);

	XRES = (int)((float)sdl_map_xres / u + 0.001f);
	YRES = (int)((float)sdl_map_yres / u + 0.001f);
	if (XRES < XRES0) {
		XRES = XRES0;
	}
	if (YRES < YRESMIN) {
		YRES = YRESMIN;
	}
	gui_dx = (XRES - XRES0) / 2;
	sdl_world_scale = sdl_world_scale_for(sdl_world_zoom);

	return XRES != ox || YRES != oy || sdl_map_xres != omx || sdl_map_yres != omy || float_differs(u, ou) ||
	       float_differs(sdl_world_scale, os);
}

// Same for a window of w x h map pixels
int sdl_set_game_size(int w, int h)
{
	return sdl_set_game_size_u(w, h, sdl_fit_ui_scale(w, h));
}

// With a world zoom the world is drawn into this texture at its own size and then copied
// enlarged into the window, so the floor tiles stay seamless.
static SDL_Texture *world_tex = NULL;
static int world_tex_w = 0, world_tex_h = 0, world_tex_active = 0, world_tex_failed = 0;

// Copy the zoomed world into the window
static void sdl_world_flush(void)
{
	SDL_FRect dr;

	if (!world_tex_active) {
		return;
	}
	world_tex_active = 0;

	SDL_SetRenderTarget(sdlren, NULL);
	SDL_SetRenderScale(sdlren, 1.0f, 1.0f);
	dr.x = (float)(map_offx * sdl_scale);
	dr.y = (float)(map_offy * sdl_scale);
	dr.w = (float)world_tex_w * sdl_world_scale;
	dr.h = (float)world_tex_h * sdl_world_scale;
	SDL_RenderTexture(sdlren, world_tex, NULL, &dr);
}

// Draw the world: world pixels, enlarged by the world zoom
void sdl_map_pass(void)
{
	int tw, th;

	if (sdl_world_scale != 1.0f) {
		tw = sdl_world_xres() * sdl_scale;
		th = sdl_world_yres() * sdl_scale;
		if (world_tex && (world_tex_w != tw || world_tex_h != th)) {
			SDL_DestroyTexture(world_tex);
			world_tex = NULL;
		}
		if (!world_tex && !world_tex_failed) {
			world_tex = SDL_CreateTexture(sdlren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, tw, th);
			if (world_tex) {
				SDL_SetTextureBlendMode(world_tex, SDL_BLENDMODE_NONE);
				SDL_SetTextureScaleMode(world_tex, SDL_SCALEMODE_LINEAR);
				world_tex_w = tw;
				world_tex_h = th;
			} else {
				warn("world zoom: could not create texture: %s", SDL_GetError());
				world_tex_failed = 1;
			}
		}
		if (world_tex && SDL_SetRenderTarget(sdlren, world_tex)) {
			world_tex_active = 1;
			SDL_SetRenderScale(sdlren, 1.0f, 1.0f);
			SDL_SetRenderDrawColor(sdlren, 0, 0, 0, 255);
			SDL_RenderClear(sdlren);
			render_set_offset(0, 0);
			return;
		}
		// no texture: let SDL enlarge each sprite instead
		SDL_SetRenderScale(sdlren, sdl_world_scale, sdl_world_scale);
		render_set_offset(
		    (int)((float)map_offx / sdl_world_scale + 0.5f), (int)((float)map_offy / sdl_world_scale + 0.5f));
		return;
	}

	SDL_SetRenderScale(sdlren, 1.0f, 1.0f);
	render_set_offset(map_offx, map_offy);
}

// Size of the world picture in world pixels
int sdl_world_xres(void)
{
	return (int)((float)sdl_map_xres / sdl_world_scale + 0.999f);
}

int sdl_world_yres(void)
{
	return (int)((float)sdl_map_yres / sdl_world_scale + 0.999f);
}

// The Options window changed the world zoom. The map is re-centered for the new size.
void sdl_set_world_zoom(int percent)
{
	float s;

	if (percent < WORLD_ZOOM_MIN) {
		percent = WORLD_ZOOM_MIN;
	}
	if (percent > WORLD_ZOOM_MAX) {
		percent = WORLD_ZOOM_MAX;
	}
	sdl_world_zoom = percent;
	s = sdl_world_scale_for(percent);
	if (float_differs(sdl_world_scale, s)) {
		sdl_world_scale = s;
		if (sdlren) {
			gui_layout_changed();
		}
	}
}

// Draw the interface: GUI pixels, scaled by the interface size. This is also the state between
// frames, so mouse positions are worked out with the GUI offset.
void sdl_gui_pass(void)
{
	sdl_world_flush();
	SDL_SetRenderScale(sdlren, sdl_ui_scale, sdl_ui_scale);
	render_set_offset(gui_offx, gui_offy);
}

// Draw one interface panel: GUI pixels scaled by the interface size and the panel's scale s,
// with the panel's top left corner (ox, oy) staying where it is
void sdl_panel_pass(float s, int ox, int oy)
{
	sdl_world_flush();
	SDL_SetRenderScale(sdlren, sdl_ui_scale * s, sdl_ui_scale * s);
	render_set_offset((int)SDL_floorf((float)(ox + gui_offx) / s - (float)ox + 0.5f),
	    (int)SDL_floorf((float)(oy + gui_offy) / s - (float)oy + 0.5f));
}

// End of frame: SDL converts mouse events with the render scale, so leave it at 1
void sdl_end_passes(void)
{
	sdl_world_flush();
	SDL_SetRenderScale(sdlren, 1.0f, 1.0f);
	render_set_offset(gui_offx, gui_offy);
}

// The Options window changed the interface size
void sdl_set_ui_size(int percent)
{
	sdl_ui_size = percent;
	sdl_update_presentation();
}

// Map the game's picture onto the window. The game area grows with the window so it is
// drawn pixel for pixel without borders. Only when the window is smaller than the GUI
// needs, or larger than the server's view range, is the picture scaled to fit.
static void sdl_update_presentation(void)
{
	int w = 0, h = 0, gw, gh, nx, ny, changed, minx, miny;
	float u;
	SDL_Texture *target;

	if (!sdlren || !SDL_GetRenderOutputSize(sdlren, &w, &h) || w <= 0 || h <= 0) {
		return;
	}

	nx = w / sdl_scale;
	ny = h / sdl_scale;

	u = sdl_fit_ui_scale(nx, ny);
	minx = (int)(XRES0 * u + 0.01f);
	miny = (int)(YRESMIN * u + 0.01f);

	if (nx >= minx && ny >= miny && nx <= XRESMAX && ny <= YRESMAX) {
		changed = sdl_set_game_size_u(nx, ny, u);
	} else {
		// keep the window's shape: grow (small window) or shrink (huge window) the
		// game area until it is inside the limits, then scale the picture
		double k = 1.0;
		if (nx < minx || ny < miny) {
			k = (double)minx / nx > (double)miny / ny ? (double)minx / nx : (double)miny / ny;
		} else {
			k = (double)XRESMAX / nx < (double)YRESMAX / ny ? (double)XRESMAX / nx : (double)YRESMAX / ny;
		}
		changed = sdl_set_game_size_u((int)(nx * k + 0.5), (int)(ny * k + 0.5), u);
	}

	gw = sdl_map_xres * sdl_scale;
	gh = sdl_map_yres * sdl_scale;

	target = SDL_GetRenderTarget(sdlren);
	SDL_SetRenderTarget(sdlren, NULL);

	if (w >= gw && h >= gh && w - gw < sdl_scale * 2 && h - gh < sdl_scale * 2) {
		// pixel for pixel; the leftover is less than one game pixel
		SDL_SetRenderLogicalPresentation(sdlren, w, h, SDL_LOGICAL_PRESENTATION_LETTERBOX);
		map_offx = (w / sdl_scale - sdl_map_xres) / 2;
		map_offy = (h / sdl_scale - sdl_map_yres) / 2;
	} else {
		SDL_SetRenderLogicalPresentation(sdlren, gw, gh, SDL_LOGICAL_PRESENTATION_LETTERBOX);
		map_offx = map_offy = 0;
	}
	gui_offx = (int)((float)map_offx / sdl_ui_scale + 0.5f);
	gui_offy = (int)((float)map_offy / sdl_ui_scale + 0.5f);
	sdl_end_passes();

	SDL_SetRenderTarget(sdlren, target);

	if (changed) {
		gui_layout_changed();
	}
}

static SDL_DisplayID sdl_window_display(void)
{
	SDL_DisplayID id = sdlwnd ? SDL_GetDisplayForWindow(sdlwnd) : 0;

	return id ? id : SDL_GetPrimaryDisplay();
}

// Desktop resolution of the monitor the window is on
void sdl_get_display_size(int *w, int *h)
{
	const SDL_DisplayMode *dm = SDL_GetDesktopDisplayMode(sdl_window_display());

	*w = dm ? dm->w : 1920;
	*h = dm ? dm->h : 1080;
}

// Size that belongs to the current display mode: the window size when windowed,
// the screen mode when fullscreen.
void sdl_get_mode_size(int *w, int *h)
{
	const SDL_DisplayMode *fm;

	*w = *h = 0;
	if (!sdlwnd) {
		return;
	}
	if (sdl_display_mode == DISPMODE_FULLSCREEN && (fm = SDL_GetWindowFullscreenMode(sdlwnd))) {
		*w = fm->w;
		*h = fm->h;
		return;
	}
	if (sdl_display_mode == DISPMODE_BORDERLESS) {
		sdl_get_display_size(w, h);
		return;
	}
	SDL_GetWindowSize(sdlwnd, w, h);
}

static int sdl_add_resolution(int *rw, int *rh, int cnt, int max, int w, int h)
{
	for (int i = 0; i < cnt; i++) {
		if (rw[i] == w && rh[i] == h) {
			return cnt;
		}
	}
	if (cnt >= max) {
		return cnt;
	}
	rw[cnt] = w;
	rh[cnt] = h;
	return cnt + 1;
}

// List of resolutions to offer, smallest first: the monitor's fullscreen modes plus
// common window sizes that fit on the desktop.
int sdl_get_resolutions(int *rw, int *rh, int max)
{
	static const int common[][2] = {{800, 600}, {1024, 768}, {1280, 720}, {1280, 800}, {1280, 1024}, {1366, 768},
	    {1440, 900}, {1600, 900}, {1600, 1200}, {1680, 1050}, {1920, 1080}, {1920, 1200}, {2560, 1080}, {2560, 1440},
	    {2560, 1600}, {3440, 1440}, {3840, 2160}};
	int cnt = 0, dw, dh, n = 0;
	SDL_DisplayMode **modes;

	sdl_get_display_size(&dw, &dh);

	modes = SDL_GetFullscreenDisplayModes(sdl_window_display(), &n);
	if (modes) {
		for (int i = 0; i < n; i++) {
			if (modes[i]->w >= 800 && modes[i]->h >= 450) {
				cnt = sdl_add_resolution(rw, rh, cnt, max, modes[i]->w, modes[i]->h);
			}
		}
		SDL_free(modes);
	}
	for (size_t i = 0; i < sizeof(common) / sizeof(common[0]); i++) {
		if (common[i][0] <= dw && common[i][1] <= dh) {
			cnt = sdl_add_resolution(rw, rh, cnt, max, common[i][0], common[i][1]);
		}
	}
	cnt = sdl_add_resolution(rw, rh, cnt, max, dw, dh);

	// sort by width, then height
	for (int i = 1; i < cnt; i++) {
		int w = rw[i], h = rh[i], j = i - 1;
		while (j >= 0 && (rw[j] > w || (rw[j] == w && rh[j] > h))) {
			rw[j + 1] = rw[j];
			rh[j + 1] = rh[j];
			j--;
		}
		rw[j + 1] = w;
		rh[j + 1] = h;
	}
	return cnt;
}

// Switch between windowed, borderless (windowed fullscreen) and exclusive fullscreen.
// width and height are the window size or the screen mode; borderless ignores them.
int sdl_set_display_mode(int mode, int width, int height)
{
	SDL_DisplayID disp;
	SDL_DisplayMode fm;
	SDL_Rect usable;

	if (!sdlwnd || mode < DISPMODE_WINDOWED || mode > DISPMODE_FULLSCREEN) {
		return 0;
	}
	disp = sdl_window_display();

	if (mode == DISPMODE_FULLSCREEN) {
		if (width > 0 && height > 0 && SDL_GetClosestFullscreenDisplayMode(disp, width, height, 0.0f, false, &fm)) {
			SDL_SetWindowFullscreenMode(sdlwnd, &fm);
		} else {
			SDL_SetWindowFullscreenMode(sdlwnd, NULL);
		}
		SDL_SetWindowFullscreen(sdlwnd, true);
	} else if (mode == DISPMODE_BORDERLESS) {
		SDL_SetWindowFullscreenMode(sdlwnd, NULL);
		SDL_SetWindowFullscreen(sdlwnd, true);
	} else {
		SDL_SetWindowFullscreen(sdlwnd, false);
		SDL_SyncWindow(sdlwnd);
		if (width > 0 && height > 0 && (SDL_GetWindowFlags(sdlwnd) & SDL_WINDOW_MAXIMIZED)) {
			// a maximized window ignores size changes until it is restored
			SDL_RestoreWindow(sdlwnd);
			SDL_SyncWindow(sdlwnd);
		}
		SDL_SetWindowBordered(sdlwnd, true);
		SDL_SetWindowResizable(sdlwnd, true);
		if (width > 0 && height > 0) {
			if (SDL_GetDisplayUsableBounds(disp, &usable)) {
				if (width > usable.w) {
					width = usable.w;
				}
				if (height > usable.h) {
					height = usable.h;
				}
			}
			SDL_SetWindowSize(sdlwnd, width, height);
			SDL_SetWindowPosition(
			    sdlwnd, (int)SDL_WINDOWPOS_CENTERED_DISPLAY(disp), (int)SDL_WINDOWPOS_CENTERED_DISPLAY(disp));
		}
	}
	SDL_SyncWindow(sdlwnd);

	sdl_display_mode = mode;
	sdl_update_presentation();

	note("Display mode %d, %dx%d", mode, width, height);

	return 1;
}

void sdl_capture_mouse(int flag)
{
	SDL_CaptureMouse(flag ? true : false);
}

/* This function is a hack. It can only load one specific type of
   Windows cursor file: 32x32 pixels with 1 bit depth. */

SDL_Cursor *sdl_create_cursor(char *filename)
{
	FILE *fp;
	unsigned char mask[128], data[128], buf[326];
	unsigned char mask2[128 * 16] = {0}, data2[128 * 16] = {0};

	fp = fopen(filename, "rb");
	if (!fp) {
		warn("SDL Error: Could not open cursor file %s.\n", filename);
		return NULL;
	}

	if (fread(buf, 1, 326, fp) != 326) {
		warn("SDL Error: Read cursor file failed.\n");
		fclose(fp);
		return NULL;
	}
	fclose(fp);

	// translate .cur
	for (int i = 0; i < 32; i++) {
		for (int j = 0; j < 4; j++) {
			data[i * 4 + j] = (unsigned char)((~buf[322 - i * 4 + j]) & (~buf[194 - i * 4 + j]));
			mask[i * 4 + j] = buf[194 - i * 4 + j];
		}
	}

	// scale up if needed and add frame to cross
	static char cross[11][11] = {{0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0}, {0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0},
	    {0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0}, {0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0}, {1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1},
	    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}, {1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1}, {0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0},
	    {0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0}, {0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0}, {0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0}};
	int dst, src, i1, b1, i2, b2, y1, y2;

	for (y2 = 0; y2 < 32 * sdl_scale; y2++) {
		y1 = y2 / sdl_scale;

		for (dst = 0; dst < 32 * sdl_scale; dst++) {
			src = dst / sdl_scale;

			i1 = src / 8 + y1 * 4;
			b1 = 128 >> (src & 7);

			i2 = dst / 8 + y2 * 4 * sdl_scale;
			b2 = 128 >> (dst & 7);

			if (src < 11 && y1 < 11 && cross[y1][src]) {
				data2[i2] |= b2;
				mask2[i2] |= b2;
			} else {
				if (data[i1] & b1) {
					data2[i2] |= b2;
				} else {
					data2[i2] &= ~b2;
				}
				if (mask[i1] & b1) {
					mask2[i2] |= b2;
				} else {
					mask2[i2] &= ~b2;
				}
			}
		}
	}
	return SDL_CreateCursor(data2, mask2, 32 * sdl_scale, 32 * sdl_scale, 6 * sdl_scale, 6 * sdl_scale);
}

int sdl_create_cursors(void)
{
	int error = 0;

	curs[SDL_CUR_c_only] = sdl_create_cursor("res/cursor/c_only.cur");
	if (!curs[SDL_CUR_c_only]) {
		error = 1;
	}

	curs[SDL_CUR_c_take] = sdl_create_cursor("res/cursor/c_take.cur");
	if (!curs[SDL_CUR_c_take]) {
		error = 1;
	}

	curs[SDL_CUR_c_drop] = sdl_create_cursor("res/cursor/c_drop.cur");
	if (!curs[SDL_CUR_c_drop]) {
		error = 1;
	}

	curs[SDL_CUR_c_attack] = sdl_create_cursor("res/cursor/c_atta.cur");
	if (!curs[SDL_CUR_c_attack]) {
		error = 1;
	}

	curs[SDL_CUR_c_raise] = sdl_create_cursor("res/cursor/c_rais.cur");
	if (!curs[SDL_CUR_c_raise]) {
		error = 1;
	}

	curs[SDL_CUR_c_give] = sdl_create_cursor("res/cursor/c_give.cur");
	if (!curs[SDL_CUR_c_give]) {
		error = 1;
	}

	curs[SDL_CUR_c_use] = sdl_create_cursor("res/cursor/c_use.cur");
	if (!curs[SDL_CUR_c_use]) {
		error = 1;
	}

	curs[SDL_CUR_c_usewith] = sdl_create_cursor("res/cursor/c_usew.cur");
	if (!curs[SDL_CUR_c_usewith]) {
		error = 1;
	}

	curs[SDL_CUR_c_swap] = sdl_create_cursor("res/cursor/c_swap.cur");
	if (!curs[SDL_CUR_c_swap]) {
		error = 1;
	}

	curs[SDL_CUR_c_sell] = sdl_create_cursor("res/cursor/c_sell.cur");
	if (!curs[SDL_CUR_c_sell]) {
		error = 1;
	}

	curs[SDL_CUR_c_buy] = sdl_create_cursor("res/cursor/c_buy.cur");
	if (!curs[SDL_CUR_c_buy]) {
		error = 1;
	}

	curs[SDL_CUR_c_look] = sdl_create_cursor("res/cursor/c_look.cur");
	if (!curs[SDL_CUR_c_look]) {
		error = 1;
	}

	curs[SDL_CUR_c_set] = sdl_create_cursor("res/cursor/c_set.cur");
	if (!curs[SDL_CUR_c_set]) {
		error = 1;
	}

	curs[SDL_CUR_c_spell] = sdl_create_cursor("res/cursor/c_spell.cur");
	if (!curs[SDL_CUR_c_spell]) {
		error = 1;
	}

	curs[SDL_CUR_c_pix] = sdl_create_cursor("res/cursor/c_pix.cur");
	if (!curs[SDL_CUR_c_pix]) {
		error = 1;
	}

	curs[SDL_CUR_c_say] = sdl_create_cursor("res/cursor/c_say.cur");
	if (!curs[SDL_CUR_c_say]) {
		error = 1;
	}

	curs[SDL_CUR_c_junk] = sdl_create_cursor("res/cursor/c_junk.cur");
	if (!curs[SDL_CUR_c_junk]) {
		error = 1;
	}

	curs[SDL_CUR_c_get] = sdl_create_cursor("res/cursor/c_get.cur");
	if (!curs[SDL_CUR_c_get]) {
		error = 1;
	}

	if (error) {
		fail("Failed to load one or more cursor files");
		return 0;
	}

	return 1;
}

void sdl_set_cursor(int cursor)
{
	if (cursor < SDL_CUR_c_only || cursor > SDL_CUR_c_get) {
		return;
	}
	if (!curs[cursor]) {
		return;
	}
	SDL_SetCursor(curs[cursor]);
}

// Worker function to process jobs from the queue
// Used in single-threaded mode (called from main thread)
// Returns 1 if work was found and processed, 0 if queue was empty or no work
// was done because it was called from a multi-threaded context.
int if_single_thread_process_one_job(void)
{
	// This function is ONLY for single-threaded mode
	// In multi-threaded mode, worker threads run their own loop
	if (sdl_multi) {
		return 0;
	}

	// Pop a job from the queue (non-blocking)
	texture_job_t job;
	if (!tex_jobs_pop(&job, 0)) {
		return 0; // Queue empty
	}

	int cache_index = job.cache_index;
	struct sdl_texture *tex = &sdlt[cache_index];

	// Single-threaded: no races possible, generation check is sufficient
	// If generation changed, the slot was evicted and reused - abandon this work
	if (tex->generation != job.generation) {
		return 0;
	}

	// Mark in-worker (no lock needed in single-threaded mode)
	tex->work_state = TX_WORK_IN_WORKER;

	// Do the actual work: load image and do stages 1+2
	unsigned int sprite = tex->sprite;

	if (sdl_ic_load(sprite, NULL) < 0) {
		// Failed: mark idle and leave DIDMAKE unset
		// Generation can't change under us in single-threaded mode.
		return 0;
	}

	// Stage 1 + 2
	sdl_make(tex, &sdli[sprite], 1);
	sdl_make(tex, &sdli[sprite], 2);

	return 1;
}

void sdl_pre_add(unsigned int sprite, signed char sink, unsigned char freeze, unsigned char scale, char cr, char cg,
    char cb, char light, char sat, int c1, int c2, int c3, int shine, char ml, char ll, char rl, char ul, char dl)
{
	Uint64 start;

	if (sprite >= MAXSPRITE) {
		note("illegal sprite %u wanted in pre_add", sprite);
		return;
	}

	// Ensure there is a cache slot (but don't force full make+tex)
	start = SDL_GetTicks();
	int cache_index = sdl_tx_load(sprite, sink, freeze, scale, cr, cg, cb, light, sat, c1, c2, c3, shine, ml, ll, rl,
	    ul, dl, NULL, 0, 0, NULL, 0, 1);
	extern long long sdl_time_alloc;
	sdl_time_alloc += (long long)(SDL_GetTicks() - start);

	if (cache_index == -1) {
		// Already in cache
		return;
	}

	if (cache_index == STX_NONE) {
		// Failed to allocate
		return;
	}

	struct sdl_texture *slot = &sdlt[cache_index];

	// Single-threaded: do the CPU work inline
	if (!sdl_multi) {
		if (!(flags_load(slot) & SF_DIDMAKE)) {
			unsigned int sprite_id = slot->sprite;
			if (sdl_ic_load(sprite_id, NULL) >= 0) {
				sdl_make(slot, &sdli[sprite_id], 1);
				sdl_make(slot, &sdli[sprite_id], 2);
			}
		}
		return;
	}

	// Multi-threaded: enqueue background job
	SDL_LockMutex(g_tex_jobs.mutex);

	// Check if already queued or in-progress
	if (slot->work_state != TX_WORK_IDLE) {
		SDL_UnlockMutex(g_tex_jobs.mutex);
		return;
	}

	// Check if queue is full
	if (g_tex_jobs.count >= TEX_JOB_CAPACITY) {
#ifdef DEVELOPER
		static uint64_t last_log_time = 0;
		uint64_t now = SDL_GetTicks();
		if (now - last_log_time > 1000) {
			warn("Texture job queue full: capacity=%d, dropping preload for sprite %u", TEX_JOB_CAPACITY, sprite);
			last_log_time = now;
		}
#endif
		SDL_UnlockMutex(g_tex_jobs.mutex);
		return;
	}

	// Add job to queue
	texture_job_t *job = &g_tex_jobs.jobs[g_tex_jobs.tail];
	job->cache_index = cache_index;
	job->generation = slot->generation;
	job->kind = TEXTURE_JOB_MAKE_STAGES_1_2;

	g_tex_jobs.tail = (g_tex_jobs.tail + 1) % TEX_JOB_CAPACITY;
	g_tex_jobs.count++;

	// Mark as queued
	slot->work_state = TX_WORK_QUEUED;

	SDL_SignalCondition(g_tex_jobs.cond);
	SDL_UnlockMutex(g_tex_jobs.mutex);

	// Wake a worker
	SDL_SignalSemaphore(prework);
}

long long sdl_time_mutex = 0;

void sdl_lock(void *a)
{
	Uint64 start = SDL_GetTicks();
	SDL_LockMutex(a);
	sdl_time_mutex += (long long)(SDL_GetTicks() - start);
}

#define SDL_LockMutex(a) sdl_lock(a)

int sdl_pre_do(void)
{
	Uint64 start;
	int uploads = 0;

	start = SDL_GetTicks();

	// Single-threaded: process jobs from queue (will no-op if called from multi)
	if_single_thread_process_one_job();

	// Main thread: upload any textures where CPU work is done (SF_DIDMAKE) but
	// GPU upload hasn't happened (!SF_DIDTEX)
	// This is stage 3: creating the actual SDL_Texture
	const int max_uploads_per_call = 64; // Safety bound to avoid stalling frame

	for (int i = 0; i < MAX_TEXCACHE && uploads < max_uploads_per_call; i++) {
		struct sdl_texture *slot = &sdlt[i];

		uint16_t flags = flags_load(slot);
		if (!(flags & SF_SPRITE)) {
			continue;
		}

		// Worker did stage 1+2, GPU upload hasn't happened yet
		if ((flags & SF_DIDMAKE) && !(flags & SF_DIDTEX)) {
			unsigned int sprite = slot->sprite;
			sdl_make(slot, &sdli[sprite], 3);
			uploads++;
		}
	}

	extern long long sdl_time_pre2;
	sdl_time_pre2 += (long long)(SDL_GetTicks() - start);

	return uploads;
}

uint64_t sdl_backgnd_wait = 0, sdl_backgnd_work = 0, sdl_backgnd_jobs = 0;

int sdl_pre_backgnd(void *ptr)
{
	int worker_id = (int)(long long)ptr;
	struct zip_handles *zips = worker_zips ? &worker_zips[worker_id] : NULL;
	uint64_t wait_start, work_start;

	SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_LOW);

	for (;;) {
		// Wait for work to be available (blocks until signaled)
		wait_start = SDL_GetTicks();
		SDL_WaitSemaphore(prework);
		sdl_backgnd_wait += SDL_GetTicks() - wait_start;

		if (!prework) {
			SDL_Log("sdl_pre_backgnd: SDL_WaitSemaphore failed: %s - exiting worker thread", SDL_GetError());
			return -1;
		}

		// Check for shutdown before each job attempt
		if (quit || SDL_GetAtomicInt(&worker_quit)) {
			return 0;
		}

		work_start = SDL_GetTicks();

		// Pop a job from the queue
		texture_job_t job;
		if (!tex_jobs_pop(&job, 0)) {
			// No work found
			continue;
		}

		int cache_index = job.cache_index;
		struct sdl_texture *tex = &sdlt[cache_index];

		// Check generation: if stale, skip
		if (tex->generation != job.generation) {
			continue;
		}

		// Mark in-worker
		SDL_LockMutex(g_tex_jobs.mutex);
		if (tex->generation != job.generation) {
			SDL_UnlockMutex(g_tex_jobs.mutex);
			continue;
		}
		tex->work_state = TX_WORK_IN_WORKER;
		SDL_UnlockMutex(g_tex_jobs.mutex);

		// Do the actual work: load image and do stages 1+2
		unsigned int sprite = tex->sprite;

		if (sdl_ic_load(sprite, zips) < 0) {
			// Failed: leave DIDMAKE unset, allow main thread to handle fallback
			SDL_LockMutex(g_tex_jobs.mutex);
			if (tex->generation == job.generation) {
				tex->work_state = TX_WORK_IDLE;
			}
			SDL_UnlockMutex(g_tex_jobs.mutex);
			continue;
		}

		// Stage 1 + 2
		sdl_make(tex, &sdli[sprite], 1);
		sdl_make(tex, &sdli[sprite], 2);

		SDL_LockMutex(g_tex_jobs.mutex);
		if (tex->generation == job.generation) {
			// CPU work done; GPU creation is main-thread
			tex->work_state = TX_WORK_IDLE;
		}
		SDL_UnlockMutex(g_tex_jobs.mutex);

		sdl_backgnd_work += SDL_GetTicks() - work_start;
		sdl_backgnd_jobs++;
	}

	return 0;
}

bool sdl_is_shown(void)
{
	SDL_WindowFlags flags;

	flags = SDL_GetWindowFlags(sdlwnd);

	if (flags & SDL_WINDOW_HIDDEN) {
		return false;
	}
	if (flags & SDL_WINDOW_MINIMIZED) {
		return false;
	}

	return true;
}

bool sdl_has_focus(void)
{
	SDL_WindowFlags flags;

	flags = SDL_GetWindowFlags(sdlwnd);

	if (flags & SDL_WINDOW_MOUSE_FOCUS) {
		return true;
	}

	return false;
}

void sdl_set_title(char *title)
{
	SDL_SetWindowTitle(sdlwnd, title);
}

void *sdl_create_texture(int width, int height)
{
	return SDL_CreateTexture(sdlren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, width, height);
}

void sdl_render_copy(void *tex, void *sr, void *dr)
{
	SDL_RenderTexture(sdlren, tex, sr, dr);
}

void sdl_render_copy_ex(void *tex, void *sr, void *dr, double angle)
{
	SDL_RenderTextureRotated(sdlren, tex, sr, dr, angle, 0, SDL_FLIP_NONE);
}

void sdl_flush_textinput(void)
{
	SDL_FlushEvent(SDL_EVENT_TEXT_INPUT);
}

int sdl_check_mouse(void)
{
	float fx, fy;
	int x, y, x2, y2, x3, y3, top;
	SDL_GetGlobalMouseState(&fx, &fy);

	// Convert to int for discrete position comparisons
	x = (int)fx;
	y = (int)fy;

	SDL_GetWindowPosition(sdlwnd, &x2, &y2);
	SDL_GetWindowSize(sdlwnd, &x3, &y3);
	SDL_GetWindowBordersSize(sdlwnd, &top, NULL, NULL, NULL);

	if (x < x2 || x > x2 + x3 || y > y2 + y3) {
		return 1;
	}

	if (game_options & GO_TINYTOP) {
		if (y2 - y > top) {
			return 1;
		}
	} else {
		if (y2 - y > 100 * sdl_scale) {
			return 1;
		}
	}

	if (y < y2) {
		return -1;
	}

	return 0;
}
