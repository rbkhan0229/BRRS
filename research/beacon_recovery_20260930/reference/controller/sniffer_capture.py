#!/usr/bin/env python3
"""Own one sealed passive N2 sniffer from flash to STOP/readback; never retry."""
import argparse, hashlib, json, os, re, signal, sys, time
from datetime import datetime, timezone
from pathlib import Path

SCHEMA='passive-timebase-v1'
ARM=0x534E4131
STOP=0x534E5354
RAM0,RAM1=0x20000000,0x20040000
COUNTERS=('sn_state','sn_error','sn_rf_off','sn_aux_count','sn_beacon_count','sn_window_count','sn_aux_bad','sn_beacon_bad','sn_rx_errors','sn_spi_errors','sn_initial_anchor','sn_last_anchor','sn_first_aux_seq','sn_last_aux_seq')
ARRAYS={'sn_aux_records':(28000,6),'sn_beacon_records':(128,6),'sn_window_start':(32,4),'sn_window_end':(32,4)}

def utc(): return datetime.now(timezone.utc).isoformat()
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def save(p,x):
 p=Path(p);t=p.with_name(p.name+'.'+str(os.getpid())+'.tmp')
 with t.open('w') as f:json.dump(x,f,sort_keys=True,indent=2);f.write('\n');f.flush();os.fsync(f.fileno())
 os.replace(t,p)
def num(x):return int(x,0) if isinstance(x,str) else int(x)
def load(root,index):
 root=root.resolve()
 if sha(root/'payload_hashes.json')!=index:raise ValueError('payload index mismatch')
 hashes=json.loads((root/'payload_hashes.json').read_text())
 for rel,expected in hashes.items():
  p=(root/rel).resolve()
  if not p.is_relative_to(root) or sha(p)!=expected:raise ValueError('payload file changed: '+rel)
 sys.path.insert(0,str(root/'sdk/Drivers/API'))
 from brrs_single_host import load_bundle
 from brrs_suite_case import hex_chunks
 c=load_bundle(root,index);j=c['sniffer_job']
 if j['schema']!=SCHEMA or j['physical_role']!='N2' or str(j['serial'])!='1050211584':raise ValueError('sniffer identity/schema')
 if str(c['boards']['N2']['serial'])!=str(j['serial']):raise ValueError('board mapping mismatch')
 if any(str(w['serial'])==str(j['serial']) for w in c['jobs']):raise ValueError('sniffer overlaps victim job')
 image=(root/j['hex']).resolve();elf=(root/j['elf']).resolve()
 if not image.is_relative_to(root) or not elf.is_relative_to(root):raise ValueError('image escape')
 if sha(image)!=j['hex_sha256'] or sha(elf)!=j['elf_sha256']:raise ValueError('image hash mismatch')
 chunks=hex_chunks(image)
 if any(a<0 or a+len(b)>0x100000 for a,b in chunks):raise ValueError('non-main-flash image')
 symbols={}
 for name in ('sn_host_arm','sn_host_stop',*COUNTERS,*ARRAYS):
  x=j['symbols'][name];a=num(x['address']);size=num(x['size']);wanted=ARRAYS[name][0]*ARRAYS[name][1] if name in ARRAYS else 4
  if size!=wanted or a<RAM0 or a+size>RAM1 or a%4:raise ValueError('bad RAM symbol '+name)
  symbols[name]=a
 if num(j['arm_magic'])!=ARM or num(j['stop_magic'])!=STOP:raise ValueError('mailbox magic')
 rtt=num(j['rtt_address'])
 if not RAM0<=rtt<RAM1:raise ValueError('RTT address')
 paths=[root/'STOP']+[Path(p) for p in j['stop_paths']]
 if len(paths)<3 or any(not p.is_absolute() for p in paths[1:]):raise ValueError('STOP paths')
 return c,j,image,chunks,symbols,rtt,paths

