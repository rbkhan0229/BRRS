#!/usr/bin/env python3
"""Full, standard, essential, lite, and legacy paper experiment profiles.

This planner performs no hardware I/O. ``paper`` is retained only as a
backward-compatible profile name for existing manifests and bundles.
"""
import copy
import hashlib
import json

STAGES = ['stage0', 'exp1', 'exp2', 'exp3', 'exp4', 'exp5']
PUBLICATION_PROFILES = ('full', 'standard', 'essential', 'lite', 'paper')
PROFILE_CHOICES = ('preparation', *PUBLICATION_PROFILES)


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True,
                                     separators=(',', ':')).encode()).hexdigest()


def is_publication_profile(profile):
    return profile in PUBLICATION_PROFILES


def _default_filters(m):
    from brrs_suite_manifest import stage0_configs
    links = m.get('cir_link_tx_roles', [m['single_link_tx_role']])
    return {
        'stage0': {'config_ids': [c['id'] for c in stage0_configs(m)]},
        'exp1': {'preambles': list(m['exp1']['preambles']), 'pacs': list(m['pacs'])},
        'exp2': {'preambles': list(m['exp2']['preambles']), 'pacs': list(m['pacs']),
                 'links': list(links)},
        'exp3': {'variants': list(m['exp3']['variants'])},
        'exp4': {'preambles': list(m['exp4']['preambles']), 'pacs': list(m['pacs'])},
        'exp5': {'links': list(links)},
    }


def profile_config(m, profile):
    """Return one configured profile without mutating historical manifests."""
    if profile not in PUBLICATION_PROFILES:
        raise ValueError('unknown publication profile: ' + str(profile))
    if profile == 'paper':
        if 'paper' in m:
            return m['paper']
        profiles = m.get('profiles', {})
        if 'full' in profiles:
            return profiles['full']
        raise ValueError('legacy paper/full profile configuration missing')
    profiles = m.get('profiles')
    if profiles and profile in profiles:
        return profiles[profile]
    if profile == 'full' and 'paper' in m:
        return m['paper']
    raise ValueError(f'{profile} profile configuration missing')


def profile_filters(m, config):
    defaults = _default_filters(m)
    supplied = config.get('stage_filters', {})
    return {stage: copy.deepcopy(supplied.get(stage, defaults[stage])) for stage in STAGES}


