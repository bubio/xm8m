//
// eXcellent Multi-Platform emulator type 8 - 'XM8'
// based on ePC-8801MA
//
// Author (ePC-8801MA) : Takeda.Toshiya
// Author (XM8) : Tanaka.Yasushi
//
// [ menu driver ]
//

#ifdef SDL

#include "os.h"
#include "common.h"
#include "vm.h"
#include "app.h"
#include "setting.h"
#include "platform.h"
#include "audio.h"
#include "video.h"
#include "font.h"
#include "input.h"
#include "diskmgr.h"
#include "tapemgr.h"
#include "menulist.h"
#include "menuid.h"
#include "converter.h"
#ifdef __ANDROID__
#include "xm8jni.h"
#endif // __ANDROID__
#include "menu.h"

#include "clidisk.h"
#include "m3u.h"
#include "menu_file_routing.h"

#include <string>

#ifdef XM8_ENABLE_RETROACHIEVEMENTS
static_assert(MENU_RA_MIN > MENU_SCALEFILTER_MAX &&
	MENU_RA_MAX < MENU_FILE_MIN,
	"RetroAchievements menu IDs must not overlap another menu range");
static_assert(MENU_RA_HARDCORE_CONFIRM != MENU_RA &&
	MENU_RA_LOGOUT_CONFIRM != MENU_RA &&
	MENU_RA_RESET_CONFIRM != MENU_RA &&
	MENU_RA_HARDCORE_CONFIRM != MENU_RA_LOGOUT_CONFIRM &&
	MENU_RA_HARDCORE_CONFIRM != MENU_RA_RESET_CONFIRM &&
	MENU_RA_LOGOUT_CONFIRM != MENU_RA_RESET_CONFIRM,
	"RetroAchievements confirmation screens require distinct menu IDs");
#endif

//
// Menu()
// constructor
//
Menu::Menu(App *a)
{
	// save parameter
	app = a;

	// object
	platform = NULL;
	setting = NULL;
	audio = NULL;
	video = NULL;
	input = NULL;
	list = NULL;
	diskmgr = NULL;
	tapemgr = NULL;
	converter = NULL;

	// top id
	top_id = MENU_BACK;

	// file selector
	file_dir[0] = '\0';
	file_target[0] = '\0';
	file_expect[0] = '\0';
	file_id = MENU_BACK;

	// softkey
	softkey_id = MENU_INPUT_SOFTKEY1;

	// joystick to keyboard
	joymap_id = MENU_JOYMAP_DPAD_UP;

	// state menu parent
	ra_state_menu = false;
	ra_hardcore_debug_state_menu = false;
	ra_reset_confirmation_enables_mode = false;
	drive_menu_refresh_pending = false;
}

//
// ~Menu()
// destructor
//
Menu::~Menu()
{
	Deinit();
}

//
// Init()
// initialize
//
bool Menu::Init()
{
	// object
	platform = app->GetPlatform();
	setting = app->GetSetting();
	audio = app->GetAudio();
	video = app->GetVideo();
	input = app->GetInput();
	converter = app->GetConverter();

	// create menu list
	list = new MenuList(app);
	if (list->Init() == false) {
		return false;
	}

	return true;
}

//
// Deinit()
// deinitialize
//
void Menu::Deinit()
{
	if (list != NULL) {
		list->Deinit();
		delete list;
		list = NULL;
	}
}

//
// EnterMenu()
// enter menu
//
void Menu::EnterMenu(int menu_id)
{
	// get disk manager if NULL
	if (diskmgr == NULL) {
		diskmgr = app->GetDiskManager();
	}

	// get tape manager if NULL
	if (tapemgr == NULL) {
		tapemgr = app->GetTapeManager();
	}

	// save menu id
	top_id = menu_id;

	// initialize softkey id
	softkey_id = MENU_INPUT_SOFTKEY1;

	// initialize joymap id
	joymap_id = MENU_JOYMAP_DPAD_UP;

	// inform list of starting menu
	list->EnterMenu();

	// each menu
	switch (menu_id) {
	// main menu
	case MENU_MAIN:
		EnterMain(MENU_MAIN_DRIVE1);
		break;

	// quit menu
	case MENU_QUIT:
		EnterQuit();
		break;

	// others
	default:
		break;
	}
}

//
// UpdateMenu()
// update main menu
//
void Menu::UpdateMenu()
{
	// valid menu ?
	if (list == NULL) {
		return;
	}
	// main menu ?
	if (list->GetID() != MENU_MAIN) {
		return;
	}

#ifndef __ANDROID__
	// screen
	if (app->IsFullScreen() == true) {
		list->SetText(MENU_MAIN_SCREEN, "Window Screen");
	}
	else {
		list->SetText(MENU_MAIN_SCREEN, "Full Screen");
	}
#endif // !__ANDROID__

	// speed
	if (app->IsFullSpeed() == true) {
		list->SetText(MENU_MAIN_SPEED, "Normal Speed");
	}
	else {
		list->SetText(MENU_MAIN_SPEED, "Full Speed");
	}
}

void Menu::RequestDriveMenuRefresh()
{
	drive_menu_refresh_pending = true;
}

//
// ProcessMenu()
// process menu
//
void Menu::RefreshPendingDriveMenu()
{
	if (drive_menu_refresh_pending) {
		drive_menu_refresh_pending = false;
		if (list->GetID() == MENU_DRIVE1) {
			EnterDrive1(MENU_BACK);
		}
		else if (list->GetID() == MENU_DRIVE2) {
			EnterDrive2(MENU_BACK);
		}
	}
}

void Menu::ProcessMenu()
{
	RefreshPendingDriveMenu();
	if (list->GetID() == MENU_JOYTEST) {
		// joystick is now testing, do not affect menu operation
		list->ProcessMenu(false);
	}
	else {
		// normal
		list->ProcessMenu(true);
	}

#ifdef XM8_ENABLE_RETROACHIEVEMENTS
	const int focus = list->GetFocusID();
	switch (list->GetID()) {
	case MENU_RA_LIBRARY_VIEW:
		if (focus >= MENU_RA_LIBRARY_ITEM_MIN &&
			focus < MENU_RA_ACHIEVEMENT_ITEM_MIN) {
			app->SelectRaLibraryMenuItemById(
				static_cast<int64_t>(list->GetUser(focus)));
			app->SetRaMenuFirstVisibleItem(
				static_cast<size_t>(list->GetTop()));
		}
		break;
	case MENU_RA_ACHIEVEMENTS_VIEW:
		if (focus >= MENU_RA_ACHIEVEMENT_ITEM_MIN &&
			focus < MENU_RA_LEADERBOARD_ITEM_MIN) {
			app->SelectRaAchievementMenuItemById(
				static_cast<uint32_t>(list->GetUser(focus)));
			app->SetRaMenuFirstVisibleItem(
				static_cast<size_t>(list->GetTop()));
		}
		break;
	case MENU_RA_LEADERBOARDS_VIEW:
		if (focus >= MENU_RA_LEADERBOARD_ITEM_MIN &&
			focus < MENU_RA_DETAIL_LINE_MIN) {
			app->SelectRaLeaderboardMenuItemById(
				static_cast<uint32_t>(list->GetUser(focus)));
			app->SetRaMenuFirstVisibleItem(
				static_cast<size_t>(list->GetTop()));
		}
		break;
	case MENU_RA_ACHIEVEMENT_DETAIL:
		if (focus >= MENU_RA_DETAIL_LINE_MIN) {
			app->SetRaAchievementMenuDetailScroll(
				focus - MENU_RA_DETAIL_LINE_MIN);
		}
		break;
	default:
		break;
	}
#endif
}

//
// EnterMain()
// enter main menu
//
void Menu::EnterMain(int id)
{
	char title[80];
	const char *ra_status = "RA OFF";
	bool show_ra_status = true;
	Uint32 ver;

#ifdef XM8_ENABLE_RETROACHIEVEMENTS
	if (!app->IsRaRuntimeSupported()) {
		show_ra_status = false;
	}
	else if (app->IsRaModeEnabled()) {
		if (app->IsRaOfflineActive()) ra_status = "RA OFFLINE";
		else if (app->IsRaHardcoreActive()) ra_status = "RA HARD";
		else if (app->IsRaCasualActive()) ra_status = "RA SOFT";
		else ra_status = "RA ON";
	}
#endif

	// get version
	ver = app->GetAppVersion();
	if (show_ra_status) {
		snprintf(title, sizeof(title), "<< XM8 Ver %1d.%1d%1d :  [%s] >>",
			((ver >> 8) & 0x0f),
			((ver >> 4) & 0x0f),
			ver & 0x0f,
			ra_status);
	}
	else {
		snprintf(title, sizeof(title), "<< XM8 Ver %1d.%1d%1d >>",
			((ver >> 8) & 0x0f),
			((ver >> 4) & 0x0f),
			ver & 0x0f);
	}

	list->SetTitle(title, MENU_MAIN);
	list->AddButton("Drive 1", MENU_MAIN_DRIVE1);
	list->AddButton("Drive 2", MENU_MAIN_DRIVE2);
	list->AddButton("CMT", MENU_MAIN_CMT);
#ifdef XM8_ENABLE_RETROACHIEVEMENTS
	if (!app->IsRaModeEnabled()) {
#endif
		list->AddButton("Load State", MENU_MAIN_LOAD);
		list->AddButton("Save State", MENU_MAIN_SAVE);
#ifdef XM8_ENABLE_RETROACHIEVEMENTS
	}
#endif
	list->AddButton("System Options", MENU_MAIN_SYSTEM);
	list->AddButton("Video Options", MENU_MAIN_VIDEO);
	list->AddButton("Audio Options", MENU_MAIN_AUDIO);
	list->AddButton("Audio Output Device", MENU_MAIN_AUDIO_OUT);
	list->AddButton("Input Options", MENU_MAIN_INPUT);
#ifdef XM8_ENABLE_RETROACHIEVEMENTS
	if (app->IsRaRuntimeSupported()) {
		list->AddButton("RetroAchievements", MENU_MAIN_RA);
	}
#endif

#ifndef __ANDROID__
	// screen
	if (app->IsFullScreen() == true) {
		list->AddButton("Window Screen", MENU_MAIN_SCREEN);
	}
	else {
		list->AddButton("Full Screen", MENU_MAIN_SCREEN);
	}
#endif // !__ANDROID__

	// speed
	if (app->IsFullSpeed() == true) {
		list->AddButton("Normal Speed", MENU_MAIN_SPEED);
	}
	else {
		list->AddButton("Full Speed", MENU_MAIN_SPEED);
	}

	list->AddButton("Reset", MENU_MAIN_RESET);
	list->AddButton("Quit XM8", MENU_MAIN_QUIT);

	// set focus
	list->SetFocus(id);
}

//
// EnterDrive1()
// enter drive1 menu
//
void Menu::EnterDrive1(int id)
{
	int banks;
	int loop;
	char expect[_MAX_PATH * 3];

	list->SetTitle("<< Drive 1 >>", MENU_DRIVE1);

	if (diskmgr[0]->IsOpen() == true) {
		// disk banks
		banks = diskmgr[0]->GetBanks();
		for (loop=0; loop<banks; loop++) {
#if defined(_WIN32) && defined(UNICODE)
			const char* disk_name = diskmgr[0]->GetName(loop);
			list->AddButton(disk_name, MENU_DRIVE1_BANK0 + loop);
#else
			// To convert from UTF to SJIS at display time, keep internally in UTF.
			const char* disk_name = diskmgr[0]->GetName(loop);
			char* disk_name_utf = (char*)SDL_malloc(strlen(disk_name) * 3 + 1);
			converter->SjisToUtf(disk_name, disk_name_utf);
			list->AddButton(disk_name_utf, MENU_DRIVE1_BANK0 + loop);
			SDL_free(disk_name_utf);
#endif
		}

		// current bank -> focus
		if (id == MENU_BACK) {
			id = MENU_DRIVE1_BANK0 + diskmgr[0]->GetBank();
		}
	}

	// open
	list->AddButton("(Open)", MENU_DRIVE1_OPEN);
	list->AddButton("(Open 1 & 2)", MENU_DRIVE1_BOTH);

	// eject
	if (diskmgr[0]->IsOpen() == true) {
		list->AddButton("(Eject)", MENU_DRIVE1_EJECT);
	}

	// set focus
	if (id == MENU_BACK) {
		id = MENU_DRIVE1_OPEN;
	}
	list->SetFocus(id);

	// file_expect
	strcpy(expect, diskmgr[0]->GetFileName());
#if defined(_WIN32) && defined(UNICODE)
	converter->UtfToSjis(expect, file_expect);
#else
	strcpy(file_expect, expect);
#endif
}

//
// EnterDrive2()
// enter drive2 menu
//
void Menu::EnterDrive2(int id)
{
	int banks;
	int loop;
	char expect[_MAX_PATH * 3];

	list->SetTitle("<< Drive 2 >>", MENU_DRIVE2);

	if (diskmgr[1]->IsOpen() == true) {
		// disk banks
		banks = diskmgr[1]->GetBanks();
		for (loop=0; loop<banks; loop++) {
#if defined(_WIN32) && defined(UNICODE)
			const char* disk_name = diskmgr[1]->GetName(loop);
			list->AddButton(disk_name, MENU_DRIVE2_BANK0 + loop);
#else
			// To convert from UTF to SJIS at display time, keep internally in UTF.
			const char* disk_name = diskmgr[1]->GetName(loop);
			char* disk_name_utf = (char*)SDL_malloc(strlen(disk_name) * 3 + 1);
			converter->SjisToUtf(disk_name, disk_name_utf);
			list->AddButton(disk_name_utf, MENU_DRIVE2_BANK0 + loop);
			SDL_free(disk_name_utf);
#endif
		}

		// current bank -> focus
		if (id == MENU_BACK) {
			id = MENU_DRIVE2_BANK0 + diskmgr[1]->GetBank();
		}
	}

	// open
	list->AddButton("(Open)", MENU_DRIVE2_OPEN);
	list->AddButton("(Open 1 & 2)", MENU_DRIVE2_BOTH);

	// eject
	if (diskmgr[1]->IsOpen() == true) {
		list->AddButton("(Eject)", MENU_DRIVE2_EJECT);
	}

	// set focus
	if (id == MENU_BACK) {
		id = MENU_DRIVE2_OPEN;
	}
	list->SetFocus(id);

	// file_expect
	strcpy(expect, diskmgr[1]->GetFileName());
#if defined(_WIN32) && defined(UNICODE)
	converter->UtfToSjis(expect, file_expect);
#else
	strcpy(file_expect, expect);
#endif

}

//
// EnterCmt()
// enter cmt menu
//
void Menu::EnterCmt(int id)
{
	char expect[_MAX_PATH * 3];

	list->SetTitle("<< CMT >>", MENU_CMT);

	// play and rec
	list->AddButton("Play", MENU_CMT_PLAY);
	list->AddButton("Rec", MENU_CMT_REC);

	// eject
	if ((tapemgr->IsPlay() == true) || (tapemgr->IsRec() == true)) {
		list->AddButton("Eject", MENU_CMT_EJECT);

		// eject -> focus
		if (id == MENU_BACK) {
			id = MENU_CMT_EJECT;
		}
	}

	// set focus
	if (id == MENU_BACK) {
		id = MENU_CMT_PLAY;
	}
	list->SetFocus(id);

	// file_expect
	strcpy(expect, tapemgr->GetFileName());
#if defined(_WIN32) && defined(UNICODE)
	converter->UtfToSjis(expect, file_expect);
#else
	strcpy(file_expect, expect);
#endif
}

//
// EnterLoad()
// enter load menu
//
void Menu::EnterLoad(bool ra_state, bool hardcore_debug)
{
	int id;
	int last;
	int slot;
	cur_time_t ct;
	char textbuf[64];
	char timebuf[64];

	ra_state_menu = ra_state;
	ra_hardcore_debug_state_menu = hardcore_debug;
	list->SetTitle(hardcore_debug ? "<< Load Hardcore Debug State >>" :
		(ra_state ? "<< RA Load State >>" : "<< Load State >>"),
		MENU_LOAD);

	// default focus
	id = MENU_LOAD_0;

	for (slot=0; slot<10; slot++) {
		if (slot == 0) {
			strcpy(textbuf, ra_state ? "Slot 0       " : "Slot 0 (AUTO)");
		}
		else {
			sprintf(textbuf, "Slot %d       ", slot);
		}
		bool has_time = app->GetStateTime(slot, &ct);
#ifdef XM8_ENABLE_RETROACHIEVEMENTS
		if (hardcore_debug) has_time = app->GetRaDebugStateTime(slot, &ct);
#endif
		if (has_time) {
			sprintf(timebuf, "%02d-%02d-%02d %02d:%02d",
				ct.year % 100,
				ct.month,
				ct.day,
				ct.hour,
				ct.minute);
			strcat(textbuf, timebuf);
		}

		list->AddButton(textbuf, MENU_LOAD_0 + slot);
	}

	// set focus
	last = setting->GetStateNum();
	if ((last >= 0) && (last <= 9)) {
		id = MENU_LOAD_0 + last;
	}
	list->SetFocus(id);
}

