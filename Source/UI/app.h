//
// eXcellent Multi-platform emulator type 8 - 'XM8'
// based on ePC-8801MA
//
// Author (ePC-8801MA) : Takeda.Toshiya
// Author (XM8) : Tanaka.Yasushi
//
// [ application ]
//

#ifdef SDL

#ifndef APP_H
#define APP_H

#include "classes.h"
#include "clidisk.h"

// common.h defines min/max macros for the legacy emulator code. Keep them
// away from C++ standard and RetroAchievements headers, which use min()/max()
// member functions (notably <chrono> on GCC).
#ifdef min
#pragma push_macro("min")
#undef min
#define APP_RESTORE_MIN_MACRO
#endif
#ifdef max
#pragma push_macro("max")
#undef max
#define APP_RESTORE_MAX_MACRO
#endif

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#ifdef XM8_ENABLE_RETROACHIEVEMENTS
#include "ra_connectivity.h"
#include "ra_library.h"
#include "ra_media_change_policy.h"
#include "ra_disk_transaction.h"
#include "ra_media_operation_runner.h"
#include "diskmounttargets.h"
#include "ra_media_store.h"
#include "ra_menu_status.h"
#include "ra_overlay.h"
#include "ra_service.h"
#include "ra_session_policy.h"
#include "ra_session_state.h"
#include "ra_state_store.h"
#endif

#ifdef APP_RESTORE_MAX_MACRO
#pragma pop_macro("max")
#undef APP_RESTORE_MAX_MACRO
#endif
#ifdef APP_RESTORE_MIN_MACRO
#pragma pop_macro("min")
#undef APP_RESTORE_MIN_MACRO
#endif

const char* GetAppVersionString();

//
// XM8 application
//
class App
{
	friend class AppMediaTestAccess;
public:
	App();
										// constructor
	~App();
										// destructor
	bool Init(const CliOptions& options);
										// initialize
	void Deinit();
										// deinitialize

	// get component
	Setting* GetSetting();
										// get Setting instance
	Platform* GetPlatform();
										// get platform instance
	Audio* GetAudio();
										// get Audio instance
	Video* GetVideo();
										// get Video instance
	Font* GetFont();
										// get Font instance
	Input* GetInput();
										// get Input instance
	Converter* GetConverter();
										// get Converter instance
	Menu* GetMenu();
										// get Menu instance
	EMU_SDL* GetWrapper();
										// get EMU_SDL instance
	EMU* GetEmu();
										// get EMU instance
	DiskManager** GetDiskManager();
										// get DiskManager instance array
	TapeManager* GetTapeManager();
										// get TapeManager instance

	// run
	void Run();
										// run

	// mode
	void FullScreen();
										// full screen
	void WindowScreen();
										// window screen
	bool IsFullScreen();
										// get full screen flag
	void FullSpeed();
										// full speed
	void NormalSpeed();
										// normal speed
	bool IsFullSpeed();
										// get full speed flag
	void SetWindowWidth();
										// set window width
#ifdef __ANDROID__
	void ApplyAndroidRotationMode();
										// apply Android rotation mode
#endif

