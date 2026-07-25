"""
Run with:  calibre-debug -e test.py
Tests port detection and (if found) the serial protocol — no Calibre GUI needed.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

# ── port scan ────────────────────────────────────────────────────────────────
from serial.tools import list_ports

print('=== serial ports ===')
ports = list(list_ports.comports())
if not ports:
    print('  (none found)')
for p in ports:
    print(f'  {p.device}: vid={p.vid!r} pid={p.pid!r} hwid={p.hwid!r}')

VID, PID = 0x303A, 0x1001
match = next((p for p in ports if p.vid == VID and p.pid == PID), None)
print(f'\nDevice port: {match.device if match else "NOT FOUND"}')
if not match:
    sys.exit(0)

# ── protocol smoke-test ──────────────────────────────────────────────────────
import struct, zlib, serial as _serial

print('\n=== connecting ===')
ser = _serial.Serial(match.device, 115200, timeout=5)
import time; time.sleep(0.1); ser.reset_input_buffer()

def cmnd(sub, path=''):
    pkt = b'CMND' + sub
    if path:
        pb = path.encode()
        pkt += struct.pack('<H', len(pb)) + pb
    ser.write(pkt)
    return ser.readline().decode(errors='replace').strip()

print('=== CMND A /sdcard ===')
pb = b'/sdcard'
ser.write(b'CMND' + b'A' + struct.pack('<H', len(pb)) + pb)
for _ in range(30):
    line = ser.readline().decode(errors='replace').strip()
    print(' ', line)
    if line in ('END', ''):
        break

ser.close()
print('\nDone.')
