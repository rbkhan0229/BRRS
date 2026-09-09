#!/usr/bin/env python3
"""Offline analysis of immutable experiment evidence. No SSH, build, or board I/O."""
import csv
import json
import math
import os
import re
import statistics as st
import subprocess
import sys
from collections import Counter
from datetime import datetime
from pathlib import Path

os.environ.setdefault('MPLCONFIGDIR',str(Path(__file__).resolve().parent/'plot_cache'))
os.environ.setdefault('XDG_CACHE_HOME',str(Path(__file__).resolve().parent/'plot_cache'))
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np

OUT=Path(__file__).resolve().parent
RUN=OUT.parent
BASE=RUN.parents[1]
API=BASE/'DW3_QM33_SDK_1.0.2_vehicle_suite_fix_20260907/Drivers/API'
SMOKE=BASE/'logs/vehicle_suite_nlos69_smoke_20260907_2301'
LEADS=BASE/'logs/nlos69_lead_screen_rotation_20260907_2326'
sys.path.insert(0,str(API))
from brrs_suite_case import checked,sha,save
from brrs_suite_results import assess
from brrs_cir_log_to_csv_plot import parse_log,validate_raw_log,validate_firmware_summary,parse_summary_log,summarize

def read(p): return json.loads(Path(p).read_text())
def csvout(name,rows):
    assert rows,name
    keys=list(dict.fromkeys(k for row in rows for k in row))
    with (OUT/name).open('w',newline='') as f:
        w=csv.DictWriter(f,fieldnames=keys);w.writeheader();w.writerows(rows)
def kv(line): return {k:v for k,v in (s.split('=',1) for s in line.split(',')[1:] if '=' in s)}
def marker(lines,prefix):
    found=[l for l in lines if l.startswith(prefix)]
    assert len(found)==1,(prefix,len(found));return kv(found[0])
def figsave(fig,name):
    for ext in ['png','svg']:fig.savefig(OUT/f'{name}.{ext}',dpi=180,bbox_inches='tight')
    plt.close(fig)
plt.rcParams.update({'font.size':10,'axes.spines.top':False,'axes.spines.right':False,'svg.fonttype':'none'})
COLORS={4:'#D0781F',8:'#2865A5'}
inputs={}
def track(p):
    p=Path(p).resolve();inputs[str(p)]=sha(p);return p

current=read(track(RUN/'observations.json'))
plan=read(track(RUN/'ACTIVE_PLAN.json'))
assert len(current)==len(plan['cases'])==40
old=read(track(SMOKE/'RESULTS.json'))
grid=read(track(LEADS/'observations.json'))
network=read(track(LEADS/'rotation_observations.json'))
groups={}
def register(bundle,cohort):groups.setdefault(str(Path(bundle).resolve()),[]).append(cohort)
for a in current.values():register(a['bundle'],'current_onepass')
for v in old['new_runs'].values():register(v['assessment']['bundle'],'prior_smoke')
for v in grid.values():register(v['bundle'],'prior_stage0_grid')
for a in network.values():register(a['bundle'],'prior_network_lead')
register(BASE/'logs/exp4_pac4_lead27_cable_recheck_20260908_0018/capture1','prior_cable_recheck')
register(BASE/'logs/exp4_nlos69_s6_pac8_recheck_20260907_2248/capture1','prior_original_baseline')

evidence={};cases={};raw={};ledger=[]
for i,(path,cohorts) in enumerate(groups.items(),1):
    b=Path(path);c=checked(b);a=assess(b)
    evidence[path]=a;cases[path]=c
    for p in ['payload_hashes.json','case.json','board_manifest.json','results/orchestration.json']:track(b/p)
    raw[path]={}
    for j in c['jobs']:
        side='local' if j['host']=='local' else 'remote';p=b/'results'/side/(j['physical_role']+'.log')
        raw[path][j['physical_role']]=track(p).read_text().splitlines()
        track(p.with_suffix('.meta.txt'));track(b/'results'/side/'status.json')
    ledger.append({'bundle':path,'cohorts':';'.join(cohorts),'case_id':c['id'],'verdict':a['verdict'],
        'payload_index_sha256':a['payload_index_sha256'],'manifest_sha256':c['manifest_file_sha256'],
        'source_C_sha256':json.dumps(c['firmware_source_sha256'],sort_keys=True)})
    if i%20==0:print('VALIDATED',i,'/',len(groups),flush=True)
