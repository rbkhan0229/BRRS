from pathlib import Path
import hashlib, json, sys, importlib.util, shutil, platform
base = Path('/Users/songchieon/Desktop/DWM3000')
files = ['brrs_run_experiment.sh', 'brrs_stage0_capture.sh', 'brrs_exp1_capture.sh',
         'brrs_exp1_verify.py', 'brrs_exp2_capture.sh', 'brrs_exp2_capture_v3.sh',
         'brrs_exp3_capture.sh', 'brrs_exp4_build.sh', 'brrs_exp4_capture.sh',
         'brrs_exp4_multi_tx.sh', 'brrs_exp4_probe_assign.py', 'brrs_exp5_capture.sh',
         'brrs_exp5_channel_characterize.py', 'rtt_capture.py',
         'Src/examples/ex_35a_brrs_init/brrs_init.c',
         'Src/examples/ex_35b_brrs_normal/brrs_normal.c',
         'Build_Platforms/nRF52840-DK/dw3000_api.emProject']
out = {'host': platform.node(), 'python': sys.version, 'python_path': sys.executable,
       'modules': {m: importlib.util.find_spec(m) is not None for m in ['pylink', 'numpy', 'matplotlib', 'scipy']},
       'sources': {}}
for name in ['DW3_QM33_SDK_1.0.2', 'DW3_QM33_SDK_1.0.2_exp4_s6_multislot_20260907', 'DW3_QM33_SDK_1.0.2_exp4_s6_sfd64_20260907']:
    folder = base / name / 'Drivers/API'
    out['sources'][name] = {f: hashlib.sha256((folder / f).read_bytes()).hexdigest() if (folder / f).is_file() else None for f in files}
out['known_executables'] = {p: Path(p).is_file() for p in [
    '/Applications/SEGGER/SEGGER Embedded Studio 8.28/bin/emBuild',
    '/Applications/SEGGER/SEGGER Embedded Studio 8.28/gcc/arm-none-eabi/bin/nm']}
print(json.dumps(out, indent=2))
