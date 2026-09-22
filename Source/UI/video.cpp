//
// eXcellent Multi-platform emulator type 8 - 'XM8'
// based on ePC-8801MA
//
// Author (ePC-8801MA) : Takeda.Toshiya
// Author (XM8) : Tanaka.Yasushi
//
// [ video driver ]
//

#ifdef SDL

#include "os.h"
#include "common.h"
#include "vm.h"
#include "event.h"
#include "app.h"
#include "emu_sdl.h"
#include "setting.h"
#include "font.h"
#include "diskmgr.h"
#include "video.h"

#include "xbrz.h"

//
// defines
//
#define COLOR_BLACK			RGB_COLOR(0, 0, 0)
										// drive status (black)
#define COLOR_WHITE			RGB_COLOR(255, 255, 255)
										// drive status (white)
#define COLOR_NODISK		RGB_COLOR(31, 31, 31)
										// drive status (no disk)
#define COLOR_NOACCESS		RGB_COLOR(95, 95, 95)
										// drive status (no access)
#define COLOR_2D			RGB_COLOR(255, 48, 48)
										// drive status (2D access)
#define COLOR_2HD			RGB_COLOR(58, 239, 50)
										// drive status (2HD access)
#define MINIMUM_HEIGHT		2
										// minimum height
#define STATUS_HEIGHT		16
										// status area height
#define PORTRAIT_PANEL_MIN_SCALE	90
										// minimum lower-panel scale
#define DRIVE_WIDTH			(28 * 8)
										// drive area width
#define FRAME_RATE_X		56
										// x position of frame rate (per ank)
#define FULL_SPEED_X		63
										// x position of full speed (per ank)
#define SYSTEM_INFO_X		71
										// x position of system info (per ank)
#define SOFTKEY_STEP		16
										// softkey mod step
#define ACCESS_USEC			(200 * 1000)
										// access lamp delay
#define SCALE_FACTOR		2
										// Video rendering Scale factor

//
// Video()
// constructor
//
Video::Video(App *a)
{
	int loop;

	// save parameter
	app = a;

	// object
	setting = NULL;
	font = NULL;
	diskmgr = NULL;
	window = NULL;
	renderer = NULL;
	draw_texture = NULL;
	menu_texture = NULL;
	softkey_texture = NULL;
	status_texture = NULL;
	frame_buf = NULL;
	backup_buf = NULL;
	menu_buf = NULL;
	softkey_buf = NULL;

	// parameter
	horizontal = false;
	portrait_split = false;
	menu_mode = false;
	window_width = 0;
	window_height = 0;
	brightness = 0;
	softkey_mode = false;
	softkey_mod = 0xff;
	video_height = (SCREEN_HEIGHT + MINIMUM_HEIGHT + STATUS_HEIGHT);
	status_alpha = 0xffffffff;

	// draw control
	draw_ctrl = true;
	draw_line = 0;
	softkey_ctrl = false;
#ifdef XM8_ENABLE_RETROACHIEVEMENTS
	ra_status_active = false;
#endif

	// rect
	SDL_zero(draw_rect);
	SDL_zero(src_rect);
	SDL_zero(status_rect);
	SDL_zero(softkey_rect);
	SDL_zero(menu_rect);
	memset(clear_rect, 0, sizeof(clear_rect));

	// drive status
	for (loop=0; loop<MAX_DRIVE; loop++) {
		drive_status[loop].ready = false;
		drive_status[loop].readonly = false;
		drive_status[loop].access = ACCESS_NONE;
		drive_status[loop].prev = ACCESS_NONE;
		drive_status[loop].current = ACCESS_MAX;
		drive_status[loop].clock = 0;
		drive_status[loop].name[0] = '\0';
	}

	// frame rate
	frame_rate[0] = 0;
	frame_rate[1] = 0x10000;

	// system information
	system_info[0] = 0;
	system_info[1] = 0xffff;

	// full speed
	full_speed[0] = false;
	full_speed[1] = true;

	// power down
	power_down[0] = false;
	power_down[1] = false;
}

//
// ~Video()
// destructor
//
Video::~Video()
{
	Deinit();
}

//
// Init()
// initialize
//
bool Video::Init(SDL_Window *win)
{
	// save window
	window = win;

	// get object
	setting = app->GetSetting();

	// frame buffer
	frame_buf = (uint32*)SDL_malloc(SCREEN_WIDTH * (SCREEN_HEIGHT + MINIMUM_HEIGHT + STATUS_HEIGHT) * sizeof(uint32));
	if (frame_buf == NULL) {
		Deinit();
		return false;
	}
	memset(frame_buf, 0, SCREEN_WIDTH * (SCREEN_HEIGHT + MINIMUM_HEIGHT + STATUS_HEIGHT) * sizeof(uint32));

	// backup buffer
	backup_buf = (uint32*)SDL_malloc(SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint32));
	if (backup_buf == NULL) {
		Deinit();
		return false;
	}
	memset(backup_buf, 0, SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint32));

	// menu buffer
	menu_buf = (uint32*)SDL_malloc(SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint32));
	if (menu_buf == NULL) {
		Deinit();
		return false;
	}
	memset(menu_buf, 0, SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint32));

	// softkey buffer
	softkey_buf = (uint32*)SDL_malloc(SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint32));
	if (softkey_buf == NULL) {
		Deinit();
		return false;
	}
	memset(softkey_buf, 0, SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint32));

	// support RGB888 format only (see common.h)
