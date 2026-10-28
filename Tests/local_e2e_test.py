"""Runner/cleanup regressions. These are NOT successful GUI acceptance runs."""
import json
from contextlib import closing
from pathlib import Path
import sys
import tempfile
import unittest
import shutil
import struct
import zlib
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from xm8_e2e.runner import Engine, Failure, Pending, run, single_vm_lock, validate_profile
from xm8_e2e.hub_scripts import definitions
from xm8_e2e.backends import Android, Vmpal, require_unprotected_screen, compare_images, read_ocr


class Backend:
    def __init__(self, *_):
        self.closed = self.down = self.restored = False
        self.fail_restore = self.cancel = False
    def prepare(self, artifact): pass
    def observe(self):
        if self.cancel: raise KeyboardInterrupt()
        return "Drive 1\nSystem Options\nHARD\nConnected"
    def act(self, step): pass
    def guest(self, argv): pass
    def network_down(self): self.down = True
    def network_up(self):
        if self.fail_restore: raise Failure("restore failed")
        self.restored = True
    def probe_control(self): pass
    def close(self): self.closed = True


class RaAggregateObservationTest(unittest.TestCase):
    def setUp(self):
        import importlib.util
        source=Path(__file__).resolve().parents[1]/"scripts/xm8_e2e/guests/observe_ra.py"
        spec=importlib.util.spec_from_file_location("observe_ra",source)
        self.module=importlib.util.module_from_spec(spec);spec.loader.exec_module(self.module)
        self.temporary=tempfile.TemporaryDirectory();self.addCleanup(self.temporary.cleanup)
        self.path=Path(self.temporary.name)/"library.sqlite3"

    def test_missing_database_is_not_created(self):
        with self.assertRaises(ValueError):self.module.observe(self.path)
        self.assertFalse(self.path.exists())

    def test_counts_exclude_private_columns_and_do_not_modify_database(self):
        import sqlite3
        with sqlite3.connect(self.path) as db:
            db.execute("CREATE TABLE pending_unlocks(status INTEGER,account TEXT,achievement_id INTEGER)")
            db.executemany("INSERT INTO pending_unlocks VALUES(?,?,?)",[(0,'PRIVATE_ACCOUNT',99),(1,'PRIVATE_ACCOUNT',100)])
        before=self.path.read_bytes()
        self.assertEqual(self.module.observe(self.path),{"pending":1,"held":1})
        self.assertEqual(self.path.read_bytes(),before)

    def test_unreadable_schema_is_not_an_empty_outbox(self):
        import sqlite3
        with sqlite3.connect(self.path):pass
        with self.assertRaises(sqlite3.Error):self.module.observe(self.path)


class SnapshotReadinessTest(unittest.TestCase):
    def setUp(self):
        import importlib.util
        source=Path(__file__).resolve().parents[1]/"scripts/xm8_e2e/guests/snapshot_ready.py"
        spec=importlib.util.spec_from_file_location("snapshot_ready",source)
        self.module=importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.module)
        self.temporary=tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root=Path(self.temporary.name)
        mock=patch.object(Path,"glob",return_value=[])
        mock.start();self.addCleanup(mock.stop)

    def database(self, pending=False):
        import sqlite3
        with closing(sqlite3.connect(self.root/"library.sqlite3")) as db:
            db.execute("CREATE TABLE pending_unlocks(id INTEGER, status INTEGER)")
            if pending:db.execute("INSERT INTO pending_unlocks VALUES(1,1)")
            db.commit()

    def test_missing_database_is_not_created(self):
        self.assertTrue(self.module.safe_to_restore([self.root]))
        self.assertEqual(list(self.root.iterdir()),[])

    def test_empty_database_is_checked_without_writes(self):
        self.database()
        path=self.root/"library.sqlite3";before=path.read_bytes()
        self.assertTrue(self.module.safe_to_restore([self.root]))
        self.assertEqual(path.read_bytes(),before)

    def test_held_unlock_blocks_rollback(self):
        self.database(pending=True)
        self.assertFalse(self.module.safe_to_restore([self.root]))

    def test_recovery_marker_blocks_rollback(self):
        (self.root/"pending-unlock-recovery-required").touch()
        self.assertFalse(self.module.safe_to_restore([self.root]))

    def test_damaged_database_blocks_rollback(self):
        (self.root/"library.sqlite3").write_bytes(b"damaged")
        self.assertFalse(self.module.safe_to_restore([self.root]))


