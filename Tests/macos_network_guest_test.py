"""Exercise the macOS guard with fake system commands; never change host networking."""
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import unittest


FAKE = r'''
import json, os, sys
from pathlib import Path
p=Path(os.environ['E2E_FAKE_STATE']);s=json.loads(p.read_text())
name=Path(sys.argv[0]).name;a=sys.argv[1:];s['calls'].append([name,*a])
code=0;out=''
if name=='uname':out=s.get('system','Darwin')
elif name=='id':out=str(s.get('uid',0))
elif name=='route':out='interface: en0'
elif name=='networksetup':
    if a[0]=='-listnetworkserviceorder':out='(1) Ethernet\n(Hardware Port: Ethernet, Device: en0)\n(2) Ethernet 2\n(Hardware Port: Ethernet, Device: en1)'
    elif a[1]!='Ethernet':code=1
    elif a[0]=='-getnetworkserviceenabled':out='Enabled' if s['enabled'] else 'Disabled'
    elif a[0]=='-setnetworkserviceenabled':
        if a[2]=='on' and s.get('fail_on'):code=1
        else:
            s['enabled']=a[2]=='on'
            if a[2]=='off' and s.get('fail_after_off'):code=1
    else:code=1
elif name=='launchctl':
    if a[0]=='submit':
        if s.get('fail_submit'):code=1
        else:s['jobs'].append(a[2])
    elif a[0]=='list':code=0 if a[1] in s['jobs'] else 1
    elif a[0]=='remove':
        if s.get('fail_remove'):code=1
        elif a[1] in s['jobs']:s['jobs'].remove(a[1])
        else:code=1
    else:code=1
else:code=1
p.write_text(json.dumps(s))
if out:print(out)
sys.exit(code)
'''


