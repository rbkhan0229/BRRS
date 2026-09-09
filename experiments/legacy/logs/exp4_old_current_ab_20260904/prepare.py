"""Archive matched old/current images, hash-check old identities, no hardware."""
import hashlib
import json
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent
PROJECT = ROOT.parent.parent
SDK = PROJECT / 'DW3_QM33_SDK_1.0.2_exp4_rx_error_diag_20260904'
EXE = 'Drivers/API/Build_Platforms/nRF52840-DK/Output/Debug/Exe/exp4'
OLD = PROJECT / 'DW3_QM33_SDK_1.0.2' / EXE
NEW = SDK / EXE / 'plen32_sensors3_sb3000_sp2500_guard250_spiopt'
ROLES = {'init':'1050270933', 'N2':'1050211584', 'N3':'1050204212', 'N4':'1050282818'}
EXPECTED = {
    'init':'0a020d4cef54c3ac81c248fdf4595c589e500073699750650ca44a59a0d2b737',
    'N2':'9b84be4f94f75257b64b52ac248195d54e669315dc3f8cae054e99a41606ab20',
    'N3':'9a1fa14ef6fcfd81619c33c5720aec2603b6c8ceb3c1403717f4e7d6ed58f651',
    'N4':'3b4611d82afb69f4be3a23aa9275c20bab6d02a49a77ac7baf8c79c9789c07e1',
}
def digest(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    if (ROOT/'manifest.json').exists() or (ROOT/'images').exists():
        raise RuntimeError('Refusing existing archive')
    manifest = {'parameters': {'M':32,'PAC':8,'S':3,'G':250,'lead_us':15,
        'SB':3000,'SP':2500,'SF':1000,'IRQ':0,'fast_switch':0,'rx_diag':0},
        'roles':ROLES,'source_current':subprocess.check_output(['git','rev-parse','HEAD'],cwd=SDK,text=True).strip(),
        'old_source':'exact historical HEX; candidate source 90cdffb, exact source commit unproven',
        'location':'user-confirmed original position; N3 replacement4212; powered hub unchanged',
        'variants':{}, 'capture_script':str(SDK/'Drivers/API/rtt_capture.py')}
    assert manifest['source_current']=='55a23fa94fb7b4e372ec7b03d54be72f01b81e7a'
    for variant in ('old','current'):
        records = {}
        for role in ROLES:
            directory = NEW if variant=='current' else OLD / ('plen32_sensors3_guard250' if role=='init' else 'plen32_sensors3_guard250_lead11')
            source = directory / f'exp4_32_s3_{role}.hex'
            sha = digest(source)
            if variant=='old': assert sha==EXPECTED[role], (role,sha)
            rtt='0x200000a0' if role=='init' else '0x200000b4'
            if variant=='current':
                symbols=subprocess.check_output(['/opt/homebrew/bin/arm-none-eabi-nm','-n',str(source.with_suffix('.elf'))],text=True)
                rtt='0x'+next(l.split()[0] for l in symbols.splitlines() if l.split()[-1:] == ['_SEGGER_RTT'])
            records[role]={'source':str(source),'hex':f'images/{variant}/{role}.hex','sha256':sha,'rtt':rtt,'serial':ROLES[role]}
        manifest['variants'][variant]=records
    # All source identities validated before creating the first archive image.
    for variant, records in manifest['variants'].items():
        for role, record in records.items():
            destination=ROOT/record['hex']; destination.parent.mkdir(parents=True,exist_ok=True)
            shutil.copy2(record['source'],destination)
            assert digest(destination)==record['sha256']
            if variant=='current':
                source=Path(record['source'])
                for ext in ('.elf','.build.log'):
                    shutil.copy2(source.with_suffix(ext), destination.with_suffix(ext))
    manifest['capture_sha256']=digest(Path(manifest['capture_script']))
    with (ROOT/'manifest.json').open('x') as f: json.dump(manifest,f,indent=2)
    print(json.dumps(manifest,indent=2))
if __name__=='__main__': main()