csvout('evidence_ledger.csv',ledger)
save(OUT/'revalidated_assessments.json',evidence)

case_rows=[];node_rows=[];slot_rows=[];error_rows=[];timing_rows=[]
for cid,saved in current.items():
    path=saved['bundle'];a=evidence[path];c=cases[path];p=c['conditions'];rx=raw[path]['init']
    assert saved['nodes_by_serial']==a['nodes_by_serial'],cid
    assert a['verdict']==saved['verdict']
    control=read(Path(path)/'results/orchestration.json')
    common={'case_id':cid,'stage':p['stage'],'M':p['preamble'],'PAC':p['rx_pac'],'lead_us':p['lead_us'],
        'physical_TX':len(c['jobs'])-1,'slots_per_SF':len(p['slot_owners']) if p['stage']=='exp4' else '',
        'rotation_index':p.get('rotation_index',0),'bundle':path}
    offered=sum(n['offered'] for n in a['nodes_by_serial'].values());received=sum(n['rx'] for n in a['nodes_by_serial'].values())
    row={**common,'started_at_utc':control['started_at'],'finished_at_utc':control['finished_at'],
        'offered':offered,'rx':received,'lost':offered-received,'aggregate_PER_percent':100*(offered-received)/offered,
        'worst_node_PER_percent':a['worst_node_per_percent'],'verdict':a['verdict'],
        'max_node_Wilson95_upper_percent':max(n['per_wilson95_percent'][1] for n in a['nodes_by_serial'].values())}
    for serial,n in a['nodes_by_serial'].items():
        nr={**common,'physical_serial':serial,**n,'lost':n['offered']-n['rx'],
            'PER_CI95_low':n['per_wilson95_percent'][0],'PER_CI95_high':n['per_wilson95_percent'][1]}
        nr.pop('per_wilson95_percent');node_rows.append(nr)
    if p['stage']=='exp4':
        cfg=marker(rx,'EXP4_CONFIG_CSV,');e2e=marker(rx,'EXP4_SYNC_PREP_E2E_CSV,');first=marker(rx,'EXP4_FIRST_RX_ARM_CSV,')
        hot=marker(rx,'EXP4_HOT_PATH_CSV,');burst=marker(rx,'EXP4_BURST_CSV,')
        tx_total=sum(n['tx_success'] for n in a['nodes_by_serial'].values())
        assert tx_total==offered and all(n['beacon_missed']==n['delayed_tx_late']==0 for n in a['nodes_by_serial'].values())
        row.update(a['aggregate']);row['app_goodput_kbps']=a['aggregate']['app_goodput_bps']/1000
        row['app_payload_efficiency_over_elapsed_percent']=100*a['aggregate']['app_goodput_bps']/6_800_000
        row['DATA_frame_payload_fraction_percent']=100*(8*16/6.8)/int(cfg['frame_airtime_us'])
        row['configured_DATA_rx_windows_fraction_percent']=100*len(p['slot_owners'])*p['rx_window_us']/int(cfg['superframe_us'])
        row['timing_max_slots']=int(cfg['max_slots']);row['tx_success']=tx_total
        for s in a['slots']:
            s={k:int(v) if isinstance(v,str) and v.isdigit() else v for k,v in s.items()}
            assert s['rx_good']+s['error']+s['timeout']==s['attempted']==1000
            slot_rows.append({**common,**s,'min_arm_slack_if_measured_us':s['min_arm_slack_us'] if s['slack_samples'] else None})
        er=next(l for l in rx if l.startswith('RX timeouts='))
        err={k:int(v) for k,v in re.findall(r'([a-z-]+)=(\d+)',er)}
        tdma=next(l for l in rx if l.startswith('TDMA validation:'))
        err.update({'tdma_'+k:int(v) for k,v in re.findall(r'([a-z-]+)=(\d+)',tdma)})
        for family,v in a['error_counters'].items():
            err.update({family+'_'+k:int(x) for k,x in v.items() if str(x).isdigit()})
        error_rows.append({**common,**err,'original_RX_error_line':er,'original_TDMA_line':tdma})
        t={**common,'first_RX_open_slack_min_us':int(first['rx_open_slack_min_us']),
            'sync_prep_remaining_min_us':int(e2e['remaining_lead_min_us']),
            'sync_prep_e2e_max_us':int(e2e['max_us']),'hot_path_max_us':int(hot['max_us']),
            'hot_path_p99_us':int(hot['p99_us']),'deadline_close':int(burst['deadline_close']),
            'forced_prep_close':int(burst['forced_prep_close'])}
        rx_stats=[l.split(',') for l in rx if l.startswith('BRRS_SLOT_TIMING_CSV,')]
        t['RX_RMARKER_error_min_ns']=min(int(x[3]) for x in rx_stats);t['RX_RMARKER_error_max_ns']=max(int(x[4]) for x in rx_stats)
        tx_stats=[marker(raw[path][j['physical_role']],'EXP4_TX_FIRST_ARM_CSV,') for j in c['jobs'] if j['logical_node']!=1]
        t['TX_first_RMARKER_slack_min_us']=min(int(x['data_rmarker_slack_min_us']) for x in tx_stats)
        timing_rows.append(t)
    case_rows.append(row)
