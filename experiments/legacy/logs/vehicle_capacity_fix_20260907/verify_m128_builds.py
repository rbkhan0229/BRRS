"""Offline verification of newly built M128 diagnostic images; no J-Link."""
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys

BASE=Path('/Users/songchieon/Desktop/DWM3000')
ROOT=Path(__file__).resolve().parent
API=BASE/'DW3_QM33_SDK_1.0.2_vehicle_suite_fix_20260907/Drivers/API'
sys.path.insert(0,str(API))
from brrs_suite_case import checked,sha

BIN=Path('/Applications/SEGGER/SEGGER Embedded Studio 8.28/gcc/arm-none-eabi/bin')
bundle=ROOT/'TEST_ONLY_m128_pac8_k10_bundle'
c=checked(bundle)
records=[]
for j in c['jobs']:
    records.append({'role':j['physical_role'],'comparison_pac':8,'hex':bundle/j['hex'],'rtt':j['rtt_address']})

cmd=['bash',str(API/'brrs_exp4_capture.sh'),'init','128','6','1','TEST_ONLY_capacity_build',
    '--guard','250','--lead','25','--pac','4','--sync-buffer','3000','--sync-prep','2500','--cycles','1000',
    '--sequence','2345672345','--slotted-rx','--spi-opt','--serial','1050270933','--no-build','--build-only']
r=subprocess.run(cmd,env={**os.environ,'ARM_NM':str(BIN/'nm')},capture_output=True,text=True)
(ROOT/'m128_pac4_capture_build_only.log').write_text(r.stdout+r.stderr)
assert r.returncode==0,r.stderr
image=Path(re.search(r'\[build-only\] verified image (.+); no board access',r.stdout)[1])
records.append({'role':'init','comparison_pac':4,'hex':image,'rtt':re.search(r'control block @ (0x[0-9a-fA-F]+)',r.stdout)[1]})

keys=['channel','preamble_code','pac_enum','tx_code','rx_code','sfd_type','data_rate','phr_mode','phr_rate','sfd_timeout','sts_mode','sts_length','pdoa_mode']
for row in records:
    elf=row['hex'].with_suffix('.elf')
    nm=subprocess.check_output([str(BIN/'nm'),'-S',str(elf)],text=True)
    row['config']={}
    for name in ['config_data','config_sync']:
        line=next(x for x in nm.splitlines() if x.endswith(' '+name))
        addr,size,_,_=line.split()
        dump=subprocess.check_output([str(BIN/'objdump'),'-s','--start-address=0x'+addr,'--stop-address='+hex(int(addr,16)+int(size,16)),str(elf)],text=True)
        data=bytearray()
        for line in dump.splitlines():
            parts=line.split()
            if not parts or not re.fullmatch('[0-9a-f]{8}',parts[0]):continue
            for block in parts[1:5]:
                if not re.fullmatch('(?:[0-9a-f]{2}){1,4}',block):break
                data.extend(bytes.fromhex(block))
        assert len(data)==15,(name,data.hex())
        cfg=dict(zip(keys,struct.unpack('<BH7BH3B',data)))
        row['config'][name]=cfg
        expected=[9,0x0f,3 if row['role']=='init' and row['comparison_pac']==4 else 0,9,9,1,1,0,0,
            133 if row['role']=='init' and row['comparison_pac']==4 else 129,0,1,0] if name=='config_data' else [9,0x1f,0,10,10,1,1,0,0,257,0,1,0]
        assert list(cfg.values())==expected,(row['role'],name,cfg,expected)
    rtt=next(x.split()[0] for x in nm.splitlines() if x.endswith(' _SEGGER_RTT'))
    assert int(row['rtt'],16)==int(rtt,16)
    assert b'EXP_LOG_READY,channel=1' in elf.read_bytes()
    row['hex_sha256']=sha(row['hex']);row['elf_sha256']=sha(elf);row['hex']=str(row['hex'])
    row['status']='PASS'
result={'scope':'M128 6 physical TX, 10 slots, lead25 diagnostic build; not selected lead or RF validation',
    'new_rf_runs':0,'flash_operations':0,'images':records,
    'pac8_bundle_payload_index_sha256':sha(bundle/'payload_hashes.json')}
(ROOT/'build_verification.json').write_text(json.dumps(result,indent=2)+'\n')
print(f'PASS: {len(records)} HEX/ELF images; DATA/SYNC PHY, RTT, READY and build-only capture paths verified. No board access.')
