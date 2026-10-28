"""VMPal and AVD adapters. VMPal reuses an explicitly dedicated VM and one pinned baseline snapshot."""
import json
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import struct
import tempfile
import time
import uuid

from .runner import Failure, Pending, write_json

VMPAL = "/Applications/VMPal.app/Contents/Helpers/VMPalMachine.app/Contents/MacOS/VMPalMachine"


def require_unprotected_screen(text):
    lowered = text.casefold()
    if (("password" in lowered and any(label in lowered for label in ("username", "keychain", "windows security"))) or
            any(label in lowered for label in ("authentication required", "unlock login keyring", "認証が必要"))):
        raise Pending("Protected authentication requires user preparation; no screen evidence was recorded")
    return text


def native_tool(directory, name):
    folder = directory.parent.parent / "tools"
    folder.mkdir(parents=True, exist_ok=True)
    binary = folder / name
    source = Path(__file__).with_name(name + ".swift")
    if not binary.is_file() or binary.stat().st_mtime < source.stat().st_mtime:
        execute(["swiftc", "-module-cache-path", str(folder / "swift-cache"), str(source), "-o", str(binary)])
    return binary


def compare_images(directory, actual, reference, region):
    return json.loads(execute([native_tool(directory, "image_check"), actual, reference, *map(str, region)]))


def read_ocr(directory, capture, cpu_english=False):
    tool = native_tool(directory, "ocr")
    # Retry only observation of this same temporary image, never GUI input.
    for attempt in range(2):
        try:
            return json.loads(execute([tool, capture] + (["--english-cpu"] if cpu_english else []), timeout=15))
        except (Failure, json.JSONDecodeError) as error:
            if attempt:
                raise Pending("Local OCR unavailable after bounded retries; GUI input was not repeated") from error


def adb_tool(profile):
    configured = profile.get("adb") or shutil.which("adb")
    if not configured or not Path(configured).is_file():
        raise Pending("ADB inventory is unavailable; configure its absolute path to enforce one VM/AVD")
    return str(configured)


def execute(argv, timeout=120, input_text=None, discard_output=False):
    try:
        output = {"stdout":subprocess.DEVNULL, "stderr":subprocess.DEVNULL} if discard_output else {"capture_output":True}
        result = subprocess.run([str(a) for a in argv], text=True, input=input_text, timeout=timeout, **output)
    except subprocess.TimeoutExpired as error:
        raise Failure(f"{Path(str(argv[0])).name} timed out") from error
    if result.returncode:
        # Commands/output can contain private paths. Keep only the executable and code.
        raise Failure(f"{Path(str(argv[0])).name} failed (exit {result.returncode})")
    return result.stdout or ""