csvout('cases.csv',case_rows);csvout('nodes.csv',node_rows);csvout('slots.csv',slot_rows);csvout('errors.csv',error_rows);csvout('timing.csv',timing_rows)
s6=[r for r in case_rows if r['stage']=='exp4' and r['physical_TX']==6]
assert len(s6)==16 and sum(r['offered'] for r in s6)==132000 and sum(r['rx'] for r in s6)==131999

pairs=[]
for M in [32,64,128,256]:
    for K in sorted({r['slots_per_SF'] for r in s6 if r['M']==M}):
        rows={r['PAC']:r for r in s6 if r['M']==M and r['slots_per_SF']==K}
        c4,c8=(cases[rows[p]['bundle']] for p in [4,8])
        hashes=lambda c:{(j['physical_role'],j['logical_node'],j['serial']):j['hex_sha256'] for j in c['jobs'] if j['logical_node']!=1}
        assert hashes(c4)==hashes(c8),(M,K)
        pairs.append({'M':M,'slots':K,'PAC4_lead_us':27,'PAC8_lead_us':26,'same_TX_HEX_and_assignment':True,
            'PAC4_worst_node_PER':rows[4]['worst_node_PER_percent'],'PAC8_worst_node_PER':rows[8]['worst_node_PER_percent'],
            'PAC4_goodput_kbps':rows[4]['app_goodput_kbps'],'PAC8_goodput_kbps':rows[8]['app_goodput_kbps'],
            'PAC4_lost':rows[4]['lost'],'PAC8_lost':rows[8]['lost'],'independent_runs_per_setting':1,
            'PAC4_bundle':rows[4]['bundle'],'PAC8_bundle':rows[8]['bundle']})
csvout('pac_comparisons.csv',pairs)

# CIR power quality: keep original fields; mark the -128 floor/sentinel and an explicit alpha-only estimate.
cir_rows=[];cir_summary=[];fp_evidence=[];offset=(31155-29133)/256
cir_bundles=[a['bundle'] for a in current.values() if a['conditions']['stage']=='exp2']+[str(SMOKE/'exp2_m32_pac8_l25')]
objdump='/Applications/SEGGER/SEGGER Embedded Studio 8.28/gcc/arm-none-eabi/bin/objdump'
for b in cir_bundles+[str(SMOKE/'exp5_m1024_pac32_l25')]:
    c=cases[b];j=next(j for j in c['jobs'] if j['logical_node']==1);elf=(Path(b)/j['hex']).with_suffix('.elf')
    d=subprocess.run([objdump,'-d','--disassemble=ull_calculate_first_path_power',str(elf)],capture_output=True,text=True,check=True).stdout
    assert re.search(r'<dwt_read32bitoffsetreg>\s*\n[^\n]*movs\s+r3, #0',d),b
    name=c['id']+'_fp_power.disassembly.txt';(OUT/name).write_text(d)
    fp_evidence.append({'bundle':b,'elf_sha256':sha(elf),'rx_pcode_forced_zero_in_ELF':True,'disassembly':name})
