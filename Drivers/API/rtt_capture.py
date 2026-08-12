#!/usr/bin/env python3
"""
rtt_capture.py — nRF52840 단일 J-Link 연결로 flash + run + RTT ch1 캡처 + 검증.

JLinkRTTLogger를 사용하지 않는다. J-Link DLL에 대한 연결을 하나만 열고,
그 연결 안에서 플래시, UICR.APPROTECT 확인, 리셋/실행, RTT 시작, 채널 1
캡처, 종료 마커 검증까지 전부 수행한다. 두 번째 연결이 없으므로
"RTT Control Block not found" 류의 연결 경합/재초기화 문제가 원천적으로 없다.

의존성: pip install pylink-square  (J-Link Software Pack 설치 필요)

사용 예 (RX):
  python3 rtt_capture.py \
      --hex Output/Exp2_32_Init/Exe/dw3000_api.hex \
      --rtt-address 0x2000009C \
      --channel 1 \
      --ready-marker "CIR_RTT_READY,channel=1" \
      --end-marker "EXP2_DONE," \
      --require "status=PASS" \
      --expect-lines "CIR_CSV,:1000" \
      --timeout 90 \
      --out exp2_32_r1_rx.log

사용 예 (TX):
  python3 rtt_capture.py --hex ... --rtt-address 0x... \
      --ready-marker "EXP_LOG_READY,channel=1" \
      --end-marker "===== END STATS =====" \
      --timeout 600 --out exp2_32_r1_tx.log
"""

import argparse
import sys
import time

try:
    import pylink
except ImportError:
    sys.exit("ERROR: pylink-square가 필요합니다: pip install pylink-square")

# nRF52840 레지스터
NVMC_READY     = 0x4001E400
NVMC_CONFIG    = 0x4001E504
UICR_APPROTECT = 0x10001208
APPROTECT_HW_DISABLED = 0x0000005A  # rev3 이후 실리콘: HwDisabled
DEVICE = "NRF52840_XXAA"


def log(msg):
    print(f"[rtt_capture] {msg}", flush=True)


def nvmc_wait_ready(jl, timeout_s=1.0):
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        if jl.memory_read32(NVMC_READY, 1)[0] & 1:
            return
        time.sleep(0.001)
    raise RuntimeError("NVMC not ready")


def ensure_uicr_approtect(jl):
    """UICR.APPROTECT가 0x5A(HwDisabled)인지 확인, 소거 상태(0xFFFFFFFF)면 기록."""
    val = jl.memory_read32(UICR_APPROTECT, 1)[0]
    # APPROTECT는 PALL(하위 8비트)만 의미 있음 — 상위 비트는 미프로그램(1)일 수 있다
    if (val & 0xFF) == APPROTECT_HW_DISABLED:
        log(f"UICR.APPROTECT = 0x{val:08X} (PALL=0x5A, HwDisabled) — OK")
        return
    if val != 0xFFFFFFFF:
        log(f"WARNING: UICR.APPROTECT = 0x{val:08X} (0x5A도 0xFFFFFFFF도 아님). "
            "UICR 페이지 소거 없이는 0x5A로 바꿀 수 없으므로 그대로 둠.")
        return
    log("UICR.APPROTECT erased — writing 0x5A (HwDisabled)")
    jl.memory_write32(NVMC_CONFIG, [1])          # WEN = Wen
    nvmc_wait_ready(jl)
    jl.memory_write32(UICR_APPROTECT, [APPROTECT_HW_DISABLED])
    nvmc_wait_ready(jl)
    jl.memory_write32(NVMC_CONFIG, [0])          # WEN = Ren
    nvmc_wait_ready(jl)