class Vmpal:
    def __init__(self, profile, directory):
        self.profile, self.directory = profile, directory
        self.vm = None
        self.claimed = False
        self.vm_name = None
        self.network_dirty = False
        self.guard = "xm8-e2e-net-" + uuid.uuid4().hex
        self.tool = profile.get("vmpal", VMPAL)
        self.phase = "inventory"
        self.adb_ready = False

    def adb_devices(self):
        try:
            tool = adb_tool(self.profile)
            if not self.adb_ready:
                # A daemon started under Hub must not retain a capture pipe
                # that keeps the inventory subprocess waiting for EOF.
                execute([tool, "start-server"], timeout=10, discard_output=True)
                self.adb_ready = True
            return execute([tool, "devices"], timeout=10)
        except Failure as error:
            raise Pending("ADB inventory unavailable; single VM/AVD verification was not bypassed") from error

    def cli(self, *args, timeout=120):
        try:
            return execute([self.tool, "--cli", *args], timeout)
        except Failure as error:
            raise Failure(f"VMPal {args[0]} failed during {self.phase}") from error

    def rpc(self, name, args):
        value = json.loads(self.cli("tool", name, json.dumps(args)))
        if value.get("isError") or value.get("error"):
            raise Failure("VMPal " + name + " failed")
        return value

    def inventory(self):
        return self.rpc("vmpal_list_vms", {})["vms"]

    def check_exclusive(self):
        if any(v["id"] != self.vm and v["state"] not in ("stopped", "suspended") for v in self.inventory()):
            raise Pending("Another VM became active; it was not stopped")
        if any(line.startswith("emulator-") for line in self.adb_devices().splitlines()):
            raise Pending("An Android emulator became active")

    def target(self, inventory):
        if not self.profile.get("dedicated_vm") or not self.profile.get("vm_id") or not self.profile.get("baseline_snapshot"):
            raise Pending("Configure an explicitly dedicated VM and a pinned baseline snapshot")
        expected = self.profile.get("vm_name", "")
        if not expected or not isinstance(expected, str):
            raise Pending("Configure the exact name of the user-designated E2E VM")
        vm = next((v for v in inventory if v["id"] == self.profile["vm_id"]), None)
        if not vm or vm.get("name") != expected:
            raise Pending("Dedicated VM identity/name differs from the configured target; nothing was reverted")
        return vm

    def state_file(self):
        from .runner import DEFAULT_CONFIG
        root = DEFAULT_CONFIG.parent / "vm-state"
        root.mkdir(parents=True, exist_ok=True, mode=0o700)
        return root / (self.profile["vm_id"] + ".json")

    def prepare(self, artifact):
        inventory = self.inventory()
        if any(v["state"] not in ("stopped", "suspended") for v in inventory):
            raise Pending("Another VM is running; it was not stopped")
        target = self.target(inventory)
        if target["state"] != "stopped":
            raise Pending("The dedicated E2E VM must be shut down before baseline restoration")
        devices = self.adb_devices()
        if any(line.startswith("emulator-") for line in devices.splitlines()):
            raise Pending("An Android emulator is already running")
        if not self.profile.get("snapshot_ready"):
            raise Pending("Configure a read-only pending-data check before enabling snapshot reuse")
        state_file = self.state_file()
        if state_file.exists() and not json.loads(state_file.read_text()).get("safe_to_restore"):
            raise Pending("Previous run needs inspection before baseline restoration; its data was retained")
        snapshots = self.rpc("vmpal_snapshots", {"vm":target["id"]}).get("snapshots", [])
        snapshot = next((v for v in snapshots if v.get("id") == self.profile["baseline_snapshot"]), None)
        if not snapshot:
            raise Pending("The pinned baseline snapshot does not exist on this VM")
        self.vm, self.vm_name, self.claimed = target["id"], target["name"], True
        write_json(state_file, {"vm":self.vm, "safe_to_restore":False, "run":str(self.directory)})
        write_json(self.directory / "managed-vm.json", {"id":self.vm, "name":self.vm_name})
        self.inspect_before_restore()
        self.phase = "baseline restoration"
        self.rpc("vmpal_revert_snapshot", {"vm":self.vm, "snapshot":snapshot["id"], "takeSnapshotFirst":False})
        restored = self.target(self.inventory())
        if snapshot.get("hasSession"):
            # Resume only to shut down normally. Guest exec state restored from
            # RAM can be stale; inspect the same disk after its cold boot below.
            self.phase = "saved baseline session shutdown"
            self.cli("start", self.vm, "--no-sign-in", "--background", timeout=180)
            self.check_exclusive()
            self.cli("stop", self.vm, "--timeout", "110s", timeout=120)
            restored = self.target(self.inventory())
        if restored["state"] != "stopped" or restored.get("activity"):
            raise Failure("Baseline restoration did not leave the dedicated VM stopped")
        write_json(self.directory / "environment.json", {
            "backend":"vmpal", "reuse_mode":"snapshot", "vm_id":self.vm,
            "baseline_snapshot":snapshot["id"], "system":target.get("system"),
            "baseline_has_session":bool(snapshot.get("hasSession")),
            "guest":target.get("guest"), "memory_gb":target.get("memoryGB"), "cpus":target.get("cpuCount")})
        self.phase = "isolation"
        self.rpc("vmpal_update_vm", {"vm":self.vm, "sharedFolders":[], "forwardedPorts":[],
                                     "clipboard":False, "dragAndDrop":False, "microphone":False,
                                     "localNetwork":"off", "networkMode":"shared", "startAtLogin":False})
        self.check_exclusive()
        self.phase = "boot"
        self.cli("start", self.vm, "--wait", "--background", timeout=180)
        self.check_exclusive()
        self.phase = "restored baseline data inspection"
        write_json(self.directory / "baseline-check.json", {"status":"PENDING", "current_data_checked":False})
        try:
            self.guest(self.profile["snapshot_ready"])
        except Failure as error:
            raise Pending("Restored baseline needs inspection; installation was not started") from error
        write_json(self.directory / "baseline-check.json", {"status":"PASS", "current_data_checked":True})
        self.phase = "architecture check"
        architecture = self.guest(["powershell.exe", "-NoProfile", "-Command",
            "[System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture"] if self.profile["os"] == "windows"
            else ["/usr/bin/uname", "-m"]).strip()
        if architecture.casefold() not in ("arm64", "aarch64"):
            raise Pending("The selected guest is not ARM64")
        environment_file = self.directory / "environment.json"
        environment = json.loads(environment_file.read_text())
        environment["architecture"] = architecture
        environment_file.write_text(json.dumps(environment))
        self.phase = "artifact transfer"
        self.cli("cp", str(artifact.resolve()), self.vm + ":candidate", timeout=180)
        # Guest scripts are reviewed local config, executed only inside the configured dedicated VM.
        self.phase = "installation"
        for argv in self.profile.get("install", []):
            self.guest(argv)
        for argv in self.profile.get("install_admin", []):
            self.guest(argv, admin=True)
        if not self.profile.get("launch"):
            raise Pending("Guest launch command is not configured")
        if self.profile.get("ready"):
            self.phase = "GUI session readiness"
            deadline = time.monotonic() + 45
            while True:
                try:
                    self.guest(self.profile["ready"])
                    break
                except Failure:
                    if time.monotonic() >= deadline:
                        raise Pending("GUI session is unavailable; prepare user sign-in on the dedicated base VM")
                    time.sleep(1)
        self.wait_for_credentials()
        self.phase = "application launch"
        self.guest(self.profile["launch"])
        self.phase = "GUI test"

    def inspect_before_restore(self):
        # A previous clean run cannot certify data written by later manual use.
        # Inspect this VM's current disk before replacing it with the baseline.
        self.phase = "current guest inspection before restoration"
        verified = False
        proof = {"status":"PENDING", "vm":self.vm, "current_data_checked":False,
                 "normal_shutdown_verified":False}
        try:
            write_json(self.directory / "preflight.json", proof)
            self.check_exclusive()
            self.cli("start", self.vm, "--wait", "--background", timeout=180)
            self.check_exclusive()
            self.guest(self.profile["snapshot_ready"])
            proof["current_data_checked"] = True
            write_json(self.directory / "preflight.json", proof)
            self.cli("stop", self.vm, "--timeout", "110s", timeout=120)
            self.check_exclusive()
            if self.target(self.inventory())["state"] != "stopped":
                raise Failure("Current guest did not shut down normally")
            proof.update(status="PASS", normal_shutdown_verified=True)
            write_json(self.directory / "preflight.json", proof)
            verified = True
        except (Pending, Failure) as error:
            raise Pending("Current guest inspection or shutdown unavailable; data retained and baseline restoration blocked") from error
        finally:
            if not verified:
                # Leave the current disk/VM and unsafe marker for inspection.
                # The caller's generic cleanup must not turn this into a new
                # clean shutdown or overwrite the failed preflight marker.
                self.claimed = False

    def wait_for_credentials(self):
        command = self.profile.get("credential_ready")
        if not command:
            return
        self.phase = "credential-store readiness"
        try:
            self.guest(command)
            return
        except Failure as error:
            if not self.profile.get("manual_credential_unlock"):
                raise Pending("Guest credential store is locked or unavailable; user preparation is required") from error
        if self.profile["os"] != "linux":
            raise Pending("Manual credential-store preparation needs OS-specific calibration")
        timeout = float(self.profile.get("manual_unlock_timeout", 600))
        if not 30 <= timeout <= 900:
            raise ValueError("manual_unlock_timeout must be between 30 and 900 seconds")
        helper = Path(__file__).with_name("guests") / "prompt_keyring.py"
        try:
            self.guest(["/usr/bin/python3", "-c", "from gi.repository import Gio, GLib"])
        except Failure as error:
            raise Pending("Manual keyring preparation requires guest python3-gi") from error
        self.cli("cp", str(helper), self.vm + ":/tmp/xm8-e2e-prompt-keyring.py")
        state = {"status":"PENDING", "vm":self.vm, "vm_name":self.vm_name or self.profile.get("vm_name", "Dedicated E2E VM"),
                 "action":"Manually unlock the guest keyring", "timeout_seconds":timeout, "waiting":True}
        write_json(self.directory / "preparation.json", state)
        self.rpc("vmpal_show_vm", {"vm":self.vm})
        # The helper owns one persistent D-Bus connection for Unlock/Prompt.
        # It never calls GetSecret or accepts a password argument/stdin.
        self.guest(["/usr/bin/systemd-run", "--user", "--unit=xm8-e2e-keyring-prompt",
                    "/usr/bin/python3", "/tmp/xm8-e2e-prompt-keyring.py", str(int(timeout))])
        # Do not ask for manual action before the OS has actually drawn its prompt.
        prompt_deadline = time.monotonic() + 20
        while True:
            self.check_exclusive()
            # The user may unlock immediately, before our first screen observation.
            # A ready store needs no further prompt or manual-action request.
            try:
                self.guest(command)
            except Failure:
                pass
            else:
                state.update(status="READY", waiting=False,
                             action="No action required; tests are starting")
                write_json(self.directory / "preparation.json", state)
                print("READY: Keyringの解除を確認しました。RAテストを開始します。", flush=True)
                return
            observed = self.rpc("vmpal_observe", {"vm":self.vm, "mode":"text"})
            visible = json.dumps(observed, ensure_ascii=False).casefold()
            if ("authentication required" in visible or "unlock login keyring" in visible or
                    "認証が必要" in visible):
                state["prompt_confirmed"] = True
                write_json(self.directory / "preparation.json", state)
                break
            if time.monotonic() >= prompt_deadline:
                state.update(waiting=False, action="No action required; prompt is not visible",
                             reason="Manual keyring prompt was not observed")
                write_json(self.directory / "preparation.json", state)
                raise Pending(state["reason"])
            time.sleep(1)
        print(f"PENDING: VM {state['vm_name']} ({self.vm}): KeyringをVM内で手動解除してください（最大{int(timeout)}秒）。ランナーはパスワードを入力・保存しません。", flush=True)
        deadline = time.monotonic() + timeout
        while True:
            self.check_exclusive()
            try:
                self.guest(command)
                break
            except Failure:
                try:
                    self.guest(["/bin/sh", "-lc", "if systemctl --user is-failed --quiet xm8-e2e-keyring-prompt.service; then exit 1; fi"])
                except Failure as error:
                    state["waiting"] = False
                    state["action"] = "No action required; preparation ended"
                    state["reason"] = "Manual keyring unlock was cancelled or unavailable"
                    write_json(self.directory / "preparation.json", state)
                    raise Pending(state["reason"]) from error
                if time.monotonic() >= deadline:
                    state["waiting"] = False
                    state["action"] = "No action required; preparation ended"
                    state["reason"] = "Manual keyring unlock timed out"
                    write_json(self.directory / "preparation.json", state)
                    raise Pending(state["reason"])
                time.sleep(1)
        state["status"] = "READY"
        state["waiting"] = False
        state["action"] = "No action required; tests are starting"
        write_json(self.directory / "preparation.json", state)
        print("READY: Keyringの解除を確認しました。RAテストを開始します。", flush=True)

    def guest(self, argv, admin=False):
        if not self.claimed or self.vm != self.profile.get("vm_id"):
            raise Failure("Guest command requires the explicitly claimed dedicated E2E VM")
        if not argv or not all(isinstance(a, str) for a in argv):
            raise Failure("Guest argv must be nonempty strings")
        args = ["exec", self.vm, "--timeout", "120s"]
        if admin:
            args += ["--admin"]
        return self.cli(*args, "--", *argv, timeout=135)

    def observe(self, region=None):
        self.check_exclusive()
        # SDL canvases aren't in the accessibility tree; OCR is the explicit fallback.
        tree = self.rpc("vmpal_status", {"vm":self.vm})
        if self.profile.get("require_desktop_tools") and not tree.get("screenTree"):
            raise Pending("VMPal desktop tools disconnected; restore the observation channel before continuing")
        if region and tree.get("screenTree"):
            windows = json.loads(self.cli("ui", self.vm, "tree", "--json")).get("windows", [])
            if not any(w.get("app") == "xm8" and {"focused","fullscreen"}.issubset(w.get("flags", [])) for w in windows):
                raise Pending("XM8 left the calibrated fullscreen window; regional observation is unavailable")
        if tree.get("screenTree") and region is None:
            text = self.cli("ui", self.vm, "tree", "--json")
            if "XM8" in text and self.profile.get("tree_has_canvas_text"):
                return require_unprotected_screen(text)
        args = ["--region", ",".join(map(str, region))] if region else []
        value = json.loads(self.cli("ui", self.vm, "observe", *args, "--json"))
        if region:
            actual = [value.get("screen", {}).get("width"), value.get("screen", {}).get("height")]
            if actual != self.profile.get("screen_size"):
                raise Pending("Screen dimensions differ from the calibrated text region")
        if value.get("locked"):
            raise Pending("Guest screen is locked; user sign-in is required")
        if (region and self.profile.get("local_region_ocr")) or (region is None and self.profile.get("local_full_ocr")):
            # VMPal's region filter still uses full-frame OCR. A zoomed local
            # capture reads small SDL text over transparent game backgrounds.
            # Keep it temporary: only explicit expectations enter the report.
            if region:
                self.observe()
            else:
                require_unprotected_screen("\n".join(line["text"] for line in value.get("text", [])))
            with tempfile.TemporaryDirectory(prefix="xm8-e2e-ocr-") as folder:
                capture = Path(folder) / "region.png"
                crop = ["--region", ",".join(map(str, region))] if region else []
                self.cli("ui", self.vm, "screenshot", "-o", str(capture), "--png", *crop)
                words = read_ocr(self.directory, capture, self.profile.get("ocr_cpu_english", False))
            separator = " " if region else "\n"
            return require_unprotected_screen(separator.join(word["text"] for word in words))
        return require_unprotected_screen("\n".join(line["text"] for line in value.get("text", [])))

    def act(self, step):
        self.check_exclusive()
        if self.profile["os"] == "windows" and self.profile.get("windows_helper"):
            window = self.windows_helper("window")
            if window.get("focused") is not True:
                raise Pending("Windows XM8 must be focused before input")
        if step["action"] == "hold":
            self.observe()
            actions = []
            for key in step["keys"].split():
                actions += [{"action":"hold_key", "text":key, "duration":0.2}, {"action":"wait", "duration":0.2}]
            self.cli("ui", self.vm, "act", json.dumps({"actions":actions, "observe":"none"}),
                     "--mask", "XM8M local E2E", timeout=60)
        elif step["action"] == "key":
            # The emulated UI polls input; don't let a fast transport merge presses.
            for key in step["keys"].split():
                self.cli("ui", self.vm, "key", key, "--mask", "XM8M local E2E", "--observe", "none")
                time.sleep(0.18)
        elif step["action"] == "type":
            self.observe()
            self.cli("ui", self.vm, "type", step["text"], "--method", "keys", "--mask", "XM8M local E2E")
        else:
            if not step.get("text"):
                raise Pending("Pointer operation needs calibrated visible text")
            if self.profile.get("local_pointer_ocr"):
                self.observe()
                from .runner import Engine
                targets = []
                with tempfile.TemporaryDirectory(prefix="xm8-e2e-pointer-") as folder:
                    capture = Path(folder) / "screen.png"
                    for attempt in range(3):
                        self.screenshot(capture)
                        size = list(struct.unpack(">II", capture.read_bytes()[16:24]))
                        if size != self.profile.get("screen_size"):
                            raise Pending("Pointer screen dimensions differ from calibration")
                        words = read_ocr(self.directory, capture, self.profile.get("ocr_cpu_english", False))
                        compact = re.sub(r"\s+", "", step["text"].casefold())
                        targets = [w for w in words if Engine.matches(step, w["text"]) or
                                   compact in re.sub(r"\s+", "", w["text"].casefold())]
                        if targets:
                            break
                        time.sleep(0.3)
                    if len(targets) != 1 and not self.profile.get("private_evidence"):
                        shutil.copyfile(capture, self.directory / "pointer-failure.png")
                        write_json(self.directory / "pointer-failure-text.json", {"expected":step["text"], "words":words})
                if len(targets) != 1:
                    raise Failure(f"Pointer OCR target is missing or ambiguous ({len(targets)} matches)")
                target = targets[0]
                self.cli("ui", self.vm, "click", str(round(target["x"])), str(round(target["y"])),
                         "--mask", "XM8M local E2E", "--observe", "none")
                return
            self.cli("ui", self.vm, "click", "--text", step["text"], "--mask", "XM8M local E2E")

    def click_image(self, step):
        from .runner import digest
        self.check_exclusive()
        if self.profile.get("private_evidence") or self.profile["os"] != "windows":
            raise Pending("Image pointer needs OS and privacy calibration")
        if self.windows_helper("window").get("focused") is not True:
            raise Pending("Windows XM8 must be focused before image pointer input")
        region = step["region"]
        expected = self.profile["screen_size"]
        if len(region) != 4 or not (0 <= region[0] < region[2] <= expected[0] and 0 <= region[1] < region[3] <= expected[1]):
            raise Pending("Image pointer region is outside the calibrated screen")
        name = "pointer-" + uuid.uuid4().hex[:12] + ".png"
        capture = self.directory / name
        self.screenshot(capture)
        if list(struct.unpack(">II", capture.read_bytes()[16:24])) != expected:
            raise Pending("Image pointer screen dimensions differ from calibration")
        matches = []
        for reference in step["references"]:
            path = Path(reference)
            if not path.is_file():
                raise Pending("Calibrated pointer reference is unavailable")
            comparison = compare_images(self.directory, capture, path, region)
            if min(comparison["reference_contrast"], comparison["actual_contrast"]) < 32:
                raise Pending("Image pointer requires visible target detail")
            if comparison["changed_fraction"] <= float(step.get("max_changed_fraction", 0.001)):
                matches.append((path,comparison))
        if not matches:
            raise Failure("Image pointer target did not match a calibrated reference")
        reference, proof = min(matches, key=lambda item:item[1]["changed_fraction"])
        if self.windows_helper("window").get("focused") is not True:
            raise Pending("Windows XM8 lost focus before image pointer input")
        x, y = round((region[0]+region[2])/2), round((region[1]+region[3])/2)
        self.cli("ui",self.vm,"click",str(x),str(y),"--mask","XM8M local E2E","--observe","none")
        return {"action":"click_image","label":step["text"],"region":region,"coordinate":[x,y],
                "reference_sha256":digest(reference),"evidence":name,**proof}

    def observe_ra(self):
        from .runner import Engine
        self.ra_observation_phase = "configuration"
        if not self.claimed or self.vm != self.profile.get("vm_id"):
            raise Pending("RA observation requires the claimed dedicated VM")
        config = self.profile.get("ra_observation", {})
        if self.profile["os"] not in ("linux", "windows") or not all(config.get(k) for k in ("status_region", "game_text")):
            raise Pending("Configure calibrated RA aggregate observation for this guest")
        if self.profile["os"] == "linux" and not config.get("database"):
            raise Pending("Configure the Linux RA database")
        if self.profile["os"] == "windows" and config.get("aggregate_source") != "windows-default-library":
            raise Pending("Confirm the Windows default RA library observation source")
        # Full canvas text stays in memory: record static labels and counts only.
        self.ra_observation_phase = "screen"
        text = self.observe(config["authentication_region"]) if config.get("authentication_region") else self.observe()
        self.ra_observation_phase = "status_region"
        status = self.observe(config["status_region"])
        if config.get("game_region"):
            status += "\n" + self.observe(config["game_region"])
        self.ra_observation_phase = "aggregate"
        if self.profile["os"] == "windows":
            counts = self.windows_helper("observe-ra")
        else:
            if not getattr(self, "ra_observer_deployed", False):
                helper = Path(__file__).with_name("guests") / "observe_ra.py"
                self.cli("cp", str(helper), self.vm + ":/tmp/xm8-e2e-observe-ra.py")
                self.ra_observer_deployed = True
            counts = json.loads(self.guest(["/usr/bin/python3", "/tmp/xm8-e2e-observe-ra.py", config["database"]]))
        if set(counts) != {"pending", "held"} or any(type(v) is not int or v < 0 for v in counts.values()):
            raise Pending("Invalid RA aggregate observation")
        authenticated = Engine.matches({"text":"Logout", "exact_line":True}, text)
        login = Engine.matches({"text":"Login", "exact_line":True}, text)
        auth = "authenticated" if authenticated and not login else "not_authenticated" if login and not authenticated else "unobserved"
        labels = ((config["game_text"], "active"), ("RA: login failed", "authentication"),
                  ("RA: login pending", "authentication"), ("RA: disconnected", "connectivity"),
                  ("RA: pending", "identification"), ("RA: offline", "offline"))
        visible = next((stage for label, stage in labels if Engine.matches({"text":label}, status)), "unobserved")
        stage = "authentication" if auth == "not_authenticated" else visible
        # Aggregate records may belong to another account or request. Keep
        # counts separate: their presence does not prove a launch barrier.
        return {"authentication":auth, "visible_state":visible, "stage":stage,
                "outbox":counts, "guest_cut_active":self.network_dirty}

    def focus_cycle(self):
        self.check_exclusive()
        if self.profile["os"] == "windows":
            proof = self.windows_helper("focus-cycle")
            if proof.get("left") is not True or proof.get("returned") is not True:
                raise Failure("Did not observe focus leaving and returning to XM8")
            return proof
        if self.profile["os"] != "linux":
            raise Pending("Focus-loss observation has not been calibrated on this OS")
        before = json.loads(self.cli("ui", self.vm, "tree", "--json"))
        if not any(w.get("app") == "xm8" and "focused" in w.get("flags", []) for w in before.get("windows", [])):
            raise Pending("XM8 must be focused with a prepared alternate window")
        first = {"actions":[{"action":"key_down","text":"a"}, {"action":"wait","duration":0.2},
                 {"action":"key","text":"alt+Tab"}, {"action":"wait","duration":0.5}], "observe":"tree"}
        second = {"actions":[{"action":"key_up","text":"a"}, {"action":"key","text":"alt+Tab"},
                  {"action":"wait","duration":0.5}], "observe":"tree"}
        try:
            # One stream keeps the held key alive across focus changes. EOF releases it.
            output = execute([self.tool,"--cli","ui",self.vm,"stream","--mask","XM8M local E2E"],
                             timeout=45, input_text=json.dumps(first)+"\n"+json.dumps(second)+"\n")
        finally:
            self.cli("ui", self.vm, "release")
        answers = [json.loads(line) for line in output.splitlines() if line.strip()]
        if len(answers) != 2 or not all(answer.get("ok") for answer in answers):
            raise Failure("Focus-change stream failed")
        focused = [[{k:w.get(k) for k in ("app","title","flags")} for w in
                    answer.get("result", {}).get("tree", {}).get("windows", []) if "focused" in w.get("flags", [])]
                   for answer in answers]
        if not focused[0] or any(w["app"] == "xm8" for w in focused[0]) or not any(w["app"] == "xm8" for w in focused[1]):
            raise Failure("Did not observe focus leaving and returning to XM8")
        return focused

    def fullscreen(self):
        if self.profile["os"] == "windows":
            # Status may omit dimensions before the first captured frame of a
            # new boot. Observe first, also checking for protected/locked UI.
            self.observe()
            current = self.windows_helper("activate")
            screen = self.rpc("vmpal_status", {"vm":self.vm}).get("screen", {})
            expected = [screen.get("width"), screen.get("height")]
            if expected != self.profile.get("guest_screen_size", self.profile.get("screen_size")):
                raise Pending(f"Windows screen dimensions {expected} differ from calibration")
            if current.get("focused") is not True:
                self.observe()
                rect = current.get("rect", {})
                if not all(k in rect for k in ("Left", "Top", "Right", "Bottom")):
                    raise Pending("Windows XM8 window geometry is unavailable")
                # The observed Win32 title bar (or fullscreen top margin),
                # converted to VMPal's screenshot coordinate space.
                width, height = self.profile["screen_size"]
                x = round((rect["Left"] + rect["Right"]) / 2 * width / expected[0])
                y = round((rect["Top"] + 16) * height / expected[1])
                self.cli("ui", self.vm, "click", str(x), str(y), "--mask", "XM8M local E2E", "--observe", "none")
                current = self.windows_helper("window")
                if current.get("focused") is not True:
                    raise Pending("Windows XM8 focus could not be acquired")
            def is_full(proof):
                return proof.get("focused") is True and proof.get("rect") == dict(Left=0, Top=0, Right=expected[0], Bottom=expected[1])
            if not is_full(current):
                if "XM8 Ver" not in self.observe():
                    raise Pending("Open the XM8 main menu before fullscreen preparation")
                self.act({"action":"key", "keys":"End Up Up Up Return"})
                time.sleep(1)
                current = self.windows_helper("window")
            if not is_full(current):
                raise Failure("Windows XM8 fullscreen/focus was not observed")
            return current
        if self.profile["os"] != "linux":
            raise Pending("Fullscreen window observation needs OS-specific calibration")
        candidates = []
        def window():
            tree = json.loads(self.cli("ui", self.vm, "tree", "--json"))
            candidates[:] = [w for w in tree.get("windows", []) if w.get("app") == "xm8"]
            proof = [{k:w.get(k) for k in ("app", "title", "flags", "box")} for w in tree.get("windows", [])]
            (self.directory / "fullscreen-window.json").write_text(json.dumps(proof))
            return next((w for w in tree.get("windows", []) if w.get("app") == "xm8" and "focused" in w.get("flags", [])), None)
        current = window()
        if not current and len(candidates) == 1 and candidates[0].get("title"):
            # GNOME may map a background-launched window without granting focus.
            # Use the observed title, then verify focus before sending menu keys.
            self.observe()
            self.act({"action":"click", "text":candidates[0]["title"]})
        deadline = time.monotonic() + 5
        while not current and time.monotonic() < deadline:
            time.sleep(0.2)
            current = window()
        if not current:
            raise Pending("XM8 window must be focused before fullscreen preparation")
        if "fullscreen" not in current.get("flags", []):
            if "XM8 Ver" not in self.observe():
                raise Pending("Open the XM8 main menu before fullscreen preparation")
            # Calibrated Linux menu order: Full Screen, Full Speed, Reset, Quit.
            # At the initial small window size OCR cannot reliably read each item.
            self.act({"action":"key", "keys":"End Up Up Up Return"})
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                current = window()
                if current and "fullscreen" in current.get("flags", []):
                    break
                time.sleep(0.2)
            else:
                raise Failure("XM8 window did not enter fullscreen")
        return {k:current.get(k) for k in ("app", "title", "flags", "box")}

    def windows_helper(self, action):
        if not self.profile.get("windows_helper") or not self.profile.get("windows_executable"):
            raise Pending("Configure the pinned Windows test helper and executable")
        value = self.guest(["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                            self.profile["windows_helper"], "-Action", action,
                            "-Executable", self.profile["windows_executable"]])
        return json.loads(value)

    def screenshot(self, path, region=None):
        if self.profile.get("private_evidence") and not region:
            raise Pending("RA screenshots require an explicitly calibrated capture_region that excludes account information")
        self.observe()
        if region:
            self.observe(region)
        args = ["--region", ",".join(map(str, region))] if region else []
        self.cli("ui", self.vm, "screenshot", "-o", str(path), "--png", *args)

    def probe_control(self):
        status = self.rpc("vmpal_status", {"vm":self.vm})
        if not status.get("executionAvailable"):
            raise Failure("Guest control channel unavailable during disconnect")
        self.guest(["cmd.exe", "/c", "exit", "0"] if self.profile["os"] == "windows" else ["/usr/bin/true"])

    def process_identity(self):
        if self.profile["os"] == "windows":
            proof = self.windows_helper("window")
            if any(type(proof.get(k)) is not int or proof[k] <= 0 for k in ("pid", "start_ticks")):
                raise Pending("Windows process identity was not observed")
            return {k:proof[k] for k in ("pid", "start_ticks")}
        if self.profile["os"] != "linux":
            raise Pending("Warm credential sessions need OS-specific process observation")
        pids = self.guest(["/usr/bin/pgrep", "-x", "xm8"]).split()
        if len(pids) != 1 or not pids[0].isdigit():
            raise Pending("Expected exactly one XM8 process in the isolated guest")
        fields = self.guest(["/bin/cat", "/proc/" + pids[0] + "/stat"]).split(") ", 1)
        if len(fields) != 2 or len(fields[1].split()) < 20:
            raise Failure("Cannot observe XM8 process start time")
        return {"pid":int(pids[0]), "start_ticks":int(fields[1].split()[19])}

    def network_down(self):
        if self.network_dirty:
            raise Failure("Guest network is already disconnected by this backend")
        self.guard = "xm8-e2e-net-" + uuid.uuid4().hex
        network = self.profile.get("network", {})
        if not network.get("calibrated"):
            raise Pending("Guest network guard/control-channel preparation is incomplete")
        helper = Path(__file__).with_name("guests") / ("network.ps1" if self.profile["os"] == "windows" else "network.sh")
        destination = r"C:\Windows\Temp\xm8-e2e-network.ps1" if self.profile["os"] == "windows" else "/tmp/xm8-e2e-network.sh"
        self.cli("cp", str(helper), self.vm + ":" + destination)
        self.network_dirty = True
        if self.profile["os"] == "windows":
            argv = ["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", destination, "arm", self.guard]
        else:
            argv = ["/bin/sh", destination, "arm", self.guard, self.profile["os"]]
        output = self.guest(argv, admin=True)
        if self.profile["os"] == "windows":
            self.record_network_proof(output, "down")

    def network_up(self):
        if not self.network_dirty:
            return
        if self.profile["os"] == "windows":
            argv = ["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", r"C:\Windows\Temp\xm8-e2e-network.ps1", "restore", self.guard]
        else:
            argv = ["/bin/sh", "/tmp/xm8-e2e-network.sh", "restore", self.guard, self.profile["os"]]
        output = self.guest(argv, admin=True)
        if self.profile["os"] == "windows":
            self.record_network_proof(output, "up")
        self.probe_control()
        self.network_dirty = False

    def record_network_proof(self, output, state):
        proof = json.loads(output)
        if proof.get("guard") != self.guard or proof.get("state") != state or proof.get("verified") is not True or type(proof.get("adapter_count")) is not int or proof["adapter_count"] != 1:
            raise Failure("Windows guest adapter transition was not verified")
        if state == "down" and proof.get("watchdog_seconds") != 120:
            raise Failure("Windows guest watchdog was not verified")
        write_json(self.directory / (self.guard + "-" + state + ".json"), proof)

    def close(self):
        if not self.claimed or not self.vm:
            return
        if self.vm != self.profile.get("vm_id"):
            raise Failure("Refusing to stop an unrelated VM")
        self.target(self.inventory())
        # On failed network restoration retain the running VM and its watchdog.
        self.network_up()
        try:
            self.cli("ui", self.vm, "release")
        except Failure:
            pass
        safe, reason = True, ""
        state = self.target(self.inventory())["state"]
        if state != "stopped":
            try:
                if self.profile.get("stop_app"):
                    self.guest(self.profile["stop_app"])
                self.guest(self.profile["snapshot_ready"])
            except (Failure, KeyError):
                safe, reason = False, "Pending data or incomplete shutdown needs inspection; baseline restoration blocked"
            self.cli("stop", self.vm, "--timeout", "110s", timeout=120)
        else:
            # A crash or manual shutdown prevented the pending-data check.
            safe, reason = False, "Guest stopped before the pending-data check; baseline restoration blocked"
        if self.target(self.inventory())["state"] != "stopped":
            raise Failure("Dedicated VM did not shut down normally")
        write_json(self.state_file(), {"vm":self.vm, "safe_to_restore":safe,
                                      "run":str(self.directory), "reason":reason})
        preparation = self.directory / "preparation.json"
        if preparation.is_file():
            record = json.loads(preparation.read_text())
            record.update(waiting=False, vm_stopped=True,
                          action="No action required; dedicated VM is stopped")
            write_json(preparation, record)
        self.vm, self.claimed = None, False
        if not safe:
            raise Pending(reason)



