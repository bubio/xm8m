// Baseline tests deliberately execute App's production routing, persistence,
// DiskManager and real FDC. Only the platform host and RA HTTP are substitutes.
#include "ra_file_util.h"
#include <sqlite3.h>
#include <fstream>
#include <iostream>
#include <cstring>
#include <chrono>
#include <map>
#include "ra_media_probe.h"
#include "Fixtures/d88_fixture.h"
#include "ra_http_fake.h"
#include "os.h"
#include "common.h"
#include "app.h"
#include "emu_sdl.h"
#include "emu.h"
#include "vm.h"
#include "device.h"
#include "event.h"
#include "pc88.h"
#include "upd1990a.h"
#include "diskmgr.h"
#include "diskmountsnapshot.h"
#include "setting.h"
#include "platform.h"
#include "audio.h"
#include "video.h"
#include "font.h"
#include "input.h"
#include "converter.h"
#include "menu.h"
#include "menulist.h"
#include "menuid.h"
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

namespace {
std::string host_root;
void Require(bool ok, const std::string& message)
{
    if (!ok) { std::cerr << "FAIL: " << host_root << ": " << message << '\n'; std::exit(1); }
}
class NoCredentials final : public Xm8Ra::RaCredentialsStore {
public:
    explicit NoCredentials(bool saved = false) : available(saved) {}
    bool available = false;
    mutable int loads = 0;
    bool Save(const Xm8Ra::RaCredentials&, std::string*) override { return true; }
    bool Load(Xm8Ra::RaCredentials* value, std::string*) const override {
        ++loads;
        if (!available) return false;
        *value = {"fixture", "fixture-token"};
        return true;
    }
    bool Delete(std::string*) override { available = false; return true; }
    void ClearSecret(Xm8Ra::RaCredentials* value) const override { *value = {}; }
};
class ResetCounter final : public DEVICE {
public:
    ResetCounter(VM* vm, EMU* emu) : DEVICE(vm, emu) {}
    void reset() override { ++calls; }
    // The production VM resets each device twice per VM::reset invocation.
    int calls = 0;
};
}

// Platform boundary: never consult SDL_GetPrefPath, user ROMs or Keychain.
EMU_SDL::EMU_SDL(Video* v) : video(v)
{
    std::snprintf(base_path, sizeof(base_path), "%s/", host_root.c_str());
}
EMU_SDL::~EMU_SDL() = default;
_TCHAR* EMU_SDL::get_app_path() { return base_path; }
_TCHAR* EMU_SDL::get_bios_path(_TCHAR* name)
{
    std::snprintf(bios_path, sizeof(bios_path), "%s%s", base_path, name);
    return bios_path;
}
void EMU_SDL::get_host_time(cur_time_t* time) { std::memset(time, 0, sizeof(*time)); }
void EMU_SDL::printer_out(uint8) {}
void EMU_SDL::printer_strobe(bool) {}
void EMU_SDL::mute_sound() {}
scrntype* EMU_SDL::get_screen_buf(int) { return nullptr; }
void EMU_SDL::current_thread_sleep(uint32) {}