//
// EnterSave()
// enter save menu
//
void Menu::EnterSave(bool ra_state, bool hardcore_debug)
{
	int id;
	int last;
	int slot;
	cur_time_t ct;
	char textbuf[64];
	char timebuf[64];

	ra_state_menu = ra_state;
	ra_hardcore_debug_state_menu = hardcore_debug;
	list->SetTitle(hardcore_debug ? "<< Save Hardcore Debug State >>" :
		(ra_state ? "<< RA Save State >>" : "<< Save State >>"),
		MENU_SAVE);

	// default focus
	id = MENU_SAVE_0;

	for (slot=0; slot<10; slot++) {
		if (slot == 0) {
			strcpy(textbuf, ra_state ? "Slot 0       " : "Slot 0 (AUTO)");
		}
		else {
			sprintf(textbuf, "Slot %d       ", slot);
		}
		bool has_time = app->GetStateTime(slot, &ct);
#ifdef XM8_ENABLE_RETROACHIEVEMENTS
		if (hardcore_debug) has_time = app->GetRaDebugStateTime(slot, &ct);
#endif
		if (has_time) {
			sprintf(timebuf, "%02d-%02d-%02d %02d:%02d",
				ct.year % 100,
				ct.month,
				ct.day,
				ct.hour,
				ct.minute);
			strcat(textbuf, timebuf);
		}

		list->AddButton(textbuf, MENU_SAVE_0 + slot);
	}

	// set focus
	last = setting->GetStateNum();
	if ((last >= 0) && (last <= 9)) {
		id = MENU_SAVE_0 + last;
	}
	list->SetFocus(id);
}

//
// EnterSystem()
// enter system menu
//
void Menu::EnterSystem(int id)
{
	Uint8 ver[3];
	Font *font;
	bool add;

	list->SetTitle("<< System Options >>", MENU_SYSTEM);

	list->AddRadioButton("V1S mode (w/reset)", MENU_SYSTEM_V1S, MENU_SYSTEM_MODE);
	list->AddRadioButton("V1H mode (w/reset)", MENU_SYSTEM_V1H, MENU_SYSTEM_MODE);
	list->AddRadioButton("V2  mode (w/reset)", MENU_SYSTEM_V2, MENU_SYSTEM_MODE);
	list->AddRadioButton("N   mode (w/reset)", MENU_SYSTEM_N, MENU_SYSTEM_MODE);
	list->AddRadioButton("Clock 4MHz  (w/reset)", MENU_SYSTEM_4M, MENU_SYSTEM_CLOCK);
	list->AddRadioButton("Clock 8MHz  (w/reset)", MENU_SYSTEM_8M, MENU_SYSTEM_CLOCK);
	list->AddRadioButton("Clock 8MHzH (FE2/MC,w/reset)", MENU_SYSTEM_8MH, MENU_SYSTEM_CLOCK);
	list->AddCheckButton("128KB RAM board (Mx,w/reset)", MENU_SYSTEM_EXRAM);
	list->AddCheckButton("Pseudo fast disk access", MENU_SYSTEM_FASTDISK);
	list->AddCheckButton("Watch battery level", MENU_SYSTEM_BATTERY);
	list->AddButton("DIP settings (w/reset)", MENU_SYSTEM_DIP);

	// get rom version
	font = app->GetFont();
	ver[0] = font->GetROMVersion(1);
	ver[1] = font->GetROMVersion(2);
	ver[2] = font->GetROMVersion(3);
	add = false;

	if ((ver[0] < 0x32) && (add == false)) {
		list->AddButton("ROM: PC-8801", MENU_SYSTEM_ROMVER);
		add = true;
	}
	if ((ver[0] == 0x33) && (add == false)) {
		list->AddButton("ROM: PC-8801mkII", MENU_SYSTEM_ROMVER);
		add = true;
	}
	if ((ver[0] == 0x34) && (add == false)) {
		list->AddButton("ROM: PC-8801mkIISR/TR", MENU_SYSTEM_ROMVER);
		add = true;
	}
	if ((ver[0] < 0x38) && (add == false)) {
		if (ver[2] == 0xfe) {
			list->AddButton("ROM: PC-8801mkIIMR", MENU_SYSTEM_ROMVER);
		}
		else {
			list->AddButton("ROM: PC-8801mkIIFR", MENU_SYSTEM_ROMVER);
		}
		add = true;
	}
	if ((ver[0] == 0x38) && (add == false)) {
		if (ver[2] == 0xfe) {
			list->AddButton("ROM: PC-8801MH", MENU_SYSTEM_ROMVER);
		}
		else {
			list->AddButton("ROM: PC-8801FH", MENU_SYSTEM_ROMVER);
		}
		add = true;
	}
	if ((ver[1] < 0x31) && (add == false)) {
		if (ver[2] == 0xfe) {
			list->AddButton("ROM: PC-8801MA", MENU_SYSTEM_ROMVER);
		}
		else {
			list->AddButton("ROM: PC-8801FA", MENU_SYSTEM_ROMVER);
		}
		add = true;
	}
	if ((ver[1] == 0x31) && (add == false)) {
		if (ver[2] == 0xfe) {
			list->AddButton("ROM: PC-8801MA2", MENU_SYSTEM_ROMVER);
		}
		else {
			list->AddButton("ROM: PC-8801FE", MENU_SYSTEM_ROMVER);
		}
		add = true;
	}
	if ((ver[1] == 0x32) && (add == false)) {
		list->AddButton("ROM: PC-98DO", MENU_SYSTEM_ROMVER);
		add = true;
	}
	if ((ver[1] == 0x33) && (add == false)) {
		if (ver[2] == 0xfe) {
			list->AddButton("ROM: PC-8801MC", MENU_SYSTEM_ROMVER);
		}
		else {
			list->AddButton("ROM: PC-8801FE2", MENU_SYSTEM_ROMVER);
		}
		add = true;
	}
	if ((ver[1] > 0x33) && (add == false)) {
		list->AddButton("ROM: PC-98DO+", MENU_SYSTEM_ROMVER);
		add = true;
	}

#ifdef __ANDROID__
	if (Android_GetSdkVersion() >= 21) {
		// Android 5.0 or later
		if (Android_HasExternalSD() != 0) {
			// detect external SD card (supports one card only)
			list->AddCheckButton("Use ext.SD (on next launch)", MENU_ANDROID_SAF);
		}
	}
#endif // __ANDROID__

	// mode
	switch (setting->GetSystemMode()) {
	case SETTING_V1S_MODE:
		list->SetRadio(MENU_SYSTEM_V1S, MENU_SYSTEM_MODE);
		break;
	case SETTING_V1H_MODE:
		list->SetRadio(MENU_SYSTEM_V1H, MENU_SYSTEM_MODE);
		if (id == MENU_SYSTEM_V1S) {
			id = MENU_SYSTEM_V1H;
		}
		break;
	case SETTING_V2_MODE:
		list->SetRadio(MENU_SYSTEM_V2, MENU_SYSTEM_MODE);
		if (id == MENU_SYSTEM_V1S) {
			id = MENU_SYSTEM_V2;
		}
		break;
	case SETTING_N_MODE:
		list->SetRadio(MENU_SYSTEM_N, MENU_SYSTEM_MODE);
		if (id == MENU_SYSTEM_V1S) {
			id = MENU_SYSTEM_N;
		}
		break;
	default:
		break;
	}

	// clock
	if (setting->GetCPUClock() == 4) {
		list->SetRadio(MENU_SYSTEM_4M, MENU_SYSTEM_CLOCK);
	}
	else {
		if (setting->Is8HMode() == true) {
			list->SetRadio(MENU_SYSTEM_8MH, MENU_SYSTEM_CLOCK);
		}
		else {
			list->SetRadio(MENU_SYSTEM_8M, MENU_SYSTEM_CLOCK);
		}
	}

	// 128KB ram
	list->SetCheck(MENU_SYSTEM_EXRAM, setting->HasExRAM());

	// pseudo fast disk mode
	list->SetCheck(MENU_SYSTEM_FASTDISK, setting->IsFastDisk());

	// watch battery
	list->SetCheck(MENU_SYSTEM_BATTERY, setting->IsWatchBattery());

#ifdef __ANDROID__
	// use external SD
	if (Android_GetSdkVersion() >= 21) {
		// Android 5.0 or later
		if (Android_HasExternalSD() != 0) {
			if (Android_HasTreeUri() != 0) {
				list->SetCheck(MENU_ANDROID_SAF, true);
			}
			else {
				list->SetCheck(MENU_ANDROID_SAF, false);
			}
		}
	}
#endif // __ANDROID__

	// set focus
	list->SetFocus(id);
}

#ifdef XM8_ENABLE_RETROACHIEVEMENTS
//
// EnterRa()
// enter RetroAchievements menu
//
void Menu::EnterRa(int id)
{
	app->CloseRaMenuContent();
	list->SetTitle("<< RetroAchievements >>", MENU_RA);
	if (!app->IsRaRuntimeSupported()) {
		list->AddButton("Android 6.0 or later required", MENU_RA_STATUS);
		list->AddButton("Go to RetroAchievements Site", MENU_RA_WEBSITE);
		list->AddButton("Privacy Policy", MENU_RA_PRIVACY);
		if (id == MENU_BACK) id = MENU_RA_STATUS;
		list->SetFocus(id);
		return;
	}

	list->AddButton("Login", MENU_RA_LOGIN);
	list->AddButton("RA: disabled", MENU_RA_STATUS);
	list->AddButton("Now: -", MENU_RA_PRESENCE);
	list->AddCheckButton("RA mode", MENU_RA_MODE);
	list->AddCheckButton("Hardcore", MENU_RA_HARDCORE);
	list->AddButton("Library", MENU_RA_LIBRARY);
	list->AddButton("Achievements", MENU_RA_ACHIEVEMENTS);
	list->AddButton("Leaderboards", MENU_RA_LEADERBOARDS);
	if (app->IsRaModeEnabled()) {
		if (app->IsRaHardcoreActive()) {
			list->AddButton("Save Debug State", MENU_RA_SAVE);
		}
		else if (app->IsRaCasualActive()) {
			list->AddButton("Load State", MENU_RA_LOAD);
			list->AddButton("Save State", MENU_RA_SAVE);
			list->AddButton("Load Hardcore Debug State",
				MENU_RA_LOAD_HARDCORE_DEBUG);
		}
		else if (app->IsRaOfflineActive()) {
			list->AddButton("Load State", MENU_RA_LOAD);
			list->AddButton("Save State", MENU_RA_SAVE);
		}
	}
	list->AddButton("Go to RetroAchievements Site", MENU_RA_WEBSITE);
	list->AddButton("Privacy Policy", MENU_RA_PRIVACY);

	list->SetCheck(MENU_RA_MODE, app->IsRaModeEnabled());
	list->SetCheck(MENU_RA_HARDCORE, app->IsRaHardcoreSelected());
	UpdateRaStatus();

	if (id == MENU_BACK) {
		id = MENU_RA_LOGIN;
	}
	list->SetFocus(id);
}

void Menu::EnterRaLibrary(int focus)
{
	app->ShowRaLibraryMenu();
	const Xm8Ra::RaOverlayLibraryListSnapshot snapshot =
		app->GetRaLibraryMenuSnapshot();
	list->SetTitle("<< RA Library >>", MENU_RA_LIBRARY_VIEW);
	for (size_t index = 0; index < snapshot.games.size() &&
		index < static_cast<size_t>(MENU_RA_ACHIEVEMENT_ITEM_MIN -
			MENU_RA_LIBRARY_ITEM_MIN); ++index) {
		const int id = MENU_RA_LIBRARY_ITEM_MIN + static_cast<int>(index);
		list->AddButton(snapshot.games[index].title.c_str(), id);
		list->SetUser(id, static_cast<Uint64>(snapshot.games[index].game_id));
	}
	if (snapshot.games.empty()) list->AddButton("No library games", MENU_RA_LIBRARY_ITEM_MIN);
	list->SetFocus(MENU_RA_LIBRARY_ITEM_MIN + focus);
}

void Menu::EnterRaGameDetail()
{
	list->SetTitle("<< RA Game Detail >>", MENU_RA_GAME_DETAIL);
	list->AddButton("START", MENU_RA_GAME_START);
	list->SetFocus(MENU_RA_GAME_START);
}

void Menu::EnterRaAchievements(int focus)
{
	app->ShowRaAchievementsMenu();
	const Xm8Ra::RaOverlayAchievementListSnapshot snapshot =
		app->GetRaAchievementsMenuSnapshot();
	list->SetTitle("<< RA Achievements >>", MENU_RA_ACHIEVEMENTS_VIEW);
	for (size_t index = 0; index < snapshot.achievements.size() &&
		index < static_cast<size_t>(MENU_RA_LEADERBOARD_ITEM_MIN -
			MENU_RA_ACHIEVEMENT_ITEM_MIN); ++index) {
		const int id = MENU_RA_ACHIEVEMENT_ITEM_MIN + static_cast<int>(index);
		list->AddButton(snapshot.achievements[index].title.c_str(), id);
		list->SetUser(id, static_cast<Uint64>(snapshot.achievements[index].id));
	}
	if (snapshot.achievements.empty()) list->AddButton("No achievements", MENU_RA_ACHIEVEMENT_ITEM_MIN);
	list->SetFocus(MENU_RA_ACHIEVEMENT_ITEM_MIN + focus);
}

void Menu::EnterRaAchievementDetail()
{
	list->SetTitle("<< RA Achievement Detail >>", MENU_RA_ACHIEVEMENT_DETAIL);
	// MenuList owns scrolling; the rows are intentionally inert content lines.
	for (int index = 0; index < 24; ++index) {
		char line[32];
		std::snprintf(line, sizeof(line), "Detail %d", index + 1);
		list->AddButton(line, MENU_RA_DETAIL_LINE_MIN + index);
	}
	list->SetFocus(MENU_RA_DETAIL_LINE_MIN);
}

void Menu::EnterRaLeaderboards(int focus)
{
	app->ShowRaLeaderboardsMenu();
	const Xm8Ra::RaOverlayLeaderboardListSnapshot snapshot =
		app->GetRaLeaderboardsMenuSnapshot();
	list->SetTitle("<< RA Leaderboards >>", MENU_RA_LEADERBOARDS_VIEW);
	for (size_t index = 0; index < snapshot.leaderboards.size() &&
		index < static_cast<size_t>(MENU_RA_DETAIL_LINE_MIN -
			MENU_RA_LEADERBOARD_ITEM_MIN); ++index) {
		const int id = MENU_RA_LEADERBOARD_ITEM_MIN + static_cast<int>(index);
		list->AddButton(snapshot.leaderboards[index].title.c_str(), id);
		list->SetUser(id, static_cast<Uint64>(snapshot.leaderboards[index].id));
	}
	if (snapshot.leaderboards.empty()) list->AddButton("No leaderboards", MENU_RA_LEADERBOARD_ITEM_MIN);
	list->SetFocus(MENU_RA_LEADERBOARD_ITEM_MIN + focus);
}

bool Menu::IsRaContentMenu() const
{
	const int id = list == NULL ? -1 : list->GetID();
	return id == MENU_RA_LIBRARY_VIEW || id == MENU_RA_GAME_DETAIL ||
		id == MENU_RA_ACHIEVEMENTS_VIEW || id == MENU_RA_ACHIEVEMENT_DETAIL ||
		id == MENU_RA_LEADERBOARDS_VIEW;
}

bool Menu::IsRaGameDetailMenu() const
{
	return list != NULL && list->GetID() == MENU_RA_GAME_DETAIL;
}

int Menu::GetRaContentSelection() const
{
	return list == NULL ? MENU_BACK : list->GetFocusID();
}

void Menu::EnterRaHardcoreConfirmation()
{
	list->SetTitle("<< End Hardcore Session? >>", MENU_RA_HARDCORE_CONFIRM);
	list->AddButton("Yes (Switch to Casual)", MENU_RA_HARDCORE_YES);
	list->AddButton("No", MENU_RA_HARDCORE_NO);
	list->SetFocus(MENU_RA_HARDCORE_NO);
}

void Menu::EnterRaResetConfirmation(bool enable_ra_mode)
{
	ra_reset_confirmation_enables_mode = enable_ra_mode;
	list->SetTitle(enable_ra_mode ?
		"<< Enable RA Mode in Hardcore? >>" :
		"<< Switch to Hardcore? >>", MENU_RA_RESET_CONFIRM);
	list->AddButton(enable_ra_mode ?
		"Yes (Reset and Enable)" :
		"Yes (Reset and Switch)", MENU_RA_RESET_YES);
	list->AddButton("No", MENU_RA_RESET_NO);
	list->SetFocus(MENU_RA_RESET_NO);
}

void Menu::EnterRaLogoutConfirmation(size_t pending_count)
{
	char title[96];
	snprintf(title, sizeof(title), "<< Delete %zu Pending Unlock(s)? >>",
		pending_count);
	list->SetTitle(title, MENU_RA_LOGOUT_CONFIRM);
	list->AddButton("Yes (Delete and Logout)", MENU_RA_LOGOUT_YES);
	list->AddButton("No", MENU_RA_LOGOUT_NO);
	list->SetFocus(MENU_RA_LOGOUT_NO);
}

//
// UpdateRaStatus()
// update RetroAchievements status rows
//
void Menu::UpdateRaStatus()
{
	if (list->GetID() != MENU_RA) {
		return;
	}

	char ra_status[96];
	app->GetRaMenuStatus(ra_status, sizeof(ra_status));
	list->SetText(MENU_RA_STATUS, ra_status);
	if (!app->IsRaRuntimeSupported()) return;
	char ra_presence[256];
	app->GetRaMenuPresence(ra_presence, sizeof(ra_presence));
	list->SetText(MENU_RA_PRESENCE, ra_presence);
	list->SetCheck(MENU_RA_MODE, app->IsRaModeEnabled());
	list->SetCheck(MENU_RA_HARDCORE, app->IsRaHardcoreSelected());
	list->SetText(MENU_RA_LOGIN,
		app->IsRaLoggedIn() ? "Logout" : "Login");
}