@unittest.skipUnless(Path('/bin/sh').exists(), 'POSIX shell fixture')
class MacosNetworkGuardTest(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name);self.bin=self.root/'bin';self.bin.mkdir()
        self.state=self.root/'system.json'
        self.state.write_text(json.dumps({'enabled':True,'secondary_enabled':True,'jobs':[],'calls':[]}))
        for name in ('id','uname','route','networksetup','launchctl'):
            p=self.bin/name;p.write_text('#!'+sys.executable+'\n'+FAKE);p.chmod(0o700)
        source=(Path(__file__).resolve().parents[1]/'scripts/xm8_e2e/guests/network-macos.sh').read_text()
        source=source.replace('PATH=/usr/sbin:/usr/bin:/sbin:/bin','PATH='+shlex.quote(str(self.bin)+':/usr/bin:/bin'))
        source=source.replace('base=/var/run/xm8-e2e-net-macos','base='+shlex.quote(str(self.root/'guards')))
        self.helper=self.root/'guard.sh';self.helper.write_text(source)
        self.guard='xm8-e2e-net-first'

    def system(self):return json.loads(self.state.read_text())

    def configure(self,**changes):
        s=self.system();s.update(changes);self.state.write_text(json.dumps(s))

    def run_guard(self,action,guard=None,device='en0'):
        args=['/bin/sh',str(self.helper),action,guard or self.guard,'macos']
        if action=='arm':args += [device,'Ethernet']
        return subprocess.run(args,env={**os.environ,'E2E_FAKE_STATE':str(self.state)},capture_output=True,text=True,timeout=10)

    def test_restore_is_verified_idempotent_and_cancels_only_its_watchdog(self):
        result=self.run_guard('arm');self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual(json.loads(result.stdout)['watchdog_seconds'],120)
        s=self.system();self.assertFalse(s['enabled']);self.assertTrue(s['secondary_enabled'])
        submit=next(i for i,c in enumerate(s['calls']) if c[:2]==['launchctl','submit'])
        off=next(i for i,c in enumerate(s['calls']) if c[:2]==['networksetup','-setnetworkserviceenabled'])
        self.assertLess(submit,off)
        for _ in range(2):
            result=self.run_guard('restore');self.assertEqual(result.returncode,0,result.stderr)
            self.assertEqual(json.loads(result.stdout)['state'],'up')
        s=self.system();self.assertTrue(s['enabled']);self.assertTrue(s['secondary_enabled']);self.assertEqual(s['jobs'],[])

    def test_retired_watchdog_cannot_reconnect_the_next_case(self):
        self.assertEqual(self.run_guard('arm').returncode,0)
        self.assertEqual(self.run_guard('restore').returncode,0)
        second='xm8-e2e-net-second';self.assertEqual(self.run_guard('arm',second).returncode,0)
        calls=len(self.system()['calls']);self.assertEqual(self.run_guard('watchdog').returncode,0)
        s=self.system();self.assertFalse(s['enabled']);self.assertEqual(s['jobs'],[second])
        self.assertNotIn(['networksetup','-setnetworkserviceenabled','Ethernet','on'],s['calls'][calls:])
        self.assertNotEqual(self.run_guard('restore').returncode,0)
        self.assertFalse(self.system()['enabled'])
        self.assertEqual(self.run_guard('restore',second).returncode,0)

    def test_guest_watchdog_restores_without_a_host_restore_command(self):
        self.assertEqual(self.run_guard('arm').returncode,0)
        result=self.run_guard('watchdog');self.assertEqual(result.returncode,0,result.stderr)
        self.assertTrue(self.system()['enabled'])
        self.assertEqual((self.root/'guards'/self.guard/'restored').read_text().strip(),'watchdog')
        self.assertEqual(self.run_guard('restore').returncode,0)
        self.assertEqual(self.system()['jobs'],[])

    def test_watchdog_registration_failure_never_disconnects(self):
        self.configure(fail_submit=True)
        self.assertNotEqual(self.run_guard('arm').returncode,0)
        s=self.system();self.assertTrue(s['enabled']);self.assertEqual(s['jobs'],[])
        self.assertNotIn(['networksetup','-setnetworkserviceenabled','Ethernet','off'],s['calls'])
        self.assertEqual(self.run_guard('restore').returncode,0)

    def test_wrong_interface_or_unprivileged_execution_never_changes_network(self):
        for config,device in (({},'en1'),({'uid':501},'en0'),({'system':'Linux','uid':0},'en0')):
            with self.subTest(config=config):
                self.configure(**config)
                self.assertNotEqual(self.run_guard('arm',device=device).returncode,0)
                self.assertTrue(self.system()['enabled']);self.assertEqual(self.system()['jobs'],[])

    def test_failed_restore_keeps_watchdog_and_allows_retry(self):
        self.assertEqual(self.run_guard('arm').returncode,0)
        self.configure(fail_on=True)
        self.assertNotEqual(self.run_guard('restore').returncode,0)
        s=self.system();self.assertFalse(s['enabled']);self.assertEqual(s['jobs'],[self.guard])
        self.configure(fail_on=False);self.assertEqual(self.run_guard('restore').returncode,0)
        self.assertTrue(self.system()['enabled']);self.assertEqual(self.system()['jobs'],[])

    def test_partial_disconnect_failure_still_has_a_recovery_path(self):
        self.configure(fail_after_off=True)
        self.assertNotEqual(self.run_guard('arm').returncode,0)
        s=self.system();self.assertFalse(s['enabled']);self.assertEqual(s['jobs'],[self.guard])
        self.assertEqual(self.run_guard('restore').returncode,0)
        self.assertTrue(self.system()['enabled'])

    def test_failed_job_removal_is_not_reported_as_success(self):
        self.assertEqual(self.run_guard('arm').returncode,0)
        self.configure(fail_remove=True)
        self.assertNotEqual(self.run_guard('restore').returncode,0)
        self.assertTrue(self.system()['enabled'])
        self.configure(fail_remove=False);self.assertEqual(self.run_guard('restore').returncode,0)


if __name__=='__main__':unittest.main()
