import os
from pathlib import Path
import runpy
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
assess = runpy.run_path(str(ROOT/'scripts/hypr-xcsd-check'))['assess']

class Health(unittest.TestCase):
    def test_enabled_failed_disabled_and_absent(self):
        self.assertEqual(assess({'hypr-xcsd':{'enabled':False}},'', '', '', set())[0],0)
        self.assertEqual(assess({'hypr-xcsd':{'enabled':True,'failed':True}},'a','a','a',{'hypr-xcsd'})[0],1)
        self.assertEqual(assess({'hypr-xcsd':{'enabled':True}},'a','a','a',set())[0],1)
        self.assertEqual(assess({'hypr-xcsd':{'enabled':True}},'a','a','a',{'hypr-xcsd'})[0],0)
    def test_deferred_and_stale_abi_are_distinct(self):
        repo={'hypr-xcsd':{'enabled':True}}
        self.assertEqual(assess(repo,'new','new','old',set())[0],2)
        self.assertEqual(assess(repo,'old','new','new',set())[0],1)
    def test_hook_keeps_omarchy_sudo_login_and_propagates_status(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);bins=root/'bin';bins.mkdir();(root/'.local/bin').mkdir(parents=True)
            # Fake hyprpm calls sudo the way hyprpm 0.56.2 does: cache, work, then -k.
            for name,body in {
                'hyprpm':'sudo echo hyprland; sudo install -m644 a b; sudo -k; echo update >> "$TRACE"; exit "${UPDATE_STATUS:-0}"',
                'realsudo':'echo "sudo:$*" >> "$TRACE"',
            }.items():
                p=bins/name;p.write_text('#!/bin/bash\n'+body+'\n');p.chmod(0o755)
            checker=root/'.local/bin/hypr-xcsd-check'
            for update,check,with_checker,want in [('0','0',True,0),('7','0',True,7),('0','1',True,1),('0','0',False,0)]:
                checker.unlink(missing_ok=True)
                if with_checker:
                    checker.write_text('#!/bin/bash\necho verify >> "$TRACE"\nexit "${CHECK_STATUS:-0}"\n');checker.chmod(0o755)
                trace=root/'trace';trace.unlink(missing_ok=True)
                env={**os.environ,'HOME':str(root),'PATH':str(bins)+':'+os.environ['PATH'],'XDG_RUNTIME_DIR':str(root),
                     'TRACE':str(trace),'UPDATE_STATUS':update,'CHECK_STATUS':check,'XCSD_REAL_SUDO':str(bins/'realsudo')}
                p=subprocess.run(['bash',str(ROOT/'hooks/50-hyprpm')],env=env,capture_output=True)
                self.assertEqual(p.returncode,want,p.stderr)
                calls=trace.read_text().splitlines()
                # -k never reaches the real sudo; everything else passes through unchanged.
                self.assertEqual(calls[:3],['sudo:echo hyprland','sudo:install -m644 a b','update'])
                self.assertNotIn('sudo:-k',calls)
                self.assertEqual('verify' in calls, with_checker)
                self.assertEqual(list(root.glob('hyprpm-sudo.*')),[])  # wrapper dir cleaned up

unittest.main()
