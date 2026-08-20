#!/usr/bin/env python3
"""Exp5 channel characterization: RMS delay spread and Rician K-factor
from raw CIR dumps captured by brrs_exp2_capture_v3.sh (rx role, M=1024
CIR_ANALYSIS_SAMPLES build).

Reads CIR_RAW_HEADER / CIR_RAW lines from an Exp2 INIT raw log and, for
each dumped frame, computes:
  - power-delay profile P(tap) = |I + jQ|^2
  - noise floor (mean power of the samples before the first path)
  - RMS delay spread (tau_rms) over taps whose power exceeds
    noise_floor + a threshold (default 6 dB)
  - Rician K-factor estimated as (peak tap power) / (power in the
    remaining above-threshold taps), a standard first-path-dominance
    estimator suitable for a single CIR snapshot.

Path loss exponent is NOT computed here: it requires multiple
distance-separated measurements (see --help for the expected workflow).

Usage:
  python3 brrs_exp5_channel_characterize.py <init_raw_log.log> [--out-csv out.csv]
"""
import argparse
import csv
import math
import re
import sys
from dataclasses import dataclass, field

HEADER_RE = re.compile(
    r"^CIR_RAW_HEADER,frame=(\d+),cycle=(\d+),plen=(\d+),"
    r"sample_offset=(\d+),n_samples=(\d+),fp_sample=(\d+)"
)
SAMPLE_RE = re.compile(r"^CIR_RAW,(\d+),(\d+),(-?\d+),(-?\d+)")

# DW3000 Ipatov CIR tap spacing at PRF64 (approx). 1 tap ~= 1 / (2 * 499.2 MHz)
# = 1000 / (2 * 499.2) nanoseconds ~= 1.0016 ns.
TAP_SPACING_NS = 1000.0 / (2 * 499.2)

# Threshold above the noise floor (in dB) for a tap to be treated as a
# genuine multipath component rather than noise.
DEFAULT_THRESHOLD_DB = 6.0

# Mirror the firmware's own noise-floor window (brrs_init.c,
# calculate_fp_snr_from_cir): leave a small guard gap before the first-path
# tap, then average a fixed number of samples further back. Using taps right
# up to the first-path index over-estimates the noise floor because the
# leading edge of the main pulse has already started ramping up there.
NOISE_GUARD_SAMPLES = 2
NOISE_PRE_FP_SAMPLES = 12

# CLEAN deconvolution parameters. The template is the complex shape of the
# strongest peak's own immediate neighbourhood in each frame -- this is the
# correlator's response to a SINGLE path (preamble autocorrelation, filtered
# by the receive chain), not a genuine extra reflection. Every other tap's
# energy is partly "spillover" from this same shape. CLEAN repeatedly finds
# the strongest remaining peak and subtracts a shifted/scaled copy of the
# template from the residual, so what's left after convergence is a sparse
# list of genuinely distinct arrivals with the pulse shape's own sidelobes
# removed.
CLEAN_TEMPLATE_HALF_WIDTH = 20      # taps on each side of the peak. The
                                     # DW3000 Ipatov correlator's own
                                     # sidelobe structure was found (by
                                     # testing convergence at several widths
                                     # on real captures) to extend out to
                                     # roughly +-20 taps; anything narrower
                                     # left genuine sidelobe energy in the
                                     # residual and CLEAN never converged.
CLEAN_LOOP_GAIN = 0.9               # <1.0 avoids overshoot / non-convergence
CLEAN_MAX_ITERATIONS = 60           # safety cap; most frames converge in
                                     # ~20-30 components at half-width=20,
                                     # but a few need more room
CLEAN_STOP_THRESHOLD_DB = 8.0       # stop once residual peak drops within
                                     # this many dB of the noise floor
CLEAN_SEARCH_HALF_WINDOW_TAPS = 120 # only search for components within this
                                     # many taps of the peak; a flat dB
                                     # threshold applied across the full
                                     # ~300-tap window will otherwise chase
                                     # isolated noise excursions far away


@dataclass
class FrameCIR:
    frame: int
    cycle: int
    plen: int
    sample_offset: int
    n_samples: int
    fp_sample: int
    taps: list = field(default_factory=list)  # (real, imag)


def parse_raw_log(path):
    frames = []
    current = None
    with open(path, "r", errors="replace") as f:
        for line in f:
            line = line.rstrip("\n")
            m = HEADER_RE.match(line)
            if m:
                if current is not None and current.taps:
                    frames.append(current)
                current = FrameCIR(
                    frame=int(m.group(1)),
                    cycle=int(m.group(2)),
                    plen=int(m.group(3)),
                    sample_offset=int(m.group(4)),
                    n_samples=int(m.group(5)),
                    fp_sample=int(m.group(6)),
                )
                continue
            m = SAMPLE_RE.match(line)
            if m and current is not None:
                frame_no = int(m.group(1))
                if frame_no != current.frame:
                    continue
                tap_idx = int(m.group(2))
                real = int(m.group(3))
                imag = int(m.group(4))
                current.taps.append((tap_idx, real, imag))
    if current is not None and current.taps:
        frames.append(current)
    return frames


