import pathlib,json,hashlib,shutil,subprocess,re,datetime,difflib
base=pathlib.Path('/Users/songchieon/Desktop/DWM3000');oldroot=base/'logs/exp4_home_s6_multislot_pac_ab_20260907';root=base/'logs/exp4_home_s6_sfd64_20260907';oldsrc=base/'DW3_QM33_SDK_1.0.2_exp4_s6_multislot_20260907';src=base/'DW3_QM33_SDK_1.0.2_exp4_s6_sfd64_20260907';name='R13P4S64';bindir=pathlib.Path('/Applications/SEGGER/SEGGER Embedded Studio 8.28/gcc/arm-none-eabi/bin')
m=json.loads((oldroot/'manifest.json').read_text());params=dict(m['parameters_by_variant']['R13P4']);params['sfd_timeout']=64
oldrecords=m['variants']['R13P4'];m.update(created_at=datetime.datetime.now().astimezone().isoformat(),source_trial=str(src),parameters_by_variant={name:params},variants={name:{}},planned_order=[[name,1]],policy='One new candidate run only. No baseline rerun. SFD timeout diagnostic 37 to 64 at fixed PAC4/M32/13-slot schedule, lead25 and bounded RX window. No retry, physical move, role/power change, commit or push.',comparison_baseline=str(oldroot/'R13P4_r1'),diagnostic_readback='One DATA SFD register read before first RX arm, no per-slot added SPI; print after experiment.')
dest=root/'images'/name;dest.mkdir(parents=True)
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def symbol_data(p):
 t=subprocess.check_output([str(bindir/'nm'),'-an',str(p)],text=True);sy={f[2]:int(f[0],16) for line in t.splitlines() if len(f:=line.split())==3 and re.fullmatch('[0-9a-fA-F]+',f[0])}
 t=subprocess.check_output([str(bindir/'objdump'),'-s','-j','.data',str(p)],text=True);mem={}
 for line in t.splitlines():
  f=line.split()
  if f and re.fullmatch('[0-9a-fA-F]{8}',f[0]):
   addr=int(f[0],16);data=bytes.fromhex(''.join(x for x in f[1:5] if re.fullmatch('[0-9a-fA-F]{8}',x)));mem.update({addr+i:v for i,v in enumerate(data)})
 return sy,{k:bytes(mem[sy[k]+i] for i in range(16)).hex() for k in ['config_data','config_sync']}
for role in m['roles']:
 if role=='init':
  stem=src/'Drivers/API/Build_Platforms/nRF52840-DK/Output/Debug/Exe/exp4/plen32_sensors6_sb3000_sp2500_guard250_lead25_pac4_seq2345672345673_spiopt_slottedrx/exp4_32_s6_init'
  hexp=stem.with_suffix('.hex');elf=stem.with_suffix('.elf');sym,newcfg=symbol_data(elf)
  _,oldcfg=symbol_data(oldroot/'images/R13P4/init.elf')
  assert newcfg['config_sync']==oldcfg['config_sync']
  b=bytes.fromhex(oldcfg['config_data']);n=bytes.fromhex(newcfg['config_data']);assert [i for i in range(16) if b[i]!=n[i]]==[10] and (b[10],n[10])==(37,64)
  rec=dict(oldrecords[role],source=str(hexp),sha256=sha(hexp),elf_sha256=sha(elf),rtt=hex(sym['_SEGGER_RTT']))
 else:
  rec=dict(oldrecords[role]);hexp=oldroot/rec['hex'];elf=hexp.with_suffix('.elf');assert sha(hexp)==rec['sha256']
 rec['hex']=f'images/{name}/{role}.hex';shutil.copy2(hexp,dest/(role+'.hex'));shutil.copy2(elf,dest/(role+'.elf'));m['variants'][name][role]=rec
changed=[];hashes={}
for p in (src/'Drivers/API/Src').rglob('*'):
 if p.is_file():
  rel=p.relative_to(src);q=oldsrc/rel;hashes[str(rel)]=sha(p)
  if sha(p)!=sha(q):
   changed.append(str(rel));(root/'source_change.patch').write_text(''.join(difflib.unified_diff(q.read_text().splitlines(True),p.read_text().splitlines(True),fromfile=str(q),tofile=str(p))))
assert changed==['Drivers/API/Src/examples/ex_35a_brrs_init/brrs_init.c'],changed
for side in ['local','remote']:
 state=json.loads((root/(side+'_preflight.json')).read_text());expected={m['roles']['init']} if side=='local' else {v for k,v in m['roles'].items() if k!='init'};assert set(state['probes'])==expected and not state['capture_processes']
(root/'manifest.json').write_text(json.dumps(m,ensure_ascii=False,indent=2)+'\n');(root/'compiled_config_evidence.json').write_text(json.dumps({'before':oldcfg,'after':newcfg,'data_byte_change_only':{'offset':10,'before':37,'after':64},'runtime_source_files_changed':changed},indent=2)+'\n');(root/'runtime_source_hashes.json').write_text(json.dumps(hashes,indent=2)+'\n')
print(json.dumps({'init':m['variants'][name]['init'],'TX_images_match_prior':True,'runtime_changes':changed},indent=2))