//
// IsRaRichPresenceFocused()
// check whether the RA menu Rich Presence row has focus
//
bool Menu::IsRaRichPresenceFocused()
{
	return list != NULL && list->GetID() == MENU_RA &&
		list->GetFocusID() == MENU_RA_PRESENCE;
}

//
// IsRaStatusFocused()
// check whether the RA status row has focus
//
bool Menu::IsRaStatusFocused()
{
	return list != NULL && list->GetID() == MENU_RA &&
		list->GetFocusID() == MENU_RA_STATUS;
}

#endif

//
// EnterVideo()
// enter video menu
//
void Menu::EnterVideo(int id)
{
	list->SetTitle("<< Video Options >>", MENU_VIDEO);

#ifndef __ANDROID__
	// window size
	list->AddRadioButton("Window x1.0 (640x400)", MENU_VIDEO_640, MENU_VIDEO_WINDOW);
	list->AddRadioButton("Window x1.5 (960x600)", MENU_VIDEO_960, MENU_VIDEO_WINDOW);
	list->AddRadioButton("Window x2.0 (1280x800)", MENU_VIDEO_1280, MENU_VIDEO_WINDOW);
	list->AddRadioButton("Window x2.5 (1600x1000)", MENU_VIDEO_1600, MENU_VIDEO_WINDOW);
	list->AddRadioButton("Window x3.0 (1920x1200)", MENU_VIDEO_1920, MENU_VIDEO_WINDOW);
#endif // !__ANDROID__

	// skip frame
	list->AddRadioButton("No frame skip", MENU_VIDEO_SKIP0, MENU_VIDEO_SKIP);
	list->AddRadioButton("1  frame skip", MENU_VIDEO_SKIP1, MENU_VIDEO_SKIP);
	list->AddRadioButton("2  frame skip", MENU_VIDEO_SKIP2, MENU_VIDEO_SKIP);
	list->AddRadioButton("3  frame skip", MENU_VIDEO_SKIP3, MENU_VIDEO_SKIP);

	// monitor type
	list->AddRadioButton("15kHz monitor (w/reset)", MENU_VIDEO_15K, MENU_VIDEO_MONITOR);
	list->AddRadioButton("24kHz monitor (w/reset)", MENU_VIDEO_24K, MENU_VIDEO_MONITOR);

	// scan line
	list->AddCheckButton("Scan line", MENU_VIDEO_SCANLINE);
	list->AddSlider("Brightness", MENU_VIDEO_BRIGHTNESS, 0x40, 0xff, 1);

	// status line
	list->AddCheckButton("Status area", MENU_VIDEO_STATUSCHK);
	list->AddSlider("Status transparency", MENU_VIDEO_STATUSALPHA, 0, 0xff, 1);

	// scaling filter
	list->AddButton("Scaling filter", MENU_VIDEO_SCALEFILTER);

#ifdef __ANDROID__
	// force RGB565
	list->AddCheckButton("RGB565 (for Samsung Galaxy)", MENU_VIDEO_FORCERGB565);

	// rotation mode
	list->AddRadioButton("Rotation: Auto", MENU_VIDEO_ROTATE_AUTO, MENU_VIDEO_ROTATION);
	list->AddRadioButton("Rotation: Landscape", MENU_VIDEO_ROTATE_LANDSCAPE, MENU_VIDEO_ROTATION);
	list->AddRadioButton("Rotation: Portrait", MENU_VIDEO_ROTATE_PORTRAIT, MENU_VIDEO_ROTATION);
#endif // __ANDROID__

	// skip frame
	switch (setting->GetSkipFrame()) {
	case 0:
		list->SetRadio(MENU_VIDEO_SKIP0, MENU_VIDEO_SKIP);
		break;
	case 1:
		list->SetRadio(MENU_VIDEO_SKIP1, MENU_VIDEO_SKIP);
		break;
	case 2:
		list->SetRadio(MENU_VIDEO_SKIP2, MENU_VIDEO_SKIP);
		break;
	case 3:
		list->SetRadio(MENU_VIDEO_SKIP3, MENU_VIDEO_SKIP);
		break;
	default:
		break;
	}

#ifndef __ANDROID__
	// window size
	switch (setting->GetWindowWidth()) {
	case 640:
		list->SetRadio(MENU_VIDEO_640, MENU_VIDEO_WINDOW);
		break;
	case 960:
		list->SetRadio(MENU_VIDEO_960, MENU_VIDEO_WINDOW);
		break;
	case 1280:
		list->SetRadio(MENU_VIDEO_1280, MENU_VIDEO_WINDOW);
		break;
	case 1600:
		list->SetRadio(MENU_VIDEO_1600, MENU_VIDEO_WINDOW);
		break;
	case 1920:
		list->SetRadio(MENU_VIDEO_1920, MENU_VIDEO_WINDOW);
		break;
	default:
		break;
	}
#endif // !__ANDROID__

	// monitor type
	if (setting->IsLowReso() == true) {
		list->SetRadio(MENU_VIDEO_15K, MENU_VIDEO_MONITOR);
	}
	else {
		list->SetRadio(MENU_VIDEO_24K, MENU_VIDEO_MONITOR);
	}

	// scanline
	list->SetCheck(MENU_VIDEO_SCANLINE, setting->HasScanline());

	// brightness
	list->SetSlider(MENU_VIDEO_BRIGHTNESS, setting->GetBrightness());

	// status line
	list->SetCheck(MENU_VIDEO_STATUSCHK, setting->HasStatusLine());
	list->SetSlider(MENU_VIDEO_STATUSALPHA, setting->GetStatusAlpha());

#ifdef __ANDROID__
	// force RGB565
	list->SetCheck(MENU_VIDEO_FORCERGB565, setting->IsForceRGB565());

	// rotation mode
	switch (setting->GetRotationMode()) {
	case Setting::RotationLandscape:
		list->SetRadio(MENU_VIDEO_ROTATE_LANDSCAPE, MENU_VIDEO_ROTATION);
		break;
	case Setting::RotationPortrait:
		list->SetRadio(MENU_VIDEO_ROTATE_PORTRAIT, MENU_VIDEO_ROTATION);
		break;
	default:
		list->SetRadio(MENU_VIDEO_ROTATE_AUTO, MENU_VIDEO_ROTATION);
		break;
	}
#endif // __ANDROID__

	// set focus
	list->SetFocus(id);
}

//
// EnterScalingFilter()
// enter scaling filter menu
//
void Menu::EnterScalingFilter()
{
	int id;
	const char *quality;
	
	list->SetTitle("<< VIDEO SCALING FILTER >>", MENU_SCALEFILTER);


	list->AddRadioButton("Nearest", MENU_SCALEFILTER_NEAREST, MENU_VIDEO_SCALEFILTER);
	list->AddRadioButton("Linear", MENU_SCALEFILTER_LINEAR, MENU_VIDEO_SCALEFILTER);
#ifdef _WIN32
	list->AddRadioButton("Anisotropic ", MENU_SCALEFILTER_ANISOTROPIC, MENU_VIDEO_SCALEFILTER);
#endif // _WIN32
	list->AddRadioButton("xBRZ", MENU_SCALEFILTER_XBRZ, MENU_VIDEO_SCALEFILTER);

	quality = setting->GetScaleQuality();
	switch (*quality)
	{
	case '0':
		list->SetRadio(MENU_SCALEFILTER_NEAREST, MENU_VIDEO_SCALEFILTER);
		id = MENU_SCALEFILTER_NEAREST;
		break;
	
	case '1':
		list->SetRadio(MENU_SCALEFILTER_LINEAR, MENU_VIDEO_SCALEFILTER);
		id = MENU_SCALEFILTER_LINEAR;
		break;

	case '2':
		list->SetRadio(MENU_SCALEFILTER_ANISOTROPIC, MENU_VIDEO_SCALEFILTER);
		id = MENU_SCALEFILTER_ANISOTROPIC;
		break;
	case '3':
		list->SetRadio(MENU_SCALEFILTER_XBRZ, MENU_VIDEO_SCALEFILTER);
		id = MENU_SCALEFILTER_XBRZ;
		break;

	default:
		list->SetRadio(MENU_SCALEFILTER_NEAREST, MENU_VIDEO_SCALEFILTER);
		id = MENU_SCALEFILTER_NEAREST;
		break;
	}

	// set focus
	list->SetFocus(id);
}


//
// EnterAudio()
// enter audio menu
//
void Menu::EnterAudio()
{
	int id;

	list->SetTitle("<< Audio Options >>", MENU_AUDIO);

	// default focus
	id = MENU_AUDIO_44100;

	list->AddRadioButton("Freq. 44100Hz", MENU_AUDIO_44100, MENU_AUDIO_FREQ);
	list->AddRadioButton("Freq. 48000Hz", MENU_AUDIO_48000, MENU_AUDIO_FREQ);
#ifndef __ANDROID__
	list->AddRadioButton("Freq. 55467Hz", MENU_AUDIO_55467, MENU_AUDIO_FREQ);
	list->AddRadioButton("Freq. 88200Hz", MENU_AUDIO_88200, MENU_AUDIO_FREQ);
	list->AddRadioButton("Freq. 96000Hz", MENU_AUDIO_96000, MENU_AUDIO_FREQ);
#endif // !__ANDROID__
	list->AddSlider("Audio Buffer", MENU_AUDIO_BUFFER, 80, 500, 10);
	list->AddRadioButton("YM2203(OPN)  (w/reset)", MENU_AUDIO_OPN, MENU_AUDIO_SBII);
	list->AddRadioButton("YM2608(OPNA) (w/reset)", MENU_AUDIO_OPNA, MENU_AUDIO_SBII);

	// frequency
	switch (setting->GetAudioFreq()) {
	case 44100:
		list->SetRadio(MENU_AUDIO_44100, MENU_AUDIO_FREQ);
		id = MENU_AUDIO_44100;
		break;
	case 48000:
		list->SetRadio(MENU_AUDIO_48000, MENU_AUDIO_FREQ);
		id = MENU_AUDIO_48000;
		break;
#ifndef __ANDROID__
	case 55467:
		list->SetRadio(MENU_AUDIO_55467, MENU_AUDIO_FREQ);
		id = MENU_AUDIO_55467;
		break;
	case 88200:
		list->SetRadio(MENU_AUDIO_88200, MENU_AUDIO_FREQ);
		id = MENU_AUDIO_88200;
		break;
	case 96000:
		list->SetRadio(MENU_AUDIO_96000, MENU_AUDIO_FREQ);
		id = MENU_AUDIO_96000;
		break;
	default:
		break;
#endif // !__ANDROID__
	}

	// audio buffer
	list->SetSlider(MENU_AUDIO_BUFFER, setting->GetAudioBuffer());

	// sound board II
	if (setting->HasOPNA() == true) {
		list->SetRadio(MENU_AUDIO_OPNA, MENU_AUDIO_SBII);
	}
	else {
		list->SetRadio(MENU_AUDIO_OPN, MENU_AUDIO_SBII);
	}

	// set focus
	list->SetFocus(id);
}

//
// EnterAudioOut()
// enter audio out menu
//
void Menu::EnterAudioOut()
{
	int id = 0;

	list->SetTitle("<< Audio Output >>", MENU_AUDIO_OUT);

	list->AddRadioButton("SYSTEM DEFAULT", MENU_AUDIO_DEVICE_MIN, MENU_AUDIO_DEVICE);
	int device_num = audio->GetDeviceNum();
	for (int i = 0; i < device_num; i++) {
#if defined(_WIN32) && defined(UNICODE)
		const char* raw_string = audio->GetDeviceName(i);
		char* sjis = (char*)SDL_malloc(strlen(raw_string) * 3 + 1);
		if (sjis == NULL) {
			list->AddRadioButton(raw_string, MENU_AUDIO_DEVICE_MIN + i + 1, MENU_AUDIO_DEVICE);
		}
		else
		{
			converter->UtfToSjis(raw_string, sjis);
			list->AddRadioButton(sjis, MENU_AUDIO_DEVICE_MIN + i + 1, MENU_AUDIO_DEVICE);
		}
#else
		const char* raw_string = audio->GetDeviceName(i);
		list->AddRadioButton(raw_string, MENU_AUDIO_DEVICE_MIN + i + 1, MENU_AUDIO_DEVICE);
#endif
	}

	int audio_device = setting->GetAudioDevice();
	id = MENU_AUDIO_DEVICE_MIN + audio_device;
	list->SetRadio(id, MENU_AUDIO_DEVICE);

	// set focus
	list->SetFocus(id);
}

//
// EnterInput()
// enter input menu
//
void Menu::EnterInput(int id)
{
	list->SetTitle("<< Input Options >>", MENU_INPUT);

	list->AddButton("Softkey type 1", MENU_INPUT_SOFTKEY1);
	list->AddButton("Softkey type 2", MENU_INPUT_SOFTKEY2);
	list->AddButton("Softkey type 3", MENU_INPUT_SOFTKEY3);
	list->AddButton("Softkey type 4", MENU_INPUT_SOFTKEY4);
	list->AddSlider("Softkey transparency", MENU_INPUT_SOFTALPHA, 0, 0xff, 1);
	list->AddSlider("Softkey timeout", MENU_INPUT_SOFTTIME, 400, 20000, 200);
	list->AddCheckButton("Show Softkey Mouse Move", MENU_INPUT_SOFTKEY_MM);
#ifdef __ANDROID__
	list->AddCheckButton("Keyboard enable", MENU_INPUT_KEYENABLE);
#endif // __ANDROID__
	list->AddCheckButton("Joystick enable", MENU_INPUT_JOYENABLE);
	list->AddCheckButton("Joystick button swap", MENU_INPUT_JOYSWAP);
	list->AddCheckButton("Joystick to keyboard", MENU_INPUT_JOYKEY);
	list->AddButton("Joystick to keyboard map", MENU_INPUT_JOYMAP);
	list->AddCheckButton("Joystick test", MENU_INPUT_JOYTEST);
#ifndef __ANDROID__
	list->AddSlider("Mouse timeout", MENU_INPUT_MOUSETIME, 400, 20000, 200);
#endif // !__ANDROID__

	list->AddCheckButton("Cursor Key to Num Pad", MENU_INPUT_CURSOR_NUM);
	list->AddCheckButton("Num Key to Num Pad", MENU_INPUT_NUM_PAD);

	// softkey
	list->SetSlider(MENU_INPUT_SOFTALPHA, setting->GetSoftKeyAlpha());
	list->SetSlider(MENU_INPUT_SOFTTIME, setting->GetSoftKeyTime());
	list->SetCheck(MENU_INPUT_SOFTKEY_MM, setting->IsShowSoftKeyMouseMove());

#ifdef __ANDROID__
	// keyboard
	list->SetCheck(MENU_INPUT_KEYENABLE, setting->IsKeyEnable());
#endif // __ANDROID__

	// joystick
	list->SetCheck(MENU_INPUT_JOYENABLE, setting->IsJoyEnable());
	list->SetCheck(MENU_INPUT_JOYSWAP, setting->IsJoySwap());
	list->SetCheck(MENU_INPUT_JOYKEY, setting->IsJoyKey());
	list->SetCheck(MENU_INPUT_JOYTEST, setting->IsJoyEnable());

#ifndef __ANDROID__
	// mouse
	list->SetSlider(MENU_INPUT_MOUSETIME, setting->GetMouseTime());
#endif // !__ANDROID__

	// alt num pad
	list->SetCheck(MENU_INPUT_CURSOR_NUM, setting->IsCursorToNumPad());
	list->SetCheck(MENU_INPUT_NUM_PAD, setting->IsNumToNumPad());

	// focus
	list->SetFocus(id);
}

//
// EnterReset()
// enter reset menu
//
void Menu::EnterReset()
{
	list->SetTitle("<< Reset >>", MENU_RESET);

	list->AddButton("Yes (Reset)", MENU_RESET_YES);
	list->AddButton("No", MENU_RESET_NO);
}

//
// EnterQuit()
// enter quit menu
//
void Menu::EnterQuit()
{
	list->SetTitle("<< Quit XM8 >>", MENU_QUIT);

	list->AddButton("Yes (Quit)", MENU_QUIT_YES);
	list->AddButton("No", MENU_QUIT_NO);
}

//
// EnterSoftKey()
// enter softkey menu
//
void Menu::EnterSoftKey()
{
	char title[64];
	int set;
	int id;

	// get set
	set = softkey_id - MENU_INPUT_SOFTKEY1;

	sprintf(title, "<< Softkey type %d >>", set + 1);
	list->SetTitle(title, MENU_SOFTKEY1 + set);

	list->AddButton("(LR)Full", MENU_SOFTKEY_0);
	list->AddButton("(L)Cursor   (R)Ten", MENU_SOFTKEY_1);
	list->AddButton("(L)Function (R)Ten", MENU_SOFTKEY_2);
	list->AddButton("(L)Action   (R)2468", MENU_SOFTKEY_3);
	list->AddButton("(L)2468     (R)Action", MENU_SOFTKEY_4);
	list->AddButton("(L)ZX       (R)2468", MENU_SOFTKEY_5);
	list->AddButton("(L)2468     (R)ZX", MENU_SOFTKEY_6);
	list->AddButton("(L)Ten      (R)Cursor", MENU_SOFTKEY_7);
	list->AddButton("(L)Ten      (R)Function", MENU_SOFTKEY_8);
	list->AddButton("(L)Ten", MENU_SOFTKEY_9);
	list->AddButton("            (R)Ten", MENU_SOFTKEY_10);
	list->AddButton("(None)", MENU_SOFTKEY_11);

	// set focus
	id = MENU_SOFTKEY_0 + setting->GetSoftKeySet(set);
	list->SetFocus(id);
}