def build_template(complex_by_idx, peak_idx, half_width):
    """Extract a normalized complex template of the peak's own pulse shape.
    template[0] corresponds to tap (peak_idx - half_width); the peak itself
    sits at template[half_width] and is normalized to exactly 1+0j."""
    peak_val = complex_by_idx.get(peak_idx)
    if peak_val is None or (peak_val.real == 0 and peak_val.imag == 0):
        return None
    template = []
    for offset in range(-half_width, half_width + 1):
        v = complex_by_idx.get(peak_idx + offset, 0j)
        template.append(v / peak_val)
    return template  # length = 2*half_width + 1, template[half_width] == 1+0j


def build_ensemble_template(frames, half_width):
    """Average the (phase-normalized) peak neighbourhood across every frame
    to get one template for the correlator's own pulse shape. Averaging
    across many independent frames cancels out any one frame's own
    multipath/noise, leaving mostly the shape that is common to all of them
    -- i.e. the actual autocorrelation response, not a single noisy
    instance of it."""
    sums = [0j] * (2 * half_width + 1)
    count = 0
    for fc in frames:
        if not fc.taps:
            continue
        complex_by_idx = {idx: complex(real, imag) for idx, real, imag in fc.taps}
        peak_idx = max(complex_by_idx, key=lambda i: abs(complex_by_idx[i]) ** 2)
        t = build_template(complex_by_idx, peak_idx, half_width)
        if t is None:
            continue
        for i, v in enumerate(t):
            sums[i] += v
        count += 1
    if count == 0:
        return None
    return [s / count for s in sums]


def clean_deconvolve(complex_by_idx, idx_range, template, half_width,
                      noise_floor, stop_threshold_db,
                      loop_gain=CLEAN_LOOP_GAIN, max_iter=CLEAN_MAX_ITERATIONS):
    """Matching-pursuit / CLEAN-style deconvolution. Returns a list of
    (tap_idx, complex_amplitude) clean components, sparse and with the
    template's own pulse shape removed from every subtracted peak."""
    residual = dict(complex_by_idx)
    stop_power = noise_floor * (10 ** (stop_threshold_db / 10.0))
    components = []

    lo, hi = idx_range
    for _ in range(max_iter):
        peak_idx = max(
            (i for i in residual if lo <= i <= hi),
            key=lambda i: abs(residual[i]) ** 2,
            default=None,
        )
        if peak_idx is None:
            break
        peak_val = residual[peak_idx]
        peak_power = abs(peak_val) ** 2
        if peak_power < stop_power:
            break

        gain = peak_val * loop_gain
        components.append((peak_idx, gain))
        for offset in range(-half_width, half_width + 1):
            tap = peak_idx + offset
            if tap in residual:
                residual[tap] -= gain * template[offset + half_width]

    return components


