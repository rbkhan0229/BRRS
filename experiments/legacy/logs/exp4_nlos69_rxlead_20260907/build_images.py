from pathlib import Path
import subprocess,shutil,json,hashlib,sys
root=Path(__file__).resolve().parent
m=json.loads((root/'manifest.json' if (root/'manifest.json').exists() else root/'manifest_draft.json').read_text())
src=Path(m['source_trial']);old=Path(m['previous_campaign'])
oldm=json.loads((old/'manifest.json').read_text())
nm='/Applications/SEGGER/SEGGER Embedded Studio 8.28/gcc/arm-none-eabi/bin/nm'
for lead in map(int,sys.argv[1:] or ['15','5','0','25']):
    tag=f'L{lead}'
    assert not (root/'images'/tag).exists()
    command=['bash','Drivers/API/brrs_exp4_build.sh','32','3','250','init',str(lead),'--pac','8','--sync-buffer','3000','--sync-prep','2500','--cycles','1000','--slotted-rx']
    with (root/(tag+'_build_console.log')).open('x') as f:
        subprocess.run(command,cwd=src,stdout=f,stderr=subprocess.STDOUT,check=True)
    folder=src/'Drivers/API/Build_Platforms/nRF52840-DK/Output/Debug/Exe/exp4'/('plen32_sensors3_sb3000_sp2500_guard250'+(f'_lead{lead}' if lead!=15 else '')+'_slottedrx')
    dest=root/'images'/tag;dest.mkdir(parents=True)
    for ext in ['hex','elf','build.log']:
        shutil.copyfile(folder/('exp4_32_s3_init.'+ext),dest/('init.'+ext))
    sym=[line.split()[0] for line in subprocess.check_output([nm,str(dest/'init.elf')],text=True).splitlines() if line.split()[-1:]==['_SEGGER_RTT']]
    assert len(sym)==1
    entries={}
    for role in ['init','N2','N3','N4']:
        rec=dict(oldm['variants']['B'][role]);rec['hex']=str(Path('images')/tag/(role+'.hex'))
        if role=='init':rec['source']=str(folder/'exp4_32_s3_init.hex');rec['rtt']=hex(int(sym[0],16))
        else:shutil.copyfile(old/oldm['variants']['B'][role]['hex'],root/rec['hex'])
        rec['sha256']=hashlib.sha256((root/rec['hex']).read_bytes()).hexdigest();entries[role]=rec
    if lead==15:
        assert entries['init']['sha256']==oldm['variants']['B']['init']['sha256'],'Baseline rebuild differs'
        (root/'baseline_equivalence.json').write_text(json.dumps({'status':'PASS','sha256':entries['init']['sha256'],'method':'byte-identical HEX; macro file path normalized to original source path'},indent=2)+'\n')
    m['variants'][tag]=entries
    p=dict(m['parameters']);p['lead_us']=lead;m['parameters_by_variant'][tag]=p
    (root/'manifest.json').write_text(json.dumps(m,indent=2)+'\n')
    print(tag,entries['init']['sha256'],entries['init']['rtt'],flush=True)
print('Images and manifest ready.',flush=True)