//
// EnterDip()
// enter dip menu
//
void Menu::EnterDip()
{
	list->SetTitle("<< DIP settings >>", MENU_DIP);

	list->AddRadioButton("Boot as BASIC    mode", MENU_DIP_BASICMODE, MENU_DIP_BOOTMODE);
	list->AddRadioButton("Boot as TERMINAL mode", MENU_DIP_TERMMODE, MENU_DIP_BOOTMODE);
	list->AddRadioButton("Boot with 80 width", MENU_DIP_WIDTH80, MENU_DIP_WIDTH);
	list->AddRadioButton("Boot with 40 width", MENU_DIP_WIDTH40, MENU_DIP_WIDTH);
	list->AddRadioButton("Boot with 20 line", MENU_DIP_LINE20, MENU_DIP_LINE);
	list->AddRadioButton("Boot with 25 line", MENU_DIP_LINE25, MENU_DIP_LINE);
	list->AddRadioButton("Boot from disk", MENU_DIP_FROMDISK, MENU_DIP_BOOTFROM);
	list->AddRadioButton("Boot from ROM", MENU_DIP_FROMROM, MENU_DIP_BOOTFROM);
	list->AddRadioButton("Memory wait = OFF", MENU_DIP_MEMWAIT_OFF, MENU_DIP_MEMWAIT);
	list->AddRadioButton("Memory wait = ON", MENU_DIP_MEMWAIT_ON, MENU_DIP_MEMWAIT);

	list->AddRadioButton("Baud rate    75bps", MENU_DIP_BAUD75, MENU_DIP_BAUDRATE);
	list->AddRadioButton("Baud rate   150bps", MENU_DIP_BAUD150, MENU_DIP_BAUDRATE);
	list->AddRadioButton("Baud rate   300bps", MENU_DIP_BAUD300, MENU_DIP_BAUDRATE);
	list->AddRadioButton("Baud rate   600bps", MENU_DIP_BAUD600, MENU_DIP_BAUDRATE);
	list->AddRadioButton("Baud rate  1200bps", MENU_DIP_BAUD1200, MENU_DIP_BAUDRATE);
	list->AddRadioButton("Baud rate  2400bps", MENU_DIP_BAUD2400, MENU_DIP_BAUDRATE);
	list->AddRadioButton("Baud rate  4800bps", MENU_DIP_BAUD4800, MENU_DIP_BAUDRATE);
	list->AddRadioButton("Baud rate  9600bps", MENU_DIP_BAUD9600, MENU_DIP_BAUDRATE);
	list->AddRadioButton("Baud rate 19200bps", MENU_DIP_BAUD19200, MENU_DIP_BAUDRATE);

	list->AddRadioButton("Half duplex", MENU_DIP_HALFDUPLEX, MENU_DIP_DUPLEX);
	list->AddRadioButton("Full duplex", MENU_DIP_FULLDUPLEX, MENU_DIP_DUPLEX);
	list->AddRadioButton("Data bit 8bit", MENU_DIP_DATA8BIT, MENU_DIP_DATABIT);
	list->AddRadioButton("Data bit 7bit", MENU_DIP_DATA7BIT, MENU_DIP_DATABIT);
	list->AddRadioButton("Stop bit 2bit", MENU_DIP_STOP2BIT, MENU_DIP_STOPBIT);
	list->AddRadioButton("Stop bit 1bit", MENU_DIP_STOP1BIT, MENU_DIP_STOPBIT);
	list->AddRadioButton("X parameter = ON", MENU_DIP_XON, MENU_DIP_X);
	list->AddRadioButton("X parameter = OFF", MENU_DIP_XOFF, MENU_DIP_X);
	list->AddRadioButton("S parameter = ON", MENU_DIP_SON, MENU_DIP_S);
	list->AddRadioButton("S parameter = OFF", MENU_DIP_SOFF, MENU_DIP_S);
	list->AddRadioButton("DEL code = ON", MENU_DIP_DELON, MENU_DIP_DEL);
	list->AddRadioButton("DEL code = OFF", MENU_DIP_DELOFF, MENU_DIP_DEL);
	list->AddRadioButton("No   parity", MENU_DIP_NOPARITY, MENU_DIP_PARITY);
	list->AddRadioButton("Even parity", MENU_DIP_EVENPARITY, MENU_DIP_PARITY);
	list->AddRadioButton("Odd  parity", MENU_DIP_ODDPARITY, MENU_DIP_PARITY);

	list->AddButton("Restore default settings", MENU_DIP_DEFAULT);

	// set focus
	list->SetFocus(EnterDipSub());
}

//
// EnterDipSub()
// set radio buttons from setting->GetDip()
//
int Menu::EnterDipSub()
{
	Uint32 dip;
	Uint32 baudrate;
	Uint32 parity;
	int id;

	// get dip switch
	dip = setting->GetDip();

	// boot mode
	if ((dip & DIP_TERMINAL_MODE) != 0) {
		list->SetRadio(MENU_DIP_TERMMODE, MENU_DIP_BOOTMODE);
		id = MENU_DIP_TERMMODE;
	}
	else {
		list->SetRadio(MENU_DIP_BASICMODE, MENU_DIP_BOOTMODE);
		id = MENU_DIP_BASICMODE;
	}

	// boot width
	if ((dip & DIP_WIDTH_40) != 0) {
		list->SetRadio(MENU_DIP_WIDTH40, MENU_DIP_WIDTH);
	}
	else {
		list->SetRadio(MENU_DIP_WIDTH80, MENU_DIP_WIDTH);
	}

	// boot line
	if ((dip & DIP_LINE_25) != 0) {
		list->SetRadio(MENU_DIP_LINE25, MENU_DIP_LINE);
	}
	else {
		list->SetRadio(MENU_DIP_LINE20, MENU_DIP_LINE);
	}

	// boot from
	if ((dip & DIP_BOOT_ROM) != 0) {
		list->SetRadio(MENU_DIP_FROMROM, MENU_DIP_BOOTFROM);
	}
	else {
		list->SetRadio(MENU_DIP_FROMDISK, MENU_DIP_BOOTFROM);
	}

	// memory wait
	if ((dip & DIP_MEM_WAIT) != 0) {
		list->SetRadio(MENU_DIP_MEMWAIT_ON, MENU_DIP_MEMWAIT);
	}
	else {
		list->SetRadio(MENU_DIP_MEMWAIT_OFF, MENU_DIP_MEMWAIT);
	}

	// baud rate
	baudrate = (dip & DIP_BAUDRATE) >> DIP_BAUDRATE_SHIFT;
	switch (baudrate) {
	// 75bps
	case 7:
		list->SetRadio(MENU_DIP_BAUD75, MENU_DIP_BAUDRATE);
		break;
	// 150bps
	case 8:
		list->SetRadio(MENU_DIP_BAUD150, MENU_DIP_BAUDRATE);
		break;
	// 300bps
	case 9:
		list->SetRadio(MENU_DIP_BAUD300, MENU_DIP_BAUDRATE);
		break;
	// 600bps
	case 10:
		list->SetRadio(MENU_DIP_BAUD600, MENU_DIP_BAUDRATE);
		break;
	// 1200bps
	case 11:
		list->SetRadio(MENU_DIP_BAUD1200, MENU_DIP_BAUDRATE);
		break;
	// 2400bps
	case 12:
		list->SetRadio(MENU_DIP_BAUD2400, MENU_DIP_BAUDRATE);
		break;
	// 4800bps
	case 13:
		list->SetRadio(MENU_DIP_BAUD4800, MENU_DIP_BAUDRATE);
		break;
	// 9600bps
	case 14:
		list->SetRadio(MENU_DIP_BAUD9600, MENU_DIP_BAUDRATE);
		break;
	// 19200bps
	case 15:
		list->SetRadio(MENU_DIP_BAUD19200, MENU_DIP_BAUDRATE);
		break;
	// default (version 1.00 - version 1.20)
	default:
		list->SetRadio(MENU_DIP_BAUD1200, MENU_DIP_BAUDRATE);
		break;
	}

	// duplex
	if ((dip & DIP_HALFDUPLEX) != 0) {
		list->SetRadio(MENU_DIP_HALFDUPLEX, MENU_DIP_DUPLEX);
	}
	else {
		list->SetRadio(MENU_DIP_FULLDUPLEX, MENU_DIP_DUPLEX);
	}

	// data bit
	if ((dip & DIP_DATA7BIT) != 0) {
		list->SetRadio(MENU_DIP_DATA7BIT, MENU_DIP_DATABIT);
	}
	else {
		list->SetRadio(MENU_DIP_DATA8BIT, MENU_DIP_DATABIT);
	}

	// stop bit
	if ((dip & DIP_STOP2BIT) != 0) {
		list->SetRadio(MENU_DIP_STOP2BIT, MENU_DIP_STOPBIT);
	}
	else {
		list->SetRadio(MENU_DIP_STOP1BIT, MENU_DIP_STOPBIT);
	}

	// X parameter
	if ((dip & DIP_DISABLE_X) != 0) {
		list->SetRadio(MENU_DIP_XOFF, MENU_DIP_X);
	}
	else {
		list->SetRadio(MENU_DIP_XON, MENU_DIP_X);
	}

	// S parameter
	if ((dip & DIP_ENABLE_S) != 0) {
		list->SetRadio(MENU_DIP_SON, MENU_DIP_S);
	}
	else {
		list->SetRadio(MENU_DIP_SOFF, MENU_DIP_S);
	}

	// DEL code
	if ((dip & DIP_DISABLE_DEL) != 0) {
		list->SetRadio(MENU_DIP_DELOFF, MENU_DIP_DEL);
	}
	else {
		list->SetRadio(MENU_DIP_DELON, MENU_DIP_DEL);
	}

	// parity
	parity = (dip & DIP_PARITY) >> DIP_PARITY_SHIFT;
	switch (parity) {
	// none
	case 0:
		list->SetRadio(MENU_DIP_NOPARITY, MENU_DIP_PARITY);
		break;
	// even
	case 1:
		list->SetRadio(MENU_DIP_EVENPARITY, MENU_DIP_PARITY);
		break;
	// odd
	default:
		list->SetRadio(MENU_DIP_ODDPARITY, MENU_DIP_PARITY);
		break;
	}

	return id;
}

//
// EnterJoymap()
// enter joymap menu
//
void Menu::EnterJoymap(int id)
{
	list->SetTitle("<< Joystick to keyboard map >>", MENU_JOYMAP);

	list->AddButton("SDL_BUTTON_DPAD_UP", MENU_JOYMAP_DPAD_UP);
	list->AddButton("SDL_BUTTON_DPAD_DOWN", MENU_JOYMAP_DPAD_DOWN);
	list->AddButton("SDL_BUTTON_DPAD_LEFT", MENU_JOYMAP_DPAD_LEFT);
	list->AddButton("SDL_BUTTON_DPAD_RIGHT", MENU_JOYMAP_DPAD_RIGHT);
	list->AddButton("SDL_BUTTON_A", MENU_JOYMAP_A);
	list->AddButton("SDL_BUTTON_B", MENU_JOYMAP_B);
	list->AddButton("SDL_BUTTON_X", MENU_JOYMAP_X);
	list->AddButton("SDL_BUTTON_Y", MENU_JOYMAP_Y);
	list->AddButton("SDL_BUTTON_BACK", MENU_JOYMAP_BACK);
	list->AddButton("SDL_BUTTON_GUIDE", MENU_JOYMAP_GUIDE);
	list->AddButton("SDL_BUTTON_START", MENU_JOYMAP_START);
	list->AddButton("SDL_BUTTON_LEFTSTICK", MENU_JOYMAP_LEFTSTICK);
	list->AddButton("SDL_BUTTON_RIGHTSTICK", MENU_JOYMAP_RIGHTSTICK);
	list->AddButton("SDL_BUTTON_LEFTSHOULDER", MENU_JOYMAP_LEFTSLDR);
	list->AddButton("SDL_BUTTON_RIGHTSHOULDER", MENU_JOYMAP_RIGHTSLDR);

	list->AddButton("Restore default settings", MENU_JOYMAP_DEFAULT);

	list->SetFocus(id);
}

//
// EnterVmKey()
// enter vmkey menu
//
void Menu::EnterVmKey(int id)
{
	list->SetTitle("<< Select a key >>", MENU_VMKEY);

	list->AddButton("(User Interface)", MENU_VMKEY_MENU);
	list->AddButton("(Next Softkey)", MENU_VMKEY_NEXT);
	list->AddButton("(Prev Softkey)", MENU_VMKEY_PREV);
	list->AddButton("Tenkey 0", MENU_VMKEY_TEN0);
	list->AddButton("Tenkey 1", MENU_VMKEY_TEN1);
	list->AddButton("Tenkey 2", MENU_VMKEY_TEN2);
	list->AddButton("Tenkey 3", MENU_VMKEY_TEN3);
	list->AddButton("Tenkey 4", MENU_VMKEY_TEN4);
	list->AddButton("Tenkey 5", MENU_VMKEY_TEN5);
	list->AddButton("Tenkey 6", MENU_VMKEY_TEN6);
	list->AddButton("Tenkey 7", MENU_VMKEY_TEN7);
	list->AddButton("Tenkey 8", MENU_VMKEY_TEN8);
	list->AddButton("Tenkey 9", MENU_VMKEY_TEN9);
	list->AddButton("Funckey 1", MENU_VMKEY_F1);
	list->AddButton("Funckey 2", MENU_VMKEY_F2);
	list->AddButton("Funckey 3", MENU_VMKEY_F3);
	list->AddButton("Funckey 4", MENU_VMKEY_F4);
	list->AddButton("Funckey 5", MENU_VMKEY_F5);
	list->AddButton("ESC", MENU_VMKEY_ESC);
	list->AddButton("SPACE", MENU_VMKEY_SPACE);
	list->AddButton("RETURN", MENU_VMKEY_RETURN);
	list->AddButton("DEL", MENU_VMKEY_DEL);
	list->AddButton("HOMECLR", MENU_VMKEY_HOMECLR);
	list->AddButton("HELP", MENU_VMKEY_HELP);
	list->AddButton("SHIFT", MENU_VMKEY_SHIFT);
	list->AddButton("CTRL", MENU_VMKEY_CTRL);
	list->AddButton("CAPS", MENU_VMKEY_CAPS);
	list->AddButton("KANA", MENU_VMKEY_KANA);
	list->AddButton("GRPH", MENU_VMKEY_GRPH);
	list->AddButton("A", MENU_VMKEY_A);
	list->AddButton("B", MENU_VMKEY_B);
	list->AddButton("C", MENU_VMKEY_C);
	list->AddButton("D", MENU_VMKEY_D);
	list->AddButton("E", MENU_VMKEY_E);
	list->AddButton("F", MENU_VMKEY_F);
	list->AddButton("G", MENU_VMKEY_G);
	list->AddButton("H", MENU_VMKEY_H);
	list->AddButton("I", MENU_VMKEY_I);
	list->AddButton("J", MENU_VMKEY_J);
	list->AddButton("K", MENU_VMKEY_K);
	list->AddButton("L", MENU_VMKEY_L);
	list->AddButton("M", MENU_VMKEY_M);
	list->AddButton("N", MENU_VMKEY_N);
	list->AddButton("O", MENU_VMKEY_O);
	list->AddButton("P", MENU_VMKEY_P);
	list->AddButton("Q", MENU_VMKEY_Q);
	list->AddButton("R", MENU_VMKEY_R);
	list->AddButton("S", MENU_VMKEY_S);
	list->AddButton("T", MENU_VMKEY_T);
	list->AddButton("U", MENU_VMKEY_U);
	list->AddButton("V", MENU_VMKEY_V);
	list->AddButton("W", MENU_VMKEY_W);
	list->AddButton("X", MENU_VMKEY_X);
	list->AddButton("Y", MENU_VMKEY_Y);
	list->AddButton("Z", MENU_VMKEY_Z);
	list->AddButton("UP", MENU_VMKEY_UP);
	list->AddButton("DOWN", MENU_VMKEY_DOWN);
	list->AddButton("LEFT", MENU_VMKEY_LEFT);
	list->AddButton("RIGHT", MENU_VMKEY_RIGHT);
	list->AddButton("ROLL UP", MENU_VMKEY_ROLLUP);
	list->AddButton("ROLL DOWN", MENU_VMKEY_ROLLDOWN);
	list->AddButton("TAB", MENU_VMKEY_TAB);

	list->SetFocus(id);
}

//
// EnterFile()
// enter file menu
//
void Menu::EnterFile()
{
	const char *name;
	int id;
	Uint32 info;
	int id_focus;
	bool matched;

	// title
	list->SetTitle(file_dir, MENU_FILE, true);

	// first
	name = platform->FindFirst(file_dir, &info);

	// loop
	id = MENU_FILE_MIN;
	id_focus = MENU_FILE_MIN;
	matched = false;
	while (name != NULL) {
		// compare name
		if (strcmp(file_expect, name) == 0) {
			matched = true;
			id_focus = id;
		}

		// add item
		list->AddButton(name, id);
		list->SetUser(id, info);
		name = platform->FindNext(&info);
		id++;
	}

	// sort
	list->Sort();

	// set focus
	if (matched == true) {
		list->SetFocus(id_focus);
	}
	file_expect[0] = '\0';
}