for b in cir_bundles:
    c=cases[b];a=evidence[b];p=c['conditions'];prior=b.startswith(str(SMOKE))
    rr=parse_log(Path(b)/'results/local/init.log',c['id'],'prior_smoke' if prior else 'current_onepass','6.9')
    validate_raw_log(Path(b)/'results/local/init.log',rr,1000)
    validate_firmware_summary(Path(b)/'results/local/init.log',summarize(rr,1000),parse_summary_log(Path(b)/'results/local/init.log',c['id'],'prior_smoke' if prior else 'current_onepass','6.9',1000))
    assert len(rr)==sum(n['rx'] for n in a['nodes_by_serial'].values())
    for r in rr:
        assert r['fp_snr_ratio_x1000']==r['fp_peak_power']*1000//r['noise_floor_power']
        cir_rows.append({**r,'case_id':c['id'],'PAC':p['rx_pac'],'lead_us':p['lead_us'],'physical_role':'N4','physical_serial':'1050282818',
            'prior_context':prior,'rssi_floor_or_error':r['rssi_dbm']==-128.0,'rssi_dbm_nonfloor':r['rssi_dbm'] if r['rssi_dbm']!=-128 else None,
            'fp_dbm_alpha_only_adjusted_estimate':r['fp_dbm']-offset,'fp_power_absolute_calibrated':False})
    snr=[r['fp_snr_db'] for r in rr];valid=[r['rssi_dbm'] for r in rr if r['rssi_dbm']!=-128]
    cir_summary.append({'case_id':c['id'],'M':p['preamble'],'PAC':p['rx_pac'],'lead_us':p['lead_us'],'prior_context':prior,
        'CIR_rows':len(rr),'offered':1000,'PER_percent':a['worst_node_per_percent'],'FP_SNR_mean_dB':st.mean(snr),
        'FP_SNR_median_dB':st.median(snr),'FP_SNR_p05_dB':float(np.percentile(snr,5)),'FP_SNR_p95_dB':float(np.percentile(snr,95)),
        'RSSI_floor_count':len(rr)-len(valid),'RSSI_floor_percent':100*(len(rr)-len(valid))/len(rr),
        'RSSI_raw_mean_dBm':st.mean(r['rssi_dbm'] for r in rr),'RSSI_nonfloor_mean_dBm':st.mean(valid) if valid else None,
        'FP_raw_mean_dBm':st.mean(r['fp_dbm'] for r in rr),'FP_alpha_only_estimate_dBm':st.mean(r['fp_dbm'] for r in rr)-offset,
        'accum_mean':st.mean(r['accum'] for r in rr),'scope':'successful receptions on N4 only','bundle':b})
csvout('cir_samples_quality_flags.csv',cir_rows);csvout('cir_summary.csv',sorted(cir_summary,key=lambda r:(r['M'],r['PAC'])))
save(OUT/'fp_power_evidence.json',{'alpha_difference_dB':offset,'register_mask':'0x1F00','shift':8,
    'cast_before_shift_zero_for_all_pcodes':all((((v<<8)&0x1F00)&255)>>8==0 for v in range(32)),
    'logged_DATA_RX_code_from_source':9,'raw_modified':False,'firmware_modified':False,'ELF_checks':fp_evidence})

# Previous lead measurements are context, never pooled into the current one-pass comparisons.
history=[]
for b,cohorts in groups.items():
    a=evidence[b];p=cases[b]['conditions']
    if 'prior_stage0_grid' in cohorts or ('prior_network_lead' in cohorts and p.get('sensors')==6) or 'prior_cable_recheck' in cohorts or 'prior_original_baseline' in cohorts:
        history.append({'case_id':a['case_id'],'cohort':';'.join(cohorts),'M':p['preamble'],'PAC':p['rx_pac'],
            'lead_us':p['lead_us'],'physical_TX':len(cases[b]['jobs'])-1,'slots':len(p['slot_owners']),
            'rotation_index':p.get('rotation_index',0),'worst_node_PER_percent':a['worst_node_per_percent'],'verdict':a['verdict'],'bundle':b})
csvout('historical_leads_separate_context.csv',history)

# Main figures: node-level results and explicit sample-size uncertainty.
ordered=sorted(s6,key=lambda r:(r['M'],r['slots_per_SF'],r['PAC']))
matrix=np.array([[next(n['per_percent'] for n in node_rows if n['case_id']==r['case_id'] and n['physical_role']==f'N{i}') for i in range(2,8)] for r in ordered])
fig,ax=plt.subplots(figsize=(8.2,8.2));im=ax.imshow(matrix,vmin=0,vmax=1,cmap='Blues',aspect='auto')
for i in range(16):
    for j in range(6):ax.text(j,i,f'{matrix[i,j]:g}',ha='center',va='center',fontsize=9)