#ifndef _RGB888
#error invalid rgb format
#endif // _RGB888

#ifdef __ANDROID__
	if (setting->IsForceRGB565() == true) {
		// avoid red screen on Galaxy series
		// https://bugzilla.libsdl.org/show_bug.cgi?id=2291
		SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 5);
		SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 6);
		SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 5);
	}
#endif //__ANDROID__

	// renderer
	renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
	if (renderer == NULL) {
		// VirtualBox and other virtual GPUs may not expose an EGL renderer.
		// Fall back to SDL's software renderer so the emulator remains usable.
		renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
	}
	if (renderer == NULL) {
		fprintf(stderr, "XM8: SDL_CreateRenderer() failed: %s\n", SDL_GetError());
		Deinit();
		return false;
	}

	int render_width = SCREEN_WIDTH;
	int render_height = SCREEN_HEIGHT;
	int render_status_height = (MINIMUM_HEIGHT + STATUS_HEIGHT);
	if (setting->IsImageInterpolation()) {
		render_width *= SCALE_FACTOR;
		render_height *= SCALE_FACTOR;
		render_status_height *= SCALE_FACTOR;
	}

	// drawing texture
	draw_texture = SDL_CreateTexture(   renderer,
										SDL_PIXELFORMAT_RGB888,
										SDL_TEXTUREACCESS_STREAMING,
										render_width,
										render_height);
	if (draw_texture == NULL) {
		Deinit();
		return false;
	}

	// menu texture
	menu_texture = SDL_CreateTexture(   renderer,
										SDL_PIXELFORMAT_ARGB8888,
										SDL_TEXTUREACCESS_STREAMING,
										render_width,
										render_height);
	if (menu_texture == NULL) {
		Deinit();
		return false;
	}

	// softkey texture
	softkey_texture = SDL_CreateTexture(renderer,
										SDL_PIXELFORMAT_ARGB8888,
										SDL_TEXTUREACCESS_STREAMING,
										render_width,
										render_height);
	if (softkey_texture == NULL) {
		Deinit();
		return false;
	}

	// status texture
	status_texture = SDL_CreateTexture( renderer,
										SDL_PIXELFORMAT_ARGB8888,
										SDL_TEXTUREACCESS_STREAMING,
										render_width,
										render_status_height);
	if (status_texture == NULL) {
		Deinit();
		return false;
	}

	// brightness
	brightness = setting->GetBrightness();
	SDL_SetTextureColorMod(draw_texture, brightness, brightness, brightness);

	// blend mode
	SDL_SetTextureBlendMode(draw_texture, SDL_BLENDMODE_NONE);
	SDL_SetTextureBlendMode(menu_texture, SDL_BLENDMODE_BLEND);
	SDL_SetTextureBlendMode(softkey_texture, SDL_BLENDMODE_BLEND);
	SDL_SetTextureBlendMode(status_texture, SDL_BLENDMODE_NONE);
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

	return true;
}

//
// Deinit()
// deinitialize
//
void Video::Deinit()
{
	if (screenshot_notice != NULL) {
		SDL_DestroyTexture(screenshot_notice);
		screenshot_notice = NULL;
	}
	// status texture
	if (status_texture != NULL) {
		SDL_DestroyTexture(status_texture);
		status_texture = NULL;
	}

	// softkey texture
	if (softkey_texture != NULL) {
		SDL_DestroyTexture(softkey_texture);
		softkey_texture = NULL;
	}

	// menu texture
	if (menu_texture != NULL) {
		SDL_DestroyTexture(menu_texture);
		menu_texture = NULL;
	}

	// drawing texture
	if (draw_texture != NULL) {
		SDL_DestroyTexture(draw_texture);
		draw_texture = NULL;
	}

	// renderer
	if (renderer != NULL) {
		SDL_DestroyRenderer(renderer);
		renderer = NULL;
	}

	// sofkey buffer
	if (softkey_buf != NULL) {
		SDL_free(softkey_buf);
		softkey_buf = NULL;
	}

	// menu buffer
	if (menu_buf != NULL) {
		SDL_free(menu_buf);
		menu_buf = NULL;
	}

	// backup buffer
	if (backup_buf != NULL) {
		SDL_free(backup_buf);
		backup_buf = NULL;
	}

	// frame buffer
	if (frame_buf != NULL) {
		SDL_free(frame_buf);
		frame_buf = NULL;
	}
}