class Capture:
 def __init__(self,root,index,validated):
  self.root=root;self.index=index
  self.case,self.job,self.image,self.chunks,self.syms,self.rtt,self.stops=validated
  self.out=root/'results';self.status=self.out/'SNIFFER_STATUS.json';self.meta=self.out/'SNIFFER.meta.json'
  self.raw=self.out/'SNIFFER.raw.log';self.jl=None;self.started=False;self.armed=False;self.stopping=False;self.normal_stop=False;self.errors=[];self.buffer=bytearray();self.marks={x:0 for x in ('READY','START','END')};self.records={};self.ram={}
  self.state=dict(status='STARTING',case_id=self.case['id'],serial=str(self.job['serial']),schema=SCHEMA,ready=False,running=False,end=False,started_at=utc(),payload_index_sha256=index,firmware_sha256=self.job['hex_sha256'],reset_count=0,arm_count=0,stop_count=0,rf_retry_count=0)
 def persist(self):self.state['updated_at']=utc();save(self.status,self.state)
 def fail(self,e):
  e=str(e)
  if e not in self.errors:self.errors.append(e)
  self.state['error']=self.errors[0];self.persist()
 def stopped(self):return next((str(p) for p in self.stops if p.exists()),None)
 def connect(self):
  import pylink
  j=pylink.JLink();j.open(serial_no=int(self.job['serial']));j.set_tif(pylink.enums.JLinkInterfaces.SWD);j.connect('NRF52840_XXAA',speed=4000);j.exec_command('SetRestartOnClose = 0');return j
 def readback(self):
  for a,b in self.chunks:
   if bytes(self.jl.memory_read8(a,len(b)))!=b:raise RuntimeError('N2 full HEX readback mismatch at '+hex(a))
  return dict(status='PASS',bytes_verified=sum(len(b) for _,b in self.chunks),hex_sha256=self.job['hex_sha256'],at=utc())
 def write(self,name,x):
  self.jl.memory_write32(self.syms[name],[x]);y=self.jl.memory_read32(self.syms[name],1)[0]
  if y!=x:raise RuntimeError('N2 mailbox readback mismatch '+name)
 def poll_ram(self):
  r={k:int(self.jl.memory_read32(self.syms[k],1)[0]) for k in COUNTERS}
  for k,v in (('sn_aux_count',28000),('sn_beacon_count',128),('sn_window_count',32)):
   if r[k]>v:raise RuntimeError(k+' overflow')
  self.ram=r;self.state.update(running=r['sn_state']==2,ram=r,aux_rx=r['sn_aux_count'],beacon_rx=r['sn_beacon_count']);self.persist()
  if r['sn_state']==4 or r['sn_error']:raise RuntimeError('sniffer firmware error '+repr(r))
 def drain(self,fp):
  b=self.jl.rtt_read(1,16384)
  if not b:return
  b=bytes(b);fp.write(b);fp.flush();self.buffer.extend(b)
  if len(self.buffer)>1000000:raise RuntimeError('sniffer RTT volume too large')
  text=self.buffer.decode(errors='replace');lines=text.splitlines()
  if text and not text.endswith(('\n','\r')):lines=lines[:-1]
  for kind in self.marks:
   found=[s for s in lines if re.match(r'^SNIFFER_'+kind+r'(?:[\s,]|$)',s)]
   if len(found)>1:raise RuntimeError('duplicate SNIFFER '+kind)
   self.marks[kind]=len(found)
   if found:
    if kind!='START' and 'schema='+SCHEMA not in found[0]:raise RuntimeError('marker schema mismatch '+kind)
    if kind=='READY' and not all(x in found[0] for x in ('channel=5','beacon_m=512','beacon_code=10','aux_m=64','aux_code=9','tx=0','initial_anchor_wait_us=30000000')):raise RuntimeError('N2 passive CH5 PHY mismatch')
    self.records[kind]=dict(re.findall(r'(\w+)=([^\s,]+)',found[0]))
  self.state.update(ready=self.marks['READY']==1,end=self.marks['END']==1,marker_counts=dict(self.marks));self.persist()
 def request_stop(self,normal=False):
  if normal:self.normal_stop=True
  if self.started and not self.stopping:
   self.write('sn_host_stop',STOP);self.stopping=True;self.state['stop_count']=1;self.persist()
 def capture(self):
  if self.stopped():raise RuntimeError('STOP before sniffer startup')
  if (self.out/'SNIFFER_ARM').exists() or (self.out/'SNIFFER_STOP').exists():raise RuntimeError('stale sniffer request')
  self.jl=self.connect();self.jl.reset(halt=True);self.state['reset_count']+=1
  self.jl.flash_file(str(self.image),0x0);self.state['readback_before']=self.readback()
  if self.stopped():raise RuntimeError('STOP before N2 reset/run')
  self.jl.reset(halt=False);self.started=True;self.state['reset_count']+=1;self.persist()
  self.jl.rtt_start(block_address=self.rtt)
  cb_deadline=time.monotonic()+10
  while True:
   try:
    if self.jl.rtt_get_num_up_buffers()>1:break
   except Exception:
    if time.monotonic()>cb_deadline:raise
   if time.monotonic()>cb_deadline:raise TimeoutError('N2 RTT control block')
   if self.stopped():raise RuntimeError('STOP before N2 READY')
   time.sleep(.02)
  deadline=time.monotonic()+100;next_ram=0
  with self.raw.open('xb') as fp:
   while True:
    if self.stopped():raise RuntimeError('explicit STOP: '+self.stopped())
    if (self.out/'SNIFFER_STOP').exists():self.request_stop(normal=True)
    if time.monotonic()>deadline:raise TimeoutError('N2 sniffer watchdog')
    self.drain(fp)
    if time.monotonic()>=next_ram or (self.state['ready'] and not self.ram):
     self.poll_ram();next_ram=time.monotonic()+.2
    if self.state['ready'] and not self.armed and not self.stopping:
     self.state['status']='READY';self.persist()
     if self.ram['sn_state']!=1:
      self.poll_ram()
      if self.ram['sn_state']!=1:raise RuntimeError('READY marker/RAM mismatch')
     if (self.out/'SNIFFER_ARM').exists():
      if self.stopped() or (self.out/'SNIFFER_STOP').exists():raise RuntimeError('STOP before N2 ARM')
      self.write('sn_host_arm',ARM);self.armed=True;self.state['arm_count']=1;self.state['status']='RUNNING';self.persist()
    if self.state['end']:
     self.poll_ram()
     if not self.stopping:raise RuntimeError('N2 ended before host STOP')
     if self.ram['sn_state']!=3 or self.ram['sn_rf_off']!=1:raise RuntimeError('N2 END/RAM/RF-off mismatch')
     break
    time.sleep(.02)
 def read_arrays(self):
  if self.ram['sn_aux_count']>28000 or self.ram['sn_beacon_count']>128 or self.ram['sn_window_count']>32:raise RuntimeError('array count')
  spec={'sn_aux_records':self.ram['sn_aux_count']*6,'sn_beacon_records':self.ram['sn_beacon_count']*6,'sn_window_start':self.ram['sn_window_count']*4,'sn_window_end':self.ram['sn_window_count']*4}
  result={}
  for name,size in spec.items():
   b=bytes(self.jl.memory_read8(self.syms[name],size)) if size else b''
   p=self.out/(name+'.bin')
   with p.open('xb') as f:f.write(b);f.flush();os.fsync(f.fileno())
   result[name]=dict(bytes=size,sha256=sha(p),path=str(p))
  return result
 def cleanup(self):
  if self.jl is None:return
  if self.started:
   try:
    self.request_stop();until=time.monotonic()+5
    while time.monotonic()<until:
     self.poll_ram()
     if self.ram.get('sn_state') in (3,4):break
     time.sleep(.05)
    if self.ram.get('sn_state') not in (3,4):self.fail('cooperative N2 STOP timeout')
   except Exception as e:self.fail('N2 STOP failure: '+repr(e))
  try:
   self.jl.halt()
   if not self.jl.halted():raise RuntimeError('N2 not halted')
   self.state['halted_before_close']=True
  except Exception as e:self.fail('N2 halt failure: '+repr(e))
  try:self.state['readback_after']=self.readback()
  except Exception as e:self.fail('N2 readback failure: '+repr(e))
  try:
   if self.ram.get('sn_state')==3 and self.state.get('halted_before_close'):
    self.state['arrays']=self.read_arrays()
  except Exception as e:self.fail('N2 RAM extraction failure: '+repr(e))
  try:self.jl.rtt_stop()
  except Exception as e:self.fail('N2 RTT stop failure: '+repr(e))
  try:self.jl.close()
  except Exception as e:self.fail('N2 close failure: '+repr(e))
  self.jl=None
  try:
   j=self.connect();self.state['halted_after_reconnect']=bool(j.halted());j.close()
   if not self.state['halted_after_reconnect']:raise RuntimeError('N2 resumed after reconnect')
  except Exception as e:self.fail('N2 reconnect failure: '+repr(e))
 def finish(self):
  end=self.records.get('END',{});r=self.ram
  if not self.normal_stop or not self.armed or self.marks!={'READY':1,'START':1,'END':1}:self.fail('missing normal ARM/READY/START/END')
  for field,name in [('error','sn_error'),('aux','sn_aux_count'),('beacon','sn_beacon_count'),('windows','sn_window_count'),('aux_bad','sn_aux_bad'),('beacon_bad','sn_beacon_bad'),('rx_errors','sn_rx_errors'),('spi_errors','sn_spi_errors'),('rf_off','sn_rf_off')]:
   if end.get(field)!=str(r.get(name)):self.fail('END/RAM mismatch '+field)
  if r.get('sn_error') or r.get('sn_spi_errors') or r.get('sn_state')!=3 or r.get('sn_rf_off')!=1:self.fail('N2 firmware/RF-off invalid')
  if r.get('sn_beacon_count',0)<2 or r.get('sn_last_anchor',0)-r.get('sn_initial_anchor',0)<200:self.fail('insufficient shared time anchors')
  for name in ('readback_before','readback_after'):
   if self.state.get(name,{}).get('status')!='PASS':self.fail('missing '+name)
  if not self.state.get('halted_before_close') or not self.state.get('halted_after_reconnect'):self.fail('N2 halt/reconnect invalid')
  if not self.state.get('arrays'):self.fail('N2 arrays missing')
  self.state.update(status='FAIL' if self.errors else 'PASS',errors=list(self.errors),finished_at=utc(),raw_sha256=sha(self.raw) if self.raw.exists() else None,marker_counts=dict(self.marks))
  self.persist();save(self.meta,self.state)
  return 1 if self.errors else 0
 def run(self):
  if self.status.exists() or self.meta.exists():raise RuntimeError('used sniffer output')
  self.out.mkdir(exist_ok=True);self.persist()
  try:self.capture()
  except BaseException as e:self.fail(repr(e))
  finally:self.cleanup()
  return self.finish()

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--bundle',type=Path,required=True);ap.add_argument('--index',required=True);a=ap.parse_args()
 valid=load(a.bundle,a.index)
 return Capture(a.bundle,a.index,valid).run()
if __name__=='__main__':raise SystemExit(main())