	// action
	void OnKeyVM(SDL_Scancode code);
										// key down to vm
	void GetKeyVM(Uint8 *buf);
										// get key buffer from vm
	Uint32 GetKeyCode(Uint32 port, Uint32 bit);
										// get keycode from vm
	void EnterMenu(int id);
										// menu mode
	void LeaveMenu(bool check = true);
										// run mode
	void ChangeAudio();
										// change audio parameter
	void ChangeSystem(bool load = false);
										// change system
	const char* GetDiskDir(int drive = -1);
										// get disk dir
	void RememberDiskOpenDir(const char *path);
										// remember user-selected disk directory
	bool OpenDiskFromMenu(const DiskSpec& spec, std::string *error);
										// open disk from menu
	bool ChangeDiskBankFromMenu(int drive, int bank, std::string *error);
										// change mounted D88 bank through RA policy
	bool EjectDiskFromMenu(int drive, std::string *error);
										// eject after serializing with RA media work
	bool OpenDiskPairFromMenu(const std::string& path, bool *drive2_open,
		std::string *error);
										// open bank 0/1 as one transaction
	bool OpenDiskSpecsFromMenu(const std::vector<DiskSpec>& specs,
		std::string *error, bool close_drive2 = false);
	const char* GetTapeDir();
										// get tape dir
	void Reset();
										// reset
	bool Load(int slot);
										// load state
	bool Save(int slot);
										// save state
	bool GetStateTime(int slot, cur_time_t *cur_time);
										// get state time
	void Quit();
										// quit
#ifdef XM8_ENABLE_RETROACHIEVEMENTS
	bool IsRaModeEnabled() const;
										// get RA mode setting
	bool IsRaRuntimeSupported() const;
										// true when the current OS can securely run RA
	bool CheckRaStateAvailability(bool save = false,
		bool hardcore_debug = false);
										// validate RA state menu access and notify on failure
	bool LoadRaDebugState(int slot);
	bool GetRaDebugStateTime(int slot, cur_time_t *cur_time);
	bool ToggleRaMode();
										// toggle RA mode setting
	bool ToggleRaPlayMode();
										// toggle Casual/Hardcore setting
	bool IsRaHardcoreSelected() const;
										// get persisted RA play mode
	bool IsRaHardcoreActive() const;
										// get effective Hardcore session state
	bool IsRaCasualActive() const;
										// get effective Casual session state
	bool IsRaOfflineActive() const;
										// true while the current RA session is offline
	bool ToggleFastDisk();
										// toggle pseudo fast disk through RA policy
	bool OpenRaLoginOverlay();
										// open RA password login overlay
	#ifdef __ANDROID__
	void QueueAndroidRaLogin(const char *username, const char *password,
		bool canceled);
	#endif
	void OpenRaLibraryOverlay();
										// open RA library overlay
	Xm8Ra::RaOverlayLibraryListSnapshot GetRaLibraryMenuSnapshot() const;
	void ShowRaLibraryMenu();
	void SelectRaLibraryMenuItem(size_t index);
	void SelectRaLibraryMenuItemById(int64_t game_id);
	bool OpenRaLibraryMenuDetail();
	bool ActivateRaLibraryMenuDetail();
	void OpenRaAchievementsOverlay();
										// open RA achievements overlay
	Xm8Ra::RaOverlayAchievementListSnapshot GetRaAchievementsMenuSnapshot() const;
	void ShowRaAchievementsMenu();
	void SelectRaAchievementMenuItem(size_t index);
	void SelectRaAchievementMenuItemById(uint32_t achievement_id);
	bool OpenRaAchievementMenuDetail();
	void ScrollRaAchievementMenuDetail(int delta);
	void SetRaAchievementMenuDetailScroll(int offset);
	void OpenRaLeaderboardsOverlay();
										// open RA leaderboards overlay
	Xm8Ra::RaOverlayLeaderboardListSnapshot GetRaLeaderboardsMenuSnapshot() const;
	void ShowRaLeaderboardsMenu();
	void SelectRaLeaderboardMenuItem(size_t index);
	void SelectRaLeaderboardMenuItemById(uint32_t leaderboard_id);
	void SetRaMenuFirstVisibleItem(size_t index);
	void OpenRaWebsite();
										// open RetroAchievements in the default browser
	void OpenRaPrivacyPolicy();
										// open XM8M privacy policy
	void CloseRaOverlayToMenu();
	void CloseRaMenuContent();
										// close RA overlay and return to RA menu
	bool IsRaLoggedIn() const;
										// get RA login state
	bool LogoutRa(bool delete_pending = false);
										// logout RA
	bool GetRaPendingUnlockCount(size_t *count);
										// query pending unlocks for current account
	void GetRaMenuStatus(char *buffer, size_t capacity) const;
										// get RA status text for menu
	void GetRaMenuPresence(char *buffer, size_t capacity) const;
										// get Rich Presence text for menu
	uint32_t ReadRaInspectionMemory(uint32_t address, uint8_t *buffer,
		uint32_t num_bytes) const;
										// read current VM memory for RA
#endif