def analyze_frame(fc: FrameCIR, threshold_db: float, shared_template=None):
    """Return a dict of delay-spread/K-factor estimates (both the simple
    contiguous-span method and the CLEAN-deconvolved method), or None if the
    frame is unusable."""
    if not fc.taps:
        return None

    fc.taps.sort(key=lambda t: t[0])
    power = [(idx, real * real + imag * imag) for idx, real, imag in fc.taps]
    complex_by_idx = {idx: complex(real, imag) for idx, real, imag in fc.taps}

    # fp_sample is an absolute accumulator index; sample_offset is where our
    # window starts, so the first path lands at (fp_sample - sample_offset)
    # within the window.
    fp_local = fc.fp_sample - fc.sample_offset

    noise_end = max(0, fp_local - NOISE_GUARD_SAMPLES)
    noise_start = max(0, noise_end - NOISE_PRE_FP_SAMPLES)
    pre_fp = [p for idx, p in power if noise_start <= idx < noise_end]
    if not pre_fp:
        # fall back: use the first quarter of the window as a noise estimate
        cut = max(1, len(power) // 4)
        pre_fp = [p for _, p in power[:cut]]
    noise_floor = sum(pre_fp) / len(pre_fp) if pre_fp else 0.0
    if noise_floor <= 0:
        noise_floor = 1e-6

    threshold_power = noise_floor * (10 ** (threshold_db / 10.0))

    # Only integrate a contiguous span around the peak. A flat dB threshold
    # over ~300 samples will, by chance, be exceeded by isolated noise taps
    # far from the peak; including those would corrupt the delay-spread
    # estimate with spurious "multipath" at implausible delays.
    peak_idx, peak_power = max(power, key=lambda t: t[1])
    power_by_idx = {idx: p for idx, p in power}
    span_indices = [peak_idx]
    i = peak_idx - 1
    while i >= 0 and power_by_idx.get(i, 0) >= threshold_power:
        span_indices.append(i)
        i -= 1
    i = peak_idx + 1
    max_idx = power[-1][0]
    while i <= max_idx and power_by_idx.get(i, 0) >= threshold_power:
        span_indices.append(i)
        i += 1
    above = [(idx, power_by_idx[idx]) for idx in span_indices]
    if not above:
        return None

    total_power = sum(p for _, p in above)
    total_tap_delay = sum(idx * p for idx, p in above)
    tau_mean = total_tap_delay / total_power

    variance_taps = sum(((idx - tau_mean) ** 2) * p for idx, p in above) / total_power
    tau_rms_taps = math.sqrt(variance_taps)
    tau_rms_ns = tau_rms_taps * TAP_SPACING_NS

    diffuse_power = total_power - peak_power
    if diffuse_power <= 0:
        k_linear = float("inf")
    else:
        k_linear = peak_power / diffuse_power
    k_db = 10 * math.log10(k_linear) if math.isfinite(k_linear) and k_linear > 0 else float("inf")

    # --- CLEAN deconvolution: separate genuine multipath arrivals from the
    # peak's own pulse-shape sidelobes. Prefer a template averaged across
    # the whole capture (shared_template) over a per-frame self-template,
    # since a single frame's own peak neighbourhood may already contain
    # real multipath and so isn't a trustworthy "pure single path" example
    # on its own. ---
    hw = CLEAN_TEMPLATE_HALF_WIDTH
    template = shared_template if shared_template is not None else \
        build_template(complex_by_idx, peak_idx, hw)
    clean_result = None
    if template is not None:
        min_idx, max_idx_all = power[0][0], power[-1][0]
        idx_range = (
            max(min_idx + hw, peak_idx - CLEAN_SEARCH_HALF_WINDOW_TAPS),
            min(max_idx_all - hw, peak_idx + CLEAN_SEARCH_HALF_WINDOW_TAPS),
        )
        components = clean_deconvolve(
            complex_by_idx, idx_range, template, hw,
            noise_floor, CLEAN_STOP_THRESHOLD_DB,
        )
        if components:
            comp_power = [(idx, abs(amp) ** 2) for idx, amp in components]
            c_peak_idx, c_peak_power = max(comp_power, key=lambda t: t[1])
            c_total_power = sum(p for _, p in comp_power)
            c_tau_mean = sum(idx * p for idx, p in comp_power) / c_total_power
            c_variance_taps = sum(
                ((idx - c_tau_mean) ** 2) * p for idx, p in comp_power
            ) / c_total_power
            c_tau_rms_ns = math.sqrt(c_variance_taps) * TAP_SPACING_NS
            c_diffuse_power = c_total_power - c_peak_power
            if c_diffuse_power <= 0:
                c_k_linear = float("inf")
            else:
                c_k_linear = c_peak_power / c_diffuse_power
            c_k_db = (10 * math.log10(c_k_linear)
                      if math.isfinite(c_k_linear) and c_k_linear > 0 else float("inf"))
            clean_result = {
                "clean_rms_delay_spread_ns": c_tau_rms_ns,
                "clean_k_factor_db": c_k_db,
                "clean_n_components": len(components),
            }

    result = {
        "rms_delay_spread_ns": tau_rms_ns,
        "k_factor_linear": k_linear,
        "k_factor_db": k_db,
        "noise_floor_power": noise_floor,
        "peak_power": peak_power,
        "peak_tap_offset": peak_idx - fp_local,
        "n_taps_above_threshold": len(above),
        "clean_rms_delay_spread_ns": float("nan"),
        "clean_k_factor_db": float("nan"),
        "clean_n_components": 0,
    }
    if clean_result is not None:
        result.update(clean_result)
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("raw_log", help="Exp2 INIT raw RTT log (rx role)")
    ap.add_argument("--threshold-db", type=float, default=DEFAULT_THRESHOLD_DB,
                     help="Noise-floor threshold in dB for a tap to count as multipath (default: 6)")
    ap.add_argument("--out-csv", default=None, help="Write per-frame results to this CSV path")
    args = ap.parse_args()

    frames = parse_raw_log(args.raw_log)
    if not frames:
        print(f"[error] no CIR_RAW_HEADER/CIR_RAW rows found in {args.raw_log}", file=sys.stderr)
        print("        (rebuild/re-run with CIR_RAW_LOG_LIMIT > 0 in brrs_init.c)", file=sys.stderr)
        sys.exit(1)

    # NOTE: an ensemble (cross-frame-averaged) template was tried here and
    # performed *worse* than each frame's own self-template -- averaging
    # complex values across frames is sensitive to sub-tap phase alignment,
    # and small per-frame timing/phase differences apparently do not cancel
    # out cleanly. Each frame therefore builds its own template from its own
    # peak neighbourhood (see build_template / CLEAN_TEMPLATE_HALF_WIDTH).

    results = []
    for fc in frames:
        r = analyze_frame(fc, args.threshold_db)
        if r is None:
            continue
        r["frame"] = fc.frame
        r["cycle"] = fc.cycle
        r["plen"] = fc.plen
        results.append(r)

    if not results:
        print("[error] parsed frames but none produced a usable power-delay profile", file=sys.stderr)
        sys.exit(1)

    rms_values = [r["rms_delay_spread_ns"] for r in results]
    k_db_values = [r["k_factor_db"] for r in results if math.isfinite(r["k_factor_db"])]
    clean_rms_values = [r["clean_rms_delay_spread_ns"] for r in results
                         if math.isfinite(r["clean_rms_delay_spread_ns"])]
    clean_k_db_values = [r["clean_k_factor_db"] for r in results
                          if math.isfinite(r["clean_k_factor_db"])]

    def mean(xs):
        return sum(xs) / len(xs) if xs else float("nan")

    def stdev(xs):
        if len(xs) < 2:
            return 0.0
        m = mean(xs)
        return math.sqrt(sum((x - m) ** 2 for x in xs) / (len(xs) - 1))

    print(f"[info] parsed {len(frames)} raw CIR dumps, {len(results)} usable "
          f"(plen={frames[0].plen}, threshold={args.threshold_db:.1f} dB above noise floor)")
    print("\n-- simple contiguous-span method (includes pulse-shape sidelobes) --")
    print(f"RMS delay spread: mean={mean(rms_values):.2f} ns, "
          f"std={stdev(rms_values):.2f} ns, "
          f"min={min(rms_values):.2f} ns, max={max(rms_values):.2f} ns  (n={len(rms_values)})")
    if k_db_values:
        print(f"Rician K-factor:  mean={mean(k_db_values):.2f} dB, "
              f"std={stdev(k_db_values):.2f} dB, "
              f"min={min(k_db_values):.2f} dB, max={max(k_db_values):.2f} dB  (n={len(k_db_values)})")
    else:
        print("Rician K-factor:  no finite estimates (every frame had ~0 diffuse power; "
              "channel may be very clean LOS, or threshold too high)")

    print("\n-- CLEAN-deconvolved method (pulse-shape sidelobes removed) --")
    if clean_rms_values:
        print(f"RMS delay spread: mean={mean(clean_rms_values):.2f} ns, "
              f"std={stdev(clean_rms_values):.2f} ns, "
              f"min={min(clean_rms_values):.2f} ns, max={max(clean_rms_values):.2f} ns  "
              f"(n={len(clean_rms_values)})")
    else:
        print("RMS delay spread: CLEAN produced no usable result for any frame")
    if clean_k_db_values:
        print(f"Rician K-factor:  mean={mean(clean_k_db_values):.2f} dB, "
              f"std={stdev(clean_k_db_values):.2f} dB, "
              f"min={min(clean_k_db_values):.2f} dB, max={max(clean_k_db_values):.2f} dB  "
              f"(n={len(clean_k_db_values)})")
    else:
        print("Rician K-factor:  no finite CLEAN estimates")
    mean_components = mean([r["clean_n_components"] for r in results])
    print(f"(mean {mean_components:.1f} CLEAN components per frame, "
          f"template half-width={CLEAN_TEMPLATE_HALF_WIDTH} taps)")

    print("\n[note] path loss exponent requires multiple distance-separated captures; "
          "this script only characterizes a single placement. Re-run at several distances "
          "and fit log-log RSSI vs. distance separately.")
    print("[note] the CLEAN template is derived from each frame's own peak neighbourhood, "
          "not an independently measured reference pulse -- treat the CLEAN numbers as a "
          "better approximation, not a ground-truth deconvolution. A handful of frames may "
          "not fully converge within the iteration cap (their own peak region wasn't a clean "
          "enough single-path example); such frames widen the reported spread across frames.")

    if args.out_csv:
        with open(args.out_csv, "w", newline="") as f:
            w = csv.DictWriter(f, fieldnames=[
                "frame", "cycle", "plen", "rms_delay_spread_ns", "k_factor_db",
                "k_factor_linear", "noise_floor_power", "peak_power",
                "peak_tap_offset", "n_taps_above_threshold",
                "clean_rms_delay_spread_ns", "clean_k_factor_db", "clean_n_components",
            ])
            w.writeheader()
            for r in results:
                w.writerow(r)
        print(f"[done] per-frame results written to {args.out_csv}")


if __name__ == "__main__":
    main()
