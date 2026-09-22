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

#ifndef VIDEO_H
#define VIDEO_H

//
// video driver
//
class Video
{
public:
	Video(App *a);
										// constructor
	virtual ~Video();
										// destructor
	bool Init(SDL_Window *win);
										// initialize
	void Deinit();
										// deinitialize
	void SetWindowSize(int width, int height);
										// set window sizse
	void RebuildTexture(bool statusonly);
										// rebuild texture
	const uint32* GetScreenshotSource() const { return frame_buf; }
	void ShowScreenshotNotice(const char *text);
	void Draw();
										// rendering
	void DrawCtrl();
										// rendering control

	// virtual machine
	uint32* GetFrameBuf(uint32 y);
										// get frame buffer for ePC-8801MA
#ifdef XM8_ENABLE_RETROACHIEVEMENTS
	uint32* GetStatusFrame();
										// get status frame buffer for RA
	int GetStatusFrameHeight() const;
										// get status frame height
	int GetStatusContentTop() const;
										// get status text top
	void SetRaStatusActive(bool active);
										// replace normal status with RA status
	bool IsDedicatedStatusPoint(int x, int y) const;
										// point is in non-overlay status area
#endif

	// menu
	void SetMenuMode(bool mode);
										// set menu mode
	Uint32* GetMenuFrame();
										// get frame buffer for menu

	// input
	Uint32* GetSoftKeyFrame();
										// get frame buffer for softkey
	void SetSoftKey(bool enable, bool direct = false);
										// enable displaying softkey
	void UpdateSoftKey();
										// update softkey texture from frame buffer
	bool ConvertPoint(int *x, int *y);
										// convert point from window to texturure
	bool ConvertFinger(float tx, float ty, int *x, int *y);
										// convert finger point from window to texture
	bool ConvertSoftKeyPoint(int *x, int *y);
										// convert point from window to softkey texture
	bool ConvertSoftKeyFinger(float tx, float ty, int *x, int *y);
										// convert finger point from window to softkey texture

	// main scheduler
	void SetFrameRate(Uint32 rate);
										// set frame rate(x10)
	void SetSystemInfo(Uint32 info);
										// set system info
	void SetFullSpeed(bool full);
										// set running speed
	void SetPowerDown(bool down);
										// set power down

private:
	SDL_Texture *screenshot_notice = NULL;
	Uint32 screenshot_notice_until = 0;
	void DrawScreenshotNotice();
	// floppy drive information
	typedef struct _drive_info {
		bool ready;
										// media is ready
		bool readonly;
										// media is write protected
		int access;
										// access status (now)
		int prev;
										// access status (prev)
		int current;
										// current draw status
		uint32 clock;
										// last access clock
		char name[16 + 1];
										// media name
	} drive_info;

	bool DrawAccess();
										// draw access status
	bool DrawFrameRate();
										// draw frame rate
	bool DrawFullSpeed();
										// draw full speed
	bool DrawSystemInfo();
										// draw system info
	void DrawPowerDown();
										// draw power down
	void DrawMenu(bool status);
										// draw menu
	void CopyFrameBuf(SDL_Texture *texture, Uint32 *src, int height, int top = 0);
										// copy frame buffer to texture
	void ResetStatus();
										// reset status line

	// object
	App *app;
										// application
	Setting *setting;
										// setting
	Font *font;
										// font
	DiskManager **diskmgr;
										// disk manager array

	// frame buffer
	uint32 *frame_buf;
										// frame buffer for ePC-8801MA
	uint32 *backup_buf;
										// backup buffer for ePC-8801MA (1 line)
	uint32 *menu_buf;
										// frame buffer for menu
	uint32 *softkey_buf;
										// frame buffer for softkey

	// flag and parameter
	bool horizontal;
										// screen direction
	bool portrait_split;
										// Android portrait game/control split
	bool menu_mode;
										// menu mode
	int window_width;
										// window width
	int window_height;
										// window height
	SDL_Rect draw_rect;
										// drawing rect
	SDL_Rect src_rect;
										// source rect
	SDL_Rect status_rect;
										// status rect
	SDL_Rect softkey_rect;
										// dedicated softkey rect in portrait mode
	SDL_Rect menu_rect;
										// dedicated menu rect in portrait mode
	SDL_Rect clear_rect[2];
										// clear rect
	Uint8 brightness;
										// brightness
	bool softkey_mode;
										// softkey mode
	Uint8 softkey_mod;
										// softkey mod
	int video_height;
										// video height (SCREEN_HEIGHT or + MINIMUM_HEIGHT + STATUS_HEIGHT)
	Uint32 status_alpha;
										// alpha blending color for status

	// draw control
	bool draw_ctrl;
										// draw control
	int draw_line;
										// draw from line
	bool softkey_ctrl;
										// softkey control
#ifdef XM8_ENABLE_RETROACHIEVEMENTS
	bool ra_status_active;
										// RA owns complete status line
#endif

	// drive status
	drive_info drive_status[MAX_DRIVE];
										// drive status

	// frame rate
	Uint32 frame_rate[2];
										// frame rate (now & prev)

	// system info
	Uint32 system_info[2];
										// system information (now & prev)

	// full speed
	bool full_speed[2];
										// full speed (now & prev)

	// power down
	bool power_down[2];
										// power down (now & prev)

	// object (SDL)
	SDL_Window *window;
										// window
	SDL_Renderer *renderer;
										// renderer
	SDL_Texture *draw_texture;
										// drawing texture
	SDL_Texture *menu_texture;
										// menu texture
	SDL_Texture *softkey_texture;
										// softkey texture
	SDL_Texture *status_texture;
										// status texture
};

#endif // VIDEO_H

#endif // SDL