//
// SetWindowSize()
// setup draw_rect and status_rect
//
void Video::SetWindowSize(int width, int height)
{
	bool status;
	int v_height;
	int draw_height;
	int game_height;
	int panel_height;
	int panel_width;

#ifdef __ANDROID__
	int panel_min_height;
#endif

	// font and disk manager
	if (font == NULL) {
		font = app->GetFont();
	}
	if (diskmgr == NULL) {
		diskmgr = app->GetDiskManager();
	}

	// save parameter
	window_width = width;
	window_height = height;

	// update video_height
	status = setting->HasStatusLine();
	if (status == true) {
		v_height = SCREEN_HEIGHT + MINIMUM_HEIGHT + STATUS_HEIGHT;
	}
	else {
		v_height = SCREEN_HEIGHT;
	}
	if (v_height != video_height) {
		video_height = v_height;

		// rebuild texture (status only)
		RebuildTexture(true);
	}

	// Android portrait game/control split. The game receives its full-width,
	// aspect-correct height first. The lower panel may shrink uniformly to
	// 90 percent, but smaller windows retain the legacy overlay layout.
	portrait_split = false;
	SDL_zero(softkey_rect);
	SDL_zero(menu_rect);
	game_height = 0;
#ifdef __ANDROID__
	panel_min_height = 0;
	if (height > width) {
		game_height = (width * video_height) / SCREEN_WIDTH;
		panel_min_height = ((width * SCREEN_HEIGHT) / SCREEN_WIDTH) *
			PORTRAIT_PANEL_MIN_SCALE / 100;
		portrait_split = height >= game_height + panel_min_height;
	}
#endif
	if (portrait_split) {
		panel_height = height - game_height;
		panel_width = (panel_height * SCREEN_WIDTH) / SCREEN_HEIGHT;
		if (panel_width > width) {
			panel_width = width;
			panel_height = (panel_width * SCREEN_HEIGHT) / SCREEN_WIDTH;
		}

		softkey_rect.x = (width - panel_width) / 2;
		softkey_rect.y = game_height + ((height - game_height - panel_height) / 2);
		softkey_rect.w = panel_width;
		softkey_rect.h = panel_height;
		menu_rect = softkey_rect;
		softkey_mod = 0;
		if (softkey_texture != NULL) {
			SDL_SetTextureAlphaMod(softkey_texture, 0xff);
		}
	}

	draw_height = portrait_split ? game_height : height;

	// check aspect
	if ((draw_height * SCREEN_WIDTH) >= (width * video_height)) {
		horizontal = false;
	}
	else {
		horizontal = true;
	}

	if (horizontal == false) {
		// vertical (portrait)
		draw_rect.x = 0;
		draw_rect.y = 0;
		draw_rect.w = width;
		draw_rect.h = (width * video_height) / SCREEN_WIDTH;
		if (draw_rect.h == draw_height) {
			// just window rect = draw rect
			clear_rect[0].w = 0;
			clear_rect[0].h = 0;
			clear_rect[1].w = 0;
			clear_rect[1].h = 0;
		}
		else {
			// centering
			draw_rect.y = (draw_height / 2) - (draw_rect.h / 2);
			clear_rect[0].x = 0;
			clear_rect[0].y = 0;
			clear_rect[0].w = width;
			clear_rect[0].h = draw_rect.y;
			clear_rect[1].x = 0;
			clear_rect[1].y = draw_rect.y + draw_rect.h;
			clear_rect[1].w = width;
			clear_rect[1].h = draw_height - clear_rect[1].y;
		}
	}
	else {
		// horizontal (landscape)
		draw_rect.x = 0;
		draw_rect.y = 0;
		draw_rect.w = (draw_height * SCREEN_WIDTH) / video_height;
		draw_rect.h = draw_height;
		if (draw_rect.w == width) {
			// just window rect = draw rect
			clear_rect[0].w = 0;
			clear_rect[0].h = 0;
			clear_rect[1].w = 0;
			clear_rect[1].h = 0;
		}
		else {
			// centering
			draw_rect.x = (width / 2) - (draw_rect.w / 2);
			clear_rect[0].x = 0;
			clear_rect[0].y = 0;
			clear_rect[0].w = draw_rect.x;
			clear_rect[0].h = draw_height;
			clear_rect[1].x = draw_rect.x + draw_rect.w;
			clear_rect[1].y = 0;
			clear_rect[1].w = width - clear_rect[1].x;
			clear_rect[1].h = draw_height;
		}
	}

	// status rect
	if (status == true) {
		// add status line
		status_rect.h = draw_rect.h;
		draw_rect.h = (draw_rect.w * SCREEN_HEIGHT) / SCREEN_WIDTH;
		status_rect.x = draw_rect.x;
		status_rect.w = draw_rect.w;
		status_rect.y = draw_rect.y + draw_rect.h;
		status_rect.h -= draw_rect.h;
	}
	else {
		// transparent status line
		status_rect.x = draw_rect.x;
		status_rect.y = (draw_rect.w * (SCREEN_HEIGHT - STATUS_HEIGHT)) / SCREEN_WIDTH;
		status_rect.w = draw_rect.w;
		status_rect.h = draw_rect.h - status_rect.y;
		status_rect.y += draw_rect.y;
	}

	// force next draw
	DrawCtrl();
}

