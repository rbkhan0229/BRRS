from pathlib import Path
import subprocess,shutil,json,hashlib
r=Path(__file__).resolve().parent;m=json.loads((r/'manifest_draft.json').read_text());src=Path(m['source_trial']);old=Path(m['previous_campaign']);oldm=json.loads((old/'manifest.json').read_text())
cmd=['bash','Drivers/API/brrs_exp4_build.sh','32','3','250','init','25','--pac','4','--sync-buffer','3000','--sync-prep','2500','--cycles','1000','--slotted-rx','--spi-opt']
with (r/'build_console.log').open('x') as f:subprocess.run(cmd,cwd=src,stdout=f,stderr=subprocess.STDOUT,check=True)
folder=src/'Drivers/API/Build_Platforms/nRF52840-DK/Output/Debug/Exe/exp4/plen32_sensors3_sb3000_sp2500_guard250_lead25_pac4_spiopt_slottedrx'
dest=r/'images/C4';dest.mkdir(parents=True)
for ext in ['hex','elf','build.log']:shutil.copyfile(folder/('exp4_32_s3_init.'+ext),dest/('init.'+ext))
nm='/Applications/SEGGER/SEGGER Embedded Studio 8.28/gcc/arm-none-eabi/bin/nm'
syms=[line.split()[0] for line in subprocess.check_output([nm,str(dest/'init.elf')],text=True).splitlines() if line.split()[-1:]==['_SEGGER_RTT']];assert len(syms)==1
entries={}
for role,v in oldm['variants']['P25'].items():
    rec=dict(v);rec['hex']='images/C4/'+role+'.hex'
    if role=='init':rec['source']=str(folder/'exp4_32_s3_init.hex');rec['rtt']=hex(int(syms[0],16))
    else:shutil.copyfile(old/v['hex'],r/rec['hex'])
    rec['sha256']=hashlib.sha256((r/rec['hex']).read_bytes()).hexdigest();entries[role]=rec
m['variants']['C4']=entries
(r/'manifest.json').write_text(json.dumps(m,indent=2)+'\n')
print(json.dumps(entries,indent=2))