	// misc
	Uint32 GetAppVersion();
										// get version
	const char* GetAppTitle();
										// get application title
	void* GetEvMgr();
										// get event manager
	bool IsRaOverlayBlocking() const;
										// check blocking RA overlay

private:
	// drawing
	void Draw();
										// rendering
	double GetFrameRate();
										// calculate frame rate

	// power management
	void PowerMng();
										// power management

#ifdef __ANDROID__
	bool ProcessIntent(std::string *error = nullptr);
										// process intent
#endif // __ANDROID__

	// event
	void Poll(SDL_Event *e);
										// poll event
	void OnWindow(SDL_Event *e);
										// window event
	void OnKeyDown(SDL_Event *e);
										// key down event
	void OnKeyUp(SDL_Event *e);
										// key up event
	void OnDropFile(SDL_Event *e);
										// drag & drop event
	bool ApplyCommandLineSettings(const CliOptions& options);
										// apply temporary CLI settings
	void RestoreCommandLineSettings();
										// restore persistent settings
	bool ProbeDisk(const DiskSpec& spec, int *banks, std::string *error);
										// validate disk specification
	struct PreparedDisk {
		DiskSpec spec;
#ifdef XM8_ENABLE_RETROACHIEVEMENTS
		Xm8Ra::ImportedMedia media;
#endif
	};
	bool OpenDiskFromUser(const DiskSpec& spec, std::string *error,
		bool open_pair = false, bool reset_after_commit = false,
		const PreparedDisk *prepared = NULL);
										// open one disk
	bool OpenDiskSpecsFromUser(const std::vector<DiskSpec>& specs,
		std::string *error, bool close_drive2, bool reset_after_commit);
										// shared menu/CLI/playlist batch
#ifdef XM8_ENABLE_RETROACHIEVEMENTS
	bool OpenLocalDiskBatch(const std::vector<DiskSpec>& specs,
		std::string *error, bool close_drive2);
	bool ResolveDiskForRaMode(const DiskSpec& spec, DiskSpec *resolved,
		std::string *ra_hash_to_identify, int64_t *ra_game_to_identify,
		Xm8Ra::RaDiskAction *action, std::string *error,
		const Xm8Ra::ImportedMedia *prepared = NULL);
										// resolve disk to RA working copy
	bool BeginRaMediaChange(const DiskSpec& target, const std::string& hash,
		bool open_pair, int target_banks, bool reset_after_commit,
		std::string *error);
										// begin same-game media change
	bool TryBeginRaPreparedBatch(const std::vector<PreparedDisk>& prepared,
		bool close_drive2, bool reset, bool *handled, std::string *error);
	bool BeginRaMediaChangeTargets(const DiskSpec& target, const std::string& hash,
		const DiskMountTargets& targets, bool reset_after_commit, std::string *error);
	void ProcessRaMediaChange();
										// commit or roll back pending media change
	void CommitRaMediaChangeOffline(const std::string& message);
										// preserve targets across RA session termination
	void ClearRaMediaChangeState();
										// clear App media change transaction
	bool BeginRaAuxiliaryValidation(const DiskSpec& target,
		const std::string& hash, int64_t expected_ra_game_id,
		bool persist_pair, bool reset_after_commit, bool completes_launch,
		std::string *error);
										// verify Drive 2 without changing active RA media
	bool AttachDrive2ToRaAnchorLaunch(const DiskSpec& anchor,
		const DiskSpec& auxiliary, bool reset_after_commit, std::string *error,
		const Xm8Ra::ImportedMedia *prepared = NULL);
										// complete one normalized two-drive request
	void ProcessRaAuxiliaryValidation();
	void ClearRaAuxiliaryValidationState();
	void EnterRaOfflineSession(const std::string& message, bool preserve_media_operation = false);
										// stop RA evaluation for the current game
	void SetRaMenuStatusAfterSessionStop();
										// set the non-game RA menu state
	void SetRaMenuStatusForConnectivity(bool disconnected);
										// reflect a session connectivity transition
	void StopRaSession();
										// end current game session and allow a new launch
	void ProcessRaLibrarySync();
										// start or commit idle library synchronization
	bool RememberRaSourceDirForMountedDisk(int drive);
										// remember source dir for mounted RA media
	bool RememberRaLaunchDriveForMountedDisk(int drive, std::string *error);
										// persist a mounted disk in its RA launch profile
	bool RememberRaLaunchPairForMountedDisks(std::string *error);
										// persist both drives in one launch-profile transaction
	bool EnsureRaService(std::string *error);
										// create RA service if needed
	bool SaveRaModeSetting(bool enabled, std::string *error);
										// persist RA mode setting
	bool SaveRaPlayModeSetting(Xm8Ra::RaPlayMode mode, std::string *error);
										// persist Casual/Hardcore setting
	Xm8Ra::RaSessionPolicyContext GetRaPolicyContext() const;
										// snapshot central RA operation policy
	bool CheckRaOperation(Xm8Ra::RaRestrictedOperation operation,
		const char *notice);
										// reject an operation consistently
	void ApplyRaOnlineRestrictions();
										// enforce speed and disk policy at launch
	void RestoreRaSessionOverrides();
										// restore temporary settings at session end
	void HandleRaResetRequest();
										// apply an rc_client reset request once
	bool BeginRaSavedTokenLogin(bool notify_missing_token);
										// begin saved token login if possible
	void StartRaAfterBoot();
										// start RA login and mounted media after boot restore
	void BeginRaSessionForMedia(const std::string& md5, int64_t game_id);
										// begin RA session for media hash
	void BeginRaSessionForMountedDrive1();
										// begin RA session for mounted drive 1
	static void RaHostFrameComplete(void *userdata);
										// EVENT host frame callback
	void AttachRaHostFrameCallback();
										// connect callback to current EVENT instance
	void ProcessRaEmulationFrame();
										// evaluate one completed VM frame
	void ProcessRaService(bool emulation_idle);
										// progress async RA service work
	void ProcessRaConnectivity();
											// observe platform connectivity changes
	void ProcessRaPendingUnlockRetry();
											// retry durable unlocks with bounded backoff
	void ProcessRaImages();
										// progress RA badge image HTTP
	void RequestRaBadgeImage(const std::string& url,
		Xm8Ra::RaImageKind image_kind);
										// request RA badge image if needed
	void DrawRaBadgeImage(Uint32 *buf, SDL_Rect *rect,
		const std::string& url, Xm8Ra::RaImageKind image_kind,
		bool show_placeholder_text = true);
										// draw RA badge image if cached
	Xm8Ra::RaOverlayLibraryListSnapshot MakeRaLibraryOverlaySnapshot() const;
										// build library overlay snapshot
	bool LaunchRaLibraryGame(int64_t game_id, std::string *error);
										// launch registered RA library game
	Xm8Ra::RaOverlayAchievementListSnapshot MakeRaAchievementsOverlaySnapshot() const;
										// build achievements overlay snapshot
	Xm8Ra::RaOverlayLeaderboardListSnapshot MakeRaLeaderboardsOverlaySnapshot() const;
										// build leaderboards overlay snapshot
	void RefreshRaAchievementsOverlay();
										// refresh achievements overlay if visible
	void RefreshRaLeaderboardsOverlay();
										// refresh leaderboards overlay if visible
	void EnsureRaLeaderboardEntriesForSelection();
										// fetch selected leaderboard entries if needed
	void AddRaNotice(const std::string& text,
		Xm8Ra::RaNoticePriority priority = Xm8Ra::RaNoticePriority::Important,
		const std::string& badge_url = std::string());
										// add RA overlay notice
	void ReplaceRaNotice(const std::string& text,
		Xm8Ra::RaNoticePriority priority = Xm8Ra::RaNoticePriority::Important);
										// replace stale notices with the latest RA status
	void AddRaEventsAsNotices(const std::vector<Xm8Ra::RaEvent>& events);
										// translate RA events to notices
	bool HandleRaOverlayKeyDown(SDL_Event *e);
										// handle RA overlay key input
	bool HandleRaOverlayTextInput(SDL_Event *e);
										// handle RA overlay text input
	bool HandleRaOverlayMouse(SDL_Event *e);
										// handle RA overlay mouse input
	bool HandleRaOverlayFinger(SDL_Event *e);
										// handle RA overlay touch input
	bool HandleRaStatusMouse(SDL_Event *e);
										// handle RA status-line mouse input
	bool HandleRaStatusFinger(SDL_Event *e);
										// handle RA status-line touch input
	bool HandleRaOverlayJoystick();
										// handle RA overlay joystick input
	bool HandleRaOverlayAction(Xm8Ra::RaOverlayAction action);
										// handle RA overlay action
	void UpdateRaOverlayTextInput();
										// update SDL text input for RA overlay
	void ClearRaOverlayPointerState();
										// clear RA overlay pointer state
	bool SubmitRaOverlayLogin();
	#ifdef __ANDROID__
	void ProcessAndroidRaLogin();
	#endif
										// submit RA overlay login form
	void DrawRaOverlay();
										// draw RA notice overlay
	bool IsRaOverlayDrawingEnabled() const;
										// keep rich-overlay drawing and input ownership aligned
	bool GetRaStateContext(int slot, Xm8Ra::RaStateMode requested_mode,
		Xm8Ra::RaStateExpectation *expected,
		std::string *path, std::string *error) const;
										// resolve current Casual/offline state identity
	bool LoadRaState(int slot, bool hardcore_debug = false);
										// load validated RA-aware state
	bool SaveRaState(int slot);
										// save RA-aware state
	bool IsRaHardcoreMenuRunning() const;
										// Hardcore menu overlays a running VM
#endif
	bool LoadStateBody(FILEIO *fileio, int previous_audio_frequency,
		bool preserve_ra_session = false);
										// load already-open legacy XM8 state body
	bool SaveStateBody(FILEIO *fileio);
										// save legacy XM8 state body
	void ChangeSystemInternal(bool load, bool preserve_ra_session);
										// rebuild VM with explicit RA lifecycle policy
	bool OpenStartupDisks(const std::vector<DiskSpec>& disks, std::string *error);
										// open CLI disks
	void FinishDroppedDiskOpen();
	bool OpenDroppedDisk(const char *path, std::string *error);
										// open D&D disk