// Friend access wires host dependencies; no media policy is reimplemented here.
class AppMediaTestAccess {
public:
    App app;
    Xm8Ra::FakeRaHttpClient* http = nullptr;
    NoCredentials* credentials = nullptr;
    ResetCounter* resets = nullptr;
    size_t answered = 0;
    explicit AppMediaTestAccess(const std::string& root, bool enabled, Xm8Ra::RaPlayMode mode = Xm8Ra::RaPlayMode::Hardcore, bool saved = false)
    {
        host_root = root;
        app.setting = new Setting;
        app.platform = new Platform(&app);
        app.audio = new Audio;
        app.video = new Video(&app);
        app.font = new Font(&app);
        app.input = new Input(&app);
        app.converter = new Converter;
        Require(app.converter->Init(), "fixture text conversion initialization");
        app.menu = new Menu(&app);
        Require(app.menu->Init(), "headless menu initialization");
        app.wrapper = new EMU_SDL(app.video);
        app.emu = new EMU(app.wrapper);
        app.vm = new VM(app.emu);
        app.evmgr = static_cast<EVENT*>(app.vm->get_device(1));
        app.pc88 = static_cast<PC88*>(app.vm->get_device(2));
        app.upd1990a = static_cast<UPD1990A*>(app.vm->get_device(6));
        for (int drive = 0; drive < 2; ++drive) {
            app.diskmgr[drive] = new DiskManager;
            Require(app.diskmgr[drive]->Init(app.vm, drive), "disk manager initialization");
        }
        app.vm->reset();
        resets = new ResetCounter(app.vm, app.emu);
        if (enabled) {
            std::string error;
            app.ra_library = new Xm8Ra::RaLibrary;
            Require(app.ra_library->Open(root + "/ra", &error), error);
            app.ra_media_store = new Xm8Ra::RaMediaStore(app.ra_library);
            Xm8Ra::RaServiceOptions options;
            options.ra_root = root + "/ra";
            auto store = std::make_unique<NoCredentials>(saved);
            credentials = store.get();
            options.credentials_store = std::move(store);
            auto fake = std::make_unique<Xm8Ra::FakeRaHttpClient>();
            http = fake.get();
            options.http_client = std::move(fake);
            options.pending_unlock_store = app.ra_library;
            app.ra_service = new Xm8Ra::RaService(std::move(options));
            Require(app.ra_service->IsReady(), "fake-HTTP RA service initialization");
            app.ra_mode_enabled = true;
            app.ra_play_mode = mode;
        }
    }
    bool Drop(const std::string& path, std::string* error)
    {
        if (!app.OpenDroppedDisk(path.c_str(), error)) return false;
        app.FinishDroppedDiskOpen();
        return true;
    }
    bool Startup(const std::vector<DiskSpec>& disks, std::string* error)
    {
        return app.OpenStartupDisks(disks, error);
    }
    bool Launch(int64_t game, std::string* error) { return app.LaunchRaLibraryGame(game, error); }
    int64_t ActiveGameId() const { return app.ra_service->GameSessionSnapshot().game_id; }
    std::string ActiveHash() const { return app.ra_service->GameSessionSnapshot().hash; }
    std::string MediaRoot() const { return app.ra_library->MediaRoot(); }
    int64_t LibraryGame() const { return app.ra_loaded_library_game_id; }
    // The Library game which owns the working copy of a source file.
    int64_t MediaLibraryGame(const std::string& source)
    {
        Xm8Ra::ImportedMedia media;
        std::string error;
        Require(app.ra_media_store->ImportDesktopD88(source, &media, &error), error);
        return media.record.game_id;
    }
    std::string WorkingPath(int drive) const { return app.diskmgr[drive]->GetPath(); }
    // RA's hash list as the fake server reports it: RA hash -> RA Game ID.
    std::map<std::string, int> library;
    static std::string BankHash(const std::string& path, int bank)
    {
        Xm8Ra::D88MediaInfo info;
        std::string error;
        Require(Xm8Ra::ProbeD88File(path.c_str(), &info, &error), error);
        Require(bank >= 0 && bank < static_cast<int>(info.bank_md5s.size()), "fixture bank hash");
        return info.bank_md5s[bank];
    }
    void Register(const std::string& path, int bank, int game) { library[BankHash(path, bank)] = game; }
    bool Mentions(size_t first_request, const std::string& text) const
    {
        const auto requests = http->SentRequests();
        for (size_t i = first_request; i < requests.size(); ++i)
            if (requests[i].post_data.find(text) != std::string::npos) return true;
        return false;
    }
    void ExpireLaunchDeadline()
    {
        app.ra_launch_started_ticks = SDL_GetTicks() - (Xm8Ra::kRaLaunchDeadlineMs + 1);
    }
    size_t QueuedUnlocks() const
    {
        size_t count = 0;
        std::string error;
        Require(app.ra_service->HasPendingUnlocks(&count, &error), error);
        return count;
    }
    bool ToggleRaMode() { return app.ToggleRaMode(); }
    bool RaEnabled() const { return app.ra_mode_enabled; }
    void Disconnect()
    {
        const auto transition = app.ra_connectivity_tracker.Observe(Xm8Ra::RaReachabilityState::Unreachable);
        if (transition.has_signal) app.ra_session_state = Xm8Ra::TransitionRaSession(app.ra_session_state,transition.signal);
    }
    void Reconnect()
    {
        const auto transition = app.ra_connectivity_tracker.Observe(Xm8Ra::RaReachabilityState::Reachable);
        if (transition.has_signal) app.ra_session_state = Xm8Ra::TransitionRaSession(app.ra_session_state,transition.signal);
    }
    void ShowDrive(int drive)
    {
        app.app_menu = true;
        app.menu->diskmgr = app.diskmgr;
        if (drive == 0) app.menu->EnterDrive1(MENU_BACK);
        else app.menu->EnterDrive2(MENU_BACK);
    }
    std::string BankLabel(int drive, int bank = 0)
    {
        const char* label = app.menu->list->GetText((drive == 0 ? MENU_DRIVE1_BANK0 : MENU_DRIVE2_BANK0) + bank);
        return label ? label : "";
    }
    void MenuTick() { app.menu->RefreshPendingDriveMenu(); }
    void Tick() { app.ProcessRaService(false); }
    void BeginSavedLogin()
    {
        std::string error;
        app.ra_saved_login_started = app.ra_service->BeginLoginWithSavedToken(&error);
        Require(app.ra_saved_login_started,error);
    }
    void ReplyLoginFailure(bool rejected)
    {
        const auto request = http->SentRequests().back();
        Require(request.post_data.find("r=login2") != std::string::npos,"saved login request");
        answered = http->SentRequests().size();
        Xm8Ra::RaHttpResponse response;
        response.request_id = request.request_id;
        if (rejected) {
            response.http_status = 200;
            const std::string json = R"({"Success":false,"Error":"Invalid token","Code":"invalid_credentials"})";
            response.body.assign(json.begin(),json.end());
        }
        else response.transport_result = Xm8Ra::RaHttpTransportResult::Timeout;
        http->Complete(response);
        Tick();
        Require(app.ra_service->LoginSnapshot().state==Xm8Ra::RaLoginState::Failed,"fixture login failed");
    }
    void QueueUnlockSync()
    {
        Xm8Ra::RaPendingUnlockRecord record;
        record.account = "fixture";
        record.achievement_id = 1;
        record.hardcore = app.ra_play_mode == Xm8Ra::RaPlayMode::Hardcore;
        record.game_hash = ActiveHash();
        record.unlocked_at = 100;
        record.mac = Xm8Ra::ComputeRaPendingUnlockMac("fixture-token", record);
        int64_t id = 0;
        std::string error;
        Require(app.ra_library->EnqueuePendingUnlock(record,&id,&error),error);
        Require(app.ra_service->BeginPendingUnlockSync(&error),error);
    }
    // Put one unlock into the outbox which must never be sent: either the
    // server refused it for good, or it was edited (softcore turned into
    // hardcore after signing), and let the service look at the outbox.
    void QueueHeldUnlock(bool edited)
    {
        Xm8Ra::RaPendingUnlockRecord record;
        record.account = "fixture";
        record.achievement_id = 1;
        record.hardcore = !edited;
        record.game_hash = "0123456789abcdef0123456789abcdef";
        record.unlocked_at = 100;
        record.mac = Xm8Ra::ComputeRaPendingUnlockMac("fixture-token", record);
        int64_t id = 0;
        std::string error;
        Require(app.ra_library->EnqueuePendingUnlock(record,&id,&error),error);
        if (edited) {
            sqlite3* db = nullptr;
            Require(sqlite3_open_v2(app.ra_library->DatabasePath().c_str(), &db, SQLITE_OPEN_READWRITE, nullptr) == SQLITE_OK, "open outbox for editing");
            Require(sqlite3_exec(db, "UPDATE pending_unlocks SET hardcore = 1", nullptr, nullptr, nullptr) == SQLITE_OK, "edit the outbox by hand");
            sqlite3_close(db);
        }
        else Require(app.ra_library->MarkPendingUnlockAttempt(id, Xm8Ra::RaPendingUnlockStatus::Held, "refused", &error), error);
        Require(app.ra_service->BeginPendingUnlockSync(&error),error);
    }
    size_t HeldUnlocks() const { return app.ra_service->UnlockSyncSnapshot().held; }
    std::string MenuStatus() const
    {
        char text[128] = {};
        app.GetRaMenuStatus(text, sizeof(text));
        return text;
    }
    bool WaitingForUnlockSync() const
    {
        return app.ra_service->UnlockSyncSnapshot().state == Xm8Ra::RaUnlockSyncState::Pending &&
            app.ra_service->GameSessionSnapshot().state == Xm8Ra::RaGameSessionState::NoGame;
    }
    Xm8Ra::RaLoginState LoginState() const { return app.ra_service->LoginSnapshot().state; }
    void Login()
    {
        std::string error;
        app.ra_connectivity_tracker.Observe(Xm8Ra::RaReachabilityState::Reachable);
        Require(app.ra_service->BeginLoginWithPassword("fixture", "fixture", &error), error);
        Pump(false);
        Require(app.ra_service->LoginSnapshot().state == Xm8Ra::RaLoginState::LoggedIn, "fixture login");
        Require(app.ra_service->KnownHashesState() == Xm8Ra::RaKnownHashesState::Loaded,
            "RA hash list is in memory after login");
    }
    // Answer every outstanding fake request. `registered` is the server's
    // answer to identifying the Drive 1 hash at a cold reset; `game_id` is the
    // RA title it reports. Hash lookups are answered from `library`.
    void Pump(bool registered, int game_id = 1234)
    {
        for (int tick = 0; tick < 16; ++tick) {
            // Copy: handling one response may append another request.
            const auto requests = http->SentRequests();
            while (answered < requests.size()) {
                const auto request = requests[answered++];
                if (http->IsCanceled(request.request_id)) continue;
                const auto has = [&request](const char* action) {
                    return request.post_data.find(action) != std::string::npos;
                };
                const std::string id = std::to_string(game_id);
                std::string json;
                if (has("r=login2"))
                    json = R"({"Success":true,"User":"fixture","Token":"fixture-token","Score":0,"SoftcoreScore":0,"Messages":0})";
                else if (has("r=achievementsets"))
                    json = registered ? "{\"Success\":true,\"GameId\":" + id + ",\"Title\":\"Fixture\",\"ConsoleId\":47,\"ImageIconUrl\":\"\",\"RichPresenceGameId\":0,\"RichPresencePatch\":\"\",\"Sets\":[{\"AchievementSetId\":" + id + ",\"GameId\":" + id + ",\"Title\":\"Fixture\",\"Type\":\"core\",\"ImageIconUrl\":\"\",\"Achievements\":[],\"Leaderboards\":[]}]}"
                        : R"({"Success":true,"GameId":0,"Title":"","ConsoleId":47,"ImageIconUrl":"","RichPresenceGameId":0,"RichPresencePatch":"","Sets":[]})";
                else if (has("r=startsession"))
                    json = R"({"Success":true,"Unlocks":[],"HardcoreUnlocks":[],"ServerNow":1710000000})";
                else if (has("r=gameid")) {
                    int listed = 0;
                    for (const auto& entry : library)
                        if (has(entry.first.c_str())) listed = entry.second;
                    json = "{\"Success\":true,\"GameID\":" + std::to_string(listed) + "}";
                }
                else if (has("r=hashlibrary")) {
                    json = "{\"Success\":true,\"MD5List\":{";
                    bool first = true;
                    for (const auto& entry : library) {
                        json += std::string(first ? "" : ",") + "\"" + entry.first + "\":" + std::to_string(entry.second);
                        first = false;
                    }
                    json += "}}";
                }
                else if (has("r=gameinfolist"))
                    json = R"({"Success":true,"Response":[]})";
                else if (has("r=allprogress"))
                    json = R"({"Success":true,"Response":{}})";
                else if (has("r=ping"))
                    json = R"({"Success":true})";
                else if (has("r=awardachievement"))
                    json = R"({"Success":true,"AchievementID":1,"Score":1,"SoftcoreScore":0,"AchievementsRemaining":0})";
                else Require(false, "unexpected fake API action: " + request.post_data.substr(0, request.post_data.find('&')));
                Xm8Ra::RaHttpResponse response;
                response.request_id = request.request_id;
                response.http_status = 200;
                response.body.assign(json.begin(), json.end());
                http->Complete(response);
            }
            Tick();
        }
    }
    Xm8Ra::RaSessionState Session() const { return app.ra_session_state; }
    int Resets() const { Require(resets->calls % 2 == 0, "whole VM resets"); return resets->calls / 2; }
    void Expect(int drive, const std::string& source, int bank)
    {
        auto* disk = app.diskmgr[drive];
        Require(disk->IsOpen() && app.vm->disk_inserted(drive), "real FDC insertion on drive " + std::to_string(drive) + " source=" + source + " bank=" + std::to_string(bank) + " RA=" + std::to_string(app.ra_mode_enabled));
        Require(disk->GetBank() == bank, "bank on drive " + std::to_string(drive));
        if (!app.ra_mode_enabled) Require(source == disk->GetPath(), "original path in RA OFF");
        else {
            Xm8Ra::ImportedMedia media;
            std::string error;
            Require(app.ra_media_store->ImportDesktopD88(source, &media, &error), error);
            Require(media.working_path == disk->GetPath(), "working media identity: root=" + host_root + " source=" + source + " drive=" + std::to_string(drive) + " expected=" + media.working_path + " actual=" + disk->GetPath());
        }
    }
    void ExpectEmpty(int drive)
    {
        Require(!app.diskmgr[drive]->IsOpen() && !app.vm->disk_inserted(drive), "ejected real FDC");
    }
};