//
// RebuildTexture()
// rebulid texture
//
void Video::RebuildTexture(bool statusonly)
{
	SDL_Texture *texture;

	int render_width = SCREEN_WIDTH;
	int render_height = SCREEN_HEIGHT;
	int render_status_height = setting->HasStatusLine() ? (MINIMUM_HEIGHT + STATUS_HEIGHT) : STATUS_HEIGHT;
	if (setting->IsImageInterpolation()) {
		render_width *= SCALE_FACTOR;
		render_height *= SCALE_FACTOR;
		render_status_height *= SCALE_FACTOR;
	}

	if (statusonly == false) {
		// set hint
		if (setting->IsImageInterpolation()) {
			SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
		} else {
			SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, setting->GetScaleQuality());
		}

		// drawing texture
		texture = SDL_CreateTexture(renderer,
									SDL_PIXELFORMAT_RGB888,
									SDL_TEXTUREACCESS_STREAMING,
									render_width,
									render_height);
		if (texture != NULL) {
			SDL_DestroyTexture(draw_texture);
			draw_texture = texture;
			SDL_SetTextureColorMod(draw_texture, brightness, brightness, brightness);
			SDL_SetTextureBlendMode(draw_texture, SDL_BLENDMODE_NONE);
		}

		// menu texture
		texture = SDL_CreateTexture(renderer,
									SDL_PIXELFORMAT_ARGB8888,
									SDL_TEXTUREACCESS_STREAMING,
									render_width,
									render_height);
		if (texture != NULL) {
			SDL_DestroyTexture(menu_texture);
			menu_texture = texture;
			SDL_SetTextureBlendMode(menu_texture, SDL_BLENDMODE_BLEND);
		}

		// softkey texture
		texture = SDL_CreateTexture(renderer,
									SDL_PIXELFORMAT_ARGB8888,
									SDL_TEXTUREACCESS_STREAMING,
									render_width,
									render_height);
		if (texture != NULL) {
			SDL_DestroyTexture(softkey_texture);
			softkey_texture = texture;
			SDL_SetTextureBlendMode(softkey_texture, SDL_BLENDMODE_BLEND);
			CopyFrameBuf(softkey_texture, softkey_buf, SCREEN_HEIGHT);
		}
	}

	// rebuild status texture
	texture = SDL_CreateTexture(renderer,
								SDL_PIXELFORMAT_ARGB8888,
								SDL_TEXTUREACCESS_STREAMING,
								render_width,
								render_status_height);
	if (texture != NULL) {
		SDL_DestroyTexture(status_texture);
		status_texture = texture;

		// blend mode
		if (setting->HasStatusLine() == true) {
			SDL_SetTextureBlendMode(status_texture, SDL_BLENDMODE_NONE);
		}
		else {
			SDL_SetTextureBlendMode(status_texture, SDL_BLENDMODE_BLEND);
		}

		// reset status area
		ResetStatus();
	}

	// force next draw
	DrawCtrl();
}

//
// SetFrameRate()
// set frame rate from app
//
void Video::SetFrameRate(Uint32 rate)
{
	frame_rate[0] = rate;
}

//
// SetSystemInfo()
// set system information from app
//
void Video::SetSystemInfo(Uint32 info)
{
	system_info[0] = info;
}

//
// SetFullSpeed()
// set running sppeed
//
void Video::SetFullSpeed(bool full)
{
	full_speed[0] = full;
}

//
// SetPowerDown()
// set power down
//
void Video::SetPowerDown(bool down)
{
	power_down[0] = down;
}

//
// SetSoftKey()
// enable displaying softkey
//
void Video::SetSoftKey(bool visible, bool direct)
{
	if (softkey_mode != visible) {
		softkey_mode = visible;

		if ((visible == false) && (direct == false)) {
			softkey_mod = 0xff;
		}
		else {
			softkey_mod = 0x00;
		}

		// reset
		if (visible == true) {
			SDL_SetTextureAlphaMod(softkey_texture, 0xff);
		}
	}
}

//
// UpdateSoftKey()
// update softkey texture from frame buffer
//
void Video::UpdateSoftKey()
{
	CopyFrameBuf(softkey_texture, (Uint32*)softkey_buf, SCREEN_HEIGHT);
	softkey_ctrl = true;
}

//
// ConvertPoint()
// convert point from window to texture
//
bool Video::ConvertPoint(int *x, int *y)
{
	int draw_x;
	int draw_y;
	SDL_Rect *rect;
	int logical_height;

	// Menus own the dedicated lower panel in Android portrait mode.
	if (portrait_split == true && menu_mode == true) {
		rect = &menu_rect;
		logical_height = SCREEN_HEIGHT;
	}
	else {
		rect = &draw_rect;
		logical_height = video_height;
	}

	// get
	draw_x = *x;
	draw_y = *y;

	// offset
	draw_x -= rect->x;
	draw_y -= rect->y;

	// convert
	draw_x = (draw_x * SCREEN_WIDTH) / rect->w;
	draw_y = (draw_y * logical_height) / rect->h;

	// over check
	if ((draw_x < 0) || (draw_x >= SCREEN_WIDTH)) {
		*x = 0;
		*y = 0;
		return false;
	}

	if ((draw_y < 0) || (draw_y >= logical_height)) {
		*x = 0;
		*y = 0;
		return false;
	}

	*x = draw_x;
	*y = draw_y;
	return true;
}

//
// ConvertFinger()
// convert finger point from window to texture
//
bool Video::ConvertFinger(float tx, float ty, int *x, int *y)
{
	// multiple with window rect
	tx *= (float)window_width;
	ty *= (float)window_height;

	// float to int
	*x = (int)tx;
	*y = (int)ty;

	// convert point
	return ConvertPoint(x, y);
}

//
// ConvertSoftKeyPoint()
// convert point from window to softkey texture
//
bool Video::ConvertSoftKeyPoint(int *x, int *y)
{
	if (portrait_split == false) {
		return ConvertPoint(x, y);
	}

	int draw_x = *x - softkey_rect.x;
	int draw_y = *y - softkey_rect.y;
	if (draw_x < 0 || draw_x >= softkey_rect.w ||
		draw_y < 0 || draw_y >= softkey_rect.h) {
		*x = 0;
		*y = 0;
		return false;
	}

	*x = (draw_x * SCREEN_WIDTH) / softkey_rect.w;
	*y = (draw_y * SCREEN_HEIGHT) / softkey_rect.h;
	return true;
}

//
// ConvertSoftKeyFinger()
// convert finger point from window to softkey texture
//
bool Video::ConvertSoftKeyFinger(float tx, float ty, int *x, int *y)
{
	*x = (int)(tx * (float)window_width);
	*y = (int)(ty * (float)window_height);
	return ConvertSoftKeyPoint(x, y);
}