def _validate_profile(m, name, config):
    from brrs_suite_manifest import max_slots

    counts = config['repeats_by_stage']
    disabled = config.get('disabled_stages', [])
    if (not isinstance(disabled, list) or len(set(disabled)) != len(disabled) or
            any(stage not in STAGES or stage == 'stage0' for stage in disabled)):
        raise ValueError(f'invalid {name} disabled stages')
    if (set(counts) != set(STAGES) or
            any(type(x) is not int or not 0 <= x <= 100 for x in counts.values()) or
            {stage for stage, repetitions in counts.items() if repetitions == 0} != set(disabled)):
        raise ValueError(f'{name} repeats must specify every stage; only declared disabled stages may be zero')
    if counts['stage0'] != 1:
        raise ValueError(f'{name} Stage0 grid must run exactly once')
    if type(config['stage0_confirmation_repeats']) is not int or not 1 <= config['stage0_confirmation_repeats'] <= 100:
        raise ValueError(f'invalid {name} Stage0 confirmation repetitions')

    active = config['active_tx_counts']
    if (not isinstance(active, list) or not active or len(set(active)) != len(active) or
            any(type(x) is not int or not 1 <= x <= 6 for x in active) or 6 not in active):
        raise ValueError(f'{name} active TX counts must be unique values in 1..6 and include S6')
    assignment = config['exp4_assignment']
    if assignment not in ['installation_cyclic', 'installation_cyclic_all', 'installation_fixed']:
        raise ValueError(f'unsupported {name} Exp4 assignment')
    if assignment in ['installation_cyclic', 'installation_cyclic_all'] and counts['exp4'] % 6:
        raise ValueError(f'{name} cyclic Exp4 repeats must be complete six-block cycles')
    if config['condition_order'] not in ['alternating_rotated', 'as_listed']:
        raise ValueError(f'unsupported {name} condition order')

    filters = profile_filters(m, config)
    if set(config.get('stage_filters', filters)) != set(STAGES):
        raise ValueError(f'{name} stage filters must specify every stage')
    from brrs_suite_manifest import stage0_configs
    allowed_pacs = set(m['pacs'])
    allowed_stage0_ids = {c['id'] for c in stage0_configs(m)}
    links = set(m.get('cir_link_tx_roles', [m['single_link_tx_role']]))
    stage0_filter = filters['stage0']
    if 'config_ids' in stage0_filter:
        selected_ids = stage0_filter['config_ids']
        if (set(stage0_filter) != {'config_ids'} or not isinstance(selected_ids, list) or
                not selected_ids or len(set(selected_ids)) != len(selected_ids) or
                not set(selected_ids) <= allowed_stage0_ids):
            raise ValueError(f'invalid {name} Stage0 configuration filter')
    elif 'pacs' in stage0_filter:
        # Historical profile filters selected the M32 grid by PAC only.
        pacs = stage0_filter['pacs']
        if (set(stage0_filter) != {'pacs'} or not pacs or len(set(pacs)) != len(pacs) or
                not set(pacs) <= allowed_pacs):
            raise ValueError(f'invalid {name} Stage0 PAC filter')
    else:
        raise ValueError(f'invalid {name} Stage0 filter')
    for stage in ['exp1', 'exp2', 'exp4']:
        pacs = filters[stage]['pacs']
        if not pacs or len(set(pacs)) != len(pacs) or not set(pacs) <= allowed_pacs:
            raise ValueError(f'invalid {name} {stage} PAC filter')
    for stage in ['exp1', 'exp2']:
        preambles = filters[stage]['preambles']
        if not preambles or len(set(preambles)) != len(preambles) or not set(preambles) <= set(m[stage]['preambles']):
            raise ValueError(f'invalid {name} {stage} preamble filter')
    for stage in ['exp2', 'exp4']:
        pacs_by_preamble = filters[stage].get('pacs_by_preamble')
        if pacs_by_preamble is not None:
            preambles = filters[stage]['preambles']
            if set(pacs_by_preamble) != {str(plen) for plen in preambles}:
                raise ValueError(f'{name} {stage} per-M PAC keys must match its preambles')
            for plen in preambles:
                values = pacs_by_preamble[str(plen)]
                if (not isinstance(values, list) or not values or
                        len(set(values)) != len(values) or
                        not set(values) <= set(filters[stage]['pacs'])):
                    raise ValueError(f'invalid {name} {stage} M{plen} PAC filter')
    variants = filters['exp3']['variants']
    if not variants or len(set(variants)) != len(variants) or not set(variants) <= set(m['exp3']['variants']):
        raise ValueError(f'invalid {name} Exp3 variant filter')
    for stage in ['exp2', 'exp5']:
        selected_links = filters[stage]['links']
        if not selected_links or len(set(selected_links)) != len(selected_links) or not set(selected_links) <= links:
            raise ValueError(f'invalid {name} {stage} link filter')
    exp4_preambles = filters['exp4']['preambles']
    if (not exp4_preambles or len(set(exp4_preambles)) != len(exp4_preambles) or
            not set(exp4_preambles) <= set(m['exp4']['preambles'])):
        raise ValueError(f'invalid {name} Exp4 preamble filter')
    by_active = config.get('exp4_preambles_by_active_tx')
    if by_active is not None:
        if set(by_active) != {str(s) for s in active}:
            raise ValueError(f'{name} Exp4 per-S preamble keys must match active TX counts')
        for sensors in active:
            values = by_active[str(sensors)]
            if (not isinstance(values, list) or not values or len(set(values)) != len(values) or
                    not set(values) <= set(exp4_preambles)):
                raise ValueError(f'invalid {name} S{sensors} Exp4 preambles')
        s6_preambles = by_active['6']
    else:
        s6_preambles = exp4_preambles
    slot_counts = config['s6_slot_counts_by_preamble']
    if set(slot_counts) != {str(x) for x in s6_preambles}:
        raise ValueError(f'{name} S6 slot-count keys must match its Exp4 preambles')
    for plen in s6_preambles:
        values = slot_counts[str(plen)]
        if (not values or len(set(values)) != len(values) or
                any(type(k) is not int or not 6 <= k <= max_slots(m['exp4'], plen)
                    for k in values) or 6 not in values):
            raise ValueError(f'invalid {name} M{plen} S6 slot counts')