ax.set_xticks(range(6),[f'N{i}' for i in range(2,8)]);ax.set_yticks(range(16),[f'M{r["M"]} / K{r["slots_per_SF"]} / P{r["PAC"]}' for r in ordered])
ax.set_title('TX6: per-node PER (%)\nOne run per setting; PAC4 lead 27 us / PAC8 lead 26 us')
fig.colorbar(im,ax=ax,label='PER (%) — acceptance threshold is 1%');figsave(fig,'tx6_node_per')
fig,axs=plt.subplots(1,2,figsize=(12,4.6));labels=[f'M{x["M"]}\nK{x["slots"]}' for x in pairs];x=np.arange(8)
for pac,dx in [(4,-.12),(8,.12)]:
    rr=[next(r for r in s6 if r['M']==p['M'] and r['slots_per_SF']==p['slots'] and r['PAC']==pac) for p in pairs]
    vals=[r['worst_node_PER_percent'] for r in rr];hi=[r['max_node_Wilson95_upper_percent'] for r in rr]
    axs[0].errorbar(x+dx,vals,yerr=[np.zeros(8),np.array(hi)-vals],fmt='o',capsize=3,color=COLORS[pac],label=f'PAC{pac}')
    axs[1].bar(x+dx,[r['app_goodput_kbps'] for r in rr],width=.23,color=COLORS[pac],label=f'PAC{pac}')
axs[0].axhline(1,color='#A33',ls='--');axs[0].set_ylabel('Worst observed node PER (%)');axs[0].set_ylim(-.05,1.1)
axs[0].set_title('Observed PER + largest node Wilson 95% upper bound')
axs[1].set_ylabel('Delivered application goodput (kbps)');axs[1].set_title('16 application bytes per report; 10 ms superframe')
for ax in axs:ax.set_xticks(x,labels);ax.legend();ax.grid(axis='y',alpha=.2)
fig.text(.5,-.03,'Packet-binomial intervals are descriptive; one run does not establish run-to-run or vehicle reliability.',ha='center',fontsize=9)
fig.tight_layout();figsave(fig,'tx6_per_and_goodput')

fig,axs=plt.subplots(1,2,figsize=(12,4.8));x=np.arange(4)
for pac,dx in [(4,-.16),(8,.16)]:
    data=[[r['fp_snr_db'] for r in cir_rows if int(r['plen'])==M and r['PAC']==pac] for M in [32,64,128,256]]
    bp=axs[0].boxplot(data,positions=x+dx,widths=.25,patch_artist=True,showfliers=False,manage_ticks=False)
    for patch in bp['boxes']:patch.set_facecolor(COLORS[pac]);patch.set_alpha(.6)
    vals=[next(v for v in cir_summary if v['M']==M and v['PAC']==pac)['RSSI_floor_percent'] for M in [32,64,128,256]]
    axs[1].bar(x+dx,vals,width=.3,color=COLORS[pac],label=f'PAC{pac}')
axs[0].set_ylabel('CIR FP-window peak / pre-FP noise (dB)');axs[0].set_title('Exp2: successful receptions only, physical N4')
axs[1].set_ylabel('RSSI floor / error values (%)');axs[1].set_title('Do not average -128 dBm as measured power');axs[1].legend()
for ax in axs:ax.set_xticks(x,['M32','M64','M128','M256']);ax.grid(axis='y',alpha=.2)
fig.text(.5,-.04,'Orange = PAC4/27 us; blue = PAC8/26 us. M32/PAC8 reuses the earlier 25 us run (different context).',ha='center',fontsize=9)
fig.tight_layout();figsave(fig,'exp2_snr_and_rssi_quality')

fig,axs=plt.subplots(1,2,figsize=(11.5,4.5))
for ax,kind,title in [(axs[0],'prior_stage0_grid','Earlier Stage0: physical N4 only'),(axs[1],'prior_network_lead','Earlier TX6 / 13 slots / rotation block 2')]:
    for pac in [4,8]:
        rr=sorted([r for r in history if kind in r['cohort'] and r['PAC']==pac],key=lambda r:r['lead_us'])
        ax.scatter([r['lead_us'] for r in rr],[r['worst_node_PER_percent'] for r in rr],color=COLORS[pac],marker='o' if pac==4 else 'x',s=42,label=f'PAC{pac}')
    ax.axhline(1,ls='--',color='#A33');ax.set_yscale('symlog',linthresh=.1);ax.set_ylim(-.01,120);ax.set_xlabel('Lead margin (us)');ax.set_ylabel('Worst node PER (%)');ax.set_title(title);ax.legend();ax.grid(alpha=.2)
