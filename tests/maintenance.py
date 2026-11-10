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
    def test_hook_order_and_failure_propagation(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);bins=root/'bin';bins.mkdir();(root/'.local/bin').mkdir(parents=True)
            for name,body in {
                'hyprpm':'echo update >> "$TRACE"; exit "${UPDATE_STATUS:-0}"',
                'sudo':'echo "auth:$*" >> "$TRACE"; exit "${AUTH_STATUS:-0}"',
                'notify-send':'echo notify >> "$TRACE"',
            }.items():
                p=bins/name;p.write_text('#!/bin/bash\n'+body+'\n');p.chmod(0o755)
            p=root/'.local/bin/hypr-xcsd-check';p.write_text('#!/bin/bash\necho verify >> "$TRACE"\nexit "${CHECK_STATUS:-0}"\n');p.chmod(0o755)
            for update,check,auth,want in [('0','0','0',0),('7','0','0',7),('0','1','0',1),('0','0','1',1)]:
                trace=root/'trace';trace.unlink(missing_ok=True)
                env={**os.environ,'HOME':str(root),'PATH':str(bins)+':'+os.environ['PATH'],
                     'TRACE':str(trace),'OMARCHY_UPDATE_LOGGED':'1','UPDATE_STATUS':update,
                     'CHECK_STATUS':check,'AUTH_STATUS':auth}
                p=subprocess.run(['bash',str(ROOT/'hooks/50-hyprpm')],env=env,capture_output=True)
                self.assertEqual(p.returncode,want)
                calls=trace.read_text().splitlines()
                self.assertEqual(calls[:2],['update','auth:true'])
                if auth=='0':self.assertEqual(calls[2],'verify')

unittest.main()
