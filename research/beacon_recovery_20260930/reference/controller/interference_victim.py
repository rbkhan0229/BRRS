#!/usr/bin/env python3
"""Prepare/check/assess one isolated Exp1 victim link; no RF execution entrypoint.

The supervisor exclusively owns AUX. Production campaign/7-board guards are not
changed. OFF needs a clean baseline; ON may retain complete PER100 observations.
Set BRRS_VICTIM_DEPENDENCY_CONTROLLER only for explicit offline dependency tests.
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import importlib
import json
import os
from pathlib import Path
import re
import shutil
import sys
from types import SimpleNamespace

HERE = Path(__file__).resolve().parent
VERSION = 'isolated-interference-victim-v1'
ASSESSMENT_VERSION = 'isolated-code-separation-assessment-v1'
PROFILE = 'interference_victim_diagnostic'
SERIALS = dict(zip(('init', 'N2', 'N3', 'N4', 'N5', 'N6', 'N7', 'AUX'),
    ('1050270933', '1050211584', '1050273888', '1050282818',
     '1050208509', '1050227627', '1050204212', '1050257038')))
INACTIVE = ['N2', 'N3', 'N4', 'N5', 'N6']
_DEPS = None


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def new(path, value):
    with Path(path).open('x') as stream:
        json.dump(value, stream, ensure_ascii=False, indent=2)
        stream.write('\n')


def controller_dir():
    return Path(os.environ.get('BRRS_VICTIM_DEPENDENCY_CONTROLLER', HERE)).resolve()


def dependencies():
    global _DEPS
    if _DEPS is None:
        controller = controller_dir()
        api = controller / 'sdk/Drivers/API'
        for item in (controller / 'link_diagnostic.py', api / 'brrs_exp1_verify.py',
                     api / 'brrs_channel.py', api / 'brrs_exp1_capture.sh'):
            if not item.is_file():
                raise ValueError('missing isolated controller dependency: ' + str(item))
        sys.path[:0] = [str(controller), str(api)]
        link = importlib.import_module('link_diagnostic')
        modules = {name: importlib.import_module(module) for name, module in (
            ('case', 'brrs_suite_case'), ('single', 'brrs_single_host'),
            ('evidence', 'brrs_suite_evidence'), ('verify', 'brrs_exp1_verify'),
            ('channel', 'brrs_channel'), ('paper', 'brrs_suite_paper'),
            ('wilson', 'brrs_wilson_ci'))}
        for module in (link, *modules.values()):
            if not Path(module.__file__).resolve().is_relative_to(controller):
                raise ValueError('dependency imported outside the isolated controller: ' + module.__name__)
        _DEPS = SimpleNamespace(controller=controller, api=api, link=link, **modules)
    return _DEPS


def conditions(preamble, channel, aux_state, lead_us=40, data_code=9, sync_code=10, code_diagnostic=False):
    if type(preamble) is not int or preamble not in (32, 64, 128, 256):
        raise ValueError('victim preamble must be M32, M64, M128 or M256')
    if type(channel) is not int or channel not in (5, 9):
        raise ValueError('victim channel must be CH5 or CH9')
    if aux_state not in ('off', 'on') or type(lead_us) is not int or lead_us not in (40, 44, 48):
        raise ValueError('AUX must be off/on and provisional lead must be 40, 44 or 48 us')
    if any(type(v) is not int or v not in (9, 10, 11, 12) for v in (data_code, sync_code)) or type(code_diagnostic) is not bool:
        raise ValueError('explicit supported preamble codes and boolean diagnostic required')
    if (data_code, sync_code) != (9, 10) and not code_diagnostic:
        raise ValueError('nonlegacy codes require diagnostic evidence')
    return dict(data_preamble_code=data_code, sync_preamble_code=sync_code, code_diagnostic=code_diagnostic, stage='exp1', preamble=preamble, rx_pac=8,
        beacon_preamble_symbols=512, lead_us=lead_us, tail_us=0, cycles=2000,
        variant=None, slot_owners='2', run=1, sensors=1, rx_mode='delayed',
        profile=PROFILE, diagnostic_tx_role='N7', period_us=10000,
        uwb_channel=channel, rf_config_schema_version=1,
        experiment_kind='interference_characterization', aux_state=aux_state,
        zero_rx_policy='complete_failure_evidence' if aux_state == 'on' else 'reject')


def manifest(case_id, preamble, channel, aux_state, environment, lead_us=40, data_code=9, sync_code=10, code_diagnostic=False):
    if not re.fullmatch(r'interference_victim_[A-Za-z0-9_]+', case_id or ''):
        raise ValueError('fresh case ID must begin interference_victim_')
    if not isinstance(environment, dict) or not environment:
        raise ValueError('explicit environment/provenance object required')
    return dict(schema_version=VERSION, case_id=case_id, environment=case_id,
        purpose='Independent victim capture; not Standard, Stage0, or a causal conclusion',
        conditions=conditions(preamble, channel, aux_state, lead_us, data_code, sync_code, code_diagnostic),
        boards={role: dict(serial=serial, host='local',
            location='study_position_recorded_in_environment') for role, serial in SERIALS.items()},
        aux_roles=['AUX'],
        lead_selection=dict(frozen=False, provisional=True, lead_us=lead_us,
            source='User-authorized provisional interference diagnostic; not a selected field lead'),
        authorization=dict(max_rf_runs=1, standard_inclusion=False,
            stage0_completion=False, selected_tx_logical_node=2,
            fixed_physical_mapping=True, auxiliary_control_owner='external_supervisor'),
        observations=copy.deepcopy(environment))


_legacy_manifest=manifest
def manifest(case_id,preamble,channel,aux_state,environment,lead_us=40,data_code=9,sync_code=10,code_diagnostic=False):
 m=_legacy_manifest(case_id,preamble,channel,aux_state,environment,lead_us,data_code,sync_code,code_diagnostic)
 if environment.get('holdover_study'):
  mode=environment['holdover_mode']
  if type(mode) is not int or mode not in (0,1,2):raise ValueError('holdover mode')
  m['conditions'].update(holdover_mode=mode,holdover_max_missing=1,holdover_period_us=10000)
 return m

def resolve(m, capture_script_sha256=None):
    d = dependencies()
    p = m['conditions']
    reference = manifest(m['case_id'], p['preamble'], p['uwb_channel'],
                         p['aux_state'], m['observations'], p['lead_us'], p['data_preamble_code'], p['sync_preamble_code'], p['code_diagnostic'])
    if m != reference:
        raise ValueError('manifest differs from exact isolated victim schema')
    mh, ch = d.paper.digest(m), d.paper.digest(p)
    script = d.api / 'brrs_exp1_capture.sh'
    jobs = []
    for role, logical, capture_role in [('init', 1, 'rx'), ('N7', 2, 'tx')]:
        args = [capture_role, str(p['preamble']), '1', m['environment'],
            '--lead', str(p['lead_us']), '--pac', '8', '--rx-mode', 'delayed',
            '--beacon-preamble', '512', '--uwb-channel', str(p['uwb_channel']),
            '--serial', SERIALS[role], '--data-code', str(p['data_preamble_code']),
            '--sync-code', str(p['sync_preamble_code'])]
        if p['code_diagnostic']:
            args.append('--code-diagnostic')
        env = dict(BRRS_SUITE_MANIFEST_SHA256=mh, BRRS_SUITE_CONDITIONS_SHA256=ch,
            BRRS_SUITE_CASE_ID=m['case_id'], BRRS_SUITE_PHYSICAL_ROLE=role,
            BRRS_SUITE_LOGICAL_NODE=str(logical), BRRS_SUITE_PROFILE=PROFILE,
            BRRS_RECONNECT_ATTEMPTS='0', BRRS_STRICT_CONNECTION='1')
        argv = ['bash', str(script), *args]
        jobs.append(dict(physical_role=role, logical_node=logical, serial=SERIALS[role],
            host='local', location=m['boards'][role]['location'], argv=argv,
            build_only_argv=argv + ['--build-only'], environment=env,
            source_script_sha256=capture_script_sha256 or sha(script)))
    return dict(id=m['case_id'], conditions=copy.deepcopy(p), conditions_sha256=ch,
        jobs=jobs, inactive_tx_roles=list(INACTIVE), aux_roles=['AUX'],
        diagnostic_only=True, per_node_goal_strictly_below=1.0,
        auxiliary_control_owner='external_supervisor')


def validate_bundle(bundle, index):
    d = dependencies()
    bundle = Path(bundle).resolve()
    c = d.single.load_bundle(bundle, index)
    m = json.loads((bundle / 'board_manifest.json').read_text())
    expected = resolve(m, sha(bundle / 'sdk/Drivers/API/brrs_exp1_capture.sh'))
    for key in ('id', 'conditions', 'conditions_sha256', 'inactive_tx_roles',
                'aux_roles', 'diagnostic_only', 'per_node_goal_strictly_below',
                'auxiliary_control_owner'):
        if c.get(key) != expected[key]:
            raise ValueError('case binding: ' + key)
    if c['boards'] != m['boards'] or c['manifest_file_sha256'] != sha(bundle / 'board_manifest.json'):
        raise ValueError('manifest/board binding')
    if len(c['jobs']) != 2:
        raise ValueError('exactly INIT and physical N7 capture jobs required; AUX is separate')
    for job, ref in zip(c['jobs'], expected['jobs']):
        for key in ('physical_role', 'logical_node', 'serial', 'host', 'location',
                    'environment', 'source_script_sha256'):
            if job.get(key) != ref[key]:
                raise ValueError('job binding: ' + key)
        if job['args'] != ref['argv'][2:] + ['--no-build', '--timeout', '180'] or job['script'] != 'brrs_exp1_capture.sh':
            raise ValueError('capture arguments mismatch')
        config = f'Exp1_{c["conditions"]["preamble"]}_Init' if job['logical_node'] == 1 else 'Exp1_Normal'
        if job['hex'] != f'sdk/Drivers/API/Build_Platforms/nRF52840-DK/Output/{config}/Exe/dw3000_api.hex':
            raise ValueError('unexpected Exp1 image path')
        d.channel.verify_image((bundle / job['hex']).with_suffix('.elf'),
                               d.case.NM, c['conditions']['uwb_channel'])
        if c['conditions']['code_diagnostic']:
            verify_code_image((bundle / job['hex']).with_suffix('.elf'), d.case.NM, c['conditions'])
    return c, expected


def _marker(lines, prefix):
    rows = [line for line in lines if line.startswith(prefix)]
    if len(rows) != 1:
        raise ValueError('missing/duplicate marker: ' + prefix)
    fields = [field.split('=', 1) for field in rows[0].split(',')[1:]]
    if any(len(field) != 2 for field in fields) or len(dict(fields)) != len(fields):
        raise ValueError('malformed/duplicate marker field: ' + prefix)
    return dict(fields)


def verify_code_image(path, nm, p):
    """Read retained 32-bit code settings; never trust a cache stamp alone."""
    import struct
    import subprocess
    blob = Path(path).read_bytes()
    if len(blob) < 52 or blob[:6] != b'\x7fELF\x01\x01':
        raise ValueError('code evidence requires little-endian ELF32')
    header = struct.unpack_from('<16sHHIIIIIHHHHHH', blob)
    phoff, phsize, phcount = header[5], header[9], header[10]
    if phsize < 32 or phoff + phsize * phcount > len(blob):
        raise ValueError('code ELF program table invalid')
    lines = subprocess.check_output([str(nm), '-S', '--defined-only', str(path)], text=True).splitlines()
    expected = dict(brrs_data_preamble_code=p['data_preamble_code'],
                    brrs_sync_preamble_code=p['sync_preamble_code'],
                    brrs_code_diagnostic=int(p['code_diagnostic']))
    if 'holdover_mode' in p and 'Exp1_Normal' in str(path):
        expected['brrs_ho_mode']=p['holdover_mode']
    result = {}
    for name, wanted in expected.items():
        match = [line.split() for line in lines if line.split() and line.split()[-1] == name]
        if len(match) != 1 or len(match[0]) != 4 or int(match[0][1], 16) != 4:
            raise ValueError('missing/invalid code ELF symbol: ' + name)
        address = int(match[0][0], 16)
        values = []
        for index in range(phcount):
            kind, offset, vaddr, _, size, _, _, _ = struct.unpack_from('<IIIIIIII', blob, phoff + phsize * index)
            if kind == 1 and vaddr <= address and address + 4 <= vaddr + size:
                start = offset + address - vaddr
                if start + 4 > len(blob):
                    raise ValueError('truncated code ELF segment')
                values.append(int.from_bytes(blob[start:start+4], 'little'))
        if values != [wanted]:
            raise ValueError('code ELF binding mismatch: ' + name)
        result[name] = wanted
    return result


def require_code_runtime(lines, p):
    got = _marker(lines, 'BRRS_CODE_CONFIG_CSV,')
    expected = dict(data_code=str(p['data_preamble_code']),
                    sync_code=str(p['sync_preamble_code']),
                    hardware_tx_code=str(p['sync_preamble_code']),
                    hardware_rx_code=str(p['sync_preamble_code']),
                    diagnostic='1', phase='boot_sync',
                    data_hardware='not_sampled')
    if any(got.get(k) != v for k, v in expected.items()):
        raise ValueError('compiled/SYNC hardware code runtime mismatch')
    return got


def parse_code_observations(rx, tx, received, sent, attempts, offered, predicted_attempts=0):
    def row(lines, prefix, identity, keys):
        got = _marker(lines, prefix)
        if any(got.get(k) != v for k,v in identity.items()):
            raise ValueError('code observation identity: ' + prefix)
        out = {}
        for key in keys:
            if not re.fullmatch(r'[0-9]+', got.get(key, '')):
                raise ValueError('missing/non-numeric code observation: ' + prefix + key)
            out[key] = int(got[key])
        return got, out
    _, r = row(rx, 'BRRS_CODE_RX_CLASS_CSV,',
        dict(role='INIT', scope='exp1_rxfcg_events'),
        ['total','length_reject','type_reject','protocol_reject','source_reject','candidate','accepted','duplicate'])
    if r['total'] != sum(r[k] for k in ['length_reject','type_reject','protocol_reject','source_reject','candidate']):
        raise ValueError('RXFCG mutually exclusive class sum mismatch')
    if r['candidate'] != r['accepted'] + r['duplicate'] or r['accepted'] != received:
        raise ValueError('RXFCG accepted DATA count mismatch')
    _, flags = row(rx, 'BRRS_CODE_RX_FLAGS_CSV,',
        dict(role='INIT', scope='candidate_frames_overlapping_flags'),
        ['wrong_superframe','wrong_slot_source','unexpected_destination'])
    if any(flags.values()):
        raise ValueError('unexpected target identity/SF/slot flags')
    sr, sync = row(tx, 'BRRS_CODE_SYNC_CSV,',
        dict(role='NORMAL', scope='exp1_sf_seq_1_to_target'),
        ['messages','unique','expected','missed','duplicate','out_of_range','nonmonotonic','owned_slot_scope_errors','end'])
    if sr.get('node') not in ('2','N2'):
        raise ValueError('SYNC logical node mismatch')
    if sync['expected'] != offered or sync['unique'] + sync['missed'] != offered:
        raise ValueError('SYNC offered/unique/missed mismatch')
    if sync['messages'] != sync['unique'] + sync['duplicate'] + sync['out_of_range']:
        raise ValueError('SYNC message accounting mismatch')
    if sync['duplicate'] or sync['nonmonotonic'] or sync['owned_slot_scope_errors'] or sync['out_of_range'] or sync['end'] != 1:
        raise ValueError('SYNC sequence/scope/END failure')
    if sync['unique']:
        if not all(re.fullmatch(r'[0-9]+',sr.get(k,'')) for k in ('first_seq','last_seq')):
            raise ValueError('SYNC sequence bounds missing')
        if not 1 <= int(sr['first_seq']) <= int(sr['last_seq']) <= offered:
            raise ValueError('SYNC sequence bounds invalid')
        if sync['unique'] > int(sr['last_seq']) - int(sr['first_seq']) + 1:
            raise ValueError('SYNC unique count exceeds sequence span')
    elif any(sr.get(k) != 'NA' for k in ('first_seq','last_seq')):
        raise ValueError('unobserved SYNC bounds must be NA')
    tr, account = row(tx, 'BRRS_CODE_TX_ACCOUNT_CSV,',
        dict(role='NORMAL', scope='one_owned_slot_per_sf',status='VALID'),
        ['offered','success','attempts','unsent'])
    if tr.get('node') not in ('2','N2') or account != dict(offered=offered,success=sent,attempts=attempts,unsent=offered-sent):
        raise ValueError('TX accounting mismatch')
    if not sent <= attempts <= sync['unique'] + predicted_attempts <= offered:
        raise ValueError('TX attempts exceed received unique beacons')
    return dict(rx_classes=r, rx_flags=flags, sync=sync,
        first_sync_seq=sr['first_seq'],last_sync_seq=sr['last_seq'],
        tx_account=account, rx_non_target_window_events=r['total']-r['accepted'],
        beacons_missing=sync['missed'],
        received_beacon_without_tx_attempt=sync['unique']+predicted_attempts-attempts,
        predicted_tx_attempts=predicted_attempts,
        tx_attempt_without_success=attempts-sent,
        scope='Observed aggregate counters; RX categories do not identify an external transmitter')


def outcome_accounting(*, offered, received, failure_events,
                       successful_accum_samples, aux_state):
    """Keep target PER separate from the completeness of loss classification.

    A CRC-good foreign frame can close the legacy single-attempt RX window
    without becoming target DATA or a PHY error. Those outcomes were not
    individually counted by the firmware. A residual therefore remains
    unclassified; it is never relabelled as an observed AUX frame.
    """
    values = (offered, received, failure_events, successful_accum_samples)
    if any(type(value) is not int or value < 0 for value in values):
        raise ValueError('nonnegative integer RX accounting required')
    if aux_state not in ('off', 'on') or received > offered:
        raise ValueError('invalid RX accounting scope')
    if successful_accum_samples != received:
        raise ValueError('complete successful RX/ACCUM accounting required')
    missed = offered - received
    if failure_events > missed:
        raise ValueError('RX failure events exceed missed offered outcomes')
    remaining = missed - failure_events
    if aux_state == 'off' and remaining:
        raise ValueError('complete OFF RX failure accounting required')
    return dict(
        failure_classification='PARTIAL' if remaining else 'COMPLETE',
        unclassified_remaining_outcomes=remaining,
        classification_scope=(
            'Residual is missed offered outcomes minus recorded PHY failures; '
            'foreign CRC-good frames are a possible uninstrumented path, '
            'not an identified cause or measured AUX-frame count'))


def assess_diagnostic(bundle, index=None):
    d = dependencies()
    bundle = Path(bundle).resolve()
    # Default to the executed index, never silently trust a newly resealed index.
    if index is None:
        index = json.loads((bundle / 'results/orchestration.json').read_text())['payload_index_sha256']
    c, expected = validate_bundle(bundle, index)
    p = c['conditions']
    state = json.loads((bundle / 'results/status.json').read_text())
    if state.get('status') != 'COLLECTION_AND_READBACK_PASS' or state.get('rf_runs_started') != 1 or state.get('halt_errors') != {}:
        raise ValueError('exactly one fully collected and halted victim run required')
    if set(state['workers']) != {'init', 'N7'} or set(state['recovery']) != {'init', 'N7'}:
        raise ValueError('victim worker/recovery set mismatch')
    if not state['workers']['N7']['ready_at'] <= state['all_tx_ready_at'] <= state['workers']['init']['started_at']:
        raise ValueError('INIT started before N7 READY')
    for job in c['jobs']:
        recovery = state['recovery'][job['physical_role']]
        rb = recovery.get('readback', {})
        if recovery.get('halted') is not True or rb.get('status') != 'PASS' or rb.get('serial') != job['serial'] or rb.get('hex_sha256') != job['hex_sha256']:
            raise ValueError('victim HALT/readback identity failure')
    c, raw, orch = d.evidence.read_evidence(bundle, expected)
    if orch['case_id'] != c['id'] or orch['all_tx_ready_at'] != state['all_tx_ready_at']:
        raise ValueError('orchestration identity/READY mismatch')
    side = json.loads((bundle / 'results/local/status.json').read_text())
    if side.get('case_id') != c['id']:
        raise ValueError('local control case identity')
    for role in ('init', 'N7'):
        for key in ('serial', 'exit_code', 'raw_sha256', 'started_at', 'ready_at'):
            if state['workers'][role].get(key) != side['workers'][role].get(key):
                raise ValueError('local/global worker evidence mismatch: ' + key)
        if side['workers'][role]['readback'] != state['recovery'][role]['readback']:
            raise ValueError('exported readback differs from final recovery evidence')
    args = SimpleNamespace(preamble=p['preamble'], lead=p['lead_us'], tail=0,
        pac=8, rx_mode='delayed', expected=2000, beacon_preamble=512)
    rx, tx = '\n'.join(raw['init']), '\n'.join(raw['N7'])
    rv, tv = _marker(raw['init'], 'EXP1_DONE,'), _marker(raw['N7'], 'EXP1_TX_DONE,')
    config = _marker(raw['init'], 'EXP_LOG_CONFIG_CSV,')
    expected_config = dict(experiment='1', plen=str(p['preamble']), sync_plen='512',
        lead_us=str(p['lead_us']), tail_us='0', target='2000', cir='0')
    if config != expected_config:
        raise ValueError('RX runtime experiment configuration mismatch')
    verify_rx, verify_tx = d.verify.validate_rx(rx, args), d.verify.validate_tx(tx, args)
    received, sent, attempts, offered = int(rv['rx']), int(tv['success']), int(tv['attempts']), 2000
    if not 0 <= received <= sent <= offered:
        raise ValueError('inconsistent RX/TX count')
    for job in c['jobs']:
        role = job['physical_role']
        path = bundle / 'results/local' / (role + '.meta.txt')
        fields = [line.split('=', 1) for line in path.read_text().splitlines() if '=' in line]
        meta = dict(fields)
        if len(meta) != len(fields):
            raise ValueError('duplicate metadata fields')
        expected_meta = dict(suite_profile=PROFILE, mode='exp1', role='rx' if role == 'init' else 'tx',
            preamble_symbols=str(p['preamble']), beacon_preamble_symbols='512',
            lead_us=str(p['lead_us']), tail_us='0', pac='8', rx_mode='delayed',
            expected_cycles='2000', run_number='1', environment=c['id'],
            capture_method='pylink', status='PASS', uwb_channel=str(p['uwb_channel']),
            uwb_channel_validation='PASS')
        if any(meta.get(key) != value for key, value in expected_meta.items()):
            raise ValueError('victim metadata parameters: ' + role)
        d.channel.verify_runtime('\n'.join(raw[role]), p['uwb_channel'])
        if p['code_diagnostic']:
            require_code_runtime(raw[role], p)
            for key, value in dict(data_preamble_code=p['data_preamble_code'], sync_preamble_code=p['sync_preamble_code'], code_diagnostic=1).items():
                if meta.get(key) != str(value):
                    raise ValueError('code metadata mismatch: ' + key)
    diagnostic = d.link.parse_error_accum(rx, preamble=p['preamble'])
    counters = diagnostic['error_counters']
    if counters['timeouts'] != counters['fwto'] + counters['pto'] or counters['errors'] != sum(counters[key] for key in ('sfdto', 'phe', 'fce', 'fsl')):
        raise ValueError('RX failure category totals inconsistent')
    predicted_attempts=0
    if 'holdover_mode' in p:
        hs=_marker(raw['N7'],'BRRS_HO_SUMMARY,')
        if int(hs['mode'])!=p['holdover_mode']:raise ValueError('holdover runtime mode mismatch')
        predicted_attempts=int(hs['predicted_attempts'])
    code_evidence = parse_code_observations(raw['init'], raw['N7'], received, sent, attempts, offered,predicted_attempts) if p['code_diagnostic'] else None
    classified_non_target = code_evidence['rx_non_target_window_events'] if code_evidence else 0
    accounting = outcome_accounting(offered=offered, received=received,
        failure_events=diagnostic['failure_events'] + classified_non_target,
        successful_accum_samples=diagnostic['successful_accum_samples'],
        aux_state=p['aux_state'])
    if code_evidence:
        accounting['classification_scope'] = 'Missed offered outcomes minus observed PHY failures and mutually exclusive non-target/duplicate RXFCG window events; residual remains unidentified'
    if any(int(value) == 0 for value in diagnostic['successful_accum_histogram']):
        raise ValueError('successful ACCUM zero is invalid')
    if diagnostic['totals']['hist_overflow']:
        raise ValueError('failure ACCUM histogram overflow')
    diagnostic.update(case_id=c['id'], physical_tx_role='N7', physical_tx_serial=SERIALS['N7'],
        logical_tx_node=2, offered=offered, tx_success=sent, rx=received,
        unsent=offered-sent, post_tx_loss=sent-received,
        failure_event_denominator='scheduled receive outcomes; includes slots with no victim TX')
    diagnostic.update(accounting)
    if code_evidence:
        diagnostic['code_observations'] = code_evidence
    diagnostic = json.loads(json.dumps(diagnostic))
    sidecar = bundle / 'results/VICTIM_ERROR_ACCUM.json'
    if sidecar.exists():
        if json.loads(sidecar.read_text()) != diagnostic:
            raise ValueError('existing victim error sidecar differs')
    else:
        new(sidecar, diagnostic)
    if p['aux_state'] == 'off' and received == 0:
        raise ValueError('OFF baseline has zero valid RX; error sidecar preserved')
    missed = offered - received
    per = 100 * missed / offered
    verdict = 'PASS' if missed * 100 < offered else 'FAIL_PER'
    clean = received > 0 and verdict == 'PASS'
    operational = ('CHARACTERIZATION_VALID' if p['aux_state'] == 'on' else
                   'OFF_BASELINE_PASS' if clean else 'STOP_OFF_BASELINE_PER')
    row = dict(physical_role='N7', logical_node=2, location=c['boards']['N7']['location'],
        offered=offered, tx_attempts=attempts, tx_success=sent, rx=received,
        per_percent=per, per_wilson95_percent=[100 * v for v in d.wilson.wilson_interval(missed, offered, .95)],
        unsent=offered-sent, post_tx_loss=sent-received,
        post_tx_per_percent=(100 * (sent-received) / sent if sent else None))
    return dict(case_id=c['id'], conditions=p, bundle=str(bundle), verdict=verdict,
        worst_node_per_percent=per, nodes_by_serial={SERIALS['N7']: row},
        measurement_status=('VALID_PER_PARTIAL_FAILURE_CLASSIFICATION'
            if accounting['failure_classification'] == 'PARTIAL' else 'VALID'),
        operational_status=operational,
        failure_classification=accounting['failure_classification'],
        unclassified_remaining_outcomes=accounting['unclassified_remaining_outcomes'],
        off_baseline_pass=clean if p['aux_state'] == 'off' else None,
        auxiliary_control_validation='EXTERNAL_SUPERVISOR_REQUIRED',
        auxiliary_state_summary=state.get('auxiliary'),
        stage_metrics=dict(unsent=offered-sent, post_tx_loss=sent-received,
            unclassified_remaining_outcomes=accounting['unclassified_remaining_outcomes'],
            error_accum_sidecar='VICTIM_ERROR_ACCUM.json'),
        interpretation=('Victim TX omissions present; observation is not DATA-only loss'
                        if sent < offered else 'All scheduled victim TX completed'),
        code_observations=code_evidence, error_counters=counters, existing_verifier=dict(rx=verify_rx, tx=verify_tx),
        per_criterion='Official strict PER < 1% preserved, including ON FAIL_PER100',
        assessment_scope='Victim evidence only; AUX activity/parking and paired-study inference belong to supervisor',
        lead_frozen=False, provisional_lead_us=p['lead_us'], stage0_complete=False,
        standard_inclusion=False, rf_runs_started=1, diagnostic_policy=ASSESSMENT_VERSION,
        assessment_version=ASSESSMENT_VERSION,
        payload_index_sha256=index, firmware_source_sha256=c['firmware_source_sha256'],
        source_git=c.get('source_git'),
        log_sha256_by_serial={job['serial']: sha(bundle / 'results/local' / (job['physical_role'] + '.log')) for job in c['jobs']})


def prepare(root, case_id, preamble, channel, aux_state, environment, lead_us=40):
    """Build/package only on the caller's approved source host; never use hardware."""
    if sys.platform != 'darwin':
        raise ValueError('source builds are restricted to the approved Air macOS host; Ubuntu memory hold remains')
    d = dependencies()
    root = Path(root).resolve()
    root.mkdir(parents=True, exist_ok=False)
    try:
        m = manifest(case_id, preamble, channel, aux_state, environment, lead_us)
        new(root / 'diagnostic-manifest.json', m)
        args = SimpleNamespace(manifest=root / 'diagnostic-manifest.json',
            bundle=root / 'bundle', reuse=False, source_build=True)
        d.case.prepare_resolved(args, m, resolve(m))
        for source in (Path(__file__), Path(d.link.__file__),
                       d.controller / 'vehicle_diagnostic.py', d.controller / 'air_control.py'):
            shutil.copy2(source, args.bundle / source.name)
        new(args.bundle / 'provenance/interference-victim.json', dict(version=VERSION,
            adapter_sha256=sha(__file__), dependency_controller=str(d.controller),
            hardware_preflight_completed=False, rf_started=False,
            auxiliary_control_owner='external_supervisor', production_guards_unchanged=True))
        hashes = {str(path.relative_to(args.bundle)): sha(path) for path in sorted(args.bundle.rglob('*'))
                  if path.is_file() and path.name != 'payload_hashes.json'}
        (args.bundle / 'payload_hashes.json').write_text(json.dumps(hashes, indent=2) + '\n')
        index = sha(args.bundle / 'payload_hashes.json')
        validate_bundle(args.bundle, index)
        spec = dict(case_id=case_id, bundle=str(args.bundle), payload_index_sha256=index,
            manifest_sha256=sha(args.manifest), hardware_preflight_completed=False,
            rf_started=False, auxiliary_control_owner='external_supervisor')
        new(root / 'prepared.json', spec)
        return spec
    except BaseException as exc:
        (root / 'STOP').touch()
        new(root / 'prepare-failure.json', dict(error=repr(exc), rf_started=False))
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=('prepare', 'check', 'assess'))
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--case-id')
    parser.add_argument('--preamble', type=int, choices=(32, 64, 128, 256))
    parser.add_argument('--channel', type=int, choices=(5, 9))
    parser.add_argument('--aux-state', choices=('off', 'on'))
    parser.add_argument('--environment-json', type=Path)
    parser.add_argument('--index')
    args = parser.parse_args()
    if args.command == 'prepare':
        if not all((args.case_id, args.preamble, args.channel, args.aux_state, args.environment_json)):
            parser.error('prepare requires case ID, preamble, channel, AUX state and environment JSON')
        result = prepare(args.root, args.case_id, args.preamble, args.channel,
                         args.aux_state, json.loads(args.environment_json.read_text()))
    elif args.command == 'check':
        if not args.index:
            parser.error('check requires the supervisor-pinned --index; --root is the bundle')
        c, _ = validate_bundle(args.root, args.index)
        result = dict(case_id=c['id'], valid=True, rf_started=False,
                      auxiliary_control_owner='external_supervisor')
    else:
        result = assess_diagnostic(args.root, args.index)
    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