void Menu::EnterPlaylist(const char *path)
{
	std::string error;
	playlist_entries.clear();
	if (!LoadPlaylistDiskSpecs(path, 0,
		MENU_PLAYLIST_ENTRY_MAX - MENU_PLAYLIST_ENTRY_MIN + 1,
		&playlist_entries, &error)) {
		platform->MsgBox(NULL, error.c_str());
		EnterFile();
		return;
	}
	playlist_drive = file_id == MENU_DRIVE2_OPEN ? 1 : 0;
	playlist_first = -1;
	const bool pair = file_id == MENU_DRIVE1_BOTH || file_id == MENU_DRIVE2_BOTH;
	list->SetTitle(pair ? "<< Playlist: select Drive 1 >>" : "<< Playlist >>",
		MENU_PLAYLIST);
	for (size_t i = 0; i < playlist_entries.size(); ++i) {
		const std::string& entry = playlist_entries[i].path;
		const size_t slash = entry.find_last_of("/\\");
		list->AddButton(entry.substr(slash == std::string::npos ? 0 : slash + 1).c_str(),
			MENU_PLAYLIST_ENTRY_MIN + static_cast<int>(i));
	}
}

//
// EnterJoyTest()
// enter joytest menu
//
void Menu::EnterJoyTest()
{
	int loop;

	list->SetTitle("<< Joystick test >>", MENU_JOYTEST);

	list->AddButton("(Press A+B to quit)", MENU_JOYTEST_QUIT);
	for (loop=0; loop<15; loop++) {
		list->AddButton("", MENU_JOYTEST_BUTTON1 + loop);
	}
}

//
// Command()
// command
//
void Menu::Command(bool down, int id)
{
	if (IsPlaylistEntryCommand(list->GetID(), id, MENU_PLAYLIST,
		MENU_PLAYLIST_ENTRY_MIN, MENU_PLAYLIST_ENTRY_MAX)) {
		if (down == false) CmdPlaylist(id);
		return;
	}
	// back
	if (id == MENU_BACK) {
		if (down == false) {
			CmdBack();
		}
		return;
	}

	// main menu
	if ((id >= MENU_MAIN_MIN) && (id <= MENU_MAIN_MAX)) {
		if (down == false) {
			CmdMain(id);
		}
		return;
	}

	// drive1 menu
	if ((id >= MENU_DRIVE1_MIN) && (id <= MENU_DRIVE1_MAX)) {
		if (down == false) {
			CmdDrive1(id);
		}
		return;
	}

	// drive2 menu
	if ((id >= MENU_DRIVE2_MIN) && (id <= MENU_DRIVE2_MAX)) {
		if (down == false) {
			CmdDrive2(id);
		}
		return;
	}

	// cmt menu
	if ((id >= MENU_CMT_MIN) && (id <= MENU_CMT_MAX)) {
		if (down == false) {
			CmdCmt(id);
		}
		return;
	}

	// load menu
	if ((id >= MENU_LOAD_MIN) && (id <= MENU_LOAD_MAX)) {
		if (down == false) {
			CmdLoad(id);
		}
		return;
	}

	// save menu
	if ((id >= MENU_SAVE_MIN) && (id <= MENU_SAVE_MAX)) {
		if (down == false) {
			CmdSave(id);
		}
		return;
	}

	// system menu
	if ((id >= MENU_SYSTEM_MIN) && (id <= MENU_SYSTEM_MAX)) {
		if (down == false) {
			CmdSystem(id);
		}
		return;
	}

#ifdef XM8_ENABLE_RETROACHIEVEMENTS
	// RetroAchievements menu
	if ((id >= MENU_RA_MIN) && (id <= MENU_RA_MAX)) {
		if (down == false) {
			CmdRa(id);
		}
		return;
	}
#endif

	// video menu
	if ((id >= MENU_VIDEO_MIN) && (id <= MENU_VIDEO_MAX)) {
		if (down == false) {
			CmdVideo(down, id);
		}
		return;
	}

	// audio menu
	if ((id >= MENU_AUDIO_MIN) && (id <= MENU_AUDIO_MAX)) {
		CmdAudio(down, id);
		return;
	}

	// input menu
	if ((id >= MENU_INPUT_MIN) && (id <= MENU_INPUT_MAX)) {
		CmdInput(down, id);
		return;
	}

	// reset menu
	if ((id >= MENU_RESET_MIN) && (id <= MENU_RESET_MAX)) {
		if (down == false) {
			CmdReset(id);
		}
		return;
	}

	// quit menu
	if ((id >= MENU_QUIT_MIN) && (id <= MENU_QUIT_MAX)) {
		if (down == false) {
			CmdQuit(id);
		}
		return;
	}

	// softkey menu
	if ((id >= MENU_SOFTKEY_MIN) && (id <= MENU_SOFTKEY_MAX)) {
		if (down == false) {
			CmdSoftKey(id);
		}
		return;
	}

	// dipswitch menu
	if ((id >= MENU_DIP_MIN) && (id <= MENU_DIP_MAX)) {
		if (down == false) {
			CmdDip(id);
		}
		return;
	}

	// joystick to keyboard map menu
	if ((id >= MENU_JOYMAP_MIN) && (id <= MENU_JOYMAP_MAX)) {
		if (down == false) {
			CmdJoymap(id);
		}
		return;
	}

	// vmkey menu
	if ((id >= MENU_VMKEY_MIN) && (id <= MENU_VMKEY_MAX)) {
		if (down == false) {
			CmdVmKey(id);
		}
		return;
	}

	// joytest menu
	if ((id >= MENU_JOYTEST_MIN) && (id <= MENU_JOYTEST_MAX)) {
		// do nothing
		return;
	}

	// audio device menu
	if ((id >= MENU_AUDIO_DEVICE_MIN) && (id <= MENU_AUDIO_DEVICE_MAX)) {
		CmdAudioOut(down, id);
		return;
	}

	// audio device menu
	if ((id >= MENU_SCALEFILTER_MIN) && (id <= MENU_SCALEFILTER_MAX)) {
		if (down == false) {
			CmdScalingFilter(down, id);
		}
		return;
	}

	// file menu
	if (id >= MENU_FILE_MIN) {
		if (down == false) {
			CmdFile(id);
		}
	}
}

//
// CmdBack()
// command (back)
//
void Menu::CmdBack()
{
	switch (list->GetID()) {
	// main menu
	case MENU_MAIN:
		app->LeaveMenu();
		break;

	// drive1 menu
	case MENU_DRIVE1:
		EnterMain(MENU_MAIN_DRIVE1);
		break;

	// drive2 menu
	case MENU_DRIVE2:
		EnterMain(MENU_MAIN_DRIVE2);
		break;

	// cmt menu
	case MENU_CMT:
		EnterMain(MENU_MAIN_CMT);
		break;

	// load menu
	case MENU_LOAD:
		if (ra_state_menu) {
#ifdef XM8_ENABLE_RETROACHIEVEMENTS
			EnterRa(ra_hardcore_debug_state_menu ?
				MENU_RA_LOAD_HARDCORE_DEBUG : MENU_RA_LOAD);
#else
			EnterMain(MENU_MAIN_LOAD);
#endif
		}
		else {
			EnterMain(MENU_MAIN_LOAD);
		}
		break;

	// save menu
	case MENU_SAVE:
		if (ra_state_menu) {
#ifdef XM8_ENABLE_RETROACHIEVEMENTS
			EnterRa(MENU_RA_SAVE);
#else
			EnterMain(MENU_MAIN_SAVE);
#endif
		}
		else {
			EnterMain(MENU_MAIN_SAVE);
		}
		break;

	// system menu
	case MENU_SYSTEM:
		EnterMain(MENU_MAIN_SYSTEM);
		break;

#ifdef XM8_ENABLE_RETROACHIEVEMENTS
	// RetroAchievements menu
	case MENU_RA:
		EnterMain(MENU_MAIN_RA);
		break;
	case MENU_RA_HARDCORE_CONFIRM:
		EnterRa(MENU_RA_HARDCORE);
		break;
	case MENU_RA_LOGOUT_CONFIRM:
		EnterRa(MENU_RA_LOGIN);
		break;
	case MENU_RA_RESET_CONFIRM:
		EnterRa(ra_reset_confirmation_enables_mode ?
			MENU_RA_MODE : MENU_RA_HARDCORE);
		break;
	case MENU_RA_LIBRARY_VIEW:
		EnterRa(MENU_RA_LIBRARY);
		break;
	case MENU_RA_ACHIEVEMENTS_VIEW:
		EnterRa(MENU_RA_ACHIEVEMENTS);
		break;
	case MENU_RA_LEADERBOARDS_VIEW:
		EnterRa(MENU_RA_LEADERBOARDS);
		break;
	case MENU_RA_GAME_DETAIL: {
		const Xm8Ra::RaOverlayLibraryListSnapshot snapshot =
			app->GetRaLibraryMenuSnapshot();
		EnterRaLibrary(static_cast<int>(snapshot.selected_index));
		break;
	}
	case MENU_RA_ACHIEVEMENT_DETAIL: {
		const Xm8Ra::RaOverlayAchievementListSnapshot snapshot =
			app->GetRaAchievementsMenuSnapshot();
		EnterRaAchievements(static_cast<int>(snapshot.selected_index));
		break;
	}
#endif

	// video menu
	case MENU_VIDEO:
		EnterMain(MENU_MAIN_VIDEO);
		break;

	// audio menu
	case MENU_AUDIO:
		EnterMain(MENU_MAIN_AUDIO);
		break;

	// audio output menu
	case MENU_AUDIO_OUT:
		EnterMain(MENU_MAIN_AUDIO_OUT);
		break;

	// input menu
	case MENU_INPUT:
		EnterMain(MENU_MAIN_INPUT);
		break;

	// reset menu
	case MENU_RESET:
		EnterMain(MENU_MAIN_RESET);
		break;

	// quit menu
	case MENU_QUIT:
		if (top_id == MENU_QUIT) {
			app->LeaveMenu();
		}
		else {
			EnterMain(MENU_MAIN_QUIT);
		}
		break;

	// softkey menu
	case MENU_SOFTKEY1:
	case MENU_SOFTKEY2:
	case MENU_SOFTKEY3:
	case MENU_SOFTKEY4:
		EnterInput(softkey_id);
		break;

	// dipswitch menu
	case MENU_DIP:
		EnterSystem(MENU_SYSTEM_DIP);
		break;

	// joystick to keyboard map menu
	case MENU_JOYMAP:
		EnterInput(MENU_INPUT_JOYMAP);
		break;

	// vmkey menu
	case MENU_VMKEY:
		EnterJoymap(joymap_id);
		break;

	// joytest menu
	case MENU_JOYTEST:
		EnterInput(MENU_INPUT_JOYTEST);
		break;

	// video scaling filter menu
	case MENU_SCALEFILTER:
		EnterVideo(MENU_VIDEO_SCALEFILTER);
		break;

	// file menu
	case MENU_FILE:
		switch (file_id) {
		// from drive1 menu
		case MENU_DRIVE1_OPEN:
			EnterDrive1(MENU_DRIVE1_OPEN);
			break;
		case MENU_DRIVE1_BOTH:
			EnterDrive1(MENU_DRIVE1_BOTH);
			break;

		// from drive2 menu
		case MENU_DRIVE2_OPEN:
			EnterDrive2(MENU_DRIVE2_OPEN);
			break;
		case MENU_DRIVE2_BOTH:
			EnterDrive2(MENU_DRIVE2_BOTH);
			break;

		// from cmt menu
		case MENU_CMT_PLAY:
			EnterCmt(MENU_CMT_PLAY);
			break;
		case MENU_CMT_REC:
			EnterCmt(MENU_CMT_REC);
			break;

		// default
		default:
			// fail safe
			EnterMain(MENU_MAIN_DRIVE1);
			break;
		}
		break;

	default:
		// fail safe
		EnterMain(MENU_MAIN_DRIVE1);
		break;
	}
}

//
// CmdMain()
// command (main)
//
void Menu::CmdMain(int id)
{
	switch (id) {
	// drive 1
	case MENU_MAIN_DRIVE1:
		EnterDrive1(MENU_BACK);
		break;

	// drive 2
	case MENU_MAIN_DRIVE2:
		EnterDrive2(MENU_BACK);
		break;

	// cmt
	case MENU_MAIN_CMT:
		EnterCmt(MENU_BACK);
		break;

	// load state
	case MENU_MAIN_LOAD:
		EnterLoad();
		break;

	// save state
	case MENU_MAIN_SAVE:
		EnterSave();
		break;

	// system options
	case MENU_MAIN_SYSTEM:
		EnterSystem(MENU_SYSTEM_V1S);
		break;

	// video options
	case MENU_MAIN_VIDEO:
		EnterVideo(MENU_VIDEO_SKIP0);
		break;

	// audio options
	case MENU_MAIN_AUDIO:
		EnterAudio();
		break;

	// audio output device
	case MENU_MAIN_AUDIO_OUT:
		EnterAudioOut();
		break;

	// input options
	case MENU_MAIN_INPUT:
		EnterInput(MENU_INPUT_SOFTKEY1);
		break;

#ifdef XM8_ENABLE_RETROACHIEVEMENTS
	// RetroAchievements
	case MENU_MAIN_RA:
		EnterRa(MENU_RA_LOGIN);
		break;
#endif

#ifndef __ANDROID__
	// screen
	case MENU_MAIN_SCREEN:
		if (app->IsFullScreen() == true) {
			app->WindowScreen();
		}
		else {
			app->FullScreen();
		}
		break;
#endif // !__ANDROID__

	// speed
	case MENU_MAIN_SPEED:
		if (app->IsFullSpeed() == true) {
			app->NormalSpeed();
		}
		else {
			app->FullSpeed();
		}
		break;

	// reset
	case MENU_MAIN_RESET:
		EnterReset();
		break;

	// quit
	case MENU_MAIN_QUIT:
		EnterQuit();
		break;

	default:
		break;
	}
}

//
// CmdDrive1()
// command (drive1)
//
void Menu::CmdDrive1(int id)
{
	switch (id) {
	// open
	case MENU_DRIVE1_OPEN:
		file_id = MENU_DRIVE1_OPEN;
		strcpy(file_dir, app->GetDiskDir(0));
		EnterFile();
		break;

	// open 1 & 2
	case MENU_DRIVE1_BOTH:
		file_id = MENU_DRIVE1_BOTH;
		strcpy(file_dir, app->GetDiskDir());
		EnterFile();
		break;

	// eject
	case MENU_DRIVE1_EJECT:
		if (diskmgr[0]->IsOpen() == true) {
			std::string error;
			if (app->EjectDiskFromMenu(0, &error)) {
				app->LeaveMenu();
			}
			else {
				platform->MsgBox(NULL, error.c_str());
			}
		}
		break;

	default:
		id -= MENU_DRIVE1_BANK0;
		{
			std::string error;
			if (app->ChangeDiskBankFromMenu(0, id, &error)) {
				app->LeaveMenu();
			}
			else {
				platform->MsgBox(NULL, error.c_str());
			}
		}
		break;
	}
}

//
// CmdDrive2()
// command (drive2)
//
void Menu::CmdDrive2(int id)
{
	switch (id) {
	// open
	case MENU_DRIVE2_OPEN:
		file_id = MENU_DRIVE2_OPEN;
		strcpy(file_dir, app->GetDiskDir(1));
		EnterFile();
		break;

	// open 1 & 2
	case MENU_DRIVE2_BOTH:
		file_id = MENU_DRIVE2_BOTH;
		strcpy(file_dir, app->GetDiskDir());
		EnterFile();
		break;

	// eject
	case MENU_DRIVE2_EJECT:
		if (diskmgr[1]->IsOpen() == true) {
			std::string error;
			if (app->EjectDiskFromMenu(1, &error)) {
				app->LeaveMenu();
			}
			else {
				platform->MsgBox(NULL, error.c_str());
			}
		}
		break;

	default:
		id -= MENU_DRIVE2_BANK0;
		{
			std::string error;
			if (app->ChangeDiskBankFromMenu(1, id, &error)) {
				app->LeaveMenu();
			}
			else {
				platform->MsgBox(NULL, error.c_str());
			}
		}
		break;
	}
}

//
// CmdCmt()
// command (cmt)
//
void Menu::CmdCmt(int id)
{
	switch (id) {
	// play
	case MENU_CMT_PLAY:
		file_id = MENU_CMT_PLAY;
		strcpy(file_dir, app->GetTapeDir());
		EnterFile();
		break;

	// rec
	case MENU_CMT_REC:
		file_id = MENU_CMT_REC;
		strcpy(file_dir, app->GetTapeDir());
		EnterFile();
		break;

	// eject
	case MENU_CMT_EJECT:
		if ((tapemgr->IsPlay() == true) || (tapemgr->IsRec() == true)) {
			tapemgr->Eject();
			app->LeaveMenu();
		}
		break;
	}
}

//
// CmdLoad()
// command (load)
//
void Menu::CmdLoad(int id)
{
	char textprev[64];
	char textbuf[64];

	// save last id
	id -= MENU_LOAD_0;
	setting->SetStateNum(id);

	// update text
	strcpy(textprev, list->GetText(MENU_LOAD_0 + id));
	sprintf(textbuf, "Slot %d Loading...", id);
	list->SetText(MENU_LOAD_0 + id, textbuf);

	// draw
	Draw();
	video->Draw();

	// load
	bool loaded = false;
#ifdef XM8_ENABLE_RETROACHIEVEMENTS
	if (ra_hardcore_debug_state_menu) loaded = app->LoadRaDebugState(id);
	else
#endif
	loaded = app->Load(id);
	if (loaded) {
		// after load, save last id again
		setting->SetStateNum(id);
		app->LeaveMenu(false);
	}
	else {
		// load error
		list->SetText(MENU_LOAD_0 + id, textprev);
	}
}

