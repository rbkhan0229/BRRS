"""Real child-process tests with fake RF workers: no J-Link connections."""
import copy,json,os,signal,subprocess,sys,tempfile,threading,time,unittest
from pathlib import Path
from unittest.mock import patch
API=Path('/Users/songchieon/Desktop/DWM3000/DW3_QM33_SDK_1.0.2_vehicle_beacon512_20260908/Drivers/API')
sys.path.insert(0,str(API));import brrs_single_host as s
B=Path(__file__).resolve().parent/'capture1'
CASE=json.loads((B/'case.json').read_text())
class Flow(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();self.r=Path(self.tmp.name);(self.r/'results').mkdir();self.state={'workers':{},'rf_runs_started':0};self.started=[];self.handles=[];self.procs=[]
 def tearDown(self):
  s.stop_workers(self.procs)
  for f in self.handles:f.close()
  self.tmp.cleanup()
 def spawn(self,job,fail_ready=False,weak=False):
  role=job['physical_role'];self.started.append(role);log=self.r/'results'/(role+'.console.log');f=log.open('x');self.handles.append(f)
  if fail_ready and role=='N3':code='import sys;sys.exit(2)'
  else:
   # TX waits for RX-start file. RX finishes last, even when N2 fails.
   go=self.r/'rx-started'
   if role=='init':code=f'import pathlib,time;pathlib.Path({str(go)!r}).touch();print("READY marker seen",flush=True);time.sleep(.3)'
   else:
    code=f'import pathlib,time,sys;print("READY marker seen",flush=True)\np=pathlib.Path({str(go)!r})\nwhile not p.exists():time.sleep(.01)\ntime.sleep(.02)\nsys.exit({2 if weak and role=="N2" else 0})'
  p=subprocess.Popen([sys.executable,'-c',code],stdout=f,stderr=subprocess.STDOUT,start_new_session=True);self.procs.append(p);return p,log
 def test_tx_ready_then_rx(self):
  s.supervise(self.r,CASE,self.state,self.spawn,ready_timeout=3,capture_timeout=3)
  self.assertEqual(self.started,['N2','N3','N4','N5','N6','N7','init'])
  self.assertLessEqual(self.state['all_tx_ready_at'],self.state['rx_started_at'])
  self.assertTrue(all(w['exit_code']==0 for w in self.state['workers'].values()))
 def test_failed_tx_does_not_cut_off_rx(self):
  s.supervise(self.r,CASE,self.state,lambda j:self.spawn(j,weak=True),ready_timeout=3,capture_timeout=3)
  self.assertEqual(self.state['workers']['N2']['exit_code'],2)
  self.assertEqual(self.state['workers']['init']['exit_code'],0)
 def test_missing_ready_never_starts_rx(self):
  with self.assertRaisesRegex(RuntimeError,'before READY'):
   s.supervise(self.r,CASE,self.state,lambda j:self.spawn(j,fail_ready=True),ready_timeout=3,capture_timeout=3)
  self.assertNotIn('init',self.started);self.assertTrue(all(p.poll() is not None for p in self.procs))
 def test_stop_cleans_owned_processes(self):
  timer=threading.Timer(.2,lambda:(self.r/'STOP').touch());timer.start()
  try:
   with self.assertRaisesRegex(RuntimeError,'stop requested'):
    s.supervise(self.r,CASE,self.state,self.spawn,ready_timeout=3,capture_timeout=3)
  finally:timer.join()
  self.assertTrue(all(p.poll() is not None for p in self.procs))
 def test_missing_probe_refused_without_board_access(self):
  import pylink
  with patch.object(pylink,'JLink') as jl, patch.object(s.subprocess,'check_output',return_value=''):
   jl.return_value.connected_emulators.return_value=[]
   with self.assertRaisesRegex(RuntimeError,'probe set mismatch'):s.preflight(B,CASE)
   jl.return_value.open.assert_not_called()
 def test_serial_mismatch_refused(self):
  c=copy.deepcopy(CASE);c['jobs'][0]['args'][-4]='000'
  with self.assertRaises(ValueError):s.layout(c)
 def test_zero_rx_cannot_pass(self):
  state={'status':'COLLECTION_AND_READBACK_PASS','workers':{},'rf_runs_started':1}
  self.assertEqual(s.summarize(self.r,CASE,state)['verdict'],'FAIL')
 def test_wrong_payload_index_rejected(self):
  with self.assertRaisesRegex(ValueError,'payload index mismatch'):s.load_bundle(B,'bad')
if __name__=='__main__':unittest.main(verbosity=2)
