from pathlib import Path
import subprocess,shutil,json,hashlib
r=Path(__file__).resolve().parent;m=json.loads((r/'manifest.json').read_text());src=Path(m['source_trial'])
command=['bash','Drivers/API/brrs_exp4_build.sh','32','3','250','init','25','--pac','8','--sync-buffer','3000','--sync-prep','2500','--cycles','1000','--slotted-rx','--spi-opt']
with (r/'P25_build_console.log').open('x') as f:subprocess.run(command,cwd=src,stdout=f,stderr=subprocess.STDOUT,check=True)
folders=list((src/'Drivers/API/Build_Platforms/nRF52840-DK/Output/Debug/Exe/exp4').glob('*lead25*spiopt*slottedrx*'))
assert len(folders)==1,folders
folder=folders[0];dest=r/'images/P25';dest.mkdir()
for ext in ['hex','elf','build.log']:shutil.copyfile(folder/('exp4_32_s3_init.'+ext),dest/('init.'+ext))
nm='/Applications/SEGGER/SEGGER Embedded Studio 8.28/gcc/arm-none-eabi/bin/nm'
syms=[line.split()[0] for line in subprocess.check_output([nm,str(dest/'init.elf')],text=True).splitlines() if line.split()[-1:]==['_SEGGER_RTT']];assert len(syms)==1
entries={}
for role,rec0 in m['variants']['L25'].items():
    rec=dict(rec0);rec['hex']='images/P25/'+role+'.hex'
    if role=='init':rec['source']=str(folder/'exp4_32_s3_init.hex');rec['rtt']=hex(int(syms[0],16))
    else:shutil.copyfile(r/rec0['hex'],r/rec['hex'])
    rec['sha256']=hashlib.sha256((r/rec['hex']).read_bytes()).hexdigest();entries[role]=rec
m['variants']['P25']=entries
p=dict(m['parameters_by_variant']['L25']);p['SPI_OPT']=1;m['parameters_by_variant']['P25']=p
m['P25_reason']='Same slotted RX lead25, existing SPI persistent/direct feature enabled to shorten metadata/rearm/buffer-free path; no PHY/TX/timing/guard changes.'
(r/'manifest.json').write_text(json.dumps(m,indent=2)+'\n')
print(json.dumps(entries['init'],indent=2))