//
// CmdSave()
// command (save)
//
void Menu::CmdSave(int id)
{
	char textprev[64];
	char textbuf[64];

	// save last id
	id -= MENU_SAVE_0;
	setting->SetStateNum(id);

	// update text
	strcpy(textprev, list->GetText(MENU_SAVE_0 + id));
	sprintf(textbuf, "Slot %d Saving...", id);
	list->SetText(MENU_SAVE_0 + id, textbuf);

	// draw
	Draw();
	video->Draw();

	// save
	if (app->Save(id) == true) {
		app->LeaveMenu();
	}
	else {
		// save error
		list->SetText(MENU_SAVE_0 + id, textprev);
	}
}

//
// CmdSystem()
// command (system)
//
void Menu::CmdSystem(int id)
{
	Font *font;
	Uint8 ver;

	// get ROM version
	font = app->GetFont();
	ver = font->GetROMVersion(1);

	switch (id) {
	// V1S
	case MENU_SYSTEM_V1S:
		list->SetRadio(MENU_SYSTEM_V1S, MENU_SYSTEM_MODE);
		setting->SetSystemMode(SETTING_V1S_MODE);
		break;

	// V1H
	case MENU_SYSTEM_V1H:
		list->SetRadio(MENU_SYSTEM_V1H, MENU_SYSTEM_MODE);
		setting->SetSystemMode(SETTING_V1H_MODE);
		break;

	// V2
	case MENU_SYSTEM_V2:
		list->SetRadio(MENU_SYSTEM_V2, MENU_SYSTEM_MODE);
		setting->SetSystemMode(SETTING_V2_MODE);
		break;

	// N
	case MENU_SYSTEM_N:
		list->SetRadio(MENU_SYSTEM_N, MENU_SYSTEM_MODE);
		setting->SetSystemMode(SETTING_N_MODE);
		break;

	// 4MHz
	case MENU_SYSTEM_4M:
		list->SetRadio(MENU_SYSTEM_4M, MENU_SYSTEM_CLOCK);
		setting->SetCPUClock(4);
		setting->Set8HMode(false);
		break;

	// 8MHz
	case MENU_SYSTEM_8M:
		if (ver >= 0x38) {
			// PC-8801FH/MH or later
			list->SetRadio(MENU_SYSTEM_8M, MENU_SYSTEM_CLOCK);
			setting->SetCPUClock(8);
			setting->Set8HMode(false);
		}
		else {
			// PC-8801mkIISR/TR/FR/MR
			list->SetRadio(MENU_SYSTEM_4M, MENU_SYSTEM_CLOCK);
			setting->SetCPUClock(4);
			setting->Set8HMode(false);
		}
		break;

	// 8MHzH
	case MENU_SYSTEM_8MH:
		if (ver >= 0x38) {
			// PC-8801FH/MH or later
			list->SetRadio(MENU_SYSTEM_8MH, MENU_SYSTEM_CLOCK);
			setting->SetCPUClock(8);
			setting->Set8HMode(true);
		}
		else {
			// PC-8801mkIISR/TR/FR/MR
			list->SetRadio(MENU_SYSTEM_4M, MENU_SYSTEM_CLOCK);
			setting->SetCPUClock(4);
			setting->Set8HMode(false);
		}
		break;

	// 128KB RAM
	case MENU_SYSTEM_EXRAM:
		if (list->GetCheck(MENU_SYSTEM_EXRAM) == true) {
			list->SetCheck(MENU_SYSTEM_EXRAM, false);
			setting->SetExRAM(false);
		}
		else {
			list->SetCheck(MENU_SYSTEM_EXRAM, true);
			setting->SetExRAM(true);
		}
		break;

	// pseudo fast disk mode
	case MENU_SYSTEM_FASTDISK:
#ifdef XM8_ENABLE_RETROACHIEVEMENTS
		app->ToggleFastDisk();
#else
		setting->SetFastDisk(!setting->IsFastDisk());
#endif
		list->SetCheck(MENU_SYSTEM_FASTDISK, setting->IsFastDisk());
		break;

	// watch battery
	case MENU_SYSTEM_BATTERY:
		if (list->GetCheck(MENU_SYSTEM_BATTERY) == true) {
			list->SetCheck(MENU_SYSTEM_BATTERY, false);
			setting->SetWatchBattery(false);
		}
		else {
			list->SetCheck(MENU_SYSTEM_BATTERY, true);
			setting->SetWatchBattery(true);
		}
		break;

	// DIP switch
	case MENU_SYSTEM_DIP:
		EnterDip();
		break;

	// ROM version
	case MENU_SYSTEM_ROMVER:
		break;

#ifdef __ANDROID__
	case MENU_ANDROID_SAF:
		if (Android_HasTreeUri() == 0) {
			// request activity
			Android_RequestActivity();

			// update text
			list->SetText(MENU_ANDROID_SAF, "Choose SD Card \x81\xa8 Select");
		}
		else {
			// clear tree uri
			Android_ClearTreeUri();

			// unchecked
			list->SetCheck(MENU_ANDROID_SAF, false);
		}
#endif // __ANDROID__

	default:
		break;
	}
}

#ifdef XM8_ENABLE_RETROACHIEVEMENTS
//
// CmdRa()
// command (RetroAchievements)
//
void Menu::CmdRa(int id)
{
	auto update_status = [this]() {
		UpdateRaStatus();
	};

	if (id >= MENU_RA_LIBRARY_ITEM_MIN &&
		id < MENU_RA_ACHIEVEMENT_ITEM_MIN) {
		app->SelectRaLibraryMenuItemById(
			static_cast<int64_t>(list->GetUser(id)));
		if (app->OpenRaLibraryMenuDetail()) EnterRaGameDetail();
		return;
	}
	if (id >= MENU_RA_ACHIEVEMENT_ITEM_MIN &&
		id < MENU_RA_LEADERBOARD_ITEM_MIN) {
		app->SelectRaAchievementMenuItemById(
			static_cast<uint32_t>(list->GetUser(id)));
		if (app->OpenRaAchievementMenuDetail()) EnterRaAchievementDetail();
		return;
	}
	if (id >= MENU_RA_LEADERBOARD_ITEM_MIN &&
		id < MENU_RA_DETAIL_LINE_MIN) {
		app->SelectRaLeaderboardMenuItemById(
			static_cast<uint32_t>(list->GetUser(id)));
		return;
	}
	if (id >= MENU_RA_DETAIL_LINE_MIN) {
		return;
	}

	switch (id) {
	case MENU_RA_MODE:
		if (!app->IsRaModeEnabled() && app->IsRaHardcoreSelected()) {
			EnterRaResetConfirmation(true);
		}
		else {
			app->ToggleRaMode();
			EnterRa(MENU_RA_MODE);
		}
		break;

	case MENU_RA_HARDCORE:
		if (app->IsRaHardcoreActive()) {
			EnterRaHardcoreConfirmation();
		}
		else if (app->IsRaModeEnabled() &&
			!app->IsRaHardcoreSelected()) {
			EnterRaResetConfirmation(false);
		}
		else {
			app->ToggleRaPlayMode();
			EnterRa(MENU_RA_HARDCORE);
		}
		break;

	case MENU_RA_HARDCORE_YES:
		app->ToggleRaPlayMode();
		EnterRa(MENU_RA_HARDCORE);
		break;

	case MENU_RA_HARDCORE_NO:
		EnterRa(MENU_RA_HARDCORE);
		break;

	case MENU_RA_RESET_YES:
		if (ra_reset_confirmation_enables_mode) app->ToggleRaMode();
		else app->ToggleRaPlayMode();
		EnterRa(ra_reset_confirmation_enables_mode ?
			MENU_RA_MODE : MENU_RA_HARDCORE);
		break;

	case MENU_RA_RESET_NO:
		EnterRa(ra_reset_confirmation_enables_mode ?
			MENU_RA_MODE : MENU_RA_HARDCORE);
		break;

	case MENU_RA_STATUS:
		update_status();
		break;

	case MENU_RA_PRESENCE:
		update_status();
		break;

	case MENU_RA_LOGIN:
		if (app->IsRaLoggedIn()) {
			size_t pending = 0;
			if (!app->GetRaPendingUnlockCount(&pending)) break;
			if (pending != 0) EnterRaLogoutConfirmation(pending);
			else if (app->LogoutRa()) list->SetText(MENU_RA_LOGIN, "Login");
		}
		else {
			app->OpenRaLoginOverlay();
		}
		update_status();
		break;

	case MENU_RA_LOGOUT_YES:
		app->LogoutRa(true);
		EnterRa(MENU_RA_LOGIN);
		break;

	case MENU_RA_LOGOUT_NO:
		EnterRa(MENU_RA_LOGIN);
		break;

	case MENU_RA_LIBRARY:
		app->OpenRaLibraryOverlay();
		break;

	case MENU_RA_ACHIEVEMENTS:
		app->OpenRaAchievementsOverlay();
		break;

	case MENU_RA_LEADERBOARDS:
		app->OpenRaLeaderboardsOverlay();
		break;

	case MENU_RA_GAME_START:
		app->ActivateRaLibraryMenuDetail();
		break;

	case MENU_RA_LOAD:
		if (app->CheckRaStateAvailability(false, false)) {
			EnterLoad(true);
		}
		break;

	case MENU_RA_LOAD_HARDCORE_DEBUG:
		if (app->CheckRaStateAvailability(false, true)) {
			EnterLoad(true, true);
		}
		break;

	case MENU_RA_SAVE:
		if (app->CheckRaStateAvailability(true,
			app->IsRaHardcoreActive())) {
			EnterSave(true, app->IsRaHardcoreActive());
		}
		break;

	case MENU_RA_WEBSITE:
		app->OpenRaWebsite();
		break;

	case MENU_RA_PRIVACY:
		app->OpenRaPrivacyPolicy();
		break;

	case MENU_PLAYLIST:
		EnterFile();
		break;

	default:
		break;
	}
}
#endif

//
// CmdVideo()
// command (video)
//
void Menu::CmdVideo(bool down, int id)
{
	bool lowreso;
	bool radio;
	bool scanline;
	bool status;
	int width;

	// initialize
	width = 0;

	switch (id) {
#ifndef __ANDROID__
	// window x1.0
	case MENU_VIDEO_640:
		if (down == false) {
			list->SetRadio(MENU_VIDEO_640, MENU_VIDEO_WINDOW);
			width = 640;
		}
		break;

	// window x1.5
	case MENU_VIDEO_960:
		if (down == false) {
			list->SetRadio(MENU_VIDEO_960, MENU_VIDEO_WINDOW);
			width = 960;
		}
		break;

	// window x2.0
	case MENU_VIDEO_1280:
		if (down == false) {
			list->SetRadio(MENU_VIDEO_1280, MENU_VIDEO_WINDOW);
			width = 1280;
		}
		break;

	// window x2.5
	case MENU_VIDEO_1600:
		if (down == false) {
			list->SetRadio(MENU_VIDEO_1600, MENU_VIDEO_WINDOW);
			width = 1600;
		}
		break;

	// window x3.0
	case MENU_VIDEO_1920:
		if (down == false) {
			list->SetRadio(MENU_VIDEO_1920, MENU_VIDEO_WINDOW);
			width = 1920;
		}
		break;
#endif // !__ANDROID__

	// 0 frame skip
	case MENU_VIDEO_SKIP0:
		if (down == false) {
			list->SetRadio(MENU_VIDEO_SKIP0, MENU_VIDEO_SKIP);
			setting->SetSkipFrame(0);
		}
		break;

	// 0 frame skip
	case MENU_VIDEO_SKIP1:
		if (down == false) {
			list->SetRadio(MENU_VIDEO_SKIP1, MENU_VIDEO_SKIP);
			setting->SetSkipFrame(1);
		}
		break;

	// 2 frame skip
	case MENU_VIDEO_SKIP2:
		if (down == false) {
			list->SetRadio(MENU_VIDEO_SKIP2, MENU_VIDEO_SKIP);
			setting->SetSkipFrame(2);
		}
		break;

	// 3 frame skip
	case MENU_VIDEO_SKIP3:
		if (down == false) {
			list->SetRadio(MENU_VIDEO_SKIP3, MENU_VIDEO_SKIP);
			setting->SetSkipFrame(3);
		}
		break;

	// 15kHz monitor
	case MENU_VIDEO_15K:
		if (down == false) {
			list->SetRadio(MENU_VIDEO_15K, MENU_VIDEO_MONITOR);
		}
		break;

	// 24kHz monitor
	case MENU_VIDEO_24K:
		if (down == false) {
			list->SetRadio(MENU_VIDEO_24K, MENU_VIDEO_MONITOR);
		}
		break;

	// scan line
	case MENU_VIDEO_SCANLINE:
		if (down == false) {
			scanline = setting->HasScanline();
			if (scanline == true) {
				setting->SetScanline(false);
				list->SetCheck(MENU_VIDEO_SCANLINE, false);
			}
			else {
				setting->SetScanline(true);
				list->SetCheck(MENU_VIDEO_SCANLINE, true);
			}
		}
		break;

	// brightness
	case MENU_VIDEO_BRIGHTNESS:
		setting->SetBrightness((Uint8)list->GetSlider(MENU_VIDEO_BRIGHTNESS));
		break;

	// status line
	case MENU_VIDEO_STATUSCHK:
		if (down == false) {
			status = setting->HasStatusLine();
			if (status == true) {
				setting->SetStatusLine(false);
				list->SetCheck(MENU_VIDEO_STATUSCHK, false);
			}
			else {
				setting->SetStatusLine(true);
				list->SetCheck(MENU_VIDEO_STATUSCHK, true);
			}

			// to resize window
			width = setting->GetWindowWidth();
		}
		break;

	// staus alpha
	case MENU_VIDEO_STATUSALPHA:
		setting->SetStatusAlpha((Uint8)list->GetSlider(MENU_VIDEO_STATUSALPHA));
		break;

	// scaling quality
	case MENU_VIDEO_SCALEFILTER:
		EnterScalingFilter();
		return;

#ifdef __ANDROID__
	// force RGB565
	case MENU_VIDEO_FORCERGB565:
		if (down == false) {
			status = setting->IsForceRGB565();
			if (status == true) {
				setting->SetForceRGB565(false);
				list->SetCheck(MENU_VIDEO_FORCERGB565, false);
			}
			else {
				setting->SetForceRGB565(true);
				list->SetCheck(MENU_VIDEO_FORCERGB565, true);
			}
		}
		break;

	// rotation mode
	case MENU_VIDEO_ROTATE_AUTO:
	case MENU_VIDEO_ROTATE_LANDSCAPE:
	case MENU_VIDEO_ROTATE_PORTRAIT:
		if (down == false) {
			int rotation = Setting::RotationAuto;
			if (id == MENU_VIDEO_ROTATE_LANDSCAPE) {
				rotation = Setting::RotationLandscape;
			}
			else if (id == MENU_VIDEO_ROTATE_PORTRAIT) {
				rotation = Setting::RotationPortrait;
			}
			setting->SetRotationMode(rotation);
			list->SetRadio(id, MENU_VIDEO_ROTATION);
			app->ApplyAndroidRotationMode();
		}
		break;
#endif // __ANDROID__

	default:
		break;
	}

	// reset & leave menu
	if (down == false) {
		// window size
		if (width != 0) {
			setting->SetWindowWidth(width);
			app->SetWindowWidth();
		}

		// monitor type
		lowreso = setting->IsLowReso();
		radio = list->GetRadio(MENU_VIDEO_15K);
		if (lowreso != radio) {
			// not equal
			setting->SetLowReso(radio);
			app->Reset();
			app->LeaveMenu();
			return;
		}
	}
}

//
// CmdAudio()
// command (audio)
//
void Menu::CmdAudio(bool down, int id)
{
	int freq;
	int buf;
	bool opna;
	bool leave;

	// get current frequency
	freq = setting->GetAudioFreq();

	// get current buffer
	buf = setting->GetAudioBuffer();

	// leave flag
	leave = false;

	switch (id) {
	// Audio Output
	case MENU_AUDIO_OUT:
		EnterAudioOut();
		break;
		
	// 44100Hz
	case MENU_AUDIO_44100:
		if (down == false) {
			list->SetRadio(MENU_AUDIO_44100, MENU_AUDIO_FREQ);
			setting->SetAudioFreq(44100);
		}
		break;

	// 48000Hz
	case MENU_AUDIO_48000:
		if (down == false) {
			list->SetRadio(MENU_AUDIO_48000, MENU_AUDIO_FREQ);
			setting->SetAudioFreq(48000);
		}
		break;

#ifndef __ANDROID__
	// 55467Hz
	case MENU_AUDIO_55467:
		if (down == false) {
			list->SetRadio(MENU_AUDIO_55467, MENU_AUDIO_FREQ);
			setting->SetAudioFreq(55467);
		}
		break;

	// 88200Hz
	case MENU_AUDIO_88200:
		if (down == false) {
			list->SetRadio(MENU_AUDIO_88200, MENU_AUDIO_FREQ);
			setting->SetAudioFreq(88200);
		}
		break;

	// 96000Hz
	case MENU_AUDIO_96000:
		if (down == false) {
			list->SetRadio(MENU_AUDIO_96000, MENU_AUDIO_FREQ);
			setting->SetAudioFreq(96000);
		}
		break;
#endif // !__ANDROID__

	// Buffer
	case MENU_AUDIO_BUFFER:
		setting->SetAudioBuffer(list->GetSlider(MENU_AUDIO_BUFFER));
		break;

	// YM2203(OPN)
	case MENU_AUDIO_OPN:
		if (down == false) {
			list->SetRadio(MENU_AUDIO_OPN, MENU_AUDIO_SBII);

			opna =setting->HasOPNA();
			if (opna == true) {
				setting->SetOPNA(false);
				app->ChangeSystem();
				leave = true;
			}
		}
		break;

	// YM2608(OPNA);
	case MENU_AUDIO_OPNA:
		if (down == false) {
			list->SetRadio(MENU_AUDIO_OPNA, MENU_AUDIO_SBII);

			opna =setting->HasOPNA();
			if (opna == false) {
				setting->SetOPNA(true);
				app->ChangeSystem();
				leave = true;
			}
		}
		break;

	default:
		break;
	}

	// rebuild audio component if freq or buffer has been changed
	if ((setting->GetAudioFreq() != freq) || (setting->GetAudioBuffer() != buf)) {
		app->ChangeAudio();
	}

	// leave menu
	if (leave == true) {
		app->LeaveMenu();
	}
}