//
// Draw()
// rendering
//
void Video::Draw()
{
	int ret;
	Uint8 bri;
	Uint8 step;
	Uint32 alpha;
	bool status;

	// Force both drawing and erasure of the transient screenshot notice.
	if (screenshot_notice != NULL) {
		draw_ctrl = true;
		if (SDL_TICKS_PASSED(SDL_GetTicks(), screenshot_notice_until)) {
			SDL_DestroyTexture(screenshot_notice);
			screenshot_notice = NULL;
		}
	}
	// brightness
	bri = setting->GetBrightness();
	if (brightness != bri) {
		brightness = bri;
		SDL_SetTextureColorMod(draw_texture, brightness, brightness, brightness);
		draw_ctrl = true;
	}

	// status line alpha level
	alpha = 0;
	if (setting->HasStatusLine() == false) {
		alpha = (Uint32)setting->GetStatusAlpha();
	}
	alpha <<= 24;
	if (status_alpha != alpha) {
		ResetStatus();
		status_alpha = alpha;
	}

	// status line
	status = draw_ctrl;
#ifdef XM8_ENABLE_RETROACHIEVEMENTS
	if (ra_status_active) {
		status = true;
	}
	else
#endif
	{
		if (DrawAccess() == true) {
			status = true;
			draw_ctrl = true;
		}
		if (DrawFrameRate() == true) {
			status = true;
			draw_ctrl = true;
		}
		if (DrawFullSpeed() == true) {
			status = true;
			draw_ctrl = true;
		}
		if (DrawSystemInfo() == true) {
			status = true;
			draw_ctrl = true;
		}
	}

	// warning message
	DrawPowerDown();

	// frame area
	if ((draw_ctrl == true) && (draw_line < SCREEN_HEIGHT)) {
		// require to draw
		CopyFrameBuf(draw_texture,
						(Uint32*)frame_buf,
					setting->Is400Line() ? SCREEN_HEIGHT : SCREEN_HEIGHT / 2,
					setting->IsImageInterpolation() ? 0 : setting->HasScanline() ? draw_line : draw_line / 2);
	}

	// calculate source rect
	src_rect.w = setting->IsImageInterpolation() ? SCREEN_WIDTH * SCALE_FACTOR : SCREEN_WIDTH;
	int height = setting->Is400Line() ? SCREEN_HEIGHT : SCREEN_HEIGHT / SCALE_FACTOR;
	src_rect.h = setting->IsImageInterpolation() ? height * SCALE_FACTOR : height;

	// status area
	if (status == true) {
		if (setting->HasStatusLine() == true) {
			CopyFrameBuf(status_texture, (Uint32*)&frame_buf[SCREEN_WIDTH * SCREEN_HEIGHT], MINIMUM_HEIGHT + STATUS_HEIGHT);
		}
		else {
			CopyFrameBuf(status_texture, (Uint32*)&frame_buf[SCREEN_WIDTH * SCREEN_HEIGHT], STATUS_HEIGHT);
		}
	}

	// menu
	if (menu_mode == true) {
		DrawMenu(status);
		return;
	}

	// force draw if softkey_ctrl == true
	if ((softkey_ctrl == true) || (softkey_mod > 0)) {
		draw_ctrl = true;
	}

	// check draw_ctrl
	if (draw_ctrl == false) {
		// draw_buf, status area and other parameters are not changed after privious draw
		return;
	}

	// clear if required
	if (portrait_split || (clear_rect[0].w != 0) || (clear_rect[0].h != 0)) {
		SDL_SetRenderDrawColor(renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
		SDL_RenderClear(renderer);
	}

	// draw_texture & status_texture
	ret = SDL_RenderCopy(renderer, draw_texture, &src_rect, &draw_rect);
	if (ret == 0) {
		// for transparent status
		ret = SDL_RenderCopy(renderer, status_texture, NULL, &status_rect);
	}

	if (portrait_split == true && app->IsRaOverlayBlocking() == false) {
		if (ret == 0) {
			ret = SDL_RenderCopy(renderer, softkey_texture, NULL, &softkey_rect);
		}
	}
	else if (softkey_mode == true) {
		// softkey = enable
		if (ret == 0) {
			ret = SDL_RenderCopy(renderer, softkey_texture, NULL, &draw_rect);
		}
	}
	else {
		// sofkey = disable
		if ((ret == 0) && (softkey_mod > 0)) {
			// draw softkey with mod
			SDL_SetTextureAlphaMod(softkey_texture, softkey_mod);
			ret = SDL_RenderCopy(renderer, softkey_texture, NULL, &draw_rect);

			// get step
			step = setting->GetSoftKeyAlpha() / SOFTKEY_STEP;
			if (step == 0) {
				step++;
			}

			// step down
			if (softkey_mod < step) {
				softkey_mod = 0;
			}
			else {
				softkey_mod -= step;
			}
		}
	}

	// present
	if (ret == 0) {
		DrawScreenshotNotice();
		SDL_RenderPresent(renderer);
	}

	// clear draw_ctrl
	draw_ctrl = false;
	draw_line = SCREEN_HEIGHT;
}

void Video::ShowScreenshotNotice(const char *text)
{
	if (screenshot_notice != NULL) SDL_DestroyTexture(screenshot_notice);
	screenshot_notice = NULL;
	SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, SCREEN_WIDTH, 32, 32, SDL_PIXELFORMAT_ARGB8888);
	if (!surface) return;
	SDL_FillRect(surface, NULL, 0xf0202020);
	SDL_Rect rect = {8, 8, SCREEN_WIDTH - 16, 16};
	font->DrawSjisLeftOr(static_cast<Uint32 *>(surface->pixels), &rect, text, 0xffffffff);
	screenshot_notice = SDL_CreateTextureFromSurface(renderer, surface);
	SDL_FreeSurface(surface);
	if (screenshot_notice) SDL_SetTextureBlendMode(screenshot_notice, SDL_BLENDMODE_BLEND);
	screenshot_notice_until = SDL_GetTicks() + 3000;
	DrawCtrl();
}