int main(int, char **)
{
    const char* temporary = std::getenv(
#ifdef _WIN32
        "TEMP"
#else
        "TMPDIR"
#endif
    );
    const std::string root = std::string(temporary ? temporary :
#ifdef _WIN32
        "."
#else
        "/tmp"
#endif
    ) +
        "/xm8-app-media-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    std::string error;
    Require(D88Fixture::GenerateStandardSet(root, &error), error);
    const auto single = (root + "/single.d88");
    const auto second = (root + "/second.d88");
    const auto multi = (root + "/multi.d88");
    const auto third = (root + "/third.d88");
    {
        std::ifstream in(single, std::ios::binary);
        std::ofstream out(third, std::ios::binary);
        out << in.rdbuf(); out.seekp(0); out.put('T'); // Distinct valid D88 title/hash.
    }
    const auto unregistered = root + "/unregistered.d88";
    {
        std::ifstream in(second, std::ios::binary);
        std::ofstream out(unregistered, std::ios::binary);
        out << in.rdbuf(); out.seekp(0); out.put('U');
    }
    const auto triple = (root + "/triple.d88");
    { std::ofstream out(triple, std::ios::binary); std::ifstream a(multi, std::ios::binary), b(third, std::ios::binary); out << a.rdbuf() << b.rdbuf(); }
    const auto playlist = (root + "/selected.m3u");
    { std::ofstream out(playlist); out << "triple.d88#2\nsecond.d88#0\n"; }
    const auto single_playlist = root + "/single.m3u";
    { std::ofstream out(single_playlist); out << "third.d88#0\n"; }
    for (bool enabled : {false, true}) {
        Require(Xm8Ra::EnsureRaDirectoryTree(root + (enabled ? "/on" : "/off")), "create isolated host root");
        AppMediaTestAccess f((root + (enabled ? "/on" : "/off")), enabled);
        Require(f.app.OpenDiskFromMenu({single, 0, 0}, &error), error);
        f.Expect(0, single, 0);
        if (enabled) {
            f.Tick(); // No saved login: the real App must fall back, not reject mounting.
            Require(f.Session() == Xm8Ra::RaSessionState::Offline, "login-unavailable fallback");
        }
        const int baseline = f.Resets();
        Require(f.app.OpenDiskFromMenu({triple, 1, 2}, &error), error);
        f.Expect(0, single, 0); f.Expect(1, triple, 2);
        Require(f.app.ChangeDiskBankFromMenu(1, 1, &error), error);
        if (!enabled) {
            for (int frame = 0; frame < 32; ++frame)
                f.app.GetDiskManager()[1]->ProcessMgr();
        }
        f.Expect(0, single, 0); f.Expect(1, triple, 1);
        Require(f.app.OpenDiskFromMenu({second, 0, 0}, &error), error);
        f.Expect(0, second, 0); f.Expect(1, triple, 1);
        Require(f.app.OpenDiskFromMenu({third,0,0}, &error), error);
        f.Expect(0,third,0); f.Expect(1,triple,1);
        Require(f.app.OpenDiskSpecsFromMenu({{triple,0,2},{single,1,0}}, &error), error);
        f.Expect(0, triple, 2); f.Expect(1, single, 0);
        Require(f.Resets() == baseline, "ordinary local exchanges do not acquire resets");
        for (int drive : {1,0}) { Require(f.app.EjectDiskFromMenu(drive, &error), error); f.ExpectEmpty(drive); }
        for (const auto& path : {multi, single, playlist}) {
            const int before = f.Resets();
            Require(f.Drop(path, &error), error);
            Require(f.Resets() == before + 1, "D&D commits and resets exactly once");
            if (path == multi) { f.Expect(0,multi,0); f.Expect(1,multi,1); }
            else if (path == single) { f.Expect(0,single,0); f.ExpectEmpty(1); }
            else { f.Expect(0,triple,2); f.Expect(1,second,0); }
            if (enabled) f.Tick();
        }
        const int before = f.Resets();
        Require(!f.app.OpenDiskSpecsFromMenu({{single,0,0},{triple,1,99}}, &error), "invalid bank fails before either drive changes");
        f.Expect(0,triple,2); f.Expect(1,second,0);
        Require(f.Resets() == before, "local failure does not reset");
        if (enabled) Require(f.http->SentRequests().empty(), "offline local operations need no HTTP");
    }
    for (const auto mode : {Xm8Ra::RaPlayMode::Casual, Xm8Ra::RaPlayMode::Hardcore}) {
        const std::string dir = root + (mode == Xm8Ra::RaPlayMode::Casual ? "/casual" : "/hardcore");
        Require(Xm8Ra::EnsureRaDirectoryTree(dir), "create RA fixture root");
        AppMediaTestAccess f(dir, true, mode);
        f.Login();
        Require(f.app.OpenDiskFromMenu({single,0,0}, &error), error);
        f.Pump(false);
        Require(f.Session() == Xm8Ra::RaSessionState::Offline, "unregistered anchor mounts Offline");
        f.Expect(0,single,0);
        Require(f.app.OpenDiskFromMenu({triple,1,2}, &error), error);
        Require(f.app.ChangeDiskBankFromMenu(1,1,&error), error);
        f.Expect(0,single,0); f.Expect(1,triple,1);
        Require(f.app.EjectDiskFromMenu(1,&error), error); f.ExpectEmpty(1);
        Require(f.app.OpenDiskFromMenu({second,0,0}, &error), error);
        const int before = f.Resets();
        f.app.Reset();
        Require(f.Resets() == before + 1, "explicit reset once after Offline replacement");
        Require(f.Session() == Xm8Ra::RaSessionState::Starting, "replacement is identified again after reset");
        f.Pump(true);
        Require(f.Session() == Xm8Ra::RaSessionState::Active, "registered replacement becomes Active");
        f.Expect(0,second,0);
        Require(f.Resets() == before + 1, "load completion does not reset VM again");
        Require(f.Drop(second, &error), error);
        f.Pump(true);
        Require(f.Resets() == before + 2, "Active same-hash D&D resets once");
        Require(f.Session() == Xm8Ra::RaSessionState::Active, "Active D&D preserves session");
        f.Expect(0,second,0); f.ExpectEmpty(1);
    }
    const auto Active = Xm8Ra::RaSessionState::Active;
    const auto ActiveDisconnected = Xm8Ra::RaSessionState::ActiveDisconnected;
    const auto Starting = Xm8Ra::RaSessionState::Starting;
    const auto Offline = Xm8Ra::RaSessionState::Offline;
    const auto Ready = Xm8Ra::RaSessionState::Ready;
    const auto modes = {Xm8Ra::RaPlayMode::Casual, Xm8Ra::RaPlayMode::Hardcore};
    const auto Dir = [&root](const std::string& name) {
        const std::string dir = root + "/" + name;
        Require(Xm8Ra::EnsureRaDirectoryTree(dir), "create fixture root " + name);
        return dir;
    };

    // Drive 2 never involves RA: a save disk RA has never heard of, a disk of
    // another game and any bank keep the running session, without a reset,
    // a wait or a request, whether or not RA is reachable.
    for (const auto mode : modes)
    for (bool disconnected : {false,true}) {
        AppMediaTestAccess f(Dir("drive2-free-" + std::to_string(static_cast<int>(mode)) + std::to_string(disconnected)),true,mode);
        f.Register(triple,0,1234);
        f.Register(third,0,5678);
        f.Login();
        Require(f.app.OpenDiskFromMenu({triple,0,0},&error), error);
        f.Pump(true);
        Require(f.Session() == Active, "Drive 2 fixture starts Active");
        const auto anchor = f.ActiveHash();
        const int resets = f.Resets();
        if (disconnected) f.Disconnect();
        const auto expected = disconnected ? ActiveDisconnected : Active;
        const auto sent = f.http->SentRequests().size();
        Require(f.app.OpenDiskFromMenu({unregistered,1,0},&error), error);
        f.Expect(1,unregistered,0);
        Require(f.Session() == expected, "unregistered Drive 2 keeps the session");
        Require(f.app.OpenDiskFromMenu({third,1,0},&error), error);
        f.Expect(1,third,0);
        Require(f.Session() == expected, "another game's disk in Drive 2 keeps the session");
        Require(f.app.OpenDiskFromMenu({triple,1,2},&error), error);
        Require(f.app.ChangeDiskBankFromMenu(1,1,&error), error);
        f.Expect(1,triple,1);
        Require(f.app.EjectDiskFromMenu(1,&error), error);
        f.ExpectEmpty(1);
        f.Tick();
        f.Expect(0,triple,0);
        Require(f.Session() == expected && f.ActiveHash() == anchor && f.ActiveGameId() == 1234,
            "Drive 2 operations leave the running game untouched");
        Require(f.Resets() == resets, "Drive 2 operations never reset");
        Require(f.http->SentRequests().size() == sent, "Drive 2 media never contact RA");
    }
    // Drive 1 during a running game is classified from local knowledge and
    // mounted at once. Opening another file and selecting another bank are the
    // same decision. relation: 0 = running game, 1 = other game, 2 = unregistered.
    for (const auto mode : modes)
    for (bool bank : {false,true})
    for (int relation = 0; relation < 3; ++relation)
    for (bool disconnected : {false,true}) {
        AppMediaTestAccess f(Dir("drive1-relation-" + std::to_string(static_cast<int>(mode)) +
            std::to_string(bank) + std::to_string(relation) + std::to_string(disconnected)),true,mode);
        const std::string target = bank ? triple : third;
        const int target_bank = bank ? 2 : 0;
        const auto target_hash = f.BankHash(target,target_bank);
        f.Register(triple,0,1234);
        if (relation == 0) f.Register(target,target_bank,1234);
        if (relation == 1) f.Register(target,target_bank,5678);
        f.Login();
        Require(f.app.OpenDiskFromMenu({triple,0,0},&error), error);
        f.Pump(true);
        Require(f.Session() == Active && f.ActiveGameId() == 1234, "relation fixture starts Active");
        const auto old_library = f.LibraryGame();
        const int before = f.Resets();
        if (disconnected) f.Disconnect();
        const auto running = disconnected ? ActiveDisconnected : Active;
        if (bank) Require(f.app.ChangeDiskBankFromMenu(0,2,&error), error);
        else Require(f.app.OpenDiskFromMenu({third,0,0},&error), error);
        f.Expect(0,target,target_bank); f.ExpectEmpty(1); // Never delayed by RA.
        if (relation == 0) {
            Require(f.Session() == running, "a disk of the running game keeps the session");
            Require(f.ActiveGameId() == 1234 && f.ActiveHash() == target_hash,
                "the running game continues on its new disk");
            Require(f.Resets() == before, "an exchange within the running game does not reset");
            if (!disconnected) f.Pump(true); // Best-effort rc_client notification.
            Require(f.Session() == running && f.ActiveHash() == target_hash,
                "the notification result leaves the session running");
            Require(f.LibraryGame() == old_library, "the running game keeps its Library identity");
            f.app.Reset();
            f.Tick();
            Require(f.Resets() == before + 1 && f.Session() == running && f.ActiveHash() == target_hash,
                "a reset on another disk of the running game keeps the session");
        }
        else if (relation == 1 && !disconnected) {
            Require(f.Session() == Starting && f.Resets() == before + 1,
                "another registered game is cold reset and identified");
            f.Pump(true,5678);
            Require(f.Session() == Active && f.ActiveGameId() == 5678 && f.ActiveHash() == target_hash,
                "the other game becomes the running game");
            Require(f.Resets() == before + 1, "a new title is cold reset exactly once");
        }
        else {
            Require(f.Session() == Offline, relation == 1 ?
                "another game cannot be identified while disconnected" :
                "unregistered Drive 1 media end the session");
            Require(f.Resets() == before, "ending the session does not reset");
        }
    }
    // Every two-drive entry mounts both drives at once, resets once and
    // identifies Drive 1 only. A registered launch persists the layout.
    for (const auto mode : modes) {
        for (bool registered : {false,true}) {
            for (int entry = 0; entry < 5; ++entry) {
                AppMediaTestAccess f(Dir("paired-" + std::to_string(static_cast<int>(mode)) +
                    "-" + std::to_string(registered) + "-" + std::to_string(entry)), true, mode);
                if (registered) {
                    f.Register(triple,2,1234); f.Register(second,0,1234);
                    f.Register(multi,0,1234); f.Register(multi,1,1234);
                    f.Register(third,0,1234);
                }
                f.Login();
                const int before = f.Resets();
                if (entry == 0)
                    Require(f.app.OpenDiskSpecsFromMenu({{triple,0,2},{second,1,0}},&error), error);
                else if (entry == 1)
                    Require(f.Startup({{triple,0,2},{second,1,0}},&error), error);
                else Require(f.Drop(entry == 2 ? playlist : (entry == 3 ? multi : single_playlist),&error), error);
                const auto layout = [&]() {
                    if (entry == 4) { f.Expect(0,third,0); f.ExpectEmpty(1); }
                    else if (entry == 3) { f.Expect(0,multi,0); f.Expect(1,multi,1); }
                    else { f.Expect(0,triple,2); f.Expect(1,second,0); }
                };
                layout();
                Require(f.Session() == Starting && f.Resets() == before + 1,
                    "a new pair is mounted and cold reset at once");
                f.Pump(registered);
                layout();
                Require(f.Session() == (registered ? Active : Offline), "paired launch reaches expected session");
                Require(f.Resets() == before + 1, "paired new launch resets exactly once: actual=" + std::to_string(f.Resets() - before));
                if (registered) {
                    // The successful production launch persisted the real Library profile.
                    const auto game_id = f.LibraryGame();
                    Require(game_id > 0, "identified Library game exists");
                    const auto hash = f.ActiveHash();
                    const int boot = f.Resets();
                    Require(f.Launch(game_id,&error), error);
                    Require(f.Session() == Starting,
                        "Library START begins a fresh session even for the same hash");
                    layout();
                    f.Pump(true);
                    Require(f.Session() == Active && f.ActiveHash() == hash,
                        "Library profile boots its anchor");
                    layout();
                    Require(f.Resets() == boot + 1, "Library START resets once and completes");
                }
            }
        }
    }
    // A new title is identified from Drive 1 alone. Whatever stays in Drive 2
    // is neither verified nor mentioned to RA.
    for (const auto mode : modes) {
        AppMediaTestAccess f(Dir("new-title-kept-drive2-" + std::to_string(static_cast<int>(mode))),true,mode);
        f.Register(triple,0,1234);
        f.Register(triple,2,5678);
        f.Login();
        Require(f.app.OpenDiskFromMenu({triple,0,0},&error), error);
        f.Pump(true);
        Require(f.app.OpenDiskFromMenu({unregistered,1,0},&error), error);
        const auto before = f.Resets();
        const auto sent = f.http->SentRequests().size();
        Require(f.app.ChangeDiskBankFromMenu(0,2,&error), error);
        f.Pump(true,5678);
        f.Expect(0,triple,2); f.Expect(1,unregistered,0);
        Require(f.Resets() == before + 1, "new title with a kept Drive 2 resets once");
        Require(f.Session() == Active && f.ActiveGameId() == 5678,
            "Drive 2 does not decide whether a new session may activate");
        Require(!f.Mentions(sent, f.BankHash(unregistered,0)), "Drive 2 is never sent to RA");
    }
    // A separate D88 can identify as a game already registered by another
    // container. Keep one Library identity and persist the new launch layout.
    for (const auto mode : modes) {
        AppMediaTestAccess f(Dir("same-game-library-" + std::to_string(static_cast<int>(mode))),true,mode);
        f.Register(multi,0,1234); f.Register(multi,1,1234); f.Register(second,0,1234);
        f.Login();
        bool drive2 = false;
        Require(f.app.OpenDiskPairFromMenu(multi,&drive2,&error), error);
        f.Pump(true);
        const auto library_game = f.LibraryGame();
        Require(library_game > 0, "first container registered");
        Require(f.Drop(second,&error), error);
        f.Pump(true);
        f.Expect(0,second,0); f.ExpectEmpty(1);
        Require(f.Session() == Active, "second container maintains RA");
        Require(f.LibraryGame() == library_game, "same RA game reuses existing Library identity");
        Require(f.Launch(library_game,&error), error);
        f.Pump(true);
        f.Expect(0,second,0); f.ExpectEmpty(1);
    }
    // Library START restores the whole two-drive layout, including a Drive 2
    // medium kept in its own file (a save disk). The save disk stays its own
    // Library game and RA is never asked about it.
    for (const auto mode : modes)
    for (int entry = 0; entry < 3; ++entry) {
        AppMediaTestAccess f(Dir("start-save-disk-" + std::to_string(static_cast<int>(mode)) + "-" + std::to_string(entry)),true,mode);
        f.Register(triple,0,1234);
        f.Login();
        if (entry == 0) {
            // Mounted from the menu while the game runs.
            Require(f.app.OpenDiskFromMenu({triple,0,0},&error), error);
            f.Pump(true);
            Require(f.app.OpenDiskFromMenu({unregistered,1,0},&error), error);
        }
        else if (entry == 1) {
            // Given together with the game when the application starts.
            Require(f.Startup({{triple,0,0},{unregistered,1,0}},&error), error);
            f.Pump(true);
        }
        else {
            // Already in Drive 2 when the game is opened.
            Require(f.app.OpenDiskFromMenu({unregistered,1,0},&error), error);
            Require(f.app.OpenDiskFromMenu({triple,0,0},&error), error);
            f.Pump(true);
        }
        Require(f.Session() == Active, "the game runs with the save disk in Drive 2");
        const auto game = f.LibraryGame();
        Require(game > 0 && f.MediaLibraryGame(unregistered) != game, "the save disk stays its own Library game");
        // Leave the layout, then START the game from the Library.
        Require(f.app.EjectDiskFromMenu(1,&error), error);
        Require(f.app.EjectDiskFromMenu(0,&error), error);
        f.ExpectEmpty(0); f.ExpectEmpty(1);
        const auto sent = f.http->SentRequests().size();
        const int resets = f.Resets();
        Require(f.Launch(game,&error), error);
        f.Pump(true);
        f.Expect(0,triple,0); f.Expect(1,unregistered,0);
        Require(f.Session() == Active && f.Resets() == resets + 1, "START restores both drives with one reset and starts RA");
        Require(!f.Mentions(sent, f.BankHash(unregistered,0)), "the restored save disk is never sent to RA");
    }
    // D&D during a running game: disks of that game are mounted, the VM is
    // reset once and the session is kept, without identifying again.
    for (const auto mode : modes) {
        AppMediaTestAccess f(Dir("running-game-drop-" + std::to_string(static_cast<int>(mode))),true,mode);
        f.Register(triple,0,1234); f.Register(triple,2,1234); f.Register(second,0,1234);
        f.Login();
        Require(f.app.OpenDiskFromMenu({triple,0,0},&error), error);
        f.Pump(true);
        Require(f.app.OpenDiskFromMenu({second,1,0},&error), error);
        Require(f.app.ChangeDiskBankFromMenu(0,2,&error), error);
        Require(f.app.ChangeDiskBankFromMenu(0,0,&error), error);
        f.Pump(true);
        const int resets = f.Resets();
        Require(f.Drop(playlist,&error), error);
        f.Expect(0,triple,2); f.Expect(1,second,0);
        Require(f.Resets() == resets + 1 && f.Session() == Active,
            "a drop within the running game resets exactly once and keeps the session");
        f.Pump(true);
        Require(f.Resets() == resets + 1 && f.Session() == Active, "no second identification follows the drop");
    }
    // The visible Drive menu follows a mount at once: RA never holds it back.
    for (const auto mode : modes)
    for (int visible = 0; visible < 2; ++visible)
    for (bool accepted : {false,true}) {
        AppMediaTestAccess f(Dir("menu-refresh-" + std::to_string(static_cast<int>(mode)) + std::to_string(visible) + std::to_string(accepted)),true,mode);
        f.Register(third,0,1234);
        if (accepted) f.Register(multi,0,5678);
        f.Login();
        Require(f.app.OpenDiskSpecsFromMenu({{third,0,0},{third,1,0}},&error), error);
        f.Pump(true);
        Require(f.ActiveGameId() == 1234, "old RA game identity");
        const int resets = f.Resets();
        f.ShowDrive(visible);
        const auto old_label = f.BankLabel(visible);
        Require(!old_label.empty(), "old menu bank has a label");
        bool drive2_open = false;
        Require(f.app.OpenDiskPairFromMenu(multi,&drive2_open,&error), error);
        f.Expect(0,multi,0); f.Expect(1,multi,1);
        f.MenuTick();
        Require(f.BankLabel(visible) != old_label, "visible disk list refreshes with the mount");
        Require(f.BankLabel(visible) == f.app.GetDiskManager()[visible]->GetName(0), "menu label matches mounted disk");
        f.Pump(true,5678);
        Require(f.Session() == (accepted ? Active : Offline),
            "a registered new title starts; unregistered media end the session");
        if (accepted) Require(f.ActiveGameId() == 5678, "new RA game identity");
        Require(f.Resets() == resets + (accepted ? 1 : 0), "only a new title resets a menu pair");
    }
    // A one-drive Offline reset identifies the new Drive 1 medium.
    for (const auto mode : modes) {
        AppMediaTestAccess f(Dir("reset-single-" + std::to_string(static_cast<int>(mode))),true,mode);
        f.Login();
        Require(f.app.OpenDiskFromMenu({triple,0,0},&error), error);
        f.Pump(false);
        Require(f.Session() == Offline, "single-drive reset starts Offline");
        Require(f.app.ChangeDiskBankFromMenu(0,2,&error), error);
        const int resets = f.Resets();
        f.app.Reset();
        f.Pump(true);
        Require(f.Session() == Active, "Offline reset identifies the new Drive 1 medium");
        Require(f.Resets() == resets + 1, "single-drive reset happens once");
    }
    // Match the E2E sequence: Active -> disconnected replacement by a medium
    // RA does not know -> Offline -> reconnect -> known disk -> normal Reset.
    for (const auto mode : modes) {
        AppMediaTestAccess f(Dir("reset-disconnected-single-" + std::to_string(static_cast<int>(mode))),true,mode);
        f.Login();
        Require(f.app.OpenDiskFromMenu({triple,0,0},&error), error);
        f.Pump(true);
        Require(f.Session() == Active, "known anchor starts Active");
        f.Disconnect();
        Require(f.app.ChangeDiskBankFromMenu(0,1,&error), error);
        f.Tick();
        Require(f.Session() == Offline, "disconnected replacement by unknown media is Offline");
        f.Reconnect();
        Require(f.app.ChangeDiskBankFromMenu(0,0,&error), error);
        f.app.Reset();
        f.Pump(true);
        Require(f.Session() == Active, "known anchor is identified after reconnect and normal Reset");
    }
    // Explicit Reset may retry a preserved token after a transport failure,
    // but must not retry rejected credentials or loop when the retry also fails.
    for (const auto mode : modes)
    for (int outcome : {0,1,2}) {
        const bool rejected = outcome==2;
        AppMediaTestAccess f(Dir("reset-login-"+std::to_string(static_cast<int>(mode))+std::to_string(outcome)),true,mode,true);
        f.Disconnect();
        f.BeginSavedLogin();
        f.ReplyLoginFailure(rejected);
        Require(f.app.OpenDiskFromMenu({triple,0,0},&error),error);
        f.Pump(false);
        Require(f.Session()==Offline,"failed login mounts locally Offline");
        const auto sent=f.http->SentRequests().size();
        Require(f.credentials->loads==1,"no login retry while unreachable");
        f.Reconnect();
        f.app.Reset();
        f.Tick();
        if (rejected) {
            f.Pump(true);
            Require(f.Session()==Offline && f.credentials->loads==1 &&
                f.http->SentRequests().size()==sent,"Reset never reuses rejected credentials");
        }
        else {
            Require(f.credentials->loads==2 && f.http->SentRequests().size()==sent+1,
                "Reset retries the saved login once after reachability returns");
            if (outcome==0) {
                f.Pump(true);
                Require(f.Session()==Active,"reset retry identifies the mounted game");
            }
            else {
                f.ReplyLoginFailure(false);
                for (int i=0;i<12;++i) f.Tick();
                Require(f.Session()==Offline && f.credentials->loads==2 &&
                    f.http->SentRequests().size()==sent+1,"failed retry does not start a login loop");
            }
        }
    }
    // Reset can precede the completion of the login started while offline.
    // Its late timeout must not fail the replacement launch's saved login.
    for (const auto mode : modes) {
        AppMediaTestAccess f(Dir("reset-pending-login-"+std::to_string(static_cast<int>(mode))),true,mode,true);
        f.Disconnect();
        f.BeginSavedLogin();
        const auto old_login=f.http->SentRequests().back();
        Require(f.app.OpenDiskFromMenu({triple,0,0},&error),error);
        f.Tick();
        Require(f.credentials->loads==1,"offline pending login is not restarted");
        f.Reconnect();
        const auto sent=f.http->SentRequests().size();
        f.app.Reset();
        Require(f.credentials->loads==1 && f.http->SentRequests().size()==sent,
            "Reset waits for the old login callback before retrying");
        Xm8Ra::RaHttpResponse timeout;
        timeout.request_id=old_login.request_id;
        timeout.transport_result=Xm8Ra::RaHttpTransportResult::Timeout;
        f.http->Complete(timeout);
        f.Tick();
        Require(f.credentials->loads==2 && f.http->SentRequests().size()==sent+1,
            "Reset retries once after the old login timeout arrives");
        Require(f.LoginState()==Xm8Ra::RaLoginState::LoginPending,
            "late timeout does not fail the replacement login");
        f.Pump(true);
        Require(f.Session()==Active,"replacement login identifies the game (session="+
            std::to_string(static_cast<int>(f.Session()))+", login="+std::to_string(static_cast<int>(f.LoginState()))+")");
    }
    // A retained unlock is synchronised before identification, but never
    // forever: an acknowledgement releases the launch, and a server which does
    // not answer releases it after the deadline with the unlock still queued.
    for (bool answered : {true,false}) {
        AppMediaTestAccess f(Dir(std::string("reset-await-outbox-") + (answered ? "ack" : "deadline")),true);
        f.Login();
        Require(f.app.OpenDiskFromMenu({triple,0,0},&error),error);
        f.Pump(true);
        f.QueueUnlockSync();
        f.Disconnect();
        Require(f.app.ChangeDiskBankFromMenu(0,1,&error),error);
        f.Tick();
        f.Reconnect();
        Require(f.app.ChangeDiskBankFromMenu(0,0,&error),error);
        const int resets = f.Resets();
        f.app.Reset();
        for (int i=0;i<4;++i) f.Tick();
        Require(f.Session()==Starting && f.WaitingForUnlockSync(),
            "normal Reset waits for the retained unlock acknowledgement");
        if (answered) {
            f.Pump(true);
            Require(f.Session()==Active,
                "acknowledged unlock releases the reset identification barrier");
        }
        else {
            f.ExpireLaunchDeadline();
            f.Tick();
            Require(f.Session()==Offline,
                "an unanswered unlock sync releases the game after the deadline");
            Require(f.QueuedUnlocks()==1, "the unlock stays queued for a later retry");
            f.Expect(0,triple,0);
        }
        Require(f.Resets()==resets+1, "the game boots exactly once either way");
    }
    // An unlock which is never sent (refused by the server, or edited in the
    // outbox file) is kept and reported, and no longer keeps games from
    // starting an RA session.
    for (bool edited : {false,true}) {
        AppMediaTestAccess f(Dir(std::string("held-unlock-") + (edited ? "edited" : "refused")),true);
        f.Login();
        f.QueueHeldUnlock(edited);
        const auto sent = f.http->SentRequests().size();
        Require(f.app.OpenDiskFromMenu({triple,0,0},&error),error);
        f.Pump(true);
        Require(f.Session()==Active, "a held unlock does not keep the game from starting its RA session");
        Require(f.HeldUnlocks()==1 && f.QueuedUnlocks()==1, "the held unlock stays in the outbox and is counted");
        Require(!f.Mentions(sent,"r=awardachievement"), "the held unlock is never submitted");
        Require(f.MenuStatus().find("[1 held]") != std::string::npos, "the RA menu reports the held unlock: " + f.MenuStatus());
        const int resets = f.Resets();
        f.app.Reset();
        f.Pump(true);
        Require(f.Session()==Active && f.Resets()==resets+1, "a later reset keeps the session with the unlock still held");
        Require(!f.Mentions(sent,"r=awardachievement"), "the held unlock is not submitted after a reset either");
    }
    // Identification itself is bounded by the same deadline.
    for (const auto mode : modes) {
        AppMediaTestAccess f(Dir("launch-deadline-" + std::to_string(static_cast<int>(mode))),true,mode);
        f.Login();
        const int resets = f.Resets();
        Require(f.app.OpenDiskFromMenu({triple,0,0},&error),error);
        for (int i=0;i<4;++i) f.Tick(); // RA never answers.
        Require(f.Session()==Starting, "identification waits for RA");
        Require(!f.app.OpenDiskFromMenu({third,0,0},&error), "Drive 1 cannot be replaced while it is identified");
        Require(f.app.OpenDiskFromMenu({second,1,0},&error), error); // Drive 2 stays free.
        f.ExpireLaunchDeadline();
        f.Tick();
        Require(f.Session()==Offline, "an unanswered launch gives up after the deadline");
        f.Expect(0,triple,0); f.Expect(1,second,0);
        Require(f.Resets()==resets+1, "the game still boots exactly once");
    }
    // Reset from Offline identifies Drive 1 only. A mounted Drive 2 is not
    // verified, and a later reset of the running game contacts nobody.
    for (const auto mode : modes) {
        AppMediaTestAccess f(Dir("reset-kept-drive2-" + std::to_string(static_cast<int>(mode))),true,mode);
        f.Login();
        Require(f.app.OpenDiskFromMenu({triple,0,0},&error), error);
        f.Pump(false);
        Require(f.Session() == Offline, "reset starts Offline");
        Require(f.app.OpenDiskFromMenu({second,1,0},&error), error);
        Require(f.app.ChangeDiskBankFromMenu(0,2,&error), error);
        const int resets = f.Resets();
        const auto requests = f.http->SentRequests().size();
        f.app.Reset();
        Require(f.Session() == Starting, "reset identifies the new Drive 1 medium");
        f.Pump(true);
        f.Expect(0,triple,2); f.Expect(1,second,0);
        Require(f.Session() == Active, "the mounted Drive 2 does not decide activation");
        Require(!f.Mentions(requests, f.BankHash(second,0)), "reset never sends Drive 2 to RA");
        Require(f.Resets() == resets + 1, "reset happens once");
        const auto sent = f.http->SentRequests().size();
        f.app.Reset();
        f.Tick();
        Require(f.Session() == Active && f.http->SentRequests().size() == sent,
            "a reset of the running game keeps its session without a request");
        // An empty Drive 1 at reset ends the session.
        Require(f.app.EjectDiskFromMenu(0,&error), error);
        Require(f.Session() == Active, "eject alone does not end the session");
        f.app.Reset();
        f.Tick();
        Require(f.Session() == Ready, "a reset with an empty Drive 1 ends the session");
    }
    // A two-file exchange during a running game: the invalid request changes
    // nothing; the valid one mounts both drives at once.
    for (const auto mode : modes)
    for (bool accepted : {false,true})
    for (bool drop : {false,true}) {
        AppMediaTestAccess f(Dir((drop ? "active-m3u" : "active-menu-pair") +
            std::to_string(static_cast<int>(mode)) + std::to_string(accepted)),true,mode);
        f.Register(triple,0,1234);
        if (accepted) f.Register(triple,2,1234);
        f.Login();
        Require(f.app.OpenDiskFromMenu({triple,0,0},&error), error);
        f.Pump(true);
        const auto hash = f.ActiveHash();
        const int before = f.Resets();
        Require(!f.app.OpenDiskSpecsFromMenu({{triple,0,2},{second,1,99}},&error),
            "invalid second bank rejects before anything changes");
        Require(f.ActiveHash() == hash && f.Resets() == before && f.Session() == Active,
            "local validation failure preserves session and reset count");
        f.Expect(0,triple,0); f.ExpectEmpty(1);
        const bool opened = drop ? f.Drop(playlist,&error) :
            f.app.OpenDiskSpecsFromMenu({{triple,0,2},{second,1,0}},&error);
        Require(opened, "active two-file request: " + error);
        f.Expect(0,triple,2); f.Expect(1,second,0);
        f.Pump(accepted);
        f.Expect(0,triple,2); f.Expect(1,second,0);
        Require(f.Session() == (accepted ? Active : Offline),
            "a disk of the running game keeps the session; unregistered media end it");
        Require(f.Resets() == before + (drop ? 1 : 0), "only drop requests reset");
    }
    for (const auto mode : modes) {
        AppMediaTestAccess f(Dir("active-single-playlist" + std::to_string(static_cast<int>(mode))),true,mode);
        f.Register(triple,0,1234); f.Register(third,0,1234);
        f.Login();
        Require(f.app.OpenDiskFromMenu({triple,0,0},&error), error);
        f.Pump(true);
        Require(f.app.OpenDiskFromMenu({second,1,0},&error), error);
        const int before = f.Resets();
        Require(f.Drop(single_playlist,&error), error);
        f.Expect(0,third,0); f.ExpectEmpty(1);
        f.Pump(true);
        Require(f.Session() == Active, "single playlist of the running game keeps the session");
        Require(f.Resets() == before + 1, "playlist mount and eject finish with one reset");
    }
    // The same Drive 1 disk with a new Drive 2: Drive 2 is simply mounted.
    for (const auto mode : modes)
    for (bool drop : {false,true}) {
        AppMediaTestAccess f(Dir("same-anchor-pair" + std::to_string(static_cast<int>(mode)) + std::to_string(drop)),true,mode);
        f.Register(triple,2,1234);
        f.Login();
        Require(f.app.OpenDiskFromMenu({triple,0,2},&error), error);
        f.Pump(true);
        const auto hash = f.ActiveHash();
        const int before = f.Resets();
        Require(drop ? f.Drop(playlist,&error) :
            f.app.OpenDiskSpecsFromMenu({{triple,0,2},{second,1,0}},&error), error);
        f.Expect(0,triple,2); f.Expect(1,second,0);
        f.Pump(true);
        Require(f.Session() == Active && f.ActiveHash() == hash, "same-anchor pair keeps the session");
        Require(f.Resets() == before + (drop ? 1 : 0), "same-anchor pair resets only for a drop");
    }
    for (const auto mode : modes)
    for (bool active : {false,true})
    for (int entry = 0; entry < 3; ++entry) {
        const auto dir = Dir("batch-prepare-failure" +
            std::to_string(static_cast<int>(mode)) + std::to_string(entry) + std::to_string(active));
        AppMediaTestAccess f(dir,true,mode);
        f.Login();
        if (active) {
            Require(f.app.OpenDiskFromMenu({third,0,0},&error), error);
            f.Pump(true);
        }
        const auto session = f.Session();
        const auto hash = f.ActiveHash();
        const int before = f.Resets();
        const auto sent = f.http->SentRequests().size();
        Xm8Ra::D88MediaInfo blocked;
        Require(Xm8Ra::ProbeD88File(second.c_str(),&blocked,&error), error);
        // A file where the fixture's media directory should be prevents only
        // the second working copy from being prepared. Source D88 remains valid.
        { std::ofstream file(f.MediaRoot() + "/" + blocked.md5); file << "blocked"; Require(file.good(), "block fixture media directory"); }
        const auto failed_playlist = dir + "/failed.m3u";
        { std::ofstream file(failed_playlist); file << unregistered << "#0\n" << second << "#0\n"; }
        const bool opened = entry == 0 ? f.app.OpenDiskSpecsFromMenu({{unregistered,0,0},{second,1,0}},&error) :
            (entry == 1 ? f.Drop(failed_playlist,&error) : f.Startup({{unregistered,0,0},{second,1,0}},&error));
        Require(!opened, "working-copy preparation failure is reported");
        if (active) f.Expect(0,third,0);
        else f.ExpectEmpty(0);
        f.ExpectEmpty(1);
        Require(f.Session() == session && f.ActiveHash() == hash && f.Resets() == before,
            "second preparation failure preserves old session and VM");
        Require(f.http->SentRequests().size() == sent, "no RA request before both media are prepared");
    }
    // A raw D88 drop replaces both drives of the running game at once.
    for (bool accepted : {false,true}) {
        AppMediaTestAccess f(Dir(accepted ? "active-pair-success" : "active-pair-fallback"),true);
        f.Register(triple,2,1234);
        if (accepted) f.Register(triple,0,1234);
        f.Login();
        Require(f.app.OpenDiskFromMenu({triple,0,2},&error), error);
        f.Pump(true);
        const int before = f.Resets();
        Require(f.Drop(triple,&error), error);
        f.Expect(0,triple,0); f.Expect(1,triple,1);
        f.Pump(accepted);
        f.Expect(0,triple,0); f.Expect(1,triple,1);
        Require(f.Session() == (accepted ? Active : Offline),
            "a dropped pair keeps the running game or ends its session");
        Require(f.Resets() == before + 1, "active pair completes with one drop reset");
    }
    // Switching RA mode never carries a mounted disk across: the original
    // image and its RA working copy may hold different saves.
    for (const auto mode : modes) {
        AppMediaTestAccess f(Dir("mode-switch-" + std::to_string(static_cast<int>(mode))),true,mode);
        f.Register(multi,0,1234);
        f.Login();
        bool drive2 = false;
        Require(f.app.OpenDiskPairFromMenu(multi,&drive2,&error), error);
        f.Pump(true);
        Require(f.Session() == Active, "mode switch fixture starts Active");
        int resets = f.Resets();
        Require(f.ToggleRaMode() && !f.RaEnabled(), "RA mode switches OFF");
        f.ExpectEmpty(0); f.ExpectEmpty(1);
        Require(f.Session() == Ready && f.Resets() == resets + 1,
            "RA OFF starts from empty drives and one cold reset");
        // RA OFF mounts the original image directly.
        Require(f.app.OpenDiskSpecsFromMenu({{single,0,0},{second,1,0}},&error), error);
        f.Expect(0,single,0); f.Expect(1,second,0);
        resets = f.Resets();
        Require(f.ToggleRaMode() && f.RaEnabled(), "RA mode switches ON");
        f.ExpectEmpty(0); f.ExpectEmpty(1);
        Require(f.Session() == Ready && f.Resets() == resets + 1,
            "RA ON starts from empty drives and one cold reset");
        Require(f.app.OpenDiskFromMenu({multi,0,0},&error), error);
        f.Pump(true);
        f.Expect(0,multi,0);
        Require(f.Session() == Active, "a game opened after the switch is identified");
    }
    {
        const std::string dir = root + "/restore";
        Require(Xm8Ra::EnsureRaDirectoryTree(dir), "create snapshot fixture root");
        AppMediaTestAccess f(dir, false);
        Require(f.app.OpenDiskSpecsFromMenu({{single,0,0},{second,1,0}}, &error), error);
        DiskMountSnapshots anchor_only(f.app.GetDiskManager(), 0, 0);
        Require(f.app.EjectDiskFromMenu(1,&error), error);
        Require(f.app.OpenDiskFromMenu({third,0,0},&error), error);
        Require(anchor_only.Restore(f.app.GetDiskManager()), "restore selected Drive 1");
        f.Expect(0,single,0); f.ExpectEmpty(1); // Independent Eject must survive.

        const std::string missing = dir + "/removed.d88";
        Require(Xm8Ra::CopyRaFile(single, missing, &error), error);
        Require(f.app.OpenDiskSpecsFromMenu({{missing,0,0},{second,1,0}},&error), error);
        DiskMountSnapshots both(f.app.GetDiskManager());
        Require(f.app.EjectDiskFromMenu(0,&error), error);
        Require(f.app.EjectDiskFromMenu(1,&error), error);
        Require(Xm8Ra::RemoveRaFile(missing,&error), error);
        Require(!both.Restore(f.app.GetDiskManager()), "report failed restoration");
        f.ExpectEmpty(0); f.Expect(1,second,0); // Failure must not skip the other drive.
        Require(f.app.OpenDiskFromMenu({single,0,0},&error), error);
        DiskMountTargets targets;
        targets.Mount({triple,0,2});
        targets.Mount({third,1,0});
        const auto previous = targets.Capture(f.app.GetDiskManager());
        Require(targets.Apply(f.app.GetDiskManager()), "apply distinct files and selected banks together");
        f.Expect(0,triple,2); f.Expect(1,third,0);
        Require(previous.Restore(f.app.GetDiskManager()), "restore both explicit targets");
        f.Expect(0,single,0); f.Expect(1,second,0);

        const auto lost = dir + "/lost-before-commit.d88";
        Require(Xm8Ra::CopyRaFile(third,lost,&error), error);
        targets.Mount({lost,1,0});
        Require(Xm8Ra::RemoveRaFile(lost,&error), error);
        Require(!targets.Apply(f.app.GetDiskManager()), "second-drive apply failure is reported");
        f.Expect(0,triple,2); // Sequential application happened before the failure.
        Require(previous.Restore(f.app.GetDiskManager()), "restore after partial application");
        f.Expect(0,single,0); f.Expect(1,second,0);

        DiskMountTargets auxiliary_only;
        auxiliary_only.Eject(1);
        Require(auxiliary_only.Apply(f.app.GetDiskManager()), "explicit auxiliary eject");
        f.Expect(0,single,0); f.ExpectEmpty(1);
        Require(f.Resets() == 0, "apply and restoration never reset the VM");
    }
    Require(Xm8Ra::RemoveRaTree(root), "remove generated test files"); // Only this test's unique generated directory.
    std::cout << "ra_app_media_integration_test: PASS\n";
    return 0;
}