def validate(m):
    profiles = m.get('profiles')
    if profiles is not None:
        profile_names = set(profiles)
        if profile_names not in [
                {'full', 'essential', 'lite'},
                {'full', 'standard', 'essential', 'lite'}]:
            raise ValueError('profiles must contain full, essential, and lite, with optional standard for historical compatibility')
        for name, config in profiles.items():
            _validate_profile(m, name, config)
        full = profiles['full']
        standard = profiles.get('standard')
        essential = profiles['essential']
        lite = profiles['lite']
        full_filters = profile_filters(m, full)
        essential_filters = profile_filters(m, essential)
        lite_filters = profile_filters(m, lite)
        if (lite_filters != essential_filters or
                lite['active_tx_counts'] != essential['active_tx_counts'] or
                lite['s6_slot_counts_by_preamble'] != essential['s6_slot_counts_by_preamble']):
            raise ValueError('lite must use exactly the essential condition set')
        if (set(lite['repeats_by_stage'].values()) != {1} or
                lite['stage0_confirmation_repeats'] != 1 or
                lite['exp4_assignment'] != 'installation_fixed'):
            raise ValueError('lite must run exactly one fixed-installation block')
        if full['exp4_assignment'] != 'installation_cyclic' or essential['exp4_assignment'] != 'installation_cyclic':
            raise ValueError('full and essential require complete logical rotations')
        if standard is not None:
            if standard['exp4_assignment'] != 'installation_cyclic_all':
                raise ValueError('standard requires all-link Exp4 rotations')
            if (standard.get('disabled_stages') != ['exp1'] or
                    standard['repeats_by_stage']['exp1'] != 0 or
                    standard['repeats_by_stage']['exp3'] != 1 or
                    standard['repeats_by_stage']['exp4'] != 12 or
                    standard['active_tx_counts'] != [1, 2, 3, 4, 5, 6]):
                raise ValueError('standard must fold Exp1 into Exp4 S1, retain one Exp3 block, and run two complete S1..S6 rotation cycles')
            expected_standard_exp4 = {
                '1': [32, 64, 128, 256],
                '2': [32, 256], '3': [32, 256], '4': [32, 256],
                '5': [32, 256], '6': [32, 256],
            }
            if standard.get('exp4_preambles_by_active_tx') != expected_standard_exp4:
                raise ValueError('standard Exp4 must cover every M at S1 and M32/M256 at S2..S6')
            expected_standard_pacs = {
                '32': [4, 8], '64': [8], '128': [8], '256': [8],
            }
            if standard['stage_filters']['exp4'].get('pacs_by_preamble') != expected_standard_pacs:
                raise ValueError('standard Exp4 must use PAC4 only for M32 and PAC8 for every M')
            if standard['stage_filters']['exp2'].get('pacs_by_preamble') != {
                    '32': [4, 8], '256': [8]}:
                raise ValueError('standard Exp2 must use PAC4 only for M32')
        def stage0_ids(filters):
            selected = filters['stage0']
            if 'config_ids' in selected:
                return set(selected['config_ids'])
            return {c['id'] for c in stage0_configs(m)
                    if c['preamble'] == 32 and c['pac'] in selected['pacs']}
        if not stage0_ids(essential_filters).issubset(stage0_ids(full_filters)):
            raise ValueError('essential Stage0 conditions must be a subset of full')
        if any(not set(essential_filters[stage][field]).issubset(full_filters[stage][field])
               for stage, fields in {
                   'exp1': ['preambles', 'pacs'],
                   'exp2': ['preambles', 'pacs', 'links'], 'exp3': ['variants'],
                   'exp4': ['preambles', 'pacs'], 'exp5': ['links']}.items()
               for field in fields):
            raise ValueError('essential conditions must be a subset of full')
        if not set(essential['active_tx_counts']).issubset(full['active_tx_counts']):
            raise ValueError('essential active TX counts must be a subset of full')
        for preamble, counts in essential['s6_slot_counts_by_preamble'].items():
            if not set(counts).issubset(full['s6_slot_counts_by_preamble'][preamble]):
                raise ValueError('essential S6 loads must be a subset of full')
        if any(not (full['repeats_by_stage'][stage] >= essential['repeats_by_stage'][stage] >= 1)
               for stage in STAGES):
            raise ValueError('profile repetitions must follow full >= essential >= lite')
        if full['stage0_confirmation_repeats'] < essential['stage0_confirmation_repeats']:
            raise ValueError('full confirmation repetitions must cover essential')
    if 'paper' in m:
        _validate_profile(m, 'paper', m['paper'])