void Video::DrawScreenshotNotice()
{
	if (!screenshot_notice) return;
	SDL_Rect rect = {draw_rect.x, draw_rect.y, draw_rect.w, draw_rect.w * 32 / SCREEN_WIDTH};
	SDL_RenderCopy(renderer, screenshot_notice, NULL, &rect);
}

//
// DrawCtrl
// draw control (force draw)
//
void Video::DrawCtrl()
{
	draw_ctrl = true;
	draw_line = 0;
}

//
// DrawAccess()
// draw access status
//
bool Video::DrawAccess()
{
	EVENT *evmgr;
	int drive;
	bool access;
	bool ready;
	bool draw;
	Uint32 *buf;
	SDL_Rect rect;
	Uint32 fore;

	// get event manager
	evmgr = (EVENT*)app->GetEvMgr();

	// get current access status
	access = false;
	for (drive=0; drive<MAX_DRIVE; drive++) {
		drive_status[drive].access = diskmgr[drive]->GetAccess();
		if (drive_status[drive].access != ACCESS_NONE) {
			// valid access
			access = true;

			// save state and clock
			drive_status[drive].prev = drive_status[drive].access;
			drive_status[drive].clock = evmgr->current_clock();
		}
	}

	// FDC accesses no drive ?
	if (access == false) {
		// get passed usec
		for (drive=0; drive<MAX_DRIVE; drive++) {
			if (drive_status[drive].prev != ACCESS_NONE) {
				if (evmgr->passed_usec(drive_status[drive].clock) < ACCESS_USEC) {
					// use previous state
					drive_status[drive].access = drive_status[drive].prev;
				}
				else {
					// clear previous state due to timed out
					drive_status[drive].prev = ACCESS_NONE;
				}
			}
		}

		// access both drive ?
		if ((drive_status[0].access != ACCESS_NONE) && (drive_status[1].access != ACCESS_NONE)) {
			if (evmgr->passed_usec(drive_status[0].clock) < evmgr->passed_usec(drive_status[1].clock)) {
				// drive[1] -> drive [0]
				drive_status[1].access = ACCESS_NONE;
			}
			else {
				// drive[0] -> drive [1]
				drive_status[0].access = ACCESS_NONE;
			}
		}
	}

	// drive loop
	draw = false;
	for (drive=0; drive<MAX_DRIVE; drive++) {
		// compare
		ready = diskmgr[drive]->IsOpen();
		if (diskmgr[drive]->IsNext() == true) {
			ready = false;
		}
		if (ready == drive_status[drive].ready) {
			if (diskmgr[drive]->IsProtect() == drive_status[drive].readonly) {
				if (drive_status[drive].access == drive_status[drive].current) {
					if (strcmp(diskmgr[drive]->GetName(), drive_status[drive].name) == 0) {
						// all equal
						continue;
					}
				}
			}
		}

		// backup
		drive_status[drive].ready = ready;
		drive_status[drive].readonly = diskmgr[drive]->IsProtect();
		drive_status[drive].current = drive_status[drive].access;
		strcpy(drive_status[drive].name, diskmgr[drive]->GetName());

		// frame buffer pointer
		buf = &frame_buf[SCREEN_WIDTH * SCREEN_HEIGHT];
		if (drive == 0) {
			buf += DRIVE_WIDTH;
		}

		// rectanbgle
		rect.x = 0;
		rect.y = 0;
		rect.w = DRIVE_WIDTH - 1;
		if (setting->HasStatusLine() == true) {
			rect.h = MINIMUM_HEIGHT + STATUS_HEIGHT;
		}
		else {
			rect.h = STATUS_HEIGHT;
		}

		// set fill rect color
		switch (drive_status[drive].current) {
		// 2D access
		case ACCESS_2D:
			fore = COLOR_2D;
			break;

		case ACCESS_2HD:
			fore = COLOR_2HD;
			break;

		default:
			if (drive_status[drive].ready == true) {
				fore = COLOR_NOACCESS;
			}
			else {
				fore = COLOR_NODISK;
			}
			break;
		}

		// fill rect (full or half)
		if ((drive_status[drive].ready == true) && (drive_status[drive].readonly == true)) {
			font->DrawHalfRect(buf, &rect, fore | status_alpha, COLOR_BLACK | status_alpha);
		}
		else {
			font->DrawFillRect(buf, &rect, fore | status_alpha);
		}

		// drive number
		font->DrawAnkQuarter(buf, (char)(drive + '1'), COLOR_WHITE | status_alpha, COLOR_BLACK | status_alpha);

		// disk name
		if (drive_status[drive].name[0] != '\0') {
			font->DrawSjisCenterOr(buf, &rect, drive_status[drive].name, COLOR_WHITE | status_alpha);
		}

		// request to draw
		draw = true;
	}

	return draw;
}