//
// CmdInput()
// command (input)
//
void Menu::CmdInput(bool down, int id)
{
	bool enable;
	bool swap;

	// softkey type
	if ((id >= MENU_INPUT_SOFTKEY1) && (id <= MENU_INPUT_SOFTKEY4)) {
		if (down == false) {
			softkey_id = id;
			EnterSoftKey();
		}
		return;
	}

	switch (id) {
	// softkey alpha level
	case MENU_INPUT_SOFTALPHA:
		setting->SetSoftKeyAlpha((Uint8)list->GetSlider(MENU_INPUT_SOFTALPHA));
		break;

	// softkey timeout
	case MENU_INPUT_SOFTTIME:
		setting->SetSoftKeyTime((Uint32)list->GetSlider(MENU_INPUT_SOFTTIME));
		break;

	// show softkey when mouse move
	case MENU_INPUT_SOFTKEY_MM:
		if (down == false) {
			enable = list->GetCheck(MENU_INPUT_SOFTKEY_MM);
			if (enable == true) {
				list->SetCheck(MENU_INPUT_SOFTKEY_MM, false);
				setting->SetShowSoftKeyMouseMove(false);
			}
			else {
				list->SetCheck(MENU_INPUT_SOFTKEY_MM, true);
				setting->SetShowSoftKeyMouseMove(true);
			}
		}
		break;

#ifdef __ANDROID__
	// keyboard enable
	case MENU_INPUT_KEYENABLE:
		if (down == false) {
			enable = list->GetCheck(MENU_INPUT_KEYENABLE);
			if (enable == true) {
				list->SetCheck(MENU_INPUT_KEYENABLE, false);
				setting->SetKeyEnable(false);
			}
			else {
				list->SetCheck(MENU_INPUT_KEYENABLE, true);
				setting->SetKeyEnable(true);
			}
		}
		break;
#endif // __ANDROID__

	// joystick enable
	case MENU_INPUT_JOYENABLE:
		if (down == false) {
			enable = list->GetCheck(MENU_INPUT_JOYENABLE);
			if (enable == true) {
				list->SetCheck(MENU_INPUT_JOYENABLE, false);
				list->SetCheck(MENU_INPUT_JOYTEST, false);
				setting->SetJoyEnable(false);
			}
			else {
				list->SetCheck(MENU_INPUT_JOYENABLE, true);
				list->SetCheck(MENU_INPUT_JOYTEST, true);
				setting->SetJoyEnable(true);
			}
		}
		break;

	// joystick button swap
	case MENU_INPUT_JOYSWAP:
		if (down == false) {
			swap = list->GetCheck(MENU_INPUT_JOYSWAP);
			if (swap == true) {
				list->SetCheck(MENU_INPUT_JOYSWAP, false);
				setting->SetJoySwap(false);
			}
			else {
				list->SetCheck(MENU_INPUT_JOYSWAP, true);
				setting->SetJoySwap(true);
			}
		}
		break;

	// joystick to keyboard
	case MENU_INPUT_JOYKEY:
		if (down == false) {
			enable = list->GetCheck(MENU_INPUT_JOYKEY);
			if (enable == true) {
				list->SetCheck(MENU_INPUT_JOYKEY, false);
				setting->SetJoyKey(false);
			}
			else {
				list->SetCheck(MENU_INPUT_JOYKEY, true);
				setting->SetJoyKey(true);
			}
		}
		break;

	// joystick to keyboard map
	case MENU_INPUT_JOYMAP:
		if (down == false) {
			EnterJoymap(MENU_JOYMAP_DPAD_UP);
		}
		break;

	// joystick test
	case MENU_INPUT_JOYTEST:
		if (down == false) {
			if (setting->IsJoyEnable() == true) {
				EnterJoyTest();
			}
		}
		break;

#ifndef __ANDROID__
	// mouse timeout
	case MENU_INPUT_MOUSETIME:
		setting->SetMouseTime((Uint32)list->GetSlider(MENU_INPUT_MOUSETIME));
		break;
#endif // !__ANDROID__

	// cursor to num pad
	case MENU_INPUT_CURSOR_NUM:
		if (down == false) {
			enable = list->GetCheck(MENU_INPUT_CURSOR_NUM);
			enable = !enable;
			list->SetCheck(MENU_INPUT_CURSOR_NUM, enable);
			setting->SetCursorToNumPad(enable);
			input->ChangeCursorToNumPad(enable);
		}
		break;

	// num to num pad
	case MENU_INPUT_NUM_PAD:
		if (down == false) {
			enable = list->GetCheck(MENU_INPUT_NUM_PAD);
			enable = !enable;
			list->SetCheck(MENU_INPUT_NUM_PAD, enable);
			setting->SetNumToNumPad(enable);
			input->ChangeNumToNumPad(enable);
		}
		break;

	default:
		break;
	}
}

//
// CmdReset()
// command (reset)
//
void Menu::CmdReset(int id)
{
	switch (id) {
	// yes
	case MENU_RESET_YES:
		app->Reset();
		app->LeaveMenu();
		break;

	// no
	case MENU_RESET_NO:
		EnterMain(MENU_MAIN_RESET);
		break;

	default:
		break;
	}
}

//
// CmdQuit()
// command (quit)
//
void Menu::CmdQuit(int id)
{
	switch (id) {
	// yes
	case MENU_QUIT_YES:
		// update text
		list->SetText(MENU_QUIT_YES, "Saving...");

		// draw
		Draw();
		video->Draw();

		// quit
		app->Quit();
		app->LeaveMenu();
		break;

	// no
	case MENU_QUIT_NO:
		app->LeaveMenu();
		break;

	default:
		break;
	}
}

//
// CmdSoftKey()
// command (softkey)
//
void Menu::CmdSoftKey(int id)
{
	int set;

	// get set
	set = softkey_id - MENU_INPUT_SOFTKEY1;

	// get type
	id -= MENU_SOFTKEY_0;

	// setting
	if (setting->SetSoftKeySet(set, id) == true) {
		// inform input of change sofkey set
		input->RebuildList();
	}

	// enter input menu
	EnterInput(softkey_id);
}

//
// CmdDip()
// command (dip)
//
void Menu::CmdDip(int id)
{
	switch (id) {
	// boot mode
	case MENU_DIP_BASICMODE:
		list->SetRadio(MENU_DIP_BASICMODE, MENU_DIP_BOOTMODE);
		setting->SetDip(setting->GetDip() & ~DIP_TERMINAL_MODE);
		break;
	case MENU_DIP_TERMMODE:
		list->SetRadio(MENU_DIP_TERMMODE, MENU_DIP_BOOTMODE);
		setting->SetDip(setting->GetDip() | DIP_TERMINAL_MODE);
		break;

	// boot width
	case MENU_DIP_WIDTH80:
		list->SetRadio(MENU_DIP_WIDTH80, MENU_DIP_WIDTH);
		setting->SetDip(setting->GetDip() & ~DIP_WIDTH_40);
		break;
	case MENU_DIP_WIDTH40:
		list->SetRadio(MENU_DIP_WIDTH40, MENU_DIP_WIDTH);
		setting->SetDip(setting->GetDip() | DIP_WIDTH_40);
		break;

	// boot line
	case MENU_DIP_LINE20:
		list->SetRadio(MENU_DIP_LINE20, MENU_DIP_LINE);
		setting->SetDip(setting->GetDip() & ~DIP_LINE_25);
		break;
	case MENU_DIP_LINE25:
		list->SetRadio(MENU_DIP_LINE25, MENU_DIP_LINE);
		setting->SetDip(setting->GetDip() | DIP_LINE_25);
		break;

	// boot from
	case MENU_DIP_FROMDISK:
		list->SetRadio(MENU_DIP_FROMDISK, MENU_DIP_BOOTFROM);
		setting->SetDip(setting->GetDip() & ~DIP_BOOT_ROM);
		break;
	case MENU_DIP_FROMROM:
		list->SetRadio(MENU_DIP_FROMROM, MENU_DIP_BOOTFROM);
		setting->SetDip(setting->GetDip() | DIP_BOOT_ROM);
		break;

	// memory wait
	case MENU_DIP_MEMWAIT_OFF:
		list->SetRadio(MENU_DIP_MEMWAIT_OFF, MENU_DIP_MEMWAIT);
		setting->SetDip(setting->GetDip() & ~DIP_MEM_WAIT);
		break;
	case MENU_DIP_MEMWAIT_ON:
		list->SetRadio(MENU_DIP_MEMWAIT_ON, MENU_DIP_MEMWAIT);
		setting->SetDip(setting->GetDip() | DIP_MEM_WAIT);
		break;

	// baud rate
	case MENU_DIP_BAUD75:
		list->SetRadio(MENU_DIP_BAUD75, MENU_DIP_BAUDRATE);
		setting->SetDip((setting->GetDip() & ~DIP_BAUDRATE) | (7 << DIP_BAUDRATE_SHIFT));
		break;
	case MENU_DIP_BAUD150:
		list->SetRadio(MENU_DIP_BAUD150, MENU_DIP_BAUDRATE);
		setting->SetDip((setting->GetDip() & ~DIP_BAUDRATE) | (8 << DIP_BAUDRATE_SHIFT));
		break;
	case MENU_DIP_BAUD300:
		list->SetRadio(MENU_DIP_BAUD300, MENU_DIP_BAUDRATE);
		setting->SetDip((setting->GetDip() & ~DIP_BAUDRATE) | (9 << DIP_BAUDRATE_SHIFT));
		break;
	case MENU_DIP_BAUD600:
		list->SetRadio(MENU_DIP_BAUD600, MENU_DIP_BAUDRATE);
		setting->SetDip((setting->GetDip() & ~DIP_BAUDRATE) | (10 << DIP_BAUDRATE_SHIFT));
		break;
	case MENU_DIP_BAUD1200:
		list->SetRadio(MENU_DIP_BAUD1200, MENU_DIP_BAUDRATE);
		setting->SetDip((setting->GetDip() & ~DIP_BAUDRATE) | (11 << DIP_BAUDRATE_SHIFT));
		break;
	case MENU_DIP_BAUD2400:
		list->SetRadio(MENU_DIP_BAUD2400, MENU_DIP_BAUDRATE);
		setting->SetDip((setting->GetDip() & ~DIP_BAUDRATE) | (12 << DIP_BAUDRATE_SHIFT));
		break;
	case MENU_DIP_BAUD4800:
		list->SetRadio(MENU_DIP_BAUD4800, MENU_DIP_BAUDRATE);
		setting->SetDip((setting->GetDip() & ~DIP_BAUDRATE) | (13 << DIP_BAUDRATE_SHIFT));
		break;
	case MENU_DIP_BAUD9600:
		list->SetRadio(MENU_DIP_BAUD9600, MENU_DIP_BAUDRATE);
		setting->SetDip((setting->GetDip() & ~DIP_BAUDRATE) | (14 << DIP_BAUDRATE_SHIFT));
		break;
	case MENU_DIP_BAUD19200:
		list->SetRadio(MENU_DIP_BAUD19200, MENU_DIP_BAUDRATE);
		setting->SetDip((setting->GetDip() & ~DIP_BAUDRATE) | (15 << DIP_BAUDRATE_SHIFT));
		break;

	// duplex
	case MENU_DIP_HALFDUPLEX:
		list->SetRadio(MENU_DIP_HALFDUPLEX, MENU_DIP_DUPLEX);
		setting->SetDip(setting->GetDip() | DIP_HALFDUPLEX);
		break;
	case MENU_DIP_FULLDUPLEX:
		list->SetRadio(MENU_DIP_FULLDUPLEX, MENU_DIP_DUPLEX);
		setting->SetDip(setting->GetDip() & ~DIP_HALFDUPLEX);
		break;

	// data bit
	case MENU_DIP_DATA8BIT:
		list->SetRadio(MENU_DIP_DATA8BIT, MENU_DIP_DATABIT);
		setting->SetDip(setting->GetDip() & ~DIP_DATA7BIT);
		break;
	case MENU_DIP_DATA7BIT:
		list->SetRadio(MENU_DIP_DATA7BIT, MENU_DIP_DATABIT);
		setting->SetDip(setting->GetDip() | DIP_DATA7BIT);
		break;

	// stop bit
	case MENU_DIP_STOP2BIT:
		list->SetRadio(MENU_DIP_STOP2BIT, MENU_DIP_STOPBIT);
		setting->SetDip(setting->GetDip() | DIP_STOP2BIT);
		break;
	case MENU_DIP_STOP1BIT:
		list->SetRadio(MENU_DIP_STOP1BIT, MENU_DIP_STOPBIT);
		setting->SetDip(setting->GetDip() & ~DIP_STOP2BIT);
		break;

	// X parameter
	case MENU_DIP_XON:
		list->SetRadio(MENU_DIP_XON, MENU_DIP_X);
		setting->SetDip(setting->GetDip() & ~DIP_DISABLE_X);
		break;
	case MENU_DIP_XOFF:
		list->SetRadio(MENU_DIP_XOFF, MENU_DIP_X);
		setting->SetDip(setting->GetDip() | DIP_DISABLE_X);
		break;

	// S parameter
	case MENU_DIP_SON:
		list->SetRadio(MENU_DIP_SON, MENU_DIP_S);
		setting->SetDip(setting->GetDip() | DIP_ENABLE_S);
		break;
	case MENU_DIP_SOFF:
		list->SetRadio(MENU_DIP_SOFF, MENU_DIP_S);
		setting->SetDip(setting->GetDip() & ~DIP_ENABLE_S);
		break;

	// DEL code
	case MENU_DIP_DELON:
		list->SetRadio(MENU_DIP_DELON, MENU_DIP_DEL);
		setting->SetDip(setting->GetDip() & ~DIP_DISABLE_DEL);
		break;
	case MENU_DIP_DELOFF:
		list->SetRadio(MENU_DIP_DELOFF, MENU_DIP_DEL);
		setting->SetDip(setting->GetDip() | DIP_DISABLE_DEL);
		break;

	// parity
	case MENU_DIP_NOPARITY:
		list->SetRadio(MENU_DIP_NOPARITY, MENU_DIP_PARITY);
		setting->SetDip((setting->GetDip() & ~DIP_PARITY) | (0 << DIP_PARITY_SHIFT));
		break;
	case MENU_DIP_EVENPARITY:
		list->SetRadio(MENU_DIP_EVENPARITY, MENU_DIP_PARITY);
		setting->SetDip((setting->GetDip() & ~DIP_PARITY) | (1 << DIP_PARITY_SHIFT));
		break;
	case MENU_DIP_ODDPARITY:
		list->SetRadio(MENU_DIP_ODDPARITY, MENU_DIP_PARITY);
		setting->SetDip((setting->GetDip() & ~DIP_PARITY) | (2 << DIP_PARITY_SHIFT));
		break;

	// restore default settings
	case MENU_DIP_DEFAULT:
		list->SetRadio(MENU_DIP_BASICMODE, MENU_DIP_BOOTMODE);
		setting->SetDip(setting->GetDip() & ~DIP_TERMINAL_MODE);
		list->SetRadio(MENU_DIP_WIDTH80, MENU_DIP_WIDTH);
		setting->SetDip(setting->GetDip() & ~DIP_WIDTH_40);
		list->SetRadio(MENU_DIP_LINE20, MENU_DIP_LINE);
		setting->SetDip(setting->GetDip() | DIP_LINE_25);
		list->SetRadio(MENU_DIP_FROMDISK, MENU_DIP_BOOTFROM);
		setting->SetDip(setting->GetDip() & ~DIP_BOOT_ROM);
		list->SetRadio(MENU_DIP_MEMWAIT_OFF, MENU_DIP_MEMWAIT);
		setting->SetDip(setting->GetDip() & ~DIP_MEM_WAIT);
		list->SetRadio(MENU_DIP_BAUD1200, MENU_DIP_BAUDRATE);
		setting->SetDip((setting->GetDip() & ~DIP_BAUDRATE) | (11 << DIP_BAUDRATE_SHIFT));
		list->SetRadio(MENU_DIP_FULLDUPLEX, MENU_DIP_DUPLEX);
		setting->SetDip(setting->GetDip() & ~DIP_HALFDUPLEX);
		list->SetRadio(MENU_DIP_DATA8BIT, MENU_DIP_DATABIT);
		setting->SetDip(setting->GetDip() & ~DIP_DATA7BIT);
		list->SetRadio(MENU_DIP_STOP1BIT, MENU_DIP_STOPBIT);
		setting->SetDip(setting->GetDip() & ~DIP_STOP2BIT);
		list->SetRadio(MENU_DIP_XON, MENU_DIP_X);
		setting->SetDip(setting->GetDip() & ~DIP_DISABLE_X);
		list->SetRadio(MENU_DIP_SOFF, MENU_DIP_S);
		setting->SetDip(setting->GetDip() & ~DIP_ENABLE_S);
		list->SetRadio(MENU_DIP_DELON, MENU_DIP_DEL);
		setting->SetDip(setting->GetDip() & ~DIP_DISABLE_DEL);
		list->SetRadio(MENU_DIP_NOPARITY, MENU_DIP_PARITY);
		setting->SetDip((setting->GetDip() & ~DIP_PARITY) | (0 << DIP_PARITY_SHIFT));

		// version 1.70
		EnterDipSub();
		break;
	}
}