class LocalE2ETest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        patched = patch("xm8_e2e.runner.DEFAULT_CONFIG", self.root / "config.json")
        patched.start()
        self.addCleanup(patched.stop)
        self.artifact = self.root / "candidate"
        self.artifact.write_bytes(b"test artifact, not an emulator")
        self.backend = Backend()
        self.profile = {"backend":"vmpal", "os":"linux", "vm_id":"base", "vm_name":"XM8M E2E Test", "baseline_snapshot":"snapshot",
                        "dedicated_vm":True, "snapshot_ready":["probe"], "prepared":True,
                        "cases":{"basic.start":{"calibrated":True,"steps":[{"action":"expect","text":"Drive 1","timeout":0}]}}}

    def execute(self, **kw):
        return run(self.profile, "ubuntu", "basic", self.artifact, self.root / "runs",
                   backend_factory=lambda *_: self.backend, **kw)

    def test_keyboard_folder_navigation_requires_exact_directory_heading(self):
        step = {"action":"hold", "keys":"Home Return", "text":"/test/", "when_exact_line":True}
        engine = Engine(self.backend, self.profile, self.root)
        for heading, expected_calls in (("/test/",1), ("/test/E2E/",0)):
            with self.subTest(heading=heading), patch.object(self.backend,"observe",return_value=heading), patch.object(self.backend,"act") as act:
                result = {"evidence":[],"observations":[]}
                engine.perform({"calibrated":True,"steps":[step,{"action":"expect","text":heading,"timeout":0}]},result)
                self.assertEqual(act.call_count,expected_calls)
                self.assertEqual(bool(result['actions'][0].get('skipped')),not expected_calls)

    def test_ocr_colon_spacing_preserves_status_words(self):
        self.assertTrue(Engine.matches({"text":"RA: Ys"},"RA:Ys"))
        self.assertTrue(Engine.matches({"text":"RA: disconnected"},"RA : disconnected"))
        self.assertFalse(Engine.matches({"text":"RA: Ys"},"RA: disconnected"))
        self.assertFalse(Engine.matches({"text":"RA: Ys","exact_line":True},"RA:Ys"))

    def test_ocr_timeout_retries_the_same_image_without_repeating_input(self):
        capture = self.root / 'private-temporary.png'
        with patch('xm8_e2e.backends.native_tool',return_value='ocr'), patch(
                'xm8_e2e.backends.execute',side_effect=[Failure('ocr timed out'),'[{"text":"Switch to Hardcore"}]']) as execute:
            self.assertEqual(read_ocr(self.root,capture),[{"text":"Switch to Hardcore"}])
            self.assertEqual(execute.call_count,2)
            for call in execute.call_args_list:
                self.assertEqual(call.args,(['ocr',capture],))
                self.assertEqual(call.kwargs,{'timeout':15})

    def test_unavailable_ocr_is_pending_after_two_attempts(self):
        with patch('xm8_e2e.backends.native_tool',return_value='ocr'), patch(
                'xm8_e2e.backends.execute',side_effect=Failure('ocr timed out')) as execute:
            with self.assertRaisesRegex(Pending,'GUI input was not repeated'):
                read_ocr(self.root,self.root/'private-temporary.png')
            self.assertEqual(execute.call_count,2)

    def test_cpu_english_ocr_is_explicit_and_keeps_the_observation_deadline(self):
        capture=self.root/'screen.png'
        with patch('xm8_e2e.backends.native_tool',return_value='ocr'),patch('xm8_e2e.backends.execute',return_value='[{"text":"Drive 2"}]') as execute:
            self.assertEqual(read_ocr(self.root,capture,cpu_english=True),[{'text':'Drive 2'}])
            execute.assert_called_once_with(['ocr',capture,'--english-cpu'],timeout=15)

    def test_menu_region_observation_does_not_toggle_an_already_visible_menu(self):
        region = [430, 85, 850, 118]
        recipe = {"calibrated":True,"steps":[
            {"action":"ensure_menu","text":"XM8 Ver","region":region},
            {"action":"expect","text":"XM8 Ver","region":region,"timeout":0}]}
        def observe(selected=None):
            return "XM8 Ver 2.10" if selected == region else "unreadable background"
        with patch.object(self.backend,"observe",side_effect=observe) as read, patch.object(self.backend,"act") as act:
            Engine(self.backend,self.profile,self.root).perform(recipe,{"evidence":[],"observations":[]})
            self.assertEqual([call.args for call in read.call_args_list],[(region,),(region,)])
            act.assert_not_called()

    def test_menu_region_mismatch_still_fails_after_bounded_toggle_attempts(self):
        recipe = {"calibrated":True,"steps":[
            {"action":"ensure_menu","text":"XM8 Ver","region":[430,85,850,118]},
            {"action":"expect","text":"unreachable"}]}
        with patch.object(self.backend,"observe",return_value="other screen"), patch.object(self.backend,"act") as act, patch("xm8_e2e.runner.time.sleep"):
            with self.assertRaisesRegex(Failure,"Main menu did not become visible"):
                Engine(self.backend,self.profile,self.root).perform(recipe,{"evidence":[],"observations":[]})
            self.assertEqual(act.call_count,3)

    def test_ra_stage_distinguishes_authentication_outbox_and_identification(self):
        profile={**self.profile,"ra_observation":{"database":"/test/ra.sqlite","status_region":[180,240,1100,311],"game_text":"RA: Ys"}}
        for menu,status,pending,stage in (("Login","RA: offline",1,"authentication"),
                ("Logout","RA: pending",1,"identification"),("Logout","RA: pending",0,"identification"),
                ("Logout","RA: Ys",0,"active")):
            with self.subTest(stage=stage):
                backend=Vmpal(profile,self.root)
                backend.vm=profile['vm_id'];backend.claimed=True
                backend.observe=lambda region=None:status if region else menu+'\nPRIVATE_ACCOUNT'
                backend.cli=lambda *a,**k:''
                backend.guest=lambda *a:json.dumps({'pending':pending,'held':0})
                value=backend.observe_ra()
                self.assertEqual(value['stage'],stage)
                self.assertEqual(value['outbox']['pending'],pending)
                self.assertNotIn('PRIVATE_ACCOUNT',json.dumps(value))

    def test_windows_ra_observation_uses_read_only_guest_aggregate(self):
        profile={**self.profile,"os":"windows","ra_observation":{
            "aggregate_source":"windows-default-library","status_region":[285,212,900,248],
            "game_region":[285,212,480,248],"authentication_region":[285,140,500,185],"game_text":"RA: Ys"}}
        backend=Vmpal(profile,self.root);backend.vm=profile['vm_id'];backend.claimed=True
        calls=[]
        def observe(region=None):
            calls.append(region)
            return 'Logout' if region==profile['ra_observation']['authentication_region'] else 'RA: pending'
        backend.observe=observe
        with patch.object(backend,'windows_helper',return_value={'pending':2,'held':1}) as helper, patch.object(backend,'cli') as cli:
            value=backend.observe_ra()
        helper.assert_called_once_with('observe-ra');cli.assert_not_called()
        self.assertEqual(value['stage'],'identification')
        self.assertEqual(value['outbox'],{'pending':2,'held':1})
        self.assertEqual(calls,[profile['ra_observation'][key] for key in ('authentication_region','status_region','game_region')])
        self.assertEqual(value['authentication'],'authenticated')
        self.assertNotIn('PRIVATE_ACCOUNT',json.dumps(value))

    def test_windows_process_identity_requires_pid_and_start_time(self):
        backend=Vmpal({**self.profile,'os':'windows'},self.root)
        for proof in ({'pid':42},{'pid':42,'start_ticks':0},{'pid':True,'start_ticks':100}):
            with patch.object(backend,'windows_helper',return_value=proof),self.assertRaises(Pending):
                backend.process_identity()
        with patch.object(backend,'windows_helper',return_value={'pid':42,'start_ticks':100,'focused':False}):
            self.assertEqual(backend.process_identity(),{'pid':42,'start_ticks':100})

    def test_windows_network_proof_must_match_the_current_guard_and_transition(self):
        backend=Vmpal({'os':'windows'},self.root)
        good={'guard':backend.guard,'state':'up','verified':True,'adapter_count':1}
        for change in ({'guard':'another-guard'},{'state':'down'},{'verified':False},{'adapter_count':2},{'adapter_count':True}):
            with self.assertRaises(Failure):backend.record_network_proof(json.dumps({**good,**change}),'up')
        self.assertEqual(list(self.root.glob('xm8-e2e-net-*.json')),[])
        backend.record_network_proof(json.dumps(good),'up')
        self.assertEqual(json.loads((self.root/(backend.guard+'-up.json')).read_text()),good)

    def test_windows_network_uses_bypass_and_keeps_dirty_on_unverified_restore(self):
        backend=Vmpal({'os':'windows','network':{'calibrated':True}},self.root);backend.vm='base'
        backend.cli=lambda *a,**kw:''
        commands=[]
        def guest(argv,admin=False):
            self.assertTrue(admin);commands.append(argv)
            return json.dumps({'guard':backend.guard,'state':'down' if argv[-2]=='arm' else 'up',
                               'verified':argv[-2]=='arm','adapter_count':1,'watchdog_seconds':120})
        backend.guest=guest
        backend.network_down()
        with self.assertRaises(Failure):backend.network_up()
        self.assertTrue(backend.network_dirty)
        for argv in commands:
            self.assertIn('Bypass',argv)
            self.assertIn('C:\\Windows\\Temp\\xm8-e2e-network.ps1',argv)

    def test_desktop_tools_loss_stops_before_using_a_stale_screen(self):
        backend=Vmpal({**self.profile,"require_desktop_tools":True},self.root)
        backend.check_exclusive=lambda:None
        backend.rpc=lambda *_:{"screenTree":False}
        with patch.object(backend,"cli") as cli:
            with self.assertRaisesRegex(Pending,"desktop tools disconnected"):
                backend.observe()
            cli.assert_not_called()

    def test_observation_failure_records_only_safe_phase_and_exception_type(self):
        self.backend.ra_observation_phase="aggregate"
        def unavailable():raise Failure("PRIVATE_ACCOUNT")
        self.backend.observe_ra=unavailable
        result={"evidence":[]}
        with self.assertRaises(Pending):Engine(self.backend,self.profile,self.root).observe_ra(result,"after_cleanup")
        self.assertEqual(result['ra_observation_failure'],{"phase":"aggregate","kind":"Failure"})
        self.assertNotIn("PRIVATE_ACCOUNT",json.dumps(result))

    def test_failure_records_ra_stage_without_turning_it_into_pass(self):
        self.profile['ra_observation']={"configured":True}
        self.profile['cases']['basic.start']['steps']=[{'action':'expect','text':'MISSING','timeout':0}]
        self.backend.observe_ra=lambda:{'authentication':'not_authenticated','visible_state':'offline',
            'stage':'authentication','outbox':{'pending':0,'held':0},'guest_cut_active':False}
        _,result=self.execute(selected=['basic.start'])
        self.assertEqual(result['cases'][0]['status'],'FAIL')
        self.assertEqual(result['cases'][0]['failure_stage'],'authentication')

    def test_unavailable_ra_observation_does_not_pass(self):
        self.profile['ra_observation']={"configured":True}
        self.backend.observe_ra=lambda: (_ for _ in ()).throw(Failure('unavailable'))
        _,result=self.execute(selected=['basic.start'])
        self.assertEqual(result['cases'][0]['status'],'PENDING')

    def test_observed_case_passes_with_evidence(self):
        directory, result = self.execute(selected=["basic.start"])
        self.assertEqual(result["status"], "PASS")
        self.assertTrue(self.backend.closed)
        evidence = result["cases"][0]["evidence"]
        self.assertTrue(evidence)
        self.assertIn("Drive 1", (directory / evidence[0]).read_text())

    def test_ra_account_text_is_never_persisted(self):
        self.backend.observe = lambda: "Logout\nRA: logged in PRIVATE_ACCOUNT"
        self.profile["cases"] = {"ra.authentication":{"calibrated":True,"steps":[
            {"action":"expect","text":"RA: logged in"}]}}
        directory, result = run(self.profile,"ubuntu","ra",self.artifact,self.root / "runs",
            backend_factory=lambda *_:self.backend, selected=["ra.authentication"])
        self.assertEqual(result["status"], "PASS")
        for path in directory.iterdir():
            self.assertNotIn("PRIVATE_ACCOUNT", path.read_text())

    def test_ra_failure_does_not_take_an_unbounded_screenshot(self):
        self.profile["private_evidence"] = True
        self.backend.screenshot = lambda *_:self.fail("must not capture the account screen")
        self.profile["cases"]["basic.start"]["steps"] = [{"action":"expect","text":"Missing","timeout":0}]
        _, result = self.execute(selected=["basic.start"])
        self.assertEqual(result["status"], "FAIL")

    def test_ra_screenshot_without_a_crop_is_pending(self):
        self.profile["private_evidence"] = True
        self.backend.screenshot = lambda *_:self.fail("must not capture the account screen")
        self.profile["cases"]["basic.start"]["steps"].append({"action":"screenshot"})
        _, result = self.execute(selected=["basic.start"])
        self.assertEqual(result["status"], "PENDING")

    def test_private_capture_uses_only_the_selected_rectangle(self):
        self.profile["private_evidence"] = True
        captured = []
        def screenshot(path, region):
            captured.append(region)
            path.write_bytes(b"cropped test image")
        self.backend.screenshot = screenshot
        self.profile["cases"]["basic.start"]["steps"].append({"action":"screenshot","capture_region":[0,768,880,800]})
        _, result = self.execute(selected=["basic.start"])
        self.assertEqual(result["status"], "PASS")
        self.assertEqual(captured, [[0,768,880,800]])

    def test_regional_ocr_keeps_capture_temporary_and_checks_fullscreen(self):
        adapter = Vmpal({"screen_size":[1280,754], "local_region_ocr":True}, self.root)
        adapter.vm = "owned-clone"
        adapter.check_exclusive = lambda:None
        adapter.rpc = lambda *_:{"screenTree":True}
        captures = []
        def cli(*args, **_kw):
            if args[2] == "tree":
                return json.dumps({"windows":[{"app":"xm8","flags":["focused","fullscreen"]}]})
            if args[2] == "screenshot":
                capture = Path(args[args.index("-o")+1])
                capture.write_bytes(b"temporary crop")
                captures.append(capture)
                return ""
            return json.dumps({"screen":{"width":1280,"height":754},"text":[{"text":"RA: Ys"}]})
        adapter.cli = cli
        with patch("xm8_e2e.backends.native_tool", return_value="local-ocr"), patch(
                "xm8_e2e.backends.execute", return_value='[{"text":"RA:"},{"text":"Ys"}]'):
            self.assertEqual(adapter.observe([240,225,900,285]), "RA: Ys")
        self.assertEqual(len(captures), 1)
        self.assertFalse(captures[0].exists())
        self.assertFalse(any(self.root.glob("*.png")))

    def test_regional_ocr_refuses_uncalibrated_dimensions(self):
        adapter = Vmpal({"screen_size":[1280,754], "local_region_ocr":True}, self.root)
        adapter.vm = "owned-clone"
        adapter.check_exclusive = lambda:None
        adapter.rpc = lambda *_:{"screenTree":False}
        adapter.cli = lambda *_args, **_kw:json.dumps({"screen":{"width":1280,"height":800},"text":[]})
        with patch("xm8_e2e.backends.execute") as ocr:
            with self.assertRaisesRegex(Pending, "dimensions differ"):
                adapter.observe([240,225,900,285])
        ocr.assert_not_called()

    def test_repeated_disconnects_use_independent_watchdogs(self):
        adapter = Vmpal({"os":"linux","network":{"calibrated":True}}, self.root)
        adapter.vm = "owned-clone"
        adapter.cli = lambda *_args, **_kw:""
        adapter.probe_control = lambda:None
        commands = []
        adapter.guest = lambda argv, admin=False:commands.append(argv)
        adapter.network_down()
        first = adapter.guard
        adapter.network_up()
        adapter.network_down()
        second = adapter.guard
        adapter.network_up()
        self.assertNotEqual(first, second)
        self.assertEqual([(argv[2],argv[3]) for argv in commands],
                         [("arm",first),("restore",first),("arm",second),("restore",second)])
        self.assertFalse(adapter.network_dirty)

    def test_nested_disconnect_preserves_the_original_watchdog(self):
        adapter = Vmpal({"os":"linux","network":{"calibrated":True}}, self.root)
        adapter.vm = "owned-clone"
        adapter.cli = lambda *_args, **_kw:""
        adapter.guest = lambda *_args, **_kw:None
        adapter.network_down()
        guard = adapter.guard
        with self.assertRaisesRegex(Failure, "already disconnected"):
            adapter.network_down()
        self.assertEqual(adapter.guard, guard)
        self.assertTrue(adapter.network_dirty)

    def manual_adapter(self):
        adapter = Vmpal({**self.profile, "credential_ready":["probe"],
            "manual_credential_unlock":True}, self.root)
        adapter.vm = "base"
        adapter.claimed = True
        adapter.vm_name = "XM8M E2E Test"
        adapter.cli = lambda *_args, **_kw: ""
        adapter.rpc = lambda *_args: {"text":[{"text":"Authentication required"}]}
        adapter.check_exclusive = lambda: None
        return adapter

    def test_manual_unlock_waits_for_readiness_without_credentials(self):
        adapter = self.manual_adapter()
        probes, commands = [], []
        def guest(command):
            commands.append(command)
            if command == ["probe"]:
                probes.append(True)
                if len(probes) < 3: raise Failure("locked")
            return ""
        adapter.guest = guest
        with patch("xm8_e2e.backends.time.sleep"), patch("builtins.print"):
            adapter.wait_for_credentials()
        self.assertEqual(json.loads((self.root / "preparation.json").read_text())["status"], "READY")
        self.assertEqual(len(probes), 3)
        self.assertTrue(any(command[0] == "/usr/bin/systemd-run" for command in commands))
        self.assertFalse(any("GetSecret" in argument for command in commands for argument in command))

    def test_manual_unlock_timeout_is_pending(self):
        adapter = self.manual_adapter()
        def guest(command):
            if command == ["probe"]: raise Failure("locked")
            return ""
        adapter.guest = guest
        with patch("xm8_e2e.backends.time.monotonic", side_effect=[0,0,601]), patch("builtins.print"):
            with self.assertRaisesRegex(Pending, "timed out"): adapter.wait_for_credentials()
        record = json.loads((self.root / "preparation.json").read_text())
        self.assertEqual(record["status"], "PENDING")
        self.assertFalse(record["waiting"])
        self.assertIn("No action required", record["action"])

    def test_unlock_before_prompt_observation_needs_no_manual_request(self):
        adapter = self.manual_adapter()
        probes = []
        def guest(command):
            if command == ["probe"]:
                probes.append(True)
                if len(probes)==1:raise Failure("locked")
            return ""
        adapter.guest = guest
        adapter.rpc = lambda name, _args:self.assertNotEqual(name, "vmpal_observe")
        with patch("builtins.print") as output:
            adapter.wait_for_credentials()
        record = json.loads((self.root / "preparation.json").read_text())
        self.assertEqual(record["status"], "READY")
        self.assertFalse(record["waiting"])
        self.assertTrue(all("PENDING" not in str(call) for call in output.call_args_list))

    def test_missing_unlock_dialog_does_not_request_user_action(self):
        adapter = self.manual_adapter()
        adapter.rpc = lambda *_args: {"text":[{"text":"Ubuntu desktop"}]}
        def guest(command):
            if command == ["probe"]: raise Failure("locked")
            return ""
        adapter.guest = guest
        with patch("xm8_e2e.backends.time.monotonic", side_effect=[0,21]), patch("builtins.print") as output:
            with self.assertRaisesRegex(Pending, "not observed"): adapter.wait_for_credentials()
        output.assert_not_called()
        record = json.loads((self.root / "preparation.json").read_text())
        self.assertFalse(record["waiting"])

    def test_cancelled_guest_unlock_is_pending(self):
        adapter = self.manual_adapter()
        def guest(command):
            if command == ["probe"] or command[0] == "/bin/sh": raise Failure("cancelled")
            return ""
        adapter.guest = guest
        with patch("builtins.print"):
            with self.assertRaisesRegex(Pending, "cancelled"): adapter.wait_for_credentials()

    def test_cleanup_invalidates_the_unlock_request_and_preserves_vm(self):
        adapter = self.manual_adapter()
        vm = {"id":"base", "name":"XM8M E2E Test", "state":"running"}
        adapter.inventory = lambda:[vm]
        adapter.network_up = lambda:None
        adapter.guest = lambda *_args:None
        def cli(*args, **_kw):
            if args[0]=="stop":vm["state"]="stopped"
        adapter.cli = cli
        adapter.rpc = lambda *_args:self.fail("cleanup must never delete a VM")
        (self.root / "preparation.json").write_text(json.dumps({
            "status":"PENDING", "waiting":True, "action":"Manually unlock the guest keyring"}))
        adapter.close()
        record = json.loads((self.root / "preparation.json").read_text())
        self.assertFalse(record["waiting"])
        self.assertTrue(record["vm_stopped"])
        self.assertNotIn("vm_deleted", record)
        self.assertTrue(json.loads(adapter.state_file().read_text())["safe_to_restore"])
        self.assertIsNone(adapter.vm)

    def test_unlocked_store_does_not_open_a_prompt(self):
        adapter = self.manual_adapter()
        adapter.guest = lambda _command: ""
        adapter.rpc = lambda *_args:self.fail("unlocked store needs no prompt")
        adapter.wait_for_credentials()
        self.assertFalse((self.root / "preparation.json").exists())

    def test_missing_case_is_pending_and_stops_later_cases(self):
        _, result = self.execute()
        self.assertEqual(result["status"], "PENDING")
        self.assertEqual(result["cases"][0]["status"], "PASS")
        self.assertEqual(result["cases"][-1]["status"], "PENDING")

    def test_observed_mismatch_fails(self):
        self.profile["cases"]["basic.start"]["steps"][0]["text"] = "never visible"
        _, result = self.execute(selected=["basic.start"])
        self.assertEqual(result["status"], "FAIL")

    def test_no_gui_expectation_never_passes(self):
        self.profile["cases"]["basic.start"]["steps"] = [{"action":"guest","argv":["true"]}]
        _, result = self.execute(selected=["basic.start"])
        self.assertEqual(result["status"], "PENDING")

    def test_unprepared_does_not_construct_backend(self):
        self.profile["prepared"] = False
        _, result = self.execute(selected=["basic.start"])
        self.assertEqual(result["status"], "PENDING")
        self.assertFalse(self.backend.closed)

    def test_artifact_mismatch_never_starts(self):
        self.profile["artifact_sha256"] = "0" * 64
        _, result = self.execute(selected=["basic.start"])
        self.assertEqual(result["status"], "PENDING")
        self.assertFalse(self.backend.closed)

    def test_cancel_is_not_pass_and_closes(self):
        self.backend.cancel = True
        directory, result = self.execute(selected=["basic.start"])
        self.assertEqual(result["status"], "PENDING")
        self.assertTrue(self.backend.closed)
        self.assertTrue((directory / "result.json").exists())
        self.assertEqual(result["cases"][0]["status"], "PENDING")

    def test_cleanup_failure_overrides_pass(self):
        self.backend.close = lambda: (_ for _ in ()).throw(Failure("cleanup"))
        _, result = self.execute(selected=["basic.start"])
        self.assertEqual(result["status"], "FAIL")

    def test_pending_cleanup_preserves_case_failure_and_cli_exit(self):
        import contextlib
        import io
        from xm8_e2e.runner import main
        self.profile['cases']['basic.start']['steps'][0]['text']='missing'
        self.backend.close=lambda:(_ for _ in ()).throw(Pending('pending unlocks'))
        config=self.root/'config.json'
        config.write_text(json.dumps({'schema':1,'targets':{'ubuntu':{**self.profile,'artifact':str(self.artifact)}}}))
        with patch('xm8_e2e.backends.make_backend',return_value=self.backend),contextlib.redirect_stdout(io.StringIO()):
            code=main(['run','--config',str(config),'--target','ubuntu','--case','basic.start'])
        self.assertEqual(code,1)
        result=json.loads(next((self.root/'runs').glob('*/result.json')).read_text())
        self.assertEqual(result['cases'][0]['status'],'FAIL')
        self.assertEqual(result['status'],'FAIL')
        self.assertEqual(result['cleanup']['status'],'PENDING')

    def test_pending_cleanup_after_passing_case_stays_pending(self):
        self.backend.close=lambda:(_ for _ in ()).throw(Pending('pending unlocks'))
        _,result=self.execute(selected=['basic.start'])
        self.assertEqual(result['cases'][0]['status'],'PASS')
        self.assertEqual(result['status'],'PENDING')

    def test_cleanup_does_not_mask_an_operation_exception(self):
        for primary,cleanup,expected in ((Failure,Pending,'FAIL'),(Pending,Failure,'FAIL'),
                (KeyboardInterrupt,Failure,'FAIL'),(Failure,KeyboardInterrupt,'FAIL')):
            with self.subTest(primary=primary.__name__,cleanup=cleanup.__name__):
                self.backend.prepare=lambda _,kind=primary:(_ for _ in ()).throw(kind('primary'))
                self.backend.close=lambda kind=cleanup:(_ for _ in ()).throw(kind('cleanup'))
                directory,result=self.execute(selected=['basic.start'])
                self.assertEqual(result['status'],expected)
                self.assertIn('primary' if primary!=KeyboardInterrupt else 'Cancelled',result['reason'])
                self.assertEqual(result['cleanup']['reason'],'cleanup')
                self.assertIn('cleanup',(directory/'report.md').read_text())

    def test_case_failure_restores_network(self):
        recipe = {"calibrated":True,"steps":[{"action":"network","state":"down"},
                  {"action":"expect","text":"missing","timeout":0}]}
        self.profile["cases"]["network"] = recipe
        result = Engine(self.backend, self.profile, self.root).run_case({"id":"network"})
        self.assertEqual(result["status"], "FAIL")
        self.assertTrue(self.backend.restored)

    def test_cancel_during_disconnect_restores_network(self):
        self.backend.cancel = True
        self.profile["cases"]["network"] = {"calibrated":True,"steps":[
            {"action":"network","state":"down"},{"action":"expect","text":"HARD","timeout":0}]}
        with self.assertRaises(KeyboardInterrupt):
            Engine(self.backend, self.profile, self.root).run_case({"id":"network"})
        self.assertTrue(self.backend.restored)

    def test_failed_restore_is_failure(self):
        self.backend.fail_restore = True
        self.profile["cases"]["network"] = {"calibrated":True,"steps":[
            {"action":"network","state":"down"},{"action":"expect","text":"HARD","timeout":0}]}
        result = Engine(self.backend, self.profile, self.root).run_case({"id":"network"})
        self.assertEqual(result["status"], "FAIL")
        self.assertIn("restoration failed", result["reason"])

    def test_second_run_cannot_take_lock(self):
        with single_vm_lock(self.root / ".lock"):
            with self.assertRaises(Pending):
                with single_vm_lock(self.root / ".lock"): pass

    def test_legacy_android_ra_is_not_applicable(self):
        profile = {"backend":"android","os":"android","api":21,"avd":"XM8M-E2E-21"}
        _, result = run(profile,"android21","ra",None,self.root / "runs")
        self.assertEqual(result["status"], "NOT_APPLICABLE")
        self.assertTrue(all(c["status"] == "NOT_APPLICABLE" for c in result["cases"]))

    def test_personal_avd_rejected(self):
        with self.assertRaises(ValueError):
            validate_profile({"backend":"android","os":"android","api":21,"avd":"OldPhone"})

    def test_new_runs_do_not_reuse_previous_pass(self):
        first, _ = self.execute(selected=["basic.start"])
        second, _ = self.execute(selected=["basic.start"])
        self.assertNotEqual(first, second)

    def test_hub_wrappers_are_local_only(self):
        items = list(definitions(self.root / "config.json"))
        self.assertEqual(len(items), 21)
        for item in items:
            self.assertTrue(item["host"])
            self.assertNotIn("git pull", item["text"])
            self.assertNotIn("workflow", item["text"])

    def test_custom_output_cannot_bypass_host_lock(self):
        with single_vm_lock(self.root / ".vm.lock"):
            _, result = self.execute(selected=["basic.start"])
        self.assertEqual(result["status"], "PENDING")
        self.assertFalse(self.backend.closed)

    def test_network_case_requires_a_fresh_online_setup(self):
        self.profile["cases"]["network.reconnect"] = {"calibrated":True, "steps":[
            {"action":"network","state":"down"}, {"action":"expect","text":"HARD"},
            {"action":"network","state":"up"}, {"action":"expect","text":"Connected"}]}
        result = Engine(self.backend, self.profile, self.root).run_case({"id":"network.reconnect"})
        self.assertEqual(result["status"], "PENDING")
        self.assertFalse(self.backend.down)

    def test_network_case_cannot_pass_without_recovery_observation(self):
        self.profile["cases"]["network.reconnect"] = {"calibrated":True,
            "online_setup":{"calibrated":True,"steps":[{"action":"expect","text":"Connected"}]},
            "steps":[{"action":"network","state":"down"}, {"action":"expect","text":"HARD"},
                     {"action":"network","state":"up"}]}
        result = Engine(self.backend, self.profile, self.root).run_case({"id":"network.reconnect"})
        self.assertEqual(result["status"], "PENDING")
        self.assertFalse(self.backend.down)

    def test_wrong_vm_name_never_reverts_or_stops_it(self):
        adapter = Vmpal(self.profile, self.root)
        adapter.inventory = lambda:[{"id":"base","name":"My Ubuntu","state":"stopped"}]
        adapter.rpc = lambda *_args:self.fail("ordinary VM must not be touched")
        with self.assertRaisesRegex(Pending, "identity/name"):
            adapter.prepare(self.artifact)
        adapter.close()

    def test_missing_snapshot_never_claims_the_vm(self):
        adapter = Vmpal(self.profile, self.root)
        adapter.inventory = lambda:[{"id":"base","name":"XM8M E2E Test","state":"stopped"}]
        calls=[]
        adapter.rpc = lambda name,args:calls.append(name) or {"snapshots":[]}
        with patch("xm8_e2e.backends.adb_tool",return_value="adb"), patch("xm8_e2e.backends.execute",return_value=""):
            with self.assertRaisesRegex(Pending,"snapshot does not exist"):
                adapter.prepare(self.artifact)
        self.assertFalse(adapter.claimed)
        adapter.close()
        self.assertEqual(calls,["vmpal_snapshots"])

    def test_unresolved_previous_run_blocks_snapshot_rollback(self):
        adapter = Vmpal(self.profile,self.root)
        adapter.inventory=lambda:[{"id":"base","name":"XM8M E2E Test","state":"stopped"}]
        adapter.state_file().write_text('{"safe_to_restore":false}')
        adapter.rpc=lambda *_args:self.fail("must retain unresolved data")
        with patch("xm8_e2e.backends.adb_tool",return_value="adb"),patch("xm8_e2e.backends.execute",return_value=""):
            with self.assertRaisesRegex(Pending,"Previous run"):
                adapter.prepare(self.artifact)

    def preflight_adapter(self, previous, probe_error=None, stop_error=None):
        adapter=Vmpal(self.profile,self.root)
        vm={'id':'base','name':'XM8M E2E Test','state':'stopped'}
        adapter.inventory=lambda:[vm]
        if previous is not None:
            adapter.state_file().write_text(json.dumps({'safe_to_restore':previous}))
        calls=[]
        def cli(*args,**kwargs):
            calls.append(args[0])
            if args[0]=='start':vm['state']='running'
            elif args[0]=='stop':
                if stop_error=='still-running':return
                if stop_error:raise stop_error
                vm['state']='stopped'
        def guest(argv,admin=False):
            self.assertEqual(argv,self.profile['snapshot_ready'])
            calls.append('read-current-data')
            if probe_error:raise probe_error
        def rpc(name,args):
            calls.append(name)
            if name=='vmpal_snapshots':return {'snapshots':[{'id':'snapshot'}]}
            if name=='vmpal_revert_snapshot':raise Pending('test intercepted restoration')
            self.fail('unexpected mutation: '+name)
        adapter.cli,adapter.guest,adapter.rpc=cli,guest,rpc
        return adapter,vm,calls

    def test_current_pending_data_blocks_first_and_previously_safe_restores(self):
        for previous in (None,True):
            with self.subTest(previous=previous):
                adapter,vm,calls=self.preflight_adapter(previous,Failure('pending records'))
                with patch('xm8_e2e.backends.adb_tool',return_value='adb'),patch('xm8_e2e.backends.execute',return_value=''):
                    with self.assertRaisesRegex(Pending,'Current guest'):
                        adapter.prepare(self.artifact)
                adapter.close()
                self.assertNotIn('vmpal_revert_snapshot',calls)
                self.assertIn('read-current-data',calls)
                self.assertNotIn('stop',calls)
                self.assertEqual(vm['state'],'running')
                self.assertFalse(json.loads(adapter.state_file().read_text())['safe_to_restore'])

    def test_restore_requires_fresh_probe_then_verified_shutdown(self):
        for previous in (None,True):
            with self.subTest(previous=previous):
                adapter,vm,calls=self.preflight_adapter(previous)
                with patch('xm8_e2e.backends.adb_tool',return_value='adb'),patch('xm8_e2e.backends.execute',return_value=''):
                    with self.assertRaisesRegex(Pending,'test intercepted restoration'):
                        adapter.prepare(self.artifact)
                self.assertEqual(calls,['vmpal_snapshots','start','read-current-data','stop','vmpal_revert_snapshot'])
                self.assertEqual(vm['state'],'stopped')

    def test_preflight_cancel_or_failed_shutdown_never_reverts(self):
        for probe_error,stop_error in ((KeyboardInterrupt(),None),(None,Failure('stop unavailable')),(None,'still-running')):
            with self.subTest(cancel=probe_error is not None):
                adapter,vm,calls=self.preflight_adapter(True,probe_error,stop_error)
                with patch('xm8_e2e.backends.adb_tool',return_value='adb'),patch('xm8_e2e.backends.execute',return_value=''):
                    with self.assertRaises((Pending,KeyboardInterrupt)):
                        adapter.prepare(self.artifact)
                before=list(calls)
                adapter.close()
                self.assertEqual(calls,before)
                self.assertNotIn('vmpal_revert_snapshot',calls)
                self.assertFalse(json.loads(adapter.state_file().read_text())['safe_to_restore'])

    def test_preflight_boot_failure_retains_the_current_disk(self):
        adapter,vm,calls=self.preflight_adapter(True)
        def partial_boot(*args,**kwargs):
            calls.append(args[0])
            vm['state']='running'
            raise Failure('tools unavailable after boot')
        adapter.cli=partial_boot
        with patch('xm8_e2e.backends.adb_tool',return_value='adb'),patch('xm8_e2e.backends.execute',return_value=''):
            with self.assertRaisesRegex(Pending,'Current guest'):
                adapter.prepare(self.artifact)
        adapter.close()
        self.assertEqual(calls,['vmpal_snapshots','start'])
        self.assertFalse(json.loads(adapter.state_file().read_text())['safe_to_restore'])

    def test_saved_baseline_is_normally_stopped_before_cold_boot_isolation(self):
        for stop_failure, stuck in ((False,False),(True,False),(False,True)):
            with self.subTest(stop_failure=stop_failure,stuck=stuck):
                adapter,vm,calls=self.preflight_adapter(None)
                adapter.state_file().unlink(missing_ok=True)
                probes=[];stops=[]
                def rpc(name,args):
                    calls.append(name)
                    if name=='vmpal_snapshots':return {'snapshots':[{'id':'snapshot','hasSession':True}]}
                    if name=='vmpal_revert_snapshot':vm['state']='suspended';return {}
                    if name=='vmpal_update_vm':raise Pending('isolation reached')
                    self.fail('unexpected mutation')
                def guest(argv,admin=False):probes.append(argv);calls.append('probe')
                def cli(*args,**kwargs):
                    calls.append(args[0])
                    if args[0]=='start':vm['state']='running'
                    if args[0]=='stop':
                        stops.append(True)
                        if len(stops)==2 and stop_failure:raise Failure('shutdown failed')
                        if not (len(stops)==2 and stuck):vm['state']='stopped'
                adapter.rpc,adapter.guest,adapter.cli=rpc,guest,cli
                with patch('xm8_e2e.backends.adb_tool',return_value='adb'),patch('xm8_e2e.backends.execute',return_value=''):
                    with self.assertRaises((Pending,Failure)):adapter.prepare(self.artifact)
                self.assertEqual(len(probes),1)  # Fresh baseline check follows the cold boot.
                if stop_failure or stuck:self.assertNotIn('vmpal_update_vm',calls)
                else:
                    self.assertEqual(calls[-3:],['start','stop','vmpal_update_vm'])
                    self.assertEqual(vm['state'],'stopped')

    def test_pending_restored_baseline_blocks_installation_and_launch(self):
        adapter=Vmpal(self.profile,self.root)
        vm={'id':'base','name':'XM8M E2E Test','state':'stopped'}
        adapter.inventory=lambda:[vm]
        adapter.inspect_before_restore=lambda:None
        adapter.rpc=lambda name,args:{'snapshots':[{'id':'snapshot'}]} if name=='vmpal_snapshots' else {}
        calls=[]
        def cli(*args,**kwargs):
            calls.append(args[0])
            if args[0]=='start':vm['state']='running'
        def guest(argv,admin=False):
            self.assertEqual(argv,self.profile['snapshot_ready'])
            raise Failure('pending baseline data')
        adapter.cli,adapter.guest=cli,guest
        with patch('xm8_e2e.backends.adb_tool',return_value='adb'),patch('xm8_e2e.backends.execute',return_value=''):
            with self.assertRaisesRegex(Pending,'installation was not started'):adapter.prepare(self.artifact)
        self.assertNotIn('cp',calls)
        self.assertFalse(json.loads((self.root/'baseline-check.json').read_text())['current_data_checked'])

    def test_windows_fullscreen_requires_matching_dimensions_and_focus(self):
        self.profile.update(os='windows',screen_size=[1280,800])
        adapter=Vmpal(self.profile,self.root);adapter.vm='base'
        observed=[]
        adapter.observe=lambda:observed.append(True) or 'XM8 Ver 2.10'
        proof={'pid':42,'focused':True,'rect':dict(Left=0,Top=0,Right=1280,Bottom=800)}
        adapter.windows_helper=lambda action:proof
        adapter.rpc=lambda *args:{'screen':{'width':1280,'height':800}} if observed else {}
        self.assertEqual(adapter.fullscreen(),proof)
        self.assertTrue(observed)
        adapter.rpc=lambda *args:{'screen':{'width':1280,'height':720}}
        with self.assertRaises(Pending):adapter.fullscreen()
        adapter.rpc=lambda *args:{'screen':{'width':1280,'height':800}}
        proof['focused']=False
        adapter.observe=lambda:'XM8 Ver 2.10'
        adapter.act=lambda step:None
        adapter.cli=lambda *args,**kwargs:''
        with patch('xm8_e2e.backends.time.sleep'):
            with self.assertRaises(Pending):adapter.fullscreen()

    def test_windows_focus_cycle_requires_both_observed_transitions(self):
        self.profile['os']='windows'
        adapter=Vmpal(self.profile,self.root);adapter.check_exclusive=lambda:None
        for left,returned in ((True,False),(False,True)):
            adapter.windows_helper=lambda action:{'left':left,'returned':returned}
            with self.assertRaises(Failure):adapter.focus_cycle()
        adapter.windows_helper=lambda action:{'left':True,'returned':True,'app_pid':42}
        self.assertEqual(adapter.focus_cycle()['app_pid'],42)

    def test_windows_input_never_reaches_an_unfocused_window(self):
        self.profile.update(os='windows',windows_helper='helper')
        adapter=Vmpal(self.profile,self.root);adapter.check_exclusive=lambda:None
        adapter.windows_helper=lambda action:{'focused':False}
        adapter.cli=lambda *args,**kwargs:self.fail('must not send input')
        with self.assertRaises(Pending):adapter.act({'action':'key','keys':'Return'})

    def test_adb_daemon_start_does_not_hold_the_inventory_capture_pipe(self):
        adapter=Vmpal(self.profile,self.root)
        with patch('xm8_e2e.backends.adb_tool',return_value='adb'),patch('xm8_e2e.backends.execute',return_value='List of devices attached\n') as command:
            adapter.adb_devices();adapter.adb_devices()
        self.assertEqual(command.call_args_list[0].args[0],['adb','start-server'])
        self.assertTrue(command.call_args_list[0].kwargs['discard_output'])
        self.assertEqual([c.args[0] for c in command.call_args_list[1:]], [['adb','devices'],['adb','devices']])

    def test_unavailable_adb_inventory_remains_pending(self):
        adapter=Vmpal(self.profile,self.root)
        with patch('xm8_e2e.backends.adb_tool',return_value='adb'),patch('xm8_e2e.backends.execute',side_effect=Failure('timeout')):
            with self.assertRaises(Pending):adapter.adb_devices()
        self.assertFalse(adapter.adb_ready)

    def test_local_pointer_rejects_ambiguous_text_and_changed_dimensions(self):
        self.profile.update(local_pointer_ocr=True,screen_size=[1280,800])
        adapter=Vmpal(self.profile,self.root);adapter.vm='base'
        adapter.check_exclusive=lambda:None;adapter.observe=lambda:'Yes'
        adapter.cli=lambda *args,**kwargs:self.fail('must not click')
        size=[1280,800]
        adapter.screenshot=lambda path:path.write_bytes(bytes(16)+struct.pack('>II',*size))
        with patch('xm8_e2e.backends.native_tool',return_value='ocr'),patch('xm8_e2e.backends.execute',return_value='[{"text":"Yes","x":10,"y":20},{"text":"Yes","x":30,"y":40}]'):
            with self.assertRaises(Failure):adapter.act({'action':'click','text':'Yes'})
            size[1]=720
            with self.assertRaises(Pending):adapter.act({'action':'click','text':'Yes'})
        size[1]=800;clicks=[]
        adapter.cli=lambda *args,**kwargs:clicks.append(args)
        with patch('xm8_e2e.backends.native_tool',return_value='ocr'),patch('xm8_e2e.backends.execute',return_value='[{"text":"S y s t e m O p t i o n s","x":10,"y":20}]'):
            adapter.act({'action':'click','text':'System Options'})
        self.assertEqual(clicks[0][2:5],('click','10','20'))

    def test_full_canvas_ocr_preserves_command_and_output_as_separate_lines(self):
        adapter=Vmpal({'local_full_ocr':True},self.root);adapter.vm='base'
        adapter.check_exclusive=lambda:None;adapter.rpc=lambda *args:{'screenTree':False}
        captures=[]
        def cli(*args,**kwargs):
            if args[2]=='screenshot':
                path=Path(args[args.index('-o')+1]);path.write_bytes(b'fixture');captures.append(path);return ''
            return json.dumps({'text':[{'text':'BASIC'}]})
        adapter.cli=cli
        with patch('xm8_e2e.backends.native_tool',return_value='ocr'),patch('xm8_e2e.backends.execute',return_value='[{"text":"PRINT 123456"},{"text":"123456"}]'):
            observed=adapter.observe()
        self.assertEqual(observed,'PRINT 123456\n123456')
        self.assertTrue(Engine.matches({'text':'123456','exact_line':True},observed))
        self.assertFalse(captures[0].exists())

    def test_image_pointer_accepts_calibrated_blink_states_and_records_the_reference(self):
        self.profile.update(os='windows',screen_size=[1280,800])
        adapter=Vmpal(self.profile,self.root);adapter.vm='base'
        adapter.check_exclusive=lambda:None;adapter.windows_helper=lambda action:{'focused':True}
        adapter.screenshot=lambda path:path.write_bytes(bytes(16)+struct.pack('>II',1280,800))
        clicks=[];adapter.cli=lambda *args,**kwargs:clicks.append(args)
        refs=[self.root/'normal.png',self.root/'selected.png']
        for p in refs:p.write_bytes(p.name.encode())
        step={'text':'System Options','references':list(map(str,refs)),'region':[100,100,300,200]}
        bad={'changed_fraction':0.5,'reference_contrast':200,'actual_contrast':200}
        good=dict(bad,changed_fraction=0)
        with patch('xm8_e2e.backends.compare_images',side_effect=[bad,good]):proof=adapter.click_image(step)
        self.assertEqual(clicks[0][2:5],('click','200','150'))
        self.assertEqual(proof['changed_fraction'],0)
        self.assertTrue((self.root/proof['evidence']).is_file())

    def test_image_pointer_never_clicks_on_mismatch_resize_or_private_screen(self):
        self.profile.update(os='windows',screen_size=[1280,800])
        adapter=Vmpal(self.profile,self.root);adapter.vm='base'
        adapter.check_exclusive=lambda:None;adapter.windows_helper=lambda action:{'focused':True}
        size=[1280,800]
        adapter.screenshot=lambda path:path.write_bytes(bytes(16)+struct.pack('>II',*size))
        adapter.cli=lambda *args,**kwargs:self.fail('must not click')
        ref=self.root/'target.png';ref.write_bytes(b'fixture')
        step={'text':'Target','references':[str(ref)],'region':[100,100,300,200]}
        with patch('xm8_e2e.backends.compare_images',return_value={'changed_fraction':0.5,'reference_contrast':200,'actual_contrast':200}):
            with self.assertRaises(Failure):adapter.click_image(step)
        size[1]=720
        with self.assertRaises(Pending):adapter.click_image(step)
        self.profile['private_evidence']=True
        with self.assertRaises(Pending):adapter.click_image(step)

    def test_cleanup_never_stops_an_unclaimed_vm(self):
        adapter=Vmpal(self.profile,self.root)
        adapter.vm="base"
        adapter.cli=lambda *_args,**_kw:self.fail("unclaimed VM must not be touched")
        adapter.close()

    def test_release_failure_still_stops_dedicated_vm_without_deleting(self):
        adapter=Vmpal(self.profile,self.root)
        adapter.vm,adapter.claimed="base",True
        vm={"id":"base","name":"XM8M E2E Test","state":"running"}
        adapter.inventory=lambda:[vm]
        adapter.guest=lambda *_args:None
        calls=[]
        def cli(*args,**_kw):
            calls.append(args)
            if args[0]=="ui":raise Failure("input channel gone")
            if args[0]=="stop":vm["state"]="stopped"
        adapter.cli=cli
        adapter.rpc=lambda *_args:self.fail("must not clone, delete, or create a snapshot")
        adapter.close()
        self.assertTrue(any(args[0]=="stop" for args in calls))
        self.assertTrue(json.loads(adapter.state_file().read_text())["safe_to_restore"])

    def test_pending_data_keeps_vm_and_blocks_next_rollback(self):
        adapter=Vmpal(self.profile,self.root)
        adapter.vm,adapter.claimed="base",True
        vm={"id":"base","name":"XM8M E2E Test","state":"running"}
        adapter.inventory=lambda:[vm]
        adapter.guest=lambda *_args:(_ for _ in ()).throw(Failure("pending data"))
        def cli(*args,**_kw):
            if args[0]=="stop":vm["state"]="stopped"
        adapter.cli=cli
        with self.assertRaisesRegex(Pending,"Pending data"):
            adapter.close()
        self.assertFalse(json.loads(adapter.state_file().read_text())["safe_to_restore"])

    def test_another_vm_starting_aborts_before_gui_actions(self):
        adapter = Vmpal(self.profile, self.root)
        adapter.vm = "clone"
        adapter.inventory = lambda: [{"id":"base","state":"running"},{"id":"clone","state":"running"}]
        adapter.cli = lambda *_args, **_kw: self.fail("must not send input")
        with self.assertRaises(Pending): adapter.act({"action":"key","keys":"Return"})

    def test_protected_authentication_is_pending_before_evidence(self):
        with self.assertRaises(Pending):
            require_unprotected_screen("RetroAchievements\nUsername\nPassword\nLogin")

    def test_configured_expired_auth_is_pending(self):
        self.profile["cases"]["basic.start"]["steps"] = [
            {"action":"expect","text":"Logged in","pending_if":["Connected"],"timeout":0}]
        _, result = self.execute(selected=["basic.start"])
        self.assertEqual(result["status"], "PENDING")

    def test_network_cases_get_distinct_environments(self):
        recipe = {"calibrated":True,
            "online_setup":{"calibrated":True,"steps":[{"action":"expect","text":"Connected"}]},
            "steps":[{"action":"network","state":"down"}, {"action":"expect","text":"HARD"},
                     {"action":"network","state":"up"}, {"action":"expect","text":"Connected"}]}
        self.profile["cases"] = {"network.reconnect":recipe, "network.hardcore":recipe}
        environments, adapters = [], []
        def factory(profile, directory):
            environments.append(directory)
            adapters.append(Backend())
            return adapters[-1]
        _, result = run(self.profile, "ubuntu", "ra-network", self.artifact, self.root / "runs",
                        backend_factory=factory, selected=list(self.profile["cases"]))
        self.assertEqual(result["status"], "PASS")
        self.assertEqual(len(set(environments)), 2)
        self.assertTrue(all(adapter.closed and adapter.restored for adapter in adapters))

    def warm_network_run(self, same_process=False, manual=True):
        recipe = {"calibrated":True,
            "online_setup":{"calibrated":True,"steps":[{"action":"expect","text":"Connected"}]},
            "steps":[{"action":"network","state":"down"},{"action":"expect","text":"HARD"},
                     {"action":"network","state":"up"},{"action":"expect","text":"Connected"}]}
        self.profile.update(manual_credential_unlock=manual, restart_application_between_network_cases=not manual,
            cases={"network.reconnect":recipe,"network.hardcore":recipe},
            session_restart={"calibrated":True,"steps":[{"action":"guest","argv":["normal-quit-and-restart"]},
                                                      {"action":"expect","text":"Connected"}]})
        identities = iter([{"pid":11,"start_ticks":100},{"pid":11 if same_process else 12,"start_ticks":100 if same_process else 200}])
        self.backend.process_identity = lambda: next(identities)
        adapters = []
        def factory(*_args):
            adapters.append(self.backend)
            return self.backend
        _, result = run(self.profile,"ubuntu","ra-network",self.artifact,self.root / "runs",
            backend_factory=factory, selected=list(self.profile["cases"]))
        return result, adapters

    def test_manual_unlock_keeps_vm_but_requires_a_new_process_per_network_case(self):
        result, adapters = self.warm_network_run()
        self.assertEqual(result["status"], "PASS")
        self.assertEqual(len(adapters), 1)
        self.assertEqual(len(result["session_restarts"]), 1)
        self.assertTrue(self.backend.closed and self.backend.restored)

    def test_windows_network_cases_restart_application_without_a_second_vm_restore(self):
        self.profile['os']='windows'
        result,adapters=self.warm_network_run(manual=False)
        self.assertEqual(result['status'],'PASS')
        self.assertEqual(len(adapters),1)
        self.assertNotEqual(result['session_restarts'][0]['before'],result['session_restarts'][0]['after'])

    def test_reusing_the_same_process_does_not_pass_a_new_network_case(self):
        result, _ = self.warm_network_run(same_process=True)
        self.assertEqual(result["status"], "FAIL")
        self.assertIn("new XM8 process", result["reason"])
        self.assertEqual(result["cases"][1]["status"], "PENDING")

    def test_basic_command_echo_alone_is_not_execution_success(self):
        self.profile["cases"]["basic.start"]["steps"] = [
            {"action":"expect","text":"654321","exact_line":True,"timeout":0}]
        self.backend.observe = lambda: "PRINT 654321"
        _, result = self.execute(selected=["basic.start"])
        self.assertEqual(result["status"], "FAIL")
        self.backend.observe = lambda: "PRINT 654321\n654321"
        _, result = self.execute(selected=["basic.start"])
        self.assertEqual(result["status"], "PASS")

    def test_focus_stream_must_observe_real_focus_loss(self):
        adapter = Vmpal(self.profile, self.root)
        adapter.vm = "clone"
        adapter.inventory = lambda: [{"id":"clone","state":"running"}]
        tree = {"windows":[{"app":"xm8","flags":["focused"],"title":"XM8"}]}
        released = []
        def cli(*args, **kw):
            if args[-1] == "release": released.append(True)
            return json.dumps(tree)
        adapter.cli = cli
        reply = json.dumps({"ok":True,"result":{"tree":tree}})
        with patch("xm8_e2e.backends.adb_tool", return_value="adb"), patch("xm8_e2e.backends.execute", return_value=reply+"\n"+reply):
            with self.assertRaises(Failure): adapter.focus_cycle()
        self.assertTrue(released)

    def test_missing_avd_inventory_never_starts_a_vm(self):
        adapter = Vmpal(self.profile, self.root)
        adapter.inventory = lambda: [{"id":"base","name":"XM8M E2E Test","state":"stopped"}]
        adapter.rpc = lambda *_args: self.fail("must not clone when AVD inventory is unknown")
        with patch("xm8_e2e.backends.adb_tool", side_effect=Pending("ADB missing")):
            with self.assertRaises(Pending): adapter.prepare(self.artifact)

    def test_fullscreen_waits_for_application_focus(self):
        adapter = Vmpal(self.profile, self.root)
        snapshots = iter([{"windows":[]}, {"windows":[
            {"app":"xm8", "flags":["focused","fullscreen"], "title":"XM8"}]}])
        adapter.cli = lambda *_args, **_kw: json.dumps(next(snapshots))
        with patch("xm8_e2e.backends.time.sleep"):
            self.assertIn("fullscreen", adapter.fullscreen()["flags"])

    def test_fullscreen_keys_without_window_transition_are_not_success(self):
        adapter = Vmpal(self.profile, self.root)
        adapter.cli = lambda *_args, **_kw: json.dumps({"windows":[
            {"app":"xm8", "flags":["focused"], "title":"XM8"}]})
        adapter.observe = lambda: "XM8 Ver 2.10 Full Screen"
        adapter.act = lambda _step: None
        with patch("xm8_e2e.backends.time.monotonic", side_effect=[0,0,6]):
            with self.assertRaisesRegex(Failure, "did not enter fullscreen"):
                adapter.fullscreen()

    def test_android_stops_observing_when_another_vm_becomes_active(self):
        with patch("xm8_e2e.backends.adb_tool", return_value="adb"):
            adapter = Android(self.profile, self.root)
        with patch.object(Vmpal, "inventory", return_value=[{"state":"running"}]):
            with self.assertRaises(Pending): adapter.check_exclusive()

    def test_android_detects_another_emulator_but_accepts_its_own(self):
        with patch("xm8_e2e.backends.adb_tool", return_value="adb"):
            adapter = Android(self.profile, self.root)
        with patch.object(Vmpal, "inventory", return_value=[]), patch("xm8_e2e.backends.execute", return_value="emulator-5580\tdevice\n"):
            adapter.check_exclusive()
        with patch.object(Vmpal, "inventory", return_value=[]), patch("xm8_e2e.backends.execute", return_value="emulator-5582\tdevice\n"):
            with self.assertRaises(Pending): adapter.check_exclusive()

    def test_ocr_phrase_may_span_boxes_but_does_not_change_letters(self):
        self.assertTrue(Engine.matches({"text":"BASIC Version"}, "BASIC\nVersion\n2.3"))
        self.assertFalse(Engine.matches({"text":"Clock"}, "C1ock"))


