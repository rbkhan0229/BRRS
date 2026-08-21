#!/usr/bin/env python3
"""Characterize an Exp5 placement from strict DW3000 raw-CIR captures.

Default metrics are descriptive: observed PDP RMS width and observed
dominant-to-residual power ratio. They include the receiver/correlator impulse
response and are neither a Rician K-factor nor a propagation-only delay spread.
CLEAN is quantitative only when an independent same-PHY reference is supplied.
"""

import argparse
import csv
import math
import os
import re
import statistics
import sys
from dataclasses import dataclass, field

HEADER_RE = re.compile(
    r"^CIR_RAW_HEADER,frame=(\d+),cycle=(\d+),plen=(\d+),"
    r"sample_offset=(\d+),n_samples=(\d+),fp_sample=(\d+)$"
)
SAMPLE_RE = re.compile(r"^CIR_RAW,(\d+),(\d+),(-?\d+),(-?\d+)$")
DUMP_DONE_RE = re.compile(
    r"^CIR_RAW_DUMP_DONE,plen=(\d+),count=(\d+),samples_per_frame=(\d+)$"
)

TAP_SPACING_NS = 1000.0 / (2.0 * 499.2)
DEFAULT_THRESHOLD_DB = 6.0
DEFAULT_FAMILY_FALSE_ALARM = 0.01
NOISE_GUARD_SAMPLES = 4
NOISE_PRE_FP_SAMPLES = 24
FP_EARLY_ALLOWANCE = 4

CLEAN_TEMPLATE_HALF_WIDTH = 20
CLEAN_LOOP_GAIN = 0.9
CLEAN_MAX_ITERATIONS = 120
CLEAN_SEARCH_HALF_WINDOW_TAPS = 120


class LogFormatError(ValueError):
    pass


@dataclass
class FrameCIR:
    frame: int
    cycle: int
    plen: int
    sample_offset: int
    n_samples: int
    fp_sample: int
    taps: list = field(default_factory=list)


def _validate_frame(frame, path):
    if len(frame.taps) != frame.n_samples:
        raise LogFormatError(
            f"{path}: frame {frame.frame}: expected {frame.n_samples} CIR rows, "
            f"found {len(frame.taps)}"
        )
    indices = [row[0] for row in frame.taps]
    if indices != list(range(frame.n_samples)):
        raise LogFormatError(
            f"{path}: frame {frame.frame}: missing, duplicate, or out-of-order samples"
        )
    fp_local = frame.fp_sample - frame.sample_offset
    if not 0 <= fp_local < frame.n_samples:
        raise LogFormatError(
            f"{path}: frame {frame.frame}: first path is outside the dumped window"
        )


def parse_raw_log(path, allow_legacy_without_marker=False):
    frames = []
    current = None
    seen_frames = set()
    seen_cycles = set()
    dataset_shape = None
    dump_done = None

    def finish_current():
        nonlocal dataset_shape
        if current is None:
            return
        _validate_frame(current, path)
        if current.frame in seen_frames:
            raise LogFormatError(f"{path}: duplicate raw dump for frame {current.frame}")
        if current.cycle in seen_cycles:
            raise LogFormatError(f"{path}: duplicate raw dump for cycle {current.cycle}")
        shape = (current.plen, current.n_samples)
        if dataset_shape is None:
            dataset_shape = shape
        elif shape != dataset_shape:
            raise LogFormatError(
                f"{path}: mixed capture shape {shape}; expected {dataset_shape}"
            )
        seen_frames.add(current.frame)
        seen_cycles.add(current.cycle)
        frames.append(current)

    with open(path, "r", errors="replace") as stream:
        for line_no, raw_line in enumerate(stream, 1):
            line = raw_line.rstrip("\r\n")
            header = HEADER_RE.match(line)
            if header:
                finish_current()
                current = FrameCIR(
                    frame=int(header.group(1)), cycle=int(header.group(2)),
                    plen=int(header.group(3)), sample_offset=int(header.group(4)),
                    n_samples=int(header.group(5)), fp_sample=int(header.group(6)),
                )
                continue
            sample = SAMPLE_RE.match(line)
            if sample:
                if current is None:
                    raise LogFormatError(f"{path}:{line_no}: CIR_RAW precedes its header")
                frame_no = int(sample.group(1))
                if frame_no != current.frame:
                    raise LogFormatError(
                        f"{path}:{line_no}: sample frame {frame_no} does not match "
                        f"header frame {current.frame}"
                    )
                current.taps.append(
                    (int(sample.group(2)), int(sample.group(3)), int(sample.group(4)))
                )
                continue
            done = DUMP_DONE_RE.match(line)
            if done:
                if dump_done is not None:
                    raise LogFormatError(f"{path}:{line_no}: duplicate raw dump completion marker")
                dump_done = tuple(int(done.group(i)) for i in range(1, 4))
    finish_current()
    if not frames:
        raise LogFormatError(f"{path}: no CIR_RAW_HEADER/CIR_RAW frame was found")
    if dump_done is None:
        if not allow_legacy_without_marker:
            raise LogFormatError(
                f"{path}: CIR_RAW_DUMP_DONE is missing; capture may be truncated "
                "(use --allow-legacy-without-marker only for exploratory legacy data)"
            )
        print(
            "[warning] legacy raw log has no CIR_RAW_DUMP_DONE; whole-frame truncation "
            "cannot be excluded",
            file=sys.stderr,
        )
    else:
        done_plen, done_count, done_samples = dump_done
        if (done_plen, done_samples) != dataset_shape or done_count != len(frames):
            raise LogFormatError(
                f"{path}: raw completion marker declares plen={done_plen}, "
                f"count={done_count}, samples/frame={done_samples}; parsed "
                f"plen={dataset_shape[0]}, count={len(frames)}, "
                f"samples/frame={dataset_shape[1]}"
            )
    return frames