def parse_expect_lines(specs):
    """"PREFIX:COUNT" 목록 → [(prefix, count)]"""
    out = []
    for spec in specs:
        prefix, _, count = spec.rpartition(":")
        out.append((prefix, int(count)))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--hex", help="flash할 hex (생략 시 flash 건너뜀)")
    ap.add_argument("--rtt-address", required=True,
                    help="_SEGGER_RTT 주소 (예: 0x2000009C)")
    ap.add_argument("--channel", type=int, default=1)
    ap.add_argument("--serial", type=int, default=None, help="J-Link S/N")
    ap.add_argument("--speed", type=int, default=4000)
    ap.add_argument("--out", required=True, help="캡처 저장 파일")
    ap.add_argument("--timeout", type=float, default=90.0)
    ap.add_argument("--ready-marker", default=None)
    ap.add_argument("--end-marker", required=True)
    ap.add_argument("--require", action="append", default=[],
                    help="캡처에 반드시 포함돼야 하는 문자열 (여러 번 지정 가능)")
    ap.add_argument("--expect-lines", action="append", default=[],
                    help='"PREFIX:COUNT" — PREFIX로 시작하는 행이 COUNT개 이상인지 검증')
    ap.add_argument("--no-reset", action="store_true",
                    help="flash 없이 이미 실행 중인 타깃에 attach만 (reset 안 함)")
    args = ap.parse_args()

    rtt_addr = int(args.rtt_address, 0)
    expectations = parse_expect_lines(args.expect_lines)

    jl = pylink.JLink()
    jl.open(serial_no=args.serial)
    jl.set_tif(pylink.enums.JLinkInterfaces.SWD)
    jl.connect(DEVICE, speed=args.speed)
    log(f"connected: {jl.core_name()}")

    try:
        if args.hex:
            jl.reset(halt=True)
            log(f"flashing {args.hex}")
            jl.flash_file(args.hex, 0x0)
            ensure_uicr_approtect(jl)
            jl.reset(halt=False)   # 리셋 후 실행 (UICR 반영)
            log("target running")
        elif not args.no_reset:
            jl.reset(halt=False)

        # 같은 연결로 RTT 시작 — 제어 블록 주소를 직접 지정
        jl.rtt_start(block_address=rtt_addr)

        # 제어 블록 인식 대기 (펌웨어가 .bss 초기화 후 CB를 쓸 때까지)
        deadline = time.monotonic() + 10.0
        while True:
            try:
                num_up = jl.rtt_get_num_up_buffers()
                if num_up > args.channel:
                    break
            except pylink.errors.JLinkRTTException:
                pass
            if time.monotonic() > deadline:
                raise RuntimeError("RTT control block을 10초 내에 인식하지 못함")
            time.sleep(0.05)
        log(f"RTT up buffers: {num_up} — capturing channel {args.channel}")

        captured = bytearray()
        ready_seen = args.ready_marker is None
        end_seen = False
        end_b = args.end_marker.encode()
        ready_b = (args.ready_marker or "").encode()
        deadline = time.monotonic() + args.timeout

        with open(args.out, "wb") as f:
            while time.monotonic() < deadline:
                data = jl.rtt_read(args.channel, 16384)
                if data:
                    b = bytes(data)
                    f.write(b)
                    f.flush()
                    captured += b
                    if not ready_seen and ready_b in captured:
                        ready_seen = True
                        log("READY marker seen")
                    if end_b in captured:
                        # 종료 마커 이후 잔여 데이터 flush
                        time.sleep(0.3)
                        tail = jl.rtt_read(args.channel, 16384)
                        while tail:
                            tb = bytes(tail)
                            f.write(tb)
                            captured += tb
                            tail = jl.rtt_read(args.channel, 16384)
                        end_seen = True
                        break
                else:
                    time.sleep(0.02)

        if not end_seen:
            log(f"ERROR: {args.timeout}s 내에 end marker 미검출: {args.end_marker!r}")
            return 2

        text = captured.decode("utf-8", errors="replace")
        ok = True
        for needle in args.require:
            if needle not in text:
                log(f"ERROR: 필수 문자열 없음: {needle!r}")
                ok = False
        for prefix, count in expectations:
            n = sum(1 for line in text.splitlines() if line.startswith(prefix))
            if n != count:
                log(f"ERROR: {prefix!r} 행 {n}/{count}")
                ok = False
            else:
                log(f"OK: {prefix!r} 행 {n}/{count}")

        log("PASS" if ok else "FAIL")
        return 0 if ok else 3
    finally:
        try:
            jl.rtt_stop()
        except Exception:
            pass
        jl.close()


if __name__ == "__main__":
    sys.exit(main())