fig.text(.5,-.04,'Separate historical runs; unmeasured leads are not interpolated. Cable-report recheck is excluded from this panel.',ha='center',fontsize=9)
fig.tight_layout();figsave(fig,'historical_lead_context')

# Refresh Exp3 and Exp5 analysis from the same previously validated raw files.
e3=[SMOKE/f'exp3_m32_pac8_l25_{v}' for v in 'ABC'];e3logs=[str(b/'results'/side/(role+'.log')) for b in e3 for side,role in [('local','init'),('remote','N4')]]
with (OUT/'exp3_analysis.log').open('w') as f:
    subprocess.run([sys.executable,str(API/'brrs_exp3_exttxe_analyze.py'),*e3logs,'-o',str(OUT/'exp3'),'--prefix','exp3_reused'],stdout=f,stderr=subprocess.STDOUT,check=True)
with (OUT/'exp5_analysis.log').open('w') as f:
    subprocess.run([sys.executable,str(API/'brrs_exp5_channel_characterize.py'),str(SMOKE/'exp5_m1024_pac32_l25/results/local/init.log'),'--out-csv',str(OUT/'exp5_channel.csv')],stdout=f,stderr=subprocess.STDOUT,check=True)
e5=list(csv.DictReader((OUT/'exp5_channel.csv').open()));assert len(e5)==30
width=[float(r['observed_pdp_rms_width_ns']) for r in e5]
fig,ax=plt.subplots(figsize=(8,3.8));ax.plot(range(1,31),width,'o-',color=COLORS[8]);ax.set(xlabel='Successful raw-CIR frame',ylabel='Observed PDP RMS width (ns)',title='Exp5: receiver response + channel; not propagation-only delay spread');ax.grid(alpha=.2);figsave(fig,'exp5_observed_pdp_width')

summary={'created_at':datetime.now().astimezone().isoformat(),'hardware_or_network_used':False,'source_bundles_revalidated':len(groups),
    'current_cases':40,'current_node_rows':len(node_rows),'current_exp4_slot_rows':len(slot_rows),'TX6_cases':16,'TX6_offered':132000,'TX6_rx':131999,
    'TX6_all_nodes_pass':all(r['worst_node_PER_percent']<1 for r in s6),'TX6_worst_node_PER_percent':max(r['worst_node_PER_percent'] for r in s6),
    'PAC_pairs_same_TX_HEX':all(p['same_TX_HEX_and_assignment'] for p in pairs),'cir_summary':cir_summary,
    'TX6_min_first_RX_open_slack_us':min(r['first_RX_open_slack_min_us'] for r in timing_rows if r['physical_TX']==6),
    'TX6_min_sync_prep_remaining_us':min(r['sync_prep_remaining_min_us'] for r in timing_rows if r['physical_TX']==6),
    'TX6_min_TX_first_RMARKER_slack_us':min(r['TX_first_RMARKER_slack_min_us'] for r in timing_rows if r['physical_TX']==6),
    'TX6_RX_timing_range_ns':[min(r['RX_RMARKER_error_min_ns'] for r in timing_rows if r['physical_TX']==6),max(r['RX_RMARKER_error_max_ns'] for r in timing_rows if r['physical_TX']==6)],
    'exp5_observed_PDP_RMS_ns':{'mean':st.mean(width),'min':min(width),'max':max(width),'n':30},
    'scope_skipped':plan['scope_change']['skipped_unmeasured_case_ids'],'paper_repetitions_complete':False,'vehicle_measured':False}
save(OUT/'SUMMARY.json',summary)
for p in [API/'brrs_suite_results.py',API/'brrs_cir_log_to_csv_plot.py',API/'brrs_exp3_exttxe_analyze.py',API/'brrs_exp5_channel_characterize.py',
          API/'Shared/dwt_uwb_driver/dw3000/dw3000_device.c',API/'Shared/dwt_uwb_driver/deca_rsl.c',
          API/'Src/examples/ex_35a_brrs_init/brrs_init.c',API/'Src/examples/ex_35b_brrs_normal/brrs_normal.c']:track(p)
assert all(sha(p)==h for p,h in inputs.items()),'input changed during offline analysis'
save(OUT/'input_hashes.json',inputs)
print('POSTPROCESS_COMPLETE',json.dumps({k:v for k,v in summary.items() if k not in ['cir_summary','scope_skipped']},ensure_ascii=False),flush=True)