	// mode
	void CtrlAudio();
										// control audio

	// sync
	void LockVM();
										// lock vm
	void UnlockVM();
										// unlock vm

	// component
	SDL_sem *vm_sem;
										// semaphore object
	Setting *setting;
										// setting driver
	SDL_Window *window;
										// window
	Platform *platform;
										// platform driver
	Video *video;
										// video driver
	Audio *audio;
										// audio driver
	Font *font;
										// font manager
	Input *input;
										// input driver
	Converter *converter;
										// unicode converter
	Menu *menu;
										// menu driver
	EMU_SDL *wrapper;
										// emulator i/f wrapper
	EMU *emu;
										// emulator i/f
	VM *vm;
										// virtual machine
	EVENT *evmgr;
										// event manager
	PC88 *pc88;
										// PC88 device
	UPD1990A *upd1990a;
										// rtc device
	DiskManager *diskmgr[2];
										// disk manager
	TapeManager *tapemgr;
										// tape manager
	std::string disk_open_dir;
										// last user-selected disk directory
#ifdef XM8_ENABLE_RETROACHIEVEMENTS
	Xm8Ra::RaLibrary *ra_library;
										// RA library database
	Xm8Ra::RaMediaStore *ra_media_store;
										// RA media store
	Xm8Ra::RaService *ra_service;
										// RA client service
	std::unique_ptr<Xm8Ra::RaConnectivityMonitor> ra_connectivity_monitor;
										// platform network reachability monitor
	Xm8Ra::RaConnectivityTracker ra_connectivity_tracker;
											// deduplicated reachability transitions
	Xm8Ra::RaUnlockRetryBackoff ra_unlock_retry_backoff;
											// endpoint failure retry clock
	Xm8Ra::RaPauseRequestGate ra_pause_request_gate;
											// one Hardcore check per host pause request
	Xm8Ra::RaOverlay *ra_overlay;
										// RA overlay state
	std::map<uint32_t, Xm8Ra::RaLeaderboardScoreboardEvent>
		ra_leaderboard_scoreboards;
										// latest RA leaderboard scoreboards
	std::unique_ptr<Xm8Ra::RaHttpClient> ra_image_http_client;
										// RA image HTTP client
	struct RaBadgeImage {
		enum State {
			NotRequested,
			Pending,
			Ready,
			Failed
		};
		State state = NotRequested;
		uint64_t request_id = 0;
		int width = 0;
		int height = 0;
		uint32_t last_draw_ticks = 0;
		Xm8Ra::RaImageKind image_kind = Xm8Ra::RaImageKind::Other;
		std::vector<uint32_t> pixels;
	};
	std::map<std::string, RaBadgeImage> ra_badge_images;
										// RA badge image memory cache
	uint64_t ra_next_image_request_id;
										// RA image request id allocator
	int64_t ra_image_cache_limit_bytes;
										// persistent RA image cache capacity
	uint32_t ra_notification_duration_ms;
										// configured RA toast lifetime
	bool ra_mode_enabled;
										// RA mode setting
	bool ra_runtime_supported;
										// current platform supports RA runtime requirements
	Xm8Ra::RaPlayMode ra_play_mode;
										// selected Casual/Hardcore setting
	bool ra_fast_disk_override_active;
										// online session owns fast disk override
	bool ra_saved_fast_disk;
										// fast disk value before online session
	bool ra_reset_requested;
										// pending rc_client reset event
	bool ra_saved_login_started;
										// saved token login started
	bool ra_manual_login_started;
										// manual password login started
	#ifdef __ANDROID__
	std::mutex ra_android_login_mutex;
	bool ra_android_login_pending = false;
	bool ra_android_login_canceled = false;
	std::string ra_android_login_username;
	std::string ra_android_login_password;
	#endif
	bool ra_library_sync_started_for_login;
										// current login already attempted library sync
	Xm8Ra::RaSessionState ra_session_state;
										// current launch/active/disconnected/offline state
	Xm8Ra::RaMenuStatus ra_menu_status;
										// current RA menu status row
	Uint32 ra_overlay_joystick_prev;
										// RA overlay joystick previous state
	bool ra_overlay_mouse_target_valid;
										// RA mouse pressed target valid
	bool ra_overlay_mouse_outside;
										// RA mouse press began outside the dialog
	Xm8Ra::RaOverlayLoginTarget ra_overlay_mouse_target;
										// RA mouse pressed target
	int ra_overlay_mouse_detail_target;
										// RA detail mouse target: 0 none, 2 primary
	bool ra_overlay_mouse_list_target_valid;
										// RA mouse list press target valid
	size_t ra_overlay_mouse_list_target;
										// RA mouse list pressed row
	bool ra_overlay_finger_target_valid;
										// RA touch pressed target valid
	bool ra_overlay_finger_outside;
									// RA touch began outside the dialog
	Uint32 ra_overlay_finger_tick;
									// RA touch began tick
	Xm8Ra::RaOverlayLoginTarget ra_overlay_finger_target;
										// RA touch pressed target
	int ra_overlay_finger_detail_target;
										// RA detail touch target: 0 none, 2 primary
	bool ra_overlay_finger_list_target_valid;
										// RA touch list press target valid
	size_t ra_overlay_finger_list_target;
										// RA touch list pressed row
	bool ra_overlay_finger_scroll_valid;
										// RA touch scroll tracking valid
	bool ra_overlay_finger_scrolled;
										// RA touch moved enough to scroll
	int ra_overlay_finger_scroll_y;
										// RA touch scroll anchor y
	bool ra_status_mouse_pressed;
										// RA status mouse press candidate
	bool ra_status_mouse_dragged;
										// RA status mouse moved outside row
	bool ra_status_finger_pressed;
										// RA status touch press candidate
	bool ra_status_finger_dragged;
										// RA status touch moved
	Sint64 ra_status_finger_id;
										// RA status active touch id
	int ra_status_finger_start_x;
	int ra_status_finger_start_y;
	uint32_t ra_overlay_auto_scroll_revision;
										// RA auto-scroll selected text revision
	Uint32 ra_overlay_auto_scroll_started;
										// RA auto-scroll selected text start tick
	bool ra_menu_presence_scroll_active;
										// RA menu Rich Presence detail has focus
	bool ra_menu_error_scroll_active;
										// RA menu send error detail is scrolling
	bool ra_menu_detail_active;
										// RA menu or list detail line is visible
	Uint32 ra_menu_presence_scroll_started;
										// RA menu Rich Presence scroll start tick
	std::string ra_pending_game_hash;
										// media hash pending RA load
	int64_t ra_pending_library_game_id;
										// library game pending RA identification
	int64_t ra_loaded_library_game_id;
										// library game owning active RA media
	std::string ra_loaded_game_hash;
										// media hash passed to RA load
	struct RaDiskTransaction {
		Xm8Ra::RaDiskTransactionState state;
		DiskSpec target = {"", 0, 0};
		std::string new_hash;
		std::string old_hash;
		std::string auxiliary_hash;
		int64_t expected_ra_game_id = 0;
		DiskMountTargets mount_targets;
		DiskMountSnapshots before;
		bool auxiliary_verified = false;
		Xm8Ra::RaDiskProfileUpdate profile_update =
			Xm8Ra::RaDiskProfileUpdate::None;
		bool restore_failed = false;
	};
	struct RaAuxiliaryOperation {
		RaDiskTransaction request;
		Xm8Ra::MediaOperation::Runner runner;
		Xm8Ra::MediaOperation::Token verification_token;
		std::string message;
		bool ended = false;
		bool restored = true;
		bool prepared = true;
		bool profile_saved = true;
	};
	std::shared_ptr<RaAuxiliaryOperation> ra_auxiliary_operation;
	uint64_t ra_auxiliary_generation = 0;
	bool StartRaAuxiliaryOperation(std::string* error);
	void ProcessRaAuxiliaryOperation();
	void ExecuteRaAuxiliaryEffect(const std::shared_ptr<RaAuxiliaryOperation>& operation,
		Xm8Ra::MediaOperation::Effect effect, Xm8Ra::MediaOperation::Token token);
	RaDiskTransaction ra_disk_transaction;
										// single RA disk operation and reset owner
#endif

