"""Fail-closed local runner. Profiles contain calibrated, reviewable GUI steps.

No missing prerequisite, empty recipe, or successful process is a GUI PASS.
Profiles/artifacts/credentials are local and excluded from Git.
"""
import argparse
import contextlib
import datetime as dt
import fcntl
import hashlib
import json
import re
from pathlib import Path
import signal
import subprocess
import time
import uuid

ROOT = Path(__file__).resolve().parents[2]
CATALOG = json.loads(Path(__file__).with_name("catalog.json").read_text())
DEFAULT_CONFIG = ROOT / "artifacts/local-e2e/config.json"
SUITES = ("basic", "ra", "ra-network")


class Pending(Exception):
    """An unmet prerequisite or unobservable expectation, not a PASS."""


class Failure(Exception):
    """An observed mismatch, failed action, or unsafe cleanup."""


def digest(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def write_json(path, value):
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n")
    temporary.replace(path)


def validate_profile(profile):
    if profile.get("backend") not in ("vmpal", "android"):
        raise ValueError("backend must be vmpal or android")
    if profile.get("os") not in ("linux", "macos", "windows", "android"):
        raise ValueError("unsupported guest OS")
    if profile["backend"] == "vmpal" and not all(profile.get(k) for k in
            ("vm_id", "vm_name", "baseline_snapshot", "dedicated_vm")):
        raise ValueError("vm_id, vm_name, baseline_snapshot and dedicated_vm are required")
    if profile["backend"] == "android":
        if profile["os"] != "android" or not profile.get("avd", "").startswith("XM8M-E2E-"):
            raise ValueError("Android requires a dedicated XM8M-E2E-* AVD")
        if profile.get("api") not in (21, 36, "37.2"):
            raise ValueError("Android API must be 21, 36 or '37.2'")
    recipes = list(profile.get("cases", {}).values())
    if "startup" in profile:
        recipes.append(profile["startup"])
    if "session_restart" in profile:
        recipes.append(profile["session_restart"])
    recipes += [r["online_setup"] for r in recipes if isinstance(r, dict) and "online_setup" in r]
    for recipe in recipes:
        if not isinstance(recipe, dict):
            raise ValueError("case recipes must be objects")
        for step in recipe.get("steps", []):
            if step.get("action") not in ("key", "hold", "prepare_focus", "focus_cycle", "fullscreen", "click", "click_image", "type", "expect", "expect_image", "screenshot", "guest", "network", "wait", "ensure_menu", "launch", "observe_ra"):
                raise ValueError("unsupported step action")
            if step["action"] == "guest" and not isinstance(step.get("argv"), list):
                raise ValueError("guest argv must be an argument list")
            if step["action"] == "expect" and not step.get("text"):
                raise ValueError("expect requires a nonempty literal text")
            if step.get("when_exact_line") and not step.get("text"):
                raise ValueError("when_exact_line requires a nonempty literal text")
            if step["action"] in ("key", "hold") and (not isinstance(step.get("keys"), str) or not 0 < len(step["keys"].split()) <= 32):
                raise ValueError("key/hold needs between 1 and 32 key names")
            for region_key in ("region", "capture_region"):
                if not step.get(region_key):
                    continue
                region = step[region_key]
                if len(region) != 4 or not all(isinstance(n, int) for n in region) or min(region[:2]) < 0 or region[2] <= region[0] or region[3] <= region[1]:
                    raise ValueError("region must be a nonempty rectangle in screen pixels")
            if step["action"] == "network" and step.get("state") not in ("up", "down"):
                raise ValueError("network state must be up or down")
            if step["action"] == "wait" and not 0 < float(step.get("seconds", 0)) <= 5:
                raise ValueError("animation waits must be between 0 and 5 seconds")
            if step["action"] == "ensure_menu" and not step.get("text"):
                raise ValueError("ensure_menu needs visible menu text")
            if step["action"] == "expect_image":
                region = step.get("region", [])
                if len(region) != 4 or not all(isinstance(n, int) for n in region) or not step.get("reference"):
                    raise ValueError("expect_image requires a reference and four integer pixel coordinates")
                if not 0 <= float(step.get("max_changed_fraction", 0.001)) <= 0.05:
                    raise ValueError("image tolerance must be between zero and 0.05")
            if step["action"] == "click_image":
                refs = step.get("references", [])
                if not step.get("text") or not step.get("region") or not isinstance(refs,list) or not 1 <= len(refs) <= 3 or not all(isinstance(v,str) and v for v in refs):
                    raise ValueError("click_image requires a label, region and 1-3 calibrated references")
                if not 0 <= float(step.get("max_changed_fraction", 0.001)) <= 0.05:
                    raise ValueError("image pointer tolerance must be between zero and 0.05")


@contextlib.contextmanager
def single_vm_lock(path):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a+") as stream:
        try:
            fcntl.flock(stream, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise Pending("Another local E2E is running; no VM was started")
        try:
            yield
        finally:
            fcntl.flock(stream, fcntl.LOCK_UN)


class Engine:
    def __init__(self, backend, profile, directory):
        self.backend, self.profile, self.directory = backend, profile, directory
        self.network_armed = False
        self.disconnected_at = None
        self.sequence = 0

    def evidence(self, kind, content):
        self.sequence += 1
        name = f"{self.sequence:04d}-{kind}.txt"
        (self.directory / name).write_text(content)
        return name

    def perform(self, recipe, result):
        if not recipe.get("calibrated"):
            raise Pending("GUI recipe has not been calibrated on this OS/build")
        steps = recipe.get("steps", [])
        if not steps or not any(s.get("action") in ("expect", "expect_image") for s in steps):
            raise Pending("Case requires actions and explicit GUI expectations")
        for step in steps:
            action = step["action"]
            if self.network_armed and time.monotonic() - self.disconnected_at >= 90:
                raise Failure("Disconnect window exceeded; watchdog may restore the guest at 120 seconds")
            recorded = dict(step)
            if action == "guest":
                recorded = {"action": action, "executable": step["argv"][0]}
            result.setdefault("actions", []).append(recorded)
            if action == "expect":
                deadline = time.monotonic() + min(float(step.get("timeout", 15)), 45)
                while True:
                    observed = self.backend.observe(step["region"]) if step.get("region") else self.backend.observe()
                    if any(label.casefold() in observed.casefold() for label in step.get("pending_if", [])):
                        raise Pending("A configured preparation/authentication prerequisite is missing")
                    present = self.matches(step, observed)
                    if present != bool(step.get("absent", False)) or time.monotonic() >= deadline:
                        break
                    time.sleep(0.5)
                content = json.dumps({"expected":step["text"], "present":present}, ensure_ascii=False) if self.profile.get("private_evidence") else observed
                name = self.evidence("screen-text", content)
                result["evidence"].append(name)
                present = self.matches(step, observed)
                if present == bool(step.get("absent", False)):
                    raise Failure("Visible expectation did not match: " + step["text"])
                if self.network_armed and time.monotonic() - self.disconnected_at >= 90:
                    raise Failure("Observation exceeded the safe disconnect window")
                result["observations"].append({"text": step["text"], "absent": bool(step.get("absent", False)), "evidence": name})
            elif action in ("screenshot", "expect_image"):
                if self.profile.get("private_evidence") and not step.get("capture_region"):
                    raise Pending("RA screen evidence requires a calibrated capture_region that excludes account information")
                self.sequence += 1
                name = f"{self.sequence:04d}-screen.png"
                capture_region = step.get("capture_region")
                def capture():
                    if capture_region:
                        self.backend.screenshot(self.directory / name, capture_region)
                    else:
                        self.backend.screenshot(self.directory / name)
                capture()
                result["evidence"].append(name)
                if action == "expect_image":
                    from .backends import compare_images
                    reference = Path(step["reference"])
                    if not reference.is_file():
                        raise Pending("Calibrated reference image is unavailable")
                    deadline = time.monotonic() + min(float(step.get("timeout", 15)), 45)
                    attempts = 0
                    while True:
                        attempts += 1
                        comparison = compare_images(self.directory, self.directory / name, reference, step["region"])
                        differs = comparison["changed_fraction"] > float(step.get("max_changed_fraction", 0.001))
                        if differs == bool(step.get("different", False)) or time.monotonic() >= deadline:
                            break
                        time.sleep(0.5)
                        capture()
                    comparison["attempts"] = attempts
                    comparison.update(reference_sha256=digest(reference), region=step["region"], evidence=name)
                    result["observations"].append(comparison)
                    if step.get("allow_blank") and not any(s.get("action") == "expect" and not s.get("absent") for s in steps):
                        raise Pending("Blank region requires a positive live-screen expectation in the same recipe")
                    if not step.get("allow_blank") and min(comparison["reference_contrast"], comparison["actual_contrast"]) < 32:
                        raise Pending("Image region has no visible detail; recalibrate the image region")
                    if differs != bool(step.get("different", False)):
                        raise Failure("Fixed screen comparison did not match the expected equality/difference")
            elif action == "network":
                if step["state"] == "down":
                    # Mark before the command: partial disconnect must be restored too.
                    self.network_armed = True
                    self.disconnected_at = time.monotonic()
                    self.backend.network_down()
                    self.backend.probe_control()
                else:
                    self.backend.network_up()
                    self.network_armed = False
            elif action == "observe_ra":
                self.observe_ra(result, step.get("phase", "checkpoint"))
            elif action == "guest":
                # Never copy guest stdout (which might contain tokens) into evidence.
                self.backend.guest(step["argv"])
            elif action == "launch":
                if not self.profile.get("launch"):
                    raise Pending("Guest launch command is not configured")
                self.backend.guest(self.profile["launch"])
            elif action == "focus_cycle":
                proof = self.backend.focus_cycle()
                result["evidence"].append(self.evidence("focus", json.dumps(proof, ensure_ascii=False)))
            elif action == "click_image":
                pointer = getattr(self.backend, "click_image", None)
                if pointer is None:
                    raise Pending("Image pointer has not been calibrated on this backend")
                proof = pointer(step)
                result["observations"].append(proof)
                result["evidence"].append(proof["evidence"])
                result["evidence"].append(self.evidence("image-pointer",json.dumps(proof)))
            elif action == "fullscreen":
                proof = self.backend.fullscreen()
                result["evidence"].append(self.evidence("window", json.dumps(proof, ensure_ascii=False)))
            elif action == "prepare_focus":
                if not self.profile.get("focus_helper"):
                    raise Pending("Alternate test window is not configured")
                self.backend.guest(self.profile["focus_helper"])
            elif action == "wait":
                time.sleep(float(step["seconds"]))
            elif action == "ensure_menu":
                # F11 is a toggle. Observe before each attempt, never close a visible menu.
                for attempt in range(4):
                    observed = self.backend.observe(step["region"]) if step.get("region") else self.backend.observe()
                    if self.matches(step, observed):
                        break
                    if attempt == 3:
                        raise Failure("Main menu did not become visible")
                    self.backend.act({"action":"key", "keys":"F11"})
                    time.sleep(2)
            else:
                if step.get("when_exact_line") and not self.matches({**step,"exact_line":True}, self.backend.observe()):
                    recorded["skipped"] = True
                    continue
                self.backend.act(step)

    @staticmethod
    def matches(step, observed):
        if step.get("exact_line"):
            return any(line.strip().casefold() == step["text"].casefold() for line in observed.splitlines())
        # OCR may split a single visual phrase into several boxes/lines.
        def normalize(value):
            return re.sub(r"\s*:\s*", ":", " ".join(value.casefold().split()))
        return normalize(step["text"]) in normalize(observed)

    def observe_ra(self, result, phase):
        observer = getattr(self.backend, "observe_ra", None)
        if observer is None:
            raise Pending("RA stage observation is not supported by this backend")
        try:
            raw = observer()
            observation = {k:raw[k] for k in ("authentication", "visible_state", "stage", "outbox", "guest_cut_active")}
            stages = {"authentication", "active", "connectivity", "identification", "pending_unlocks", "offline", "unobserved"}
            if observation["authentication"] not in {"authenticated", "not_authenticated", "unobserved"} or observation["stage"] not in stages or observation["visible_state"] not in stages:
                raise ValueError("Invalid stage")
            if set(observation["outbox"]) != {"pending", "held"} or any(type(v) is not int or v < 0 for v in observation["outbox"].values()):
                raise ValueError("Invalid counts")
            if type(observation["guest_cut_active"]) is not bool:
                raise ValueError("Invalid network observation")
        except (Pending, Failure, OSError, ValueError, KeyError, TypeError) as error:
            phase = getattr(self.backend, "ra_observation_phase", "unobserved")
            if phase not in {"configuration", "screen", "status_region", "aggregate"}:
                phase = "unobserved"
            result["ra_observation_failure"] = {"phase":phase, "kind":type(error).__name__}
            raise Pending("RA stage observation unavailable") from error
        result.setdefault("ra_observations", []).append({"phase":phase, **observation})
        result["evidence"].append(self.evidence("ra-stage", json.dumps({"phase":phase, **observation})))

    def run_case(self, case):
        result = {**case, "status": "PENDING", "reason": "", "evidence": [], "observations": []}
        try:
            recipe = self.profile.get("cases", {}).get(case["id"], {})
            if case["id"].startswith("network."):
                setup = recipe.get("online_setup", {})
                steps = recipe.get("steps", [])
                transitions = [s for s in steps if s.get("action") == "network"]
                if [s.get("state") for s in transitions] != ["down", "up"]:
                    raise Pending("Network case requires an explicit disconnect/restore pair")
                # Each calibrated recipe must independently create and observe a fresh session.
                self.perform(setup, result)
                if self.profile.get("ra_observation"):
                    self.observe_ra(result, "before_disconnect")
                down = next(i for i,s in enumerate(steps) if s.get("action") == "network")
                up = next(i for i,s in enumerate(steps) if s.get("action") == "network" and s.get("state") == "up")
                if not any(s.get("action") == "expect" for s in steps[down+1:up]) or not any(s.get("action") == "expect" for s in steps[up+1:]):
                    raise Pending("Network case needs visible disconnected and recovered expectations")
            self.perform(recipe, result)
            if self.network_armed:
                raise Failure("Recipe ended without a verified network restoration")
            result["status"] = "PASS"
        except Pending as error:
            result["reason"] = str(error)
        except (Failure, subprocess.SubprocessError, OSError) as error:
            result.update(status="FAIL", reason=str(error))
            if not self.profile.get("private_evidence"):
                try:
                    self.sequence += 1
                    name = f"{self.sequence:04d}-failure.png"
                    self.backend.screenshot(self.directory / name)
                    result["evidence"].append(name)
                except Exception:
                    # Never replace the actual mismatch with a missing diagnostic image.
                    pass
        finally:
            if self.network_armed:
                try:
                    self.backend.network_up()
                    self.network_armed = False
                except Exception:
                    result.update(status="FAIL", reason="Network restoration failed; guest watchdog remains armed")
        if self.profile.get("ra_observation") and not self.network_armed:
            try:
                self.observe_ra(result, "after_cleanup")
                if result["status"] != "PASS":
                    result["failure_stage"] = result["ra_observations"][-1]["stage"]
            except (Pending, Failure, OSError, ValueError):
                result["ra_observation_unavailable"] = True
                if result["status"] == "PASS":
                    result.update(status="PENDING", reason="RA stage observation unavailable after cleanup")
        observations = result.get("ra_observations", [])
        if observations:
            observed = any(sum(o["outbox"].values()) for o in observations)
            result["outbox_observation"] = {"pending_records_observed":observed,
                "records_cleared":observed and not sum(observations[-1]["outbox"].values()),
                "server_acceptance":"not_independently_checked"}
        return result


def report(directory, value):
    write_json(directory / "result.json", value)
    lines = ["# XM8M ローカル E2E", "", f"- 対象: {value['target']}",
             f"- 分類: {value['suite']}", f"- 配布物SHA-256: `{value.get('artifact_sha256', '未指定')}`",
             f"- 結果: **{value['status']}**", "", "| ケース | 結果 | 理由 |", "|---|---|---|"]
    if value.get("calibration"):
        lines.insert(6, "- 検証方式: 校正実行（各ケースを個別再実行。分類一括の再実行とは区別）")
    if value.get("reason"):
        lines.insert(6, "- 理由: " + value["reason"])
    if value.get("cleanup"):
        cleanup = value["cleanup"]
        lines.insert(6, f"- 後始末: **{cleanup['status']}** — {cleanup['reason']}")
    lines.insert(6, f"- 実行範囲: {len(value['cases'])} / {len(CATALOG[value['suite']])} ケース（指定範囲だけの判定）")
    for case in value["cases"]:
        reason = case.get("reason", "").replace("|", "/").replace("\n", " ")
        lines.append(f"| {case['id']} | {case['status']} | {reason} |")
    if any(case.get("ra_observations") for case in value["cases"]):
        lines += ["", "RAの段階別観測（表示ラベルと全アカウント合計件数のみ）:", "",
                  "| ケース | 時点 | 認証表示 | 段階 | 未送信 | 保留 |", "|---|---|---|---|---|---|"]
        for case in value["cases"]:
            for observation in case.get("ra_observations", []):
                counts = observation["outbox"]
                lines.append(f"| {case['id']} | {observation['phase']} | {observation['authentication']} | {observation['stage']} | {counts['pending']} | {counts['held']} |")
        lines += ["", "未送信件数が0であることだけで同期成功やサーバー受入を推測しません。"]
    lines += ["", "実績解除・outbox再送の実サーバー受入はこの実行の合格対象ではありません。",
              "CI・実機の結果とは分離しています。対象外・未観測項目をPASSへ補完しません。", ""]
    (directory / "report.md").write_text("\n".join(lines))


def run(profile, target, suite, artifact, output, backend_factory=None, selected=None):
    from .backends import make_backend
    profile = dict(profile)
    if suite != "basic":
        profile["private_evidence"] = True
    validate_profile(profile)
    if selected and set(selected) - {case["id"] for case in CATALOG[suite]}:
        raise ValueError("Selected case does not belong to this suite")
    output.mkdir(parents=True, exist_ok=True)
    directory = output / (dt.datetime.now().strftime("%Y%m%d-%H%M%S-") + uuid.uuid4().hex[:8])
    directory.mkdir(mode=0o700)
    cases = [dict(c) for c in CATALOG[suite] if not selected or c["id"] in selected]
    if not cases:
        raise ValueError("No matching cases")
    value = {"schema": 1, "run_id": directory.name, "target": target, "suite": suite,
             "platform": profile["os"], "api": profile.get("api"), "status": "PENDING",
             "profile_sha256": hashlib.sha256(json.dumps(profile, sort_keys=True).encode()).hexdigest(),
             "cases": [{**c, "status": "PENDING", "reason": "Not executed", "evidence": []} for c in cases]}
    backend = None
    try:
        if profile["backend"] == "android" and profile["api"] == 21 and suite != "basic":
            for case in value["cases"]:
                case.update(status="NOT_APPLICABLE", reason="RA is not supported below API 23")
            value["status"] = "NOT_APPLICABLE"
            return directory, value
        if not artifact or not artifact.is_file():
            raise Pending("Select an existing local artifact; no latest-version download is performed")
        value["artifact_sha256"] = digest(artifact)
        if profile.get("artifact_sha256") and value["artifact_sha256"] != profile["artifact_sha256"]:
            raise Pending("Artifact differs from the profile's pinned build")
        if not profile.get("prepared"):
            raise Pending("Dedicated environment preparation is not complete")
        if not any(profile.get("cases", {}).get(c["id"], {}).get("calibrated") for c in cases):
            raise Pending("No selected GUI recipe is calibrated; environment was not started")
        # A custom config/output directory must not bypass the host-wide VM lock.
        with single_vm_lock(DEFAULT_CONFIG.parent / ".vm.lock"):
            backend = (backend_factory or make_backend)(profile, directory)
            operation_interrupted = False
            try:
                backend.prepare(artifact)
                engine = Engine(backend, profile, directory)
                if profile.get("startup"):
                    value["startup"] = {"evidence": [], "observations": []}
                    engine.perform(profile["startup"], value["startup"])
                for index, case in enumerate(cases):
                    if index and suite == "ra-network":
                        if profile.get("manual_credential_unlock") or profile.get("restart_application_between_network_cases"):
                            # Retain the prepared OS login/keyring, never the app session.
                            # Normal quit/restart preserves pending outbox data too.
                            restart = {"evidence":[], "observations":[], "before":backend.process_identity()}
                            value.setdefault("session_restarts", []).append(restart)
                            engine.perform(profile.get("session_restart", {}), restart)
                            restart["after"] = backend.process_identity()
                            if restart["before"] == restart["after"]:
                                raise Failure("Network case did not start a new XM8 process")
                        else:
                            # Restore the same dedicated VM baseline (or AVD baseline) per case.
                            backend.close()
                            environment = directory / f"environment-{index+1}"
                            environment.mkdir(mode=0o700)
                            backend = (backend_factory or make_backend)(profile, environment)
                            backend.prepare(artifact)
                            engine.backend = backend
                            if profile.get("startup"):
                                engine.perform(profile["startup"], value["startup"])
                    try:
                        result = engine.run_case(case)
                    except KeyboardInterrupt:
                        for remaining in value["cases"][index:]:
                            remaining["reason"] = "Cancelled before completion"
                        raise
                    value["cases"][index] = result
                    report(directory, value)
                    if result["status"] != "PASS":
                        # A failed precondition leaves the UI in an unknown state.
                        for remaining in value["cases"][index+1:]:
                            remaining["reason"] = "Previous case did not pass"
                        break
            except BaseException:
                operation_interrupted = True
                raise
            finally:
                try:
                    backend.close()
                except (Exception, KeyboardInterrupt) as error:
                    value["cleanup"] = {"status":"PENDING" if isinstance(error, (Pending, KeyboardInterrupt)) else "FAIL",
                                        "reason":str(error)}
                    if not operation_interrupted:
                        raise
        states = {c["status"] for c in value["cases"]}
        value["status"] = "FAIL" if "FAIL" in states else "PENDING" if "PENDING" in states else "PASS"
    except Pending as error:
        value["reason"] = str(error)
    except KeyboardInterrupt:
        value.update(status="PENDING", reason="Cancelled; cleanup attempted")
    except Exception as error:
        value.update(status="FAIL", reason=str(error))
    finally:
        # Cleanup can be pending even after a real mismatch. Preserve the
        # original failure and expose cleanup separately instead of downgrading.
        if any(c["status"] == "FAIL" for c in value["cases"]) or value.get("cleanup", {}).get("status") == "FAIL":
            value["status"] = "FAIL"
        report(directory, value)
    return directory, value


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("list", "run", "doctor"))
    parser.add_argument("--config", type=Path, default=DEFAULT_CONFIG)
    parser.add_argument("--target")
    parser.add_argument("--suite", choices=SUITES, default="basic")
    parser.add_argument("--case", action="append", dest="cases")
    parser.add_argument("--artifact", type=Path)
    args = parser.parse_args(argv)
    if args.command == "list":
        print(json.dumps({s: CATALOG[s] for s in SUITES}, ensure_ascii=False, indent=2))
        return 0
    try:
        config = json.loads(args.config.read_text())
        if config.get("schema") != 1:
            raise ValueError("Unsupported config schema")
        profile = dict(config["targets"][args.target])
        for override in profile.get("suites", {}).values():
            if profile.get("backend") == "vmpal" and any(
                    k in override and override[k] != profile.get(k)
                    for k in ("vm_id", "vm_name", "baseline_snapshot", "dedicated_vm")):
                raise ValueError("All suites for an OS must reuse the same dedicated VM and baseline")
        profile.update(profile.get("suites", {}).get(args.suite, {}))
        if config.get("adb") and not profile.get("adb"):
            profile["adb"] = config["adb"]
        validate_profile(profile)
        if args.command == "doctor":
            print(json.dumps({"target": args.target, "prepared": bool(profile.get("prepared")),
                              "calibrated": sorted(k for k,v in profile.get("cases", {}).items() if v.get("calibrated")),
                              "note": "Configuration inspection only; does not start a VM or claim GUI success"}, indent=2))
            return 0 if profile.get("prepared") else 2
        artifact = args.artifact or (Path(profile["artifact"]) if profile.get("artifact") else None)
        previous = signal.signal(signal.SIGTERM, lambda *_: (_ for _ in ()).throw(KeyboardInterrupt()))
        try:
            directory, value = run(profile, args.target, args.suite, artifact,
                                   args.config.parent / "runs", selected=args.cases)
        finally:
            signal.signal(signal.SIGTERM, previous)
        print(f"{value['status']}: {directory / 'report.md'}")
        if value.get("reason"):
            print(value["reason"])
        return {"PASS":0, "NOT_APPLICABLE":0, "PENDING":2, "FAIL":1}[value["status"]]
    except (OSError, ValueError, KeyError) as error:
        parser.error(str(error))
