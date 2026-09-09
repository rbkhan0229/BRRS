import datetime,hashlib,json,pathlib,shutil,subprocess,re
base=pathlib.Path('/Users/songchieon/Desktop/DWM3000');root=base/'logs/exp4_home_s6_multislot_pac_ab_20260907';src=base/'DW3_QM33_SDK_1.0.2_exp4_s6_multislot_20260907';out=src/'Drivers/API/Build_Platforms/nRF52840-DK/Output/Debug/Exe/exp4';bindir=pathlib.Path('/Applications/SEGGER/SEGGER Embedded Studio 8.28/gcc/arm-none-eabi/bin')
roles={'init':'1050270933','N2':'1050211584','N3':'1050273888','N4':'1050282818','N5':'1050208509','N6':'1050227627','N7':'1050204212'}
variants=[('L12P8',8,'234567234567'),('F13P8',8,'2345672345672'),('F13P4',4,'2345672345672'),('R13P8',8,'2345672345673'),('R13P4',4,'2345672345673')]
def folder(pac,seq):return out/('plen32_sensors6_sb3000_sp2500_guard250_lead25'+('_pac4' if pac==4 else '')+'_seq'+seq+'_spiopt_slottedrx')
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def symbols(p):
 text=subprocess.check_output([str(bindir/'nm'),'-an',str(p)],text=True);return {line.split()[2]:int(line.split()[0],16) for line in text.splitlines() if len(line.split())==3 and re.fullmatch('[0-9a-fA-F]+',line.split()[0])}
def data(p):
 text=subprocess.check_output([str(bindir/'objdump'),'-s','-j','.data',str(p)],text=True);mem={}
 for line in text.splitlines():
  f=line.split()
  if f and re.fullmatch('[0-9a-fA-F]{8}',f[0]):
   addr=int(f[0],16);buf=bytes.fromhex(''.join(x for x in f[1:5] if re.fullmatch('[0-9a-fA-F]{8}',x)));mem.update({addr+i:b for i,b in enumerate(buf)})
 return mem
m={'created_at':datetime.datetime.now().astimezone().isoformat(),'roles':roles,'source_current':'55a23fa94fb7b4e372ec7b03d54be72f01b81e7a plus previously validated P25 runtime changes','source_trial':str(src),'capture_script':str(base/'DW3_QM33_SDK_1.0.2_exp4_rx_error_diag_20260904/Drivers/API/rtt_capture.py'),'capture_sha256':'67c03d1a0afc7fec8b30a3a82d6985d623d4ee3089c31706d07e5580883bd88f','location':'Home vehicle-proxy test. RX behind washing machine, raised; current six TX placements preserved. Original three TX inside dryer per user; new three exact placement not independently measured. Not assumed equivalent to NLOS6.9m or vehicle.','power':'External TX hub supply confirmed earlier by user; added downstream USB branch observed. Preserve all current connections; secondary branch supply details not independently confirmed.','parameters_by_variant':{},'variants':{},'planned_order':[['L12P8',1],['F13P8',1],['F13P4',1],['F13P8',2],['R13P8',1],['R13P4',1],['R13P8',2]],'policy':'Exactly planned runs unless system/collection invalid; all losses preserved, beacon loss reported separately and counted in offered PER. Final PAC8. No retries, role swaps, physical moves, source edits during comparison, or Git mutations.'}
evidence={}
for name,pac,seq in variants:
 dest=root/'images'/name;dest.mkdir(parents=True,exist_ok=False);m['parameters_by_variant'][name]={'M':32,'PAC':pac,'S':6,'G':250,'lead_us':25,'SB':3000,'SP':2500,'SF':1000,'IRQ':0,'SPI_OPT':1,'sequence':seq,'slots':len(seq),'sfd_timeout':41-pac};m['variants'][name]={};evidence[name]={}
 for role,serial in roles.items():
  folder_src=folder(pac,seq) if role=='init' else folder(8,'234567234567');stem=folder_src/f'exp4_32_s6_{role}';hexp=stem.with_suffix('.hex');elf=stem.with_suffix('.elf');sym=symbols(elf);rtt=hex(sym['_SEGGER_RTT']);shutil.copy2(hexp,dest/(role+'.hex'));shutil.copy2(elf,dest/(role+'.elf'))
  m['variants'][name][role]={'serial':serial,'source':str(hexp),'hex':f'images/{name}/{role}.hex','sha256':sha(hexp),'elf_sha256':sha(elf),'rtt':rtt};evidence[name][role]={'rtt':rtt,'config_data_addr':hex(sym['config_data']),'config_sync_addr':hex(sym['config_sync'])}
  if role=='init':
   mem=data(elf);evidence[name][role]['data_config_16bytes']=bytes(mem[sym['config_data']+i] for i in range(16)).hex();evidence[name][role]['sync_config_16bytes']=bytes(mem[sym['config_sync']+i] for i in range(16)).hex()
for a,b in [('F13P8','F13P4'),('R13P8','R13P4')]:
 x=evidence[a]['init'];y=evidence[b]['init'];assert x['sync_config_16bytes']==y['sync_config_16bytes'];p=bytes.fromhex(x['data_config_16bytes']);q=bytes.fromhex(y['data_config_16bytes']);diff=[i for i in range(16) if p[i]!=q[i]];assert diff==[3,10],diff;assert (p[3],q[3],p[10],q[10])==(0,3,33,37)
for role in roles:
 if role!='init':assert len({m['variants'][name][role]['sha256'] for name,_,_ in variants})==1
old=base/'DW3_QM33_SDK_1.0.2_exp4_rxlead_20260907';changed=[];sources={}
for p in (src/'Drivers/API/Src').rglob('*'):
 if p.is_file():
  rel=p.relative_to(src);q=old/rel;assert q.exists();a=sha(p);sources[str(rel)]=a
  if a!=sha(q):changed.append(str(rel))
assert not changed,changed
(root/'manifest.json').write_text(json.dumps(m,indent=2)+'\n');(root/'compiled_config_evidence.json').write_text(json.dumps(evidence,indent=2)+'\n');(root/'runtime_source_hashes.json').write_text(json.dumps(sources,indent=2)+'\n');print(json.dumps({'variants':{n:{'PAC':p,'sequence':s,'init_hash':m['variants'][n]['init']['sha256']} for n,p,s in variants},'all_runtime_sources_match_prior_copy':True,'TX_hashes':{r:m['variants']['L12P8'][r]['sha256'] for r in roles if r!='init'}},indent=2))