//
// DrawFrameRate()
// draw frame rate
//
bool Video::DrawFrameRate()
{
	Uint32 *buf;
	char string[32];

	// compare and copy
	if (frame_rate[0] == frame_rate[1]) {
		return false;
	}

	// update frame rate
	frame_rate[1] = frame_rate[0];

	// format
	if (frame_rate[0] > 999) {
		strcpy(string, "--.-fps");
	}
	else {
		sprintf(string, "%2d.%1dfps", frame_rate[0] / 10, frame_rate[0] % 10);
	}

	// draw
	buf = &frame_buf[SCREEN_WIDTH * SCREEN_HEIGHT];
	buf += (FRAME_RATE_X * 8);
	if (setting->HasStatusLine() == true) {
		buf += SCREEN_WIDTH;
	}
	font->DrawAnkHalf(buf, string, COLOR_WHITE | status_alpha, COLOR_BLACK | status_alpha);

	// need to draw status area
	return true;
}

//
// DrawFullSpeed()
// draw full speed
//
bool Video::DrawFullSpeed()
{
	Uint32 *buf;

	// compare and copy
	if (full_speed[0] == full_speed[1]) {
		return false;
	}
	full_speed[1] = full_speed[0];

	// draw
	buf = &frame_buf[SCREEN_WIDTH * SCREEN_HEIGHT];
	buf += (FULL_SPEED_X * 8);
	if (setting->HasStatusLine() == true) {
		buf += SCREEN_WIDTH;
	}

	if (full_speed[0] == true) {
		font->DrawAnkHalf(buf, " NOWAIT ", COLOR_WHITE | status_alpha, COLOR_BLACK | status_alpha);
	}
	else {
		font->DrawAnkHalf(buf, "        ", COLOR_WHITE | status_alpha, COLOR_BLACK | status_alpha);
	}

	// need to draw status area
	return true;
}

//
// DrawSystemInfo()
// draw system info
//
bool Video::DrawSystemInfo()
{
	Uint32 info;
	Uint32 cpu;
	Uint32 *buf;
	char string[32];

	// equal ?
	if (system_info[0] == system_info[1]) {
		return false;
	}

	// update system info
	info = system_info[0];
	system_info[1] = info;

	// cpu clock
	cpu = (info >> 4) & 0x0f;
	if (cpu != 0) {
		strcpy(&string[4], "4MHz ");
	}
	else {
		if (setting->Is8HMode() == true) {
			strcpy(&string[4], "8MHzH");
		}
		else {
			strcpy(&string[4], "8MHz ");
		}
	}

	// mode
	switch (info & 0x0f) {
	case MODE_PC88_V1S:
		memcpy(string, "V1S-", 4);
		break;

	case MODE_PC88_V1H:
		memcpy(string, "V1H-", 4);
		break;

	case MODE_PC88_V2:
		memcpy(string, " V2-", 4);
		break;

	case MODE_PC88_N:
		memcpy(string, "  N-", 4);
		break;
	}

	// draw
	buf = &frame_buf[SCREEN_WIDTH * SCREEN_HEIGHT];
	buf += (SYSTEM_INFO_X * 8);
	if (setting->HasStatusLine() == true) {
		buf += SCREEN_WIDTH;
	}
	font->DrawAnkHalf(buf, string, COLOR_WHITE | status_alpha, COLOR_BLACK | status_alpha);

	// need to draw status area
	return true;
}

//
// ResetStatus()
// reset status information
//
void Video::ResetStatus()
{
	int drive;

	// drive
	for (drive=0; drive<MAX_DRIVE; drive++) {
		drive_status[drive].current = ACCESS_MAX;
	}

	// frame rate
	frame_rate[1] = 0x10000;

	// full speed
	if (app->IsFullSpeed() == true) {
		full_speed[1] = false;
	}
	else {
		full_speed[1] = true;
	}

	// system information
	system_info[1] = 0xffff;
}

//
// DrawPowerDown()
// draw power down
//
void Video::DrawPowerDown()
{
	SDL_Rect rect;

	// compare state
	if (power_down[0] == power_down[1]) {
		return;
	}

	// set current state
	power_down[1] = power_down[0];

	// if power_down[0] == false, clear message by GetFrameBuf()
	if (power_down[0] == false) {
		return;
	}

	rect.x = (SCREEN_WIDTH / 2) - (240 / 2);
	rect.y = (SCREEN_HEIGHT / 2) - (32 / 2);
	rect.w = 240;
	rect.h = 32;

	font->DrawRect(frame_buf, &rect, RGB_COLOR(255, 255, 255), RGB_COLOR(0, 0, 0));
	font->DrawSjisCenterOr(frame_buf, &rect, "Battery level is too low", RGB_COLOR(255, 255, 255));

	// force next draw
	DrawCtrl();
}