//
// CmdJoymap()
// command (joymap)
//
void Menu::CmdJoymap(int id)
{
	int data;
	int loop;

	// restore default ?
	if (id == MENU_JOYMAP_DEFAULT) {
		setting->DefJoystickToKey();
		return;
	}

	// save id
	joymap_id = id;

	// get number 0 to 14
	id -= MENU_JOYMAP_DPAD_UP;
	if ((id >= 0) && (id < 15)) {
		// get data
		data = (int)setting->GetJoystickToKey(id);

		// find MENU_VMKEY id
		for (loop=0; loop < (int)(SDL_arraysize(vmkey_table) / 2); loop++) {
			// compare data with vmkey table data
			if (vmkey_table[loop * 2 + 1] == data) {
				// equal
				EnterVmKey(vmkey_table[loop * 2 + 0]);
				break;
			}
		}
	}
}

//
// CmdVmKey()
// command (vmkey)
//
void Menu::CmdVmKey(int id)
{
	Uint32 data;
	int loop;

	// initialize
	data = 0x10000;

	switch (id) {
	case MENU_VMKEY_MENU:
		data = 0x1000;
		break;

	case MENU_VMKEY_NEXT:
		data = 0x1001;
		break;

	case MENU_VMKEY_PREV:
		data = 0x1002;
		break;

	default:
		// find loop
		for (loop = 0; loop < (int)(SDL_arraysize(vmkey_table) / 2); loop++) {
			// compare id
			if (vmkey_table[loop * 2 + 0] == id) {
				// equal
				data = (Uint32)vmkey_table[loop * 2 + 1];
				break;
			}
		}
		break;
	}

	// set
	if (data < 0x10000) {
		setting->SetJoystickToKey(joymap_id - MENU_JOYMAP_DPAD_UP, data);
	}

	EnterJoymap(joymap_id);
}

//
// CmdAudioOut()
// command (audio out)
//
void Menu::CmdAudioOut(bool down, int id)
{
	// get current audio device
	int current_audio_device = setting->GetAudioDevice();

	if (down == false) {
		list->SetRadio(id, MENU_AUDIO_DEVICE);
		setting->SetAudioDevice(id - MENU_AUDIO_DEVICE_MIN);
	}

	// rebuild audio component if freq or buffer has been changed
	if (setting->GetAudioDevice() != current_audio_device) {
		app->ChangeAudio();
	}
}

//
// CmdScalingFilter()
// command (Scaling Filter)
//
void Menu::CmdScalingFilter(bool down, int id)
{
	const char* quality = setting->GetScaleQuality();
	if (down == false) {
		int filterId = (id - MENU_SCALEFILTER_MIN - 1);
		if ((*quality - '0') != filterId) {
			setting->SetScaleQuality(filterId);
			EnterScalingFilter();
			video->RebuildTexture(false);
		}
	}
}

//
// CmdFile()
// command (file)
//
void Menu::CmdFile(int id)
{
	const char *name;
	bool ret;
	bool drive2;

	// get file or directory name
	name = list->GetName(id);
	if (name == NULL) {
		return;
	}

	// copy dir to target
	strcpy(file_target, file_dir);

	// directory ?
	if (platform->IsDir(static_cast<Uint32>(list->GetUser(id))) == true) {
#ifdef __ANDROID__
		if (Android_ChDir(file_target, name) != 0) {
			MakeExpect(name);
			strcpy(file_dir, file_target);
			EnterFile();
			return;
		}
#endif // __ANDROID__

		if (platform->MakePath(file_target, name, true) == true) {
			MakeExpect(name);
			strcpy(file_dir, file_target);
			EnterFile();
		}
		return;
	}

	// normal file
	if (platform->MakePath(file_target, name, false) == false) {
		return;
	}
	if (IsM3UPath(file_target)) {
		if (file_id == MENU_DRIVE1_BOTH || file_id == MENU_DRIVE2_BOTH) {
			std::vector<DiskSpec> specs;
			std::string error;
			if (!LoadPlaylistDiskSpecs(file_target, 0, MAX_DRIVE, &specs, &error) ||
				!app->OpenDiskSpecsFromMenu(specs, &error, specs.size() == 1)) {
				platform->MsgBox(NULL, error.c_str());
				return;
			}
			EnterDrive1(MENU_DRIVE1_BANK0);
			return;
		}
		if (file_id == MENU_DRIVE1_OPEN || file_id == MENU_DRIVE2_OPEN) {
			EnterPlaylist(file_target);
			return;
		}
	}

	// tape ?
	if ((file_id == MENU_CMT_PLAY) || (file_id == MENU_CMT_REC)) {
		// eject tape
		tapemgr->Eject();

		// play or rec
		if (file_id == MENU_CMT_PLAY) {
			ret = tapemgr->Play(file_target);
		}
		else {
			ret = tapemgr->Rec(file_target);
		}

		if (ret == true) {
			if (file_id == MENU_CMT_PLAY) {
				EnterCmt(MENU_CMT_PLAY);
			}
			else {
				EnterCmt(MENU_CMT_REC);
			}
		}
		return;
	}

	// disk
	switch (file_id) {
	case MENU_DRIVE1_OPEN:
	case MENU_DRIVE1_BOTH:
	case MENU_DRIVE2_BOTH:
	{
		std::string error;
		drive2 = false;
		ret = file_id == MENU_DRIVE1_OPEN ?
			app->OpenDiskFromMenu({file_target, 0, 0}, &error) :
			app->OpenDiskPairFromMenu(file_target, &drive2, &error);

		if (ret == true) {
			if (file_id == MENU_DRIVE2_BOTH) {
				if (drive2 == true) {
					EnterDrive2(MENU_DRIVE2_BANK0 + 1);
				}
				else {
					EnterDrive2(MENU_DRIVE2_OPEN);
				}
			}
			else {
				EnterDrive1(MENU_DRIVE1_BANK0);
			}
		}
		else {
			platform->MsgBox(NULL, error.empty() ?
				"failed to open disk" : error.c_str());
		}
		break;
	}

	case MENU_DRIVE2_OPEN:
	{
		std::string error;
		// drive 2
		ret = app->OpenDiskFromMenu({file_target, 1, 0}, &error);
		if (ret == true) {
			EnterDrive2(MENU_DRIVE2_BANK0);
		}
		else {
			platform->MsgBox(NULL, error.empty() ?
				"failed to open disk in Drive 2" : error.c_str());
		}
		break;
	}

	default:
		break;
	}
}

void Menu::CmdPlaylist(int id)
{
	const size_t index = static_cast<size_t>(id - MENU_PLAYLIST_ENTRY_MIN);
	if (index >= playlist_entries.size()) return;
	const bool pair = file_id == MENU_DRIVE1_BOTH || file_id == MENU_DRIVE2_BOTH;
	if (!pair) {
		DiskSpec spec = playlist_entries[index];
		spec.drive = playlist_drive;
		std::string error;
		if (!app->OpenDiskFromMenu(spec, &error)) {
			platform->MsgBox(NULL, error.c_str());
			return;
		}
		if (playlist_drive == 0) EnterDrive1(MENU_DRIVE1_BANK0);
		else EnterDrive2(MENU_DRIVE2_BANK0);
		return;
	}
	if (playlist_first < 0) {
		playlist_first = static_cast<int>(index);
		list->SetTitle("<< Playlist: select Drive 2 >>", MENU_PLAYLIST);
		for (size_t entry_index = 0; entry_index < playlist_entries.size(); ++entry_index) {
			const std::string& entry = playlist_entries[entry_index].path;
			const size_t slash = entry.find_last_of("/\\");
			list->AddButton(entry.substr(slash == std::string::npos ? 0 : slash + 1).c_str(),
				MENU_PLAYLIST_ENTRY_MIN + static_cast<int>(entry_index));
		}
		return;
	}
	DiskSpec first = playlist_entries[playlist_first];
	DiskSpec second = playlist_entries[index];
	first.drive = 0;
	second.drive = 1;
	std::string error;
	if (!app->OpenDiskSpecsFromMenu({first, second}, &error)) {
		platform->MsgBox(NULL, error.c_str());
		return;
	}
	EnterDrive1(MENU_DRIVE1_BANK0);
}

//
// MakeExpect()
// make file_exepct[]
//
void Menu::MakeExpect(const char *name)
{
	char *ptr;
	char *last;

	// init
	file_expect[0] = '\0';

	// directory up only
	if ((strcmp(name, "..\\") != 0) && (strcmp(name, "../") != 0)) {
		return;
	}

	// find last terminator from file_dir[]
	ptr = file_dir;
	last = file_dir;

	// search last '\\' or '/'
	while (*ptr != '\0') {
		if ((*ptr == '\\') || (*ptr == '/')) {
			if (ptr[1] != '\0') {
				last = ptr + 1;
			}
		}
		ptr++;
	}

	// set directory name to file_expect[]
#if defined(_WIN32) && defined(UNICODE)
	converter->UtfToSjis(last, file_expect);
#else
	strcpy(file_expect, last);
#endif
}

//
// JoyTest()
// joystick test
//
void Menu::JoyTest()
{
	Uint32 status[2];
	int id;
	int loop;

	// get status
	input->GetJoystick(status);

	// check A+B to quit
	if ((status[0] & 0x30) == 0x30) {
		// quit
		EnterInput(MENU_INPUT_JOYTEST);
		return;
	}

	// init id
	id = MENU_JOYTEST_BUTTON1;

	// button loop
	for (loop=0; loop<15; loop++) {
		// check button
		if (((status[0] & joytest_table[loop * 2 + 0]) != 0) || ((status[1] & joytest_table[loop * 2 + 1]) != 0)) {
			// pressed
			list->SetText(id, joytest_name[loop]);
			id++;
		}
	}

	// others
	while (id <= MENU_JOYTEST_BUTTON15) {
		list->SetText(id, "");
		id++;
	}
}

//
// Draw()
// draw menu screen
//
void Menu::Draw()
{
	list->Draw();
}

//
// OnKeyDown()
// key down
//
void Menu::OnKeyDown(SDL_Event *e)
{
	list->OnKeyDown(e);
}

//
// OnMouseMotion()
// mouse motion
//
void Menu::OnMouseMotion(SDL_Event *e)
{
	list->OnMouseMotion(e);
}

//
// OnMouseButtonDown()
// mouse button down
//
void Menu::OnMouseButtonDown(SDL_Event *e)
{
	list->OnMouseButtonDown(e);
}

//
// OnMouseButtonUp()
// mouse button up
//
void Menu::OnMouseButtonUp(SDL_Event *e)
{
	list->OnMouseButtonUp(e);
}

//
// OnMouseWheel()
// mouse wheel
//
void Menu::OnMouseWheel(SDL_Event *e)
{
	list->OnMouseWheel(e);
}

//
// OnJoystick()
// joystick
//
void Menu::OnJoystick()
{
	if (list->GetID() == MENU_JOYTEST) {
		// call directly
		JoyTest();
	}
	else {
		// normal
		list->OnJoystick();
	}
}

//
// OnFingerDown()
// finger down
//
void Menu::OnFingerDown(SDL_Event *e)
{
	list->OnFingerDown(e);
}

//
// OnFingerUp()
// finger up
//
void Menu::OnFingerUp(SDL_Event *e)
{
	list->OnFingerUp(e);
}

//
// OnFingerMotion()
// finger motion
//
void Menu::OnFingerMotion(SDL_Event *e)
{
	list->OnFingerMotion(e);
}

//
// MENU_VMKEY to data table
//
const int Menu::vmkey_table[62 * 2] = {
	MENU_VMKEY_MENU,	0x1000,
	MENU_VMKEY_NEXT,	0x1001,
	MENU_VMKEY_PREV,	0x1002,
	MENU_VMKEY_TEN0,	0x0000,
	MENU_VMKEY_TEN1,	0x0001,
	MENU_VMKEY_TEN2,	0x0002,
	MENU_VMKEY_TEN3,	0x0003,
	MENU_VMKEY_TEN4,	0x0004,
	MENU_VMKEY_TEN5,	0x0005,
	MENU_VMKEY_TEN6,	0x0006,
	MENU_VMKEY_TEN7,	0x0007,
	MENU_VMKEY_TEN8,	0x0100,
	MENU_VMKEY_TEN9,	0x0101,
	MENU_VMKEY_F1,		0x0901,
	MENU_VMKEY_F2,		0x0902,
	MENU_VMKEY_F3,		0x0903,
	MENU_VMKEY_F4,		0x0904,
	MENU_VMKEY_F5,		0x0905,
	MENU_VMKEY_ESC,		0x0907,
	MENU_VMKEY_SPACE,	0x0906,
	MENU_VMKEY_RETURN,	0x0e00,
	MENU_VMKEY_DEL,		0x0c07,
	MENU_VMKEY_HOMECLR,	0x0800,
	MENU_VMKEY_HELP,	0x0a03,
	MENU_VMKEY_SHIFT,	0x0e02,
	MENU_VMKEY_CTRL,	0x0807,
	MENU_VMKEY_CAPS,	0x0a07,
	MENU_VMKEY_KANA,	0x0805,
	MENU_VMKEY_GRPH,	0x0804,
	MENU_VMKEY_A,		0x0201,
	MENU_VMKEY_B,		0x0202,
	MENU_VMKEY_C,		0x0203,
	MENU_VMKEY_D,		0x0204,
	MENU_VMKEY_E,		0x0205,
	MENU_VMKEY_F,		0x0206,
	MENU_VMKEY_G,		0x0207,
	MENU_VMKEY_H,		0x0300,
	MENU_VMKEY_I,		0x0301,
	MENU_VMKEY_J,		0x0302,
	MENU_VMKEY_K,		0x0303,
	MENU_VMKEY_L,		0x0304,
	MENU_VMKEY_M,		0x0305,
	MENU_VMKEY_N,		0x0306,
	MENU_VMKEY_O,		0x0307,
	MENU_VMKEY_P,		0x0400,
	MENU_VMKEY_Q,		0x0401,
	MENU_VMKEY_R,		0x0402,
	MENU_VMKEY_S,		0x0403,
	MENU_VMKEY_T,		0x0404,
	MENU_VMKEY_U,		0x0405,
	MENU_VMKEY_V,		0x0406,
	MENU_VMKEY_W,		0x0407,
	MENU_VMKEY_X,		0x0500,
	MENU_VMKEY_Y,		0x0501,
	MENU_VMKEY_Z,		0x0502,
	MENU_VMKEY_UP,		0x0801,
	MENU_VMKEY_DOWN,	0x0A01,
	MENU_VMKEY_LEFT,	0x0A02,
	MENU_VMKEY_RIGHT,	0x0802,
	MENU_VMKEY_ROLLUP,	0x0B00,
	MENU_VMKEY_ROLLDOWN,0x0B01,
	MENU_VMKEY_TAB,		0x0A00
};

//
// MENU_JOYTEST table
// see input.cpp (Input::joystick_button[])
//
const Uint32 Menu::joytest_table[15 * 2] = {
	0x00000001, 0x00000000,				// SDL_BUTTON_DPAD_UP
	0x00000002, 0x00000000,				// SDL_BUTTON_DPAD_DOWN
	0x00000004, 0x00000000,				// SDL_BUTTON_DPAD_LEFT
	0x00000008, 0x00000000,				// SDL_BUTTON_DPAD_RIGHT
	0x00000010, 0x00000000,				// SDL_BUTTON_A
	0x00000020, 0x00000000,				// SDL_BUTTON_B
	0x00000040, 0x00000000,				// SDL_BUTTON_X
	0x00000080, 0x00000000,				// SDL_BUTTON_Y
	0x00000000, 0x00000001,				// SDL_BUTTON_BACK
	0x00000000, 0x00000002,				// SDL_BUTTON_GUIDE
	0x00000000, 0x00000004,				// SDL_BUTTON_START
	0x00000000, 0x00000008,				// SDL_BUTTON_LEFTSTICK
	0x00000000, 0x00000010,				// SDL_BUTTON_RIGHTSTICK
	0x00000000, 0x00000020,				// SDL_BUTTON_LEFTSHOULDER
	0x00000000, 0x00000040				// SDL_BUTTON_RIGHTSHOULDER
};

//
// MENU_JOYTEST name table
//
const char* Menu::joytest_name[15] = {
	"SDL_BUTTON_DPAD_UP",
	"SDL_BUTTON_DPAD_DOWN",
	"SDL_BUTTON_DPAD_LEFT",
	"SDL_BUTTON_DPAD_RIGHT",
	"SDL_BUTTON_A",
	"SDL_BUTTON_B",
	"SDL_BUTTON_X",
	"SDL_BUTTON_Y",
	"SDL_BUTTON_BACK",
	"SDL_BUTTON_GUIDE",
	"SDL_BUTTON_START",
	"SDL_BUTTON_LEFTSTICK",
	"SDL_BUTTON_RIGHTSTICK",
	"SDL_BUTTON_LEFTSHOULDER",
	"SDL_BUTTON_RIGHTSHOULDER"
};

#endif // SDL