	// flags
	bool app_quit;
										// application quit flag
	bool app_fullspeed;
										// application full speed flag
	bool app_fullscreen;
										// application full screen flag
	bool app_background;
										// application back ground mode
	bool app_mobile;
										// mobile platform flag
	bool app_menu;
										// application menu flag
	bool app_powerdown;
										// application power down flag
	bool app_forcesync;
										// application force synchronize flag

	// power management
	int power_counter;
										// power counter
	int power_pointer;
										// power pointer
	int power_level[4];
										// power level

	// frame rate
	Uint32 draw_tick[0x40];
										// calculate frame rate
	Uint32 draw_tick_count;
										// calculate frame rate
	int draw_tick_point;
										// calculate frame rate

	// mouse
	Uint32 mouse_tick;
										// mouse timeout (ms)

	// system
	Uint32 system_info;
										// system information
	bool startup_disk_boot;
										// boot from CLI disk
	bool cli_system_override;
										// CLI system override active
	bool cli_clock_override;
										// CLI clock override active
	bool cli_settings_restored;
										// CLI settings already restored
	int cli_original_system;
										// persistent system mode
	int cli_original_clock;
										// persistent CPU clock
	bool cli_original_8h;
										// persistent 8MHzH mode

	// state path
	char state_path[_MAX_PATH * 3];
										// state path

	// sound sample multiple table
	static const int multi_table[16];
										// multiple table

	// audio parameter
	Uint8 *audio_param;
										// Audio::OpenParam
	bool audio_opened;
										// audio open flag
};

#endif // APP_H

#endif // SDL
