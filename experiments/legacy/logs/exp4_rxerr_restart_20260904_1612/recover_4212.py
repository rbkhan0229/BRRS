#!/usr/bin/env python3
"""One-board authorized ERASEALL + verified BRRS N3 programming. Not a generic tool."""
import hashlib
import pathlib
import sys
import time
import pylink

SERIAL = 1050204212
EXPECTED_HASH = "d7e50b966bbfc1024c81339e45303e8304049aaad94af5723539453287956c27"
if len(sys.argv) != 3 or sys.argv[1] != str(SERIAL):
    raise SystemExit("usage: recover_4212.py 1050204212 VERIFIED_N3.hex")
hex_path = pathlib.Path(sys.argv[2]).resolve()
assert hashlib.sha256(hex_path.read_bytes()).hexdigest() == EXPECTED_HASH, "wrong HEX"

def say(*values):
    print(*values, flush=True)

def read_ctrl(j, offset):
    j.coresight_write(2, 0x01000000 | (offset & 0xF0), ap=False)
    j.coresight_read((offset & 0x0F) >> 2, ap=True)
    return j.coresight_read((offset & 0x0F) >> 2, ap=True)

def wait_nvmc(j):
    end = time.monotonic() + 2
    while time.monotonic() < end:
        if j.memory_read32(0x4001E400, 1)[0] & 1:
            return
        time.sleep(0.001)
    raise RuntimeError("NVMC timeout")

def verify_hex(j):
    # Verify every addressed data byte; do not inspect previous user contents.
    upper = 0
    total = 0
    for line in hex_path.read_text().splitlines():
        assert line.startswith(":"), "invalid Intel HEX"
        row = bytes.fromhex(line[1:])
        assert sum(row) & 255 == 0 and len(row) == row[0] + 5, "HEX checksum"
        count, kind = row[0], row[3]
        offset = int.from_bytes(row[1:3], "big")
        data = row[4:4 + count]
        if kind == 0:
            assert bytes(j.memory_read8(upper + offset, count)) == data, f"verify mismatch {upper + offset:#x}"
            total += count
        elif kind == 4:
            upper = int.from_bytes(data, "big") << 16
        elif kind == 2:
            upper = int.from_bytes(data, "big") << 4
        elif kind == 1:
            break
        elif kind not in (3, 5):
            raise RuntimeError("unsupported HEX record")
    assert total > 100000, "incomplete image"
    say("FLASH_READBACK_VERIFIED_BYTES", total)

j = pylink.JLink()
erased = False
try:
    j.open(serial_no=SERIAL)
    voltage = j.hardware_status.VTarget
    assert 3000 <= voltage <= 3500, f"unexpected VTref {voltage}"
    j.set_tif(pylink.enums.JLinkInterfaces.SWD)
    j.set_speed(100)
    j.coresight_configure()
    assert j.coresight_read(0, ap=False) == 0x2BA01477, "unexpected debug port"
    j.coresight_write(1, 0x50000000, ap=False)
    assert read_ctrl(j, 0xFC) == 0x02880000, "unexpected CTRL-AP"
    before = read_ctrl(j, 0x0C)
    say("SERIAL", SERIAL, "VTREF", voltage, "APPROTECT_BEFORE", hex(before))
    assert before == 0, "protection changed; reassess before erase"
    say("AUTHORIZED_ERASEALL_START", SERIAL)
    j.coresight_write(2, 0x01000000, ap=False)
    j.coresight_write(1, 1, ap=True)  # CTRL-AP ERASEALL, only this selected probe.
    end = time.monotonic() + 30
    while time.monotonic() < end:
        if read_ctrl(j, 0x08) == 0:
            break
        time.sleep(0.05)
    else:
        raise RuntimeError("ERASEALL timeout")
    after = read_ctrl(j, 0x0C)
    assert after == 1, f"protection not released {after:#x}"
    erased = True
    say("ERASEALL_DONE", "APPROTECT_AFTER", hex(after))
    j.coresight_write(2, 0, ap=False)
    # No reset/power cycle on the blank protected-revision part before programming.
    j.connect("NRF52840_XXAA", speed=1000)
    j.halt()
    part = j.memory_read32(0x10000100, 1)[0]
    variant = j.memory_read32(0x10000104, 1)[0]
    say("FICR_PART", hex(part), "VARIANT", hex(variant))
    assert part == 0x52840, "unexpected MCU, not programming image"
    say("FLASH_N3", str(hex_path), EXPECTED_HASH)
    j.flash_file(str(hex_path), 0)
    verify_hex(j)
    old = j.memory_read32(0x10001208, 1)[0]
    assert old == 0xFFFFFFFF or (old & 255) == 0x5A, "unexpected UICR after erase"
    if old == 0xFFFFFFFF:
        j.memory_write32(0x4001E504, [1])
        wait_nvmc(j)
        j.memory_write32(0x10001208, [0x5A])
        wait_nvmc(j)
        j.memory_write32(0x4001E504, [0])
        wait_nvmc(j)
    assert j.memory_read32(0x10001208, 1)[0] & 255 == 0x5A
    # Image DEBUG main() writes APPROTECT.DISABLE before initialization.
    j.reset(halt=False)
    time.sleep(0.5)
    say("UICR_HW_DISABLED", hex(j.memory_read32(0x10001208, 1)[0]))
finally:
    j.close()

j = pylink.JLink()
try:
    j.open(serial_no=SERIAL)
    j.set_tif(pylink.enums.JLinkInterfaces.SWD)
    j.connect("NRF52840_XXAA", speed=1000)
    assert j.memory_read32(0x10000100, 1)[0] == 0x52840
    assert j.memory_read32(0x10001208, 1)[0] & 255 == 0x5A
    say("RECOVERY_PASS", SERIAL, "reconnect_ok", "erased", erased)
finally:
    j.close()