def assignments(m, sensors, block, mode='installation_cyclic'):
    """Map fixed installed boards to logical roles; never move hardware."""
    roles = ['N2', 'N3', 'N4', 'N5', 'N6', 'N7']
    if sensors == 1 and mode != 'installation_cyclic_all':
        return [m['single_link_tx_role']]
    if mode == 'installation_fixed':
        return roles[:sensors]
    return [roles[(i + block - 1) % 6] for i in range(sensors)]


def _selected(m, stage, case, filters):
    p = case['conditions']
    selected = filters[stage]
    if stage == 'stage0':
        if 'config_ids' in selected:
            from brrs_suite_manifest import stage0_config_id
            return stage0_config_id(p['preamble'], p['rx_pac']) in selected['config_ids']
        return p['preamble'] == 32 and p['rx_pac'] in selected['pacs']
    if stage == 'exp1':
        return p['preamble'] in selected['preambles'] and p['rx_pac'] in selected['pacs']
    if stage == 'exp2':
        link = p.get('link_tx_role', m['single_link_tx_role'])
        by_preamble = selected.get('pacs_by_preamble', {})
        allowed_pacs = by_preamble.get(str(p['preamble']), selected['pacs'])
        return (p['preamble'] in selected['preambles'] and p['rx_pac'] in allowed_pacs
                and link in selected['links'])
    if stage == 'exp3':
        return p['variant'] in selected['variants']
    if stage == 'exp4':
        by_preamble = selected.get('pacs_by_preamble', {})
        allowed_pacs = by_preamble.get(str(p['preamble']), selected['pacs'])
        return p['preamble'] in selected['preambles'] and p['rx_pac'] in allowed_pacs
    if stage == 'exp5':
        return p.get('link_tx_role', m['single_link_tx_role']) in selected['links']
    raise ValueError('unknown stage')