//
// DrawMenu()
// draw menu
//
void Video::DrawMenu(bool status)
{
	int ret;
	bool cmp;

	// compare
	cmp = true;
	if (memcmp(menu_buf, backup_buf, SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint32)) != 0) {
		// menu_buf has been changed
		cmp = false;
		memcpy(backup_buf, menu_buf, SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint32));
	}

	// no draw is needed ?
	if ((draw_ctrl == false) && (status == false) && (cmp == true)) {
		return;
	}

	// copy menu frame to texture
	CopyFrameBuf(menu_texture, menu_buf, SCREEN_HEIGHT);

	// clear if required
	if (portrait_split || (clear_rect[0].w != 0) || (clear_rect[0].h != 0)) {
		SDL_SetRenderDrawColor(renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
		SDL_RenderClear(renderer);
	}

	// draw_texture & status_texture
	ret = SDL_RenderCopy(renderer, draw_texture, &src_rect, &draw_rect);
	if (ret == 0) {
		ret = SDL_RenderCopy(renderer, status_texture, NULL, &status_rect);
	}

	// menu texture
	if (ret == 0) {
		// menu texture
		ret = SDL_RenderCopy(renderer, menu_texture, NULL,
			portrait_split ? &menu_rect : &draw_rect);
		if (ret == 0) {
			DrawScreenshotNotice();
			SDL_RenderPresent(renderer);
		}
	}

	// clear draw_ctrl
	draw_ctrl = false;
	draw_line = SCREEN_HEIGHT;
}

//
// CopyFrameBuf()
// copy frame buffer to texture
//
void Video::CopyFrameBuf(SDL_Texture *texture, Uint32 *src, int height, int top)
{
	Uint32 *dest;
	int ret;
	void *pixels;
	int pitch;

	// lock entire texture
	ret = SDL_LockTexture(texture, NULL, &pixels, &pitch);
	if (ret == 0) {
		dest = (Uint32*)pixels;

		if (setting->IsImageInterpolation()) {
			// Interpolate and copy
			Uint32 format;
			SDL_QueryTexture(texture, &format, nullptr, nullptr, nullptr);
			xbrz::ScalerCfg scaleCfg = xbrz::ScalerCfg();
			xbrz::ColorFormat colorFormat = format == SDL_PIXELFORMAT_ARGB8888 ? xbrz::ColorFormat::ARGB : xbrz::ColorFormat::RGB;
			xbrz::scale(SCALE_FACTOR, src, dest, SCREEN_WIDTH, height, colorFormat, scaleCfg, top, height);
		} else {
			// Copy as is
			src += SCREEN_WIDTH * top;
			dest += SCREEN_WIDTH * top;
			memcpy(dest, src, SCREEN_WIDTH * (height - top) * sizeof(uint32));				
		}

		// unlock texture
		SDL_UnlockTexture(texture);
	}
}

//
// GetFrameBuf()
// get frame buffer pointer for ePC-8801MA
//
uint32* Video::GetFrameBuf(uint32 y)
{
	if ((draw_ctrl == false) && (y >= 2) && ((y & 1) == 0)) {
		// compare previous 2 line
		if (memcmp(backup_buf, &frame_buf[SCREEN_WIDTH * (y - 2)], SCREEN_WIDTH * 2 * sizeof(uint32)) != 0) {
			// frame buffer has been changed, need to draw from that line
			draw_line = y - 2;
			draw_ctrl = true;
		}
	}

	if ((draw_ctrl == false) && (y < SCREEN_HEIGHT) && ((y & 1) == 0)) {
		// copy current line and next line
		memcpy(backup_buf, &frame_buf[SCREEN_WIDTH * y], SCREEN_WIDTH * 2 * sizeof(uint32));
	}

	return &frame_buf[SCREEN_WIDTH * y];
}

#ifdef XM8_ENABLE_RETROACHIEVEMENTS
uint32* Video::GetStatusFrame()
{
	return &frame_buf[SCREEN_WIDTH * SCREEN_HEIGHT];
}

int Video::GetStatusFrameHeight() const
{
	return setting->HasStatusLine() ? MINIMUM_HEIGHT + STATUS_HEIGHT :
		STATUS_HEIGHT;
}

int Video::GetStatusContentTop() const
{
	return setting->HasStatusLine() ? 1 : 0;
}

void Video::SetRaStatusActive(bool active)
{
	if (ra_status_active == active) {
		return;
	}
	if (ra_status_active && !active && frame_buf != NULL) {
		// Normal status widgets intentionally leave one-pixel separators
		// between the drive panels. Clear the complete status frame before
		// forcing them to redraw so pixels from a full-width RA row cannot
		// survive in those separators.
		uint32 *status_frame =
			&frame_buf[SCREEN_WIDTH * SCREEN_HEIGHT];
		const int pixel_count = SCREEN_WIDTH * GetStatusFrameHeight();
		const uint32 background = COLOR_BLACK | status_alpha;
		for (int index = 0; index < pixel_count; ++index) {
			status_frame[index] = background;
		}
	}
	ra_status_active = active;
	ResetStatus();
	DrawCtrl();
}

bool Video::IsDedicatedStatusPoint(int x, int y) const
{
	return setting->HasStatusLine() && x >= 0 && x < SCREEN_WIDTH &&
		y >= SCREEN_HEIGHT && y < SCREEN_HEIGHT + MINIMUM_HEIGHT +
		STATUS_HEIGHT;
}
#endif

//
// SetMenuMode()
// set menu mode
//
void Video::SetMenuMode(bool mode)
{
	if (menu_mode != mode) {
		menu_mode = mode;
		DrawCtrl();
	}
}

//
// GetMenuFrame()
// get frame buffer pointer for menu
//
Uint32* Video::GetMenuFrame()
{
	return (Uint32*)menu_buf;
}

//
// GetSoftKeyFrame()
// get frame buffer pointer for softkey
//
Uint32* Video::GetSoftKeyFrame()
{
	return (Uint32*)softkey_buf;
}


#endif // SDL