@unittest.skipUnless(sys.platform == "darwin" and shutil.which("swiftc"), "macOS Swift image decoder required")
class FixedRegionTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.root = Path(cls.temporary.name)
        cls.directory = cls.root / "runs/run"
        cls.directory.mkdir(parents=True)
        cls.reference, cls.changed = cls.root / "reference.png", cls.root / "changed.png"
        def png(path, changed):
            def chunk(kind, data):
                return struct.pack("!I", len(data)) + kind + data + struct.pack("!I", zlib.crc32(kind+data) & 0xffffffff)
            rows = b"".join(b"\0" + b"".join(
                (b"\xff\xff\xff" if not changed else b"\xff\0\0") if (x,y) == (1,2) else b"\0\0\0"
                for x in range(16)) for y in range(16))
            path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack("!2I5B",16,16,8,2,0,0,0)) +
                             chunk(b"IDAT",zlib.compress(rows)) + chunk(b"IEND",b""))
        png(cls.reference, False)
        png(cls.changed, True)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def test_changed_pixel_uses_top_left_screen_coordinates(self):
        value = compare_images(self.directory, self.changed, self.reference, [0,0,3,4])
        self.assertGreater(value["changed_fraction"], 0)
        self.assertEqual(value["reference_contrast"], 255)

    def test_pixels_outside_fixed_region_do_not_affect_match(self):
        value = compare_images(self.directory, self.changed, self.reference, [0,10,3,14])
        self.assertEqual(value["changed_fraction"], 0)
        self.assertEqual(value["reference_contrast"], 0)

    def test_invalid_region_cannot_compare_as_pass(self):
        with self.assertRaises(Failure):
            compare_images(self.directory, self.changed, self.reference, [0,0,20,20])

    def test_engine_rejects_a_wrong_image(self):
        backend = Backend()
        backend.screenshot = lambda path: shutil.copyfile(self.changed, path)
        profile = {"cases":{"image":{"calibrated":True,"steps":[{"action":"expect_image",
            "reference":str(self.reference),"region":[0,0,3,4],"timeout":0}]}}}
        result = Engine(backend, profile, self.directory).run_case({"id":"image"})
        self.assertEqual(result["status"], "FAIL")
        self.assertGreater(result["observations"][0]["changed_fraction"], 0)

    def test_blank_image_alone_never_proves_app_success(self):
        backend = Backend()
        backend.screenshot = lambda path: shutil.copyfile(self.reference, path)
        profile = {"cases":{"image":{"calibrated":True,"steps":[{"action":"expect_image",
            "reference":str(self.reference),"region":[0,10,3,14],"allow_blank":True,"timeout":0}]}}}
        result = Engine(backend, profile, self.directory).run_case({"id":"image"})
        self.assertEqual(result["status"], "PENDING")


if __name__ == "__main__":
    unittest.main()
