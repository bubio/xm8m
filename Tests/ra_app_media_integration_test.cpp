// Baseline tests deliberately execute App's production routing, persistence,
// DiskManager and real FDC. Only the platform host and RA HTTP are substitutes.
#include "ra_file_util.h"
#include <fstream>
#include <iostream>
#include <cstring>
#include <chrono>
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
    bool Save(const Xm8Ra::RaCredentials&, std::string*) override { return true; }
    bool Load(Xm8Ra::RaCredentials*, std::string*) const override { return false; }
    bool Delete(std::string*) override { return true; }
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
    ResetCounter* resets = nullptr;
    size_t answered = 0;
    explicit AppMediaTestAccess(const std::string& root, bool enabled, Xm8Ra::RaPlayMode mode = Xm8Ra::RaPlayMode::Hardcore)
    {
        host_root = root;
        app.setting = new Setting;
        app.platform = new Platform(&app);
        app.audio = new Audio;
        app.video = new Video(&app);
        app.font = new Font(&app);
        app.input = new Input(&app);
        app.converter = new Converter;
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
            options.credentials_store = std::make_unique<NoCredentials>();
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
    std::string ActiveHash() const { return app.ra_service->GameSessionSnapshot().hash; }
    int64_t LibraryGame() const { return app.ra_loaded_library_game_id; }
    bool Pending() const { return app.ra_disk_transaction.state.Active(); }
    void Tick() { app.ProcessRaService(false); }
    void Login()
    {
        std::string error;
        app.ra_connectivity_tracker.Observe(Xm8Ra::RaReachabilityState::Reachable);
        Require(app.ra_service->BeginLoginWithPassword("fixture", "fixture", &error), error);
        Pump(false);
        Require(app.ra_service->LoginSnapshot().state == Xm8Ra::RaLoginState::LoggedIn, "fixture login");
    }
    void Pump(bool registered)
    {
        for (int tick = 0; tick < 16; ++tick) {
            // Copy: handling one response may append another request.
            const auto requests = http->SentRequests();
            while (answered < requests.size()) {
                const auto request = requests[answered++];
                if (http->IsCanceled(request.request_id)) continue;
                std::string json;
                if (request.post_data.find("r=login2") != std::string::npos)
                    json = R"({"Success":true,"User":"fixture","Token":"fixture-token","Score":0,"SoftcoreScore":0,"Messages":0})";
                else if (request.post_data.find("r=achievementsets") != std::string::npos)
                    json = registered ? R"({"Success":true,"GameId":1234,"Title":"Fixture","ConsoleId":47,"ImageIconUrl":"","RichPresenceGameId":0,"RichPresencePatch":"","Sets":[{"AchievementSetId":1234,"GameId":1234,"Title":"Fixture","Type":"core","ImageIconUrl":"","Achievements":[],"Leaderboards":[]}]})"
                        : R"({"Success":true,"GameId":0,"Title":"","ConsoleId":47,"ImageIconUrl":"","RichPresenceGameId":0,"RichPresencePatch":"","Sets":[]})";
                else if (request.post_data.find("r=startsession") != std::string::npos)
                    json = R"({"Success":true,"Unlocks":[],"HardcoreUnlocks":[],"ServerNow":1710000000})";
                else if (request.post_data.find("r=gameid") != std::string::npos)
                    json = registered ? R"({"Success":true,"GameID":1234})" : R"({"Success":true,"GameID":0})";
                else if (request.post_data.find("r=hashlibrary") != std::string::npos)
                    json = R"({"Success":true,"MD5List":{}})";
                else if (request.post_data.find("r=allprogress") != std::string::npos)
                    json = R"({"Success":true,"Response":{}})";
                else if (request.post_data.find("r=ping") != std::string::npos)
                    json = R"({"Success":true})";
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

int main()
{
    const char* temporary = std::getenv("TMPDIR");
    const std::string root = std::string(temporary ? temporary : "/tmp") +
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
    // Hold fake HTTP completions to observe the production async boundary.
    for (const auto mode : {Xm8Ra::RaPlayMode::Casual, Xm8Ra::RaPlayMode::Hardcore}) {
        const std::string dir = root + (mode == Xm8Ra::RaPlayMode::Casual ? "/async-casual" : "/async-hardcore");
        Require(Xm8Ra::EnsureRaDirectoryTree(dir), "create async fixture root");
        AppMediaTestAccess f(dir, true, mode);
        f.Login();
        Require(f.app.OpenDiskFromMenu({triple,0,0}, &error), error);
        f.Pump(true);
        Require(f.Session() == Xm8Ra::RaSessionState::Active, "async fixture Active");
        const std::string anchor = f.ActiveHash();
        const int before = f.Resets();
        Require(f.app.OpenDiskFromMenu({second,1,0}, &error), error);
        f.Tick(); // Dispatch verification, deliberately leave the reply pending.
        Require(f.Pending(), "Drive 2 waits for RA verification");
        f.Expect(0,triple,0); f.ExpectEmpty(1);
        Require(f.ActiveHash() == anchor, "pending auxiliary keeps RA anchor");
        Require(!f.app.ChangeDiskBankFromMenu(0,2,&error), "competing bank change is busy");
        f.Expect(0,triple,0); f.ExpectEmpty(1);
        f.Pump(true);
        f.Expect(0,triple,0); f.Expect(1,second,0);
        Require(!f.Pending() && f.Session() == Xm8Ra::RaSessionState::Active,
            "verified auxiliary finishes without ending session");
        Require(f.ActiveHash() == anchor && f.Resets() == before,
            "Drive 2 changes neither active hash nor reset count");

        Require(f.app.ChangeDiskBankFromMenu(0,2,&error), error);
        f.Tick();
        Require(f.Pending(), "different anchor bank waits for media change");
        f.Expect(0,triple,0); f.Expect(1,second,0);
        Require(f.ActiveHash() == anchor, "pending change retains active hash");
        f.Pump(true);
        f.Expect(0,triple,2); f.Expect(1,second,0);
        Require(f.ActiveHash() != anchor && f.Session() == Xm8Ra::RaSessionState::Active,
            "successful anchor change updates RA and VM");
        Require(f.Resets() == before, "ordinary anchor bank exchange does not reset");

        const auto changed_anchor = f.ActiveHash();
        Require(f.app.OpenDiskFromMenu({unregistered,1,0}, &error), error);
        f.Tick();
        f.Expect(1,second,0);
        Require(f.ActiveHash() == changed_anchor, "unanswered auxiliary preserves anchor");
        f.Pump(false);
        f.Expect(0,triple,2); f.Expect(1,unregistered,0);
        Require(f.Session() == Xm8Ra::RaSessionState::Offline,
            "unregistered auxiliary still mounts and ends RA session");
        Require(f.Resets() == before, "auxiliary fallback does not invent a reset");
    }
    for (const auto mode : {Xm8Ra::RaPlayMode::Casual, Xm8Ra::RaPlayMode::Hardcore}) {
        for (bool registered : {false,true}) {
            for (int entry = 0; entry < 5; ++entry) {
                const auto dir = root + "/paired-" + std::to_string(static_cast<int>(mode)) +
                    "-" + std::to_string(registered) + "-" + std::to_string(entry);
                Require(Xm8Ra::EnsureRaDirectoryTree(dir), "create paired fixture root");
                AppMediaTestAccess f(dir, true, mode);
                f.Login();
                const int before = f.Resets();
                if (entry == 0)
                    Require(f.app.OpenDiskSpecsFromMenu({{triple,0,2},{second,1,0}},&error), error);
                else if (entry == 1)
                    Require(f.Startup({{triple,0,2},{second,1,0}},&error), error);
                else Require(f.Drop(entry == 2 ? playlist : (entry == 3 ? multi : single_playlist),&error), error);
                f.Pump(registered);
                if (entry == 4) { f.Expect(0,third,0); f.ExpectEmpty(1); }
                else if (entry == 3) { f.Expect(0,multi,0); f.Expect(1,multi,1); }
                else { f.Expect(0,triple,2); f.Expect(1,second,0); }
                Require(f.Session() == (registered ? Xm8Ra::RaSessionState::Active :
                    Xm8Ra::RaSessionState::Offline), "paired launch reaches expected session");
                Require(f.Resets() == before + 1, "paired new launch resets exactly once: actual=" + std::to_string(f.Resets() - before));
                Require(!f.Pending(), "paired launch finishes its transaction");
                if (registered) {
                    // The successful production launch persisted the real Library profile.
                    const auto game_id = f.LibraryGame();
                    Require(game_id > 0, "identified Library game exists");
                    const auto hash = f.ActiveHash();
                    const int boot = f.Resets();
                    Require(f.Launch(game_id,&error), error);
                    Require(f.Session() == Xm8Ra::RaSessionState::Starting,
                        "Library START begins a fresh session even for the same hash");
                    f.Pump(true);
                    Require(f.Session() == Xm8Ra::RaSessionState::Active && f.ActiveHash() == hash,
                        "Library profile boots its anchor");
                    if (entry == 4) { f.Expect(0,third,0); f.ExpectEmpty(1); }
                    else if (entry == 3) { f.Expect(0,multi,0); f.Expect(1,multi,1); }
                    else { f.Expect(0,triple,2); f.Expect(1,second,0); }
                    Require(f.Resets() == boot + 1 && !f.Pending(), "Library START resets once and completes");
                }
            }
        }
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
        Require(f.Resets() == 0, "snapshot and restoration never reset the VM");
    }
    Require(Xm8Ra::RemoveRaTree(root), "remove generated test files"); // Only this test's unique generated directory.
    std::cout << "ra_app_media_integration_test: PASS\n";
}