def mean(values):
    return sum(values) / len(values) if values else float("nan")


def stdev(values):
    return statistics.stdev(values) if len(values) >= 2 else 0.0


def noise_floor_for_frame(powers, fp_local):
    end = max(0, fp_local - NOISE_GUARD_SAMPLES)
    start = max(0, end - NOISE_PRE_FP_SAMPLES)
    values = powers[start:end]
    if len(values) < 8:
        values = powers[:max(8, len(powers) // 4)]
    if not values:
        return None
    # Complex Gaussian noise power is exponential: median / ln(2) estimates
    # its mean without letting one large tap dominate the estimate.
    return max(statistics.median(values) / math.log(2.0), 1e-9)


def cfar_threshold_db(requested_db, tested_taps, family_false_alarm):
    if not 0.0 < family_false_alarm < 1.0:
        raise ValueError("--family-false-alarm must be between 0 and 1")
    tested_taps = max(1, tested_taps)
    per_tap_pfa = 1.0 - (1.0 - family_false_alarm) ** (1.0 / tested_taps)
    multiplier = -math.log(per_tap_pfa)
    return max(requested_db, 10.0 * math.log10(multiplier))


def weighted_metrics(entries):
    if not entries:
        return None
    total = sum(power for _, power in entries)
    if total <= 0.0:
        return None
    centroid = sum(index * power for index, power in entries) / total
    variance = sum(((index - centroid) ** 2) * power for index, power in entries) / total
    peak = max(power for _, power in entries)
    residual = total - peak
    ratio_db = float("inf") if residual <= 0.0 else 10.0 * math.log10(peak / residual)
    return math.sqrt(max(0.0, variance)) * TAP_SPACING_NS, ratio_db


def build_template(complex_by_idx, peak_idx, half_width):
    peak = complex_by_idx.get(peak_idx, 0j)
    if peak == 0j:
        return None
    return [
        complex_by_idx.get(peak_idx + offset, 0j) / peak
        for offset in range(-half_width, half_width + 1)
    ]


def select_reference_template(reference_frames, half_width):
    best = None
    for frame in reference_frames:
        complex_by_idx = {idx: complex(real, imag) for idx, real, imag in frame.taps}
        powers = [real * real + imag * imag for _, real, imag in frame.taps]
        fp_local = frame.fp_sample - frame.sample_offset
        noise = noise_floor_for_frame(powers, fp_local)
        if not noise:
            continue
        peak_idx = max(complex_by_idx, key=lambda idx: abs(complex_by_idx[idx]) ** 2)
        score = abs(complex_by_idx[peak_idx]) ** 2 / noise
        template = build_template(complex_by_idx, peak_idx, half_width)
        if template is not None and (best is None or score > best[0]):
            best = (score, template, frame.frame)
    if best is None:
        raise LogFormatError("independent reference contains no usable template frame")
    return best[1], best[2]


def clean_deconvolve(complex_by_idx, search_range, template, half_width,
                     noise_floor, stop_threshold_db):
    residual = dict(complex_by_idx)
    stop_power = noise_floor * 10.0 ** (stop_threshold_db / 10.0)
    # Repeated detections at one tap are a single coherent component, not
    # independent paths. Aggregate them before computing any power metric.
    components = {}
    lo, hi = search_range
    converged = False
    iterations = 0
    for iterations in range(1, CLEAN_MAX_ITERATIONS + 1):
        candidates = [idx for idx in residual if lo <= idx <= hi]
        if not candidates:
            converged = True
            break
        peak_idx = max(candidates, key=lambda idx: abs(residual[idx]) ** 2)
        peak = residual[peak_idx]
        if abs(peak) ** 2 < stop_power:
            converged = True
            break
        gain = peak * CLEAN_LOOP_GAIN
        components[peak_idx] = components.get(peak_idx, 0j) + gain
        for offset in range(-half_width, half_width + 1):
            idx = peak_idx + offset
            if idx in residual:
                residual[idx] -= gain * template[offset + half_width]
    remaining = [abs(value) ** 2 for idx, value in residual.items() if lo <= idx <= hi]
    final_peak = max(remaining, default=0.0)
    final_db = (
        10.0 * math.log10(final_peak / noise_floor)
        if final_peak > 0.0 else float("-inf")
    )
    return components, converged, iterations, final_db


def analyze_frame(frame, requested_threshold_db, family_false_alarm,
                  clean_template=None, clean_source="disabled"):
    powers = [real * real + imag * imag for _, real, imag in frame.taps]
    complex_by_idx = {idx: complex(real, imag) for idx, real, imag in frame.taps}
    fp_local = frame.fp_sample - frame.sample_offset
    noise_floor = noise_floor_for_frame(powers, fp_local)
    if not noise_floor:
        return None

    start = max(0, fp_local - FP_EARLY_ALLOWANCE)
    threshold_db = cfar_threshold_db(
        requested_threshold_db, frame.n_samples - start, family_false_alarm
    )
    threshold_power = noise_floor * 10.0 ** (threshold_db / 10.0)
    significant = [
        (idx, max(0.0, powers[idx] - noise_floor))
        for idx in range(start, frame.n_samples)
        if powers[idx] >= threshold_power
    ]
    observed = weighted_metrics(significant)
    if observed is None:
        return None

    peak_idx = max(range(start, frame.n_samples), key=lambda idx: powers[idx])
    result = {
        "frame": frame.frame, "cycle": frame.cycle, "plen": frame.plen,
        "noise_floor_power": noise_floor, "threshold_db": threshold_db,
        "peak_tap_offset": peak_idx - fp_local,
        "n_significant_taps": len(significant),
        "observed_pdp_rms_width_ns": observed[0],
        "observed_dominant_to_residual_db": observed[1],
        "clean_source": clean_source, "clean_converged": "NA",
        "clean_iterations": 0, "clean_unique_components": 0,
        "clean_final_peak_over_noise_db": float("nan"),
        "clean_pdp_rms_width_ns": float("nan"),
        "clean_dominant_to_residual_db": float("nan"),
    }

    if clean_template is not None:
        lo = max(CLEAN_TEMPLATE_HALF_WIDTH, peak_idx - CLEAN_SEARCH_HALF_WINDOW_TAPS)
        hi = min(frame.n_samples - 1 - CLEAN_TEMPLATE_HALF_WIDTH,
                 peak_idx + CLEAN_SEARCH_HALF_WINDOW_TAPS)
        components, converged, iterations, residual_db = clean_deconvolve(
            complex_by_idx, (lo, hi), clean_template, CLEAN_TEMPLATE_HALF_WIDTH,
            noise_floor, threshold_db,
        )
        result.update({
            "clean_converged": "PASS" if converged else "FAIL",
            "clean_iterations": iterations,
            "clean_unique_components": len(components),
            "clean_final_peak_over_noise_db": residual_db,
        })
        if converged:
            metrics = weighted_metrics(
                [(idx, abs(amplitude) ** 2) for idx, amplitude in components.items()]
            )
            if metrics is not None:
                result["clean_pdp_rms_width_ns"] = metrics[0]
                result["clean_dominant_to_residual_db"] = metrics[1]
    return result


def print_summary(name, values):
    finite = [value for value in values if math.isfinite(value)]
    if not finite:
        print(f"{name}: no finite estimates")
        return
    print(
        f"{name}: mean={mean(finite):.2f}, std={stdev(finite):.2f}, "
        f"min={min(finite):.2f}, max={max(finite):.2f} (n={len(finite)})"
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("raw_log", help="Exp2/Exp5 INIT raw RTT log")
    parser.add_argument("--threshold-db", type=float, default=DEFAULT_THRESHOLD_DB)
    parser.add_argument("--family-false-alarm", type=float,
                        default=DEFAULT_FAMILY_FALSE_ALARM)
    parser.add_argument("--reference-log",
                        help="independent same-PHY single-path reference CIR log")
    parser.add_argument("--diagnostic-self-template", action="store_true",
                        help="run confounded self-template CLEAN for debugging only")
    parser.add_argument("--allow-legacy-without-marker", action="store_true",
                        help="analyze old complete-frame logs without a dump marker; "
                             "whole-frame truncation cannot be detected")
    parser.add_argument("--out-csv", help="write strict per-frame results")
    args = parser.parse_args()
    if args.reference_log and args.diagnostic_self_template:
        parser.error("choose either --reference-log or --diagnostic-self-template")

    try:
        frames = parse_raw_log(args.raw_log, args.allow_legacy_without_marker)
        clean_template = None
        clean_source = "disabled"
        reference_frame = None
        if args.reference_log:
            if os.path.realpath(args.reference_log) == os.path.realpath(args.raw_log):
                raise LogFormatError("reference log must be independent of the measurement log")
            reference_frames = parse_raw_log(
                args.reference_log, args.allow_legacy_without_marker
            )
            if any(frame.plen != frames[0].plen for frame in reference_frames):
                raise LogFormatError("reference and measurement preamble lengths differ")
            if any(frame.n_samples != frames[0].n_samples for frame in reference_frames):
                raise LogFormatError("reference and measurement CIR window sizes differ")
            clean_template, reference_frame = select_reference_template(
                reference_frames, CLEAN_TEMPLATE_HALF_WIDTH
            )
            clean_source = "independent_reference"
        elif args.diagnostic_self_template:
            clean_source = "self_template_diagnostic"
    except (OSError, LogFormatError, ValueError) as exc:
        print(f"[error] {exc}", file=sys.stderr)
        return 1

    results = []
    for frame in frames:
        template = clean_template
        if args.diagnostic_self_template:
            values = {idx: complex(real, imag) for idx, real, imag in frame.taps}
            peak_idx = max(values, key=lambda idx: abs(values[idx]) ** 2)
            template = build_template(values, peak_idx, CLEAN_TEMPLATE_HALF_WIDTH)
        result = analyze_frame(
            frame, args.threshold_db, args.family_false_alarm, template, clean_source
        )
        if result is not None:
            results.append(result)

    if len(results) != len(frames):
        print(f"[error] {len(frames) - len(results)} complete frames produced no usable PDP",
              file=sys.stderr)
        return 1

    print(
        f"[info] strict raw-CIR validation PASS: frames={len(frames)}, "
        f"samples/frame={frames[0].n_samples}, plen={frames[0].plen}"
    )
    print(f"[info] effective CFAR threshold: {mean([r['threshold_db'] for r in results]):.2f} dB")
    print("\n-- observed PDP (receiver response + propagation channel) --")
    print_summary("RMS width (ns)", [r["observed_pdp_rms_width_ns"] for r in results])
    print_summary("Dominant-to-residual ratio (dB)",
                  [r["observed_dominant_to_residual_db"] for r in results])

    if clean_template is not None or args.diagnostic_self_template:
        converged = [r for r in results if r["clean_converged"] == "PASS"]
        print(f"\n-- CLEAN ({clean_source}) --")
        print(f"Convergence: {len(converged)}/{len(results)}; rejected={len(results)-len(converged)}")
        print_summary("RMS width (ns)", [r["clean_pdp_rms_width_ns"] for r in converged])
        print_summary("Dominant-to-residual ratio (dB)",
                      [r["clean_dominant_to_residual_db"] for r in converged])
        if reference_frame is not None:
            print(f"[info] template selected from independent reference frame {reference_frame}")
        if args.diagnostic_self_template:
            print("[warning] self-template CLEAN is confounded by channel multipath")

    print("\n[note] Dominant-to-residual ratio is not a Rician K-factor.")
    print("[note] Observed RMS width includes the DW3000/system impulse response.")
    print("[note] Propagation-only estimates require a validated same-PHY reference.")

    if args.out_csv:
        with open(args.out_csv, "w", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=list(results[0].keys()))
            writer.writeheader()
            writer.writerows(results)
        print(f"[done] per-frame results written to {args.out_csv}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