class Android:
    def __init__(self, profile, directory):
        self.profile, self.directory = profile, directory
        self.adb_binary = adb_tool(profile)
        self.serial = profile.get("serial", "emulator-5580")
        if not re.fullmatch(r"emulator-\d+", self.serial):
            raise ValueError("Android E2E requires an explicit emulator serial")
        self.process = None
        self.network_dirty = False
        self.old_wifi = self.old_data = None

    def adb(self, *args):
        return execute([self.adb_binary, "-s", self.serial, *args])

    def check_exclusive(self):
        inventory = Vmpal(self.profile, self.directory).inventory()
        if any(v["state"] not in ("stopped", "suspended") for v in inventory):
            raise Pending("A VMPal VM became active; it was not stopped")
        devices = execute([self.adb_binary, "devices"]).splitlines()
        if any(line.startswith("emulator-") and line.split()[0] != self.serial for line in devices):
            raise Pending("Another Android emulator became active")

    def prepare(self, artifact):
        # Same global lock as VMPal. Check other apps' VMs and all active AVDs.
        inventory = Vmpal(self.profile, self.directory).inventory()
        if any(v["state"] not in ("stopped", "suspended") for v in inventory):
            raise Pending("A VMPal VM is already running")
        if any(line.startswith("emulator-") for line in execute([self.adb_binary, "devices"]).splitlines()):
            raise Pending("An Android emulator is already running")
        if not self.profile.get("snapshot"):
            raise Pending("Dedicated AVD baseline snapshot is required")
        port = int(self.serial.split("-")[1])
        if port % 2:
            raise ValueError("Emulator console port must be even")
        log = (self.directory / "emulator.log").open("w")
        try:
            self.process = subprocess.Popen([self.profile["emulator"], "-avd", self.profile["avd"],
                "-port", str(port), "-snapshot", self.profile["snapshot"], "-read-only", "-no-snapshot-save"],
                stdout=log, stderr=subprocess.STDOUT)
        finally:
            log.close()
        deadline = time.monotonic() + 180
        while time.monotonic() < deadline:
            if self.process.poll() is not None:
                raise Failure("Dedicated Android emulator exited before boot")
            try:
                if self.adb("shell", "getprop", "sys.boot_completed").strip() == "1":
                    break
            except Failure:
                pass
            time.sleep(1)
        else:
            raise Failure("Android boot timeout")
        self.check_exclusive()
        api = self.adb("shell", "getprop", "ro.build.version.sdk").strip()
        expected = str(self.profile["api"]).split(".")[0]
        if api != expected:
            raise Failure("Actual Android API does not match the configured target")
        minor = self.adb("shell", "getprop", "ro.build.version.sdk_minor").strip()
        if self.profile["api"] == "37.2" and minor != "2":
            raise Failure("Actual Android minor API is not 37.2")
        abi = self.adb("shell", "getprop", "ro.product.cpu.abi").strip()
        if abi != "arm64-v8a":
            raise Failure("Dedicated Android E2E requires an ARM64 system image")
        (self.directory / "environment.json").write_text(json.dumps({"backend":"android", "api":api, "minor":minor, "abi":abi,
            "avd":self.profile["avd"], "snapshot":self.profile["snapshot"]}))
        self.adb("install", "-r", str(artifact.resolve()))
        self.adb("shell", "am", "start", "-W", "-n", self.profile["activity"])

    def observe(self, region=None):
        self.check_exclusive()
        # XML dump works on old AVDs too and uses no host-side OCR guesses.
        self.adb("shell", "uiautomator", "dump", "/data/local/tmp/xm8-e2e-ui.xml")
        value = self.adb("shell", "cat", "/data/local/tmp/xm8-e2e-ui.xml")
        ocr = native_tool(self.directory, "ocr")
        # OCR may encounter a login screen. Keep it transient, outside run evidence.
        with tempfile.TemporaryDirectory(prefix="xm8-e2e-ocr-") as temporary:
            image = Path(temporary) / "screen.png"
            self.capture(image)
            self.labels = json.loads(execute([str(ocr), str(image)]))
        text = require_unprotected_screen(value + "\n" + "\n".join(item["text"] for item in self.labels if item["confidence"] >= 0.8))
        if region:
            return "\n".join(item["text"] for item in self.labels if item["confidence"] >= 0.8 and
                             region[0] <= item["x"] <= region[2] and region[1] <= item["y"] <= region[3])
        return text

    def guest(self, argv):
        return self.adb("shell", shlex.join(argv))

    def act(self, step):
        self.check_exclusive()
        if step["action"] == "hold":
            raise Pending("Physical key hold needs Android-specific calibration")
        elif step["action"] == "key":
            self.guest(["input", "keyevent", step["keys"]])
        elif step["action"] == "type":
            self.observe()
            self.guest(["input", "text", step["text"].replace(" ", "%s")])
        else:
            self.observe()
            choices = [item for item in self.labels if item["confidence"] >= 0.8 and
                       step["text"].casefold() in item["text"].casefold()]
            if len(choices) != 1:
                raise Failure("Android visible tap target is missing or ambiguous")
            self.adb("shell", "input", "tap", str(round(choices[0]["x"])), str(round(choices[0]["y"])))

    def focus_cycle(self):
        raise Pending("Focus-loss observation needs Android-specific calibration")

    def fullscreen(self):
        raise Pending("Android screen calibration is separate from desktop fullscreen")

    def screenshot(self, path, region=None):
        if self.profile.get("private_evidence") or region:
            raise Pending("Private Android screen capture requires OS-specific calibration")
        self.observe()
        self.capture(path)

    def capture(self, path):
        self.adb("shell", "screencap", "-p", "/data/local/tmp/xm8-e2e-screen.png")
        self.adb("pull", "/data/local/tmp/xm8-e2e-screen.png", str(path))

    def probe_control(self):
        if self.adb("get-state").strip() != "device":
            raise Failure("ADB control lost during guest disconnect")

    def network_down(self):
        if not self.profile.get("network", {}).get("calibrated"):
            raise Pending("Android network watchdog has not been calibrated")
        self.old_wifi = self.adb("shell", "settings", "get", "global", "wifi_on").strip()
        self.old_data = self.adb("shell", "settings", "get", "global", "mobile_data").strip()
        if self.old_wifi not in ("0", "1") or self.old_data not in ("0", "1"):
            raise Pending("Cannot determine original guest radio states")
        self.network_dirty = True
        restore = f"svc wifi {'enable' if self.old_wifi == '1' else 'disable'}; svc data {'enable' if self.old_data == '1' else 'disable'}"
        self.guest(["sh", "-c", "nohup sh -c " + shlex.quote("sleep 120; " + restore) + " >/dev/null 2>&1 </dev/null &"])
        self.adb("shell", "svc", "wifi", "disable")
        self.adb("shell", "svc", "data", "disable")

    def network_up(self):
        if self.network_dirty:
            self.adb("shell", "svc", "wifi", "enable" if self.old_wifi == "1" else "disable")
            self.adb("shell", "svc", "data", "enable" if self.old_data == "1" else "disable")
            self.probe_control()
            self.network_dirty = False

    def close(self):
        self.network_up()
        if self.process:
            if self.process.poll() is None:
                try:
                    self.adb("emu", "kill")
                    self.process.wait(timeout=20)
                except (Failure, subprocess.TimeoutExpired):
                    # Only the Popen child we created; never another user's emulator.
                    self.process.terminate()
                    try:
                        self.process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        self.process.kill()
                        self.process.wait(timeout=10)
            self.process = None


def make_backend(profile, directory):
    return Android(profile, directory) if profile["backend"] == "android" else Vmpal(profile, directory)