def plan(m, stage, capacity_candidates=False, confirmation=False, profile='full'):
    from brrs_suite_manifest import plan as base_plan, capacity_counts

    validate(m)
    config = profile_config(m, profile)
    filters = profile_filters(m, config)
    result = []
    repeats = config['stage0_confirmation_repeats'] if confirmation else config['repeats_by_stage'][stage]
    for block in range(1, repeats + 1):
        batch = []
        sensors_to_run = config['active_tx_counts'] if stage == 'exp4' else [m['exp4']['sensors']]
        for sensors in sensors_to_run:
            configured = copy.deepcopy(m)
            if stage == 'exp4':
                exp4 = configured['exp4']
                exp4['sensors'] = sensors
                exp4.pop('capacity_search', None)
                by_active = config.get('exp4_preambles_by_active_tx', {})
                exp4_preambles = by_active.get(str(sensors), filters['exp4']['preambles'])
                exp4['preambles'] = list(exp4_preambles)
                configured['pacs'] = list(filters['exp4']['pacs'])
                exp4['slot_counts_by_preamble'] = {
                    str(plen): ([sensors] if sensors < 6 else
                                capacity_counts({**m['exp4'], 'sensors': 6}, plen)
                                if capacity_candidates else
                                config['s6_slot_counts_by_preamble'][str(plen)])
                    for plen in exp4_preambles
                }
                if sensors < 6:
                    exp4['sequences_by_preamble_slotcount'] = {}
                physical = assignments(m, sensors, block, config['exp4_assignment'])
                if sensors == 1:
                    configured['single_link_tx_role'] = physical[0]
                for logical, role in enumerate(physical, 2):
                    configured['boards'][f'N{logical}'] = copy.deepcopy(m['boards'][role])
            raw = [case for case in base_plan(configured, stage)
                   if _selected(m, stage, case, filters)]
            if confirmation:
                raw = [case for case in raw
                       if case['conditions']['lead_us'] in
                       confirmation_leads(m, case['conditions']['preamble'],
                                          case['conditions']['rx_pac'])]
            for case in raw:
                conditions = case['conditions']
                cyclic = config['exp4_assignment'] in ['installation_cyclic', 'installation_cyclic_all']
                rotation = ((block - 1) % 6 if stage == 'exp4' and
                            (sensors > 1 or config['exp4_assignment'] == 'installation_cyclic_all') and
                            cyclic else 0)
                conditions.update(run=block, profile=profile, rotation_index=rotation,
                                  phase='confirmation' if confirmation else 'main')
                case['condition_id'] = case['id'] + (f'_s{sensors}' if stage == 'exp4' else '') + ('_confirmation' if confirmation else '')
                case['id'] = f'{profile}_' + case['condition_id'] + f'_b{block:02d}'
                for job in case['jobs']:
                    physical_role = next(role for role, board in m['boards'].items()
                                         if board['serial'] == job['serial'])
                    job['physical_role'] = physical_role
                    job['location'] = m['boards'][physical_role]['location']
                    index = 5 if stage == 'exp4' else 3 if stage == 'exp5' else 4
                    job['argv'][index] = str(block)
                    job['build_only_argv'][index] = str(block)
                conditions['active_physical_roles'] = [job['physical_role'] for job in case['jobs']
                                                       if job['logical_node'] != 1]
                case['conditions_sha256'] = hashlib.sha256(
                    json.dumps(conditions, sort_keys=True).encode()).hexdigest()
                case['inactive_tx_roles'] = [role for role in ['N2', 'N3', 'N4', 'N5', 'N6', 'N7']
                                             if role not in conditions['active_physical_roles']]
                case['assignment_sha256'] = digest([
                    (job['physical_role'], job['logical_node'], job['serial'], job['location'])
                    for job in case['jobs']])
                for job in case['jobs']:
                    job['environment'].update(
                        BRRS_SUITE_MANIFEST_SHA256=digest(m),
                        BRRS_SUITE_CONDITIONS_SHA256=case['conditions_sha256'],
                        BRRS_SUITE_CASE_ID=case['id'],
                        BRRS_SUITE_PHYSICAL_ROLE=job['physical_role'],
                        BRRS_SUITE_LOGICAL_NODE=str(job['logical_node']),
                        BRRS_SUITE_PROFILE=profile,
                        BRRS_SUITE_BLOCK=str(block),
                        BRRS_SUITE_ROTATION_INDEX=str(rotation),
                        BRRS_SUITE_LOCATION=job['location'],
                        BRRS_SUITE_ASSIGNMENT_SHA256=case['assignment_sha256'])
                batch.append(case)
        if config['condition_order'] == 'alternating_rotated':
            offset = (block - 1) % len(batch)
            batch = batch[offset:] + batch[:offset]
            if block % 2 == 0:
                batch.reverse()
        result.extend(batch)
    return result


def confirmation_leads(m, preamble, pac=None):
    from brrs_suite_manifest import lead_candidate_map, stage0_config_id
    # Historical callers passed PAC only; that always meant M32.
    if pac is None:
        pac, preamble = preamble, 32
    candidate = lead_candidate_map(m).get(stage0_config_id(preamble, pac))
    if type(candidate) is not int or candidate not in m['stage0']['leads_us']:
        raise ValueError('candidate must be a measured Stage0 grid lead')
    # A PAC/acquisition boundary can make the integer-lead response
    # non-monotonic. Reliability comes from repeats of the selected point;
    # the complete grid retains the neighbouring sensitivity shape.
    return [candidate]
