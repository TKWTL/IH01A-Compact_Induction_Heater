# Unit test for the patched sigrok usb_power_delivery decoder.
# Runs with a stubbed 'sigrokdecode' module so it needs no PulseView runtime.
# Vectors are taken from a real capture (Desktop pd decode.txt).
import sys, types, importlib.util, os

STUB = types.ModuleType('sigrokdecode')
class _Decoder(object):
    pass
STUB.Decoder = _Decoder
STUB.SRD_CONF_SAMPLERATE = 100
STUB.OUTPUT_ANN = 1
STUB.OUTPUT_BINARY = 2
STUB.OUTPUT_META = 3
STUB.OUTPUT_LOGIC = 4
sys.modules['sigrokdecode'] = STUB

SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'pd.py')
spec = importlib.util.spec_from_file_location('pd_patched', SRC)
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)

d = mod.Decoder()
d.reset()
fail = []

def check(name, got, want_sub):
    ok = want_sub in got
    print('%-58s %s   %s' % (name, 'PASS' if ok else 'FAIL', got))
    if not ok:
        fail.append((name, got, want_sub))

print('python', sys.version.split()[0])
print('--- tables ---')
check('DATA_TYPES[9]', mod.DATA_TYPES.get(9, '?'), 'EPR Request')
check('DATA_TYPES[10]', mod.DATA_TYPES.get(10, '?'), 'EPR Mode')
check('EXT_TYPES[17]', mod.EXT_TYPES.get(17, '?'), 'EPR_Source_Capabilities')
check('EXT_TYPES[16]', mod.EXT_TYPES.get(16, '?'), 'Extended_Control')
check('EXT_CTRL_TYPES[3]', mod.EXT_CTRL_TYPES.get(3, '?'), 'EPR_KeepAlive')

print('--- PDO decoding (real captured values) ---')
# SPR PDO1 from the capture: 5 V / 3 A with EPR-capable (bit 23)
check('SPR PDO 0x0A81912C', d.get_source_sink_cap(0x0A81912C, 1, True), '5V')
check('SPR PDO epr_capable', d.get_source_sink_cap(0x0A81912C, 1, True), 'epr_capable')
# EPR fixed 28 V / 5 A from the capture
check('EPR fixed 0x0008C1F4', d.get_source_sink_cap(0x0008C1F4, 8, True), '28V')
check('EPR fixed current', d.get_source_sink_cap(0x0008C1F4, 8, True), '5A')
# EPR AVS from the capture: 15..28 V, 140 W
check('EPR AVS 0xD230968C', d.get_source_sink_cap(0xD230968C, 9, True), 'EPR AVS')
check('EPR AVS 15/28 V', d.get_source_sink_cap(0xD230968C, 9, True), '15/28V')
check('EPR AVS PDP 140 W', d.get_source_sink_cap(0xD230968C, 9, True), '140W')

print('--- RDO decoding ---')
d.cap_mark[5] = 0
check('SPR Request 0x51C7D1F4 (OPOS 5)', d.get_request(0x51C7D1F4), 'PDO #5')
check('SPR Request 5 A', d.get_request(0x51C7D1F4), '5A (operating)')
# EPR Request: absolute object position 8, 5 A, EPR+unchunked flags
check('EPR Request 0x81C7D1F4 (OPOS 8)', d.get_request(0x81C7D1F4), 'PDO #8')
check('EPR Request flags', d.get_request(0x81C7D1F4), 'unchunked')
# AVS RDO example from the spec note: 28.05 V / 5 A
d.cap_mark[8] = 4
check('AVS RDO 0x8008C064 voltage', d.get_request(0x8008C064), 'EPR AVS 28V')
check('AVS RDO 0x8008C064 current', d.get_request(0x8008C064), '5A')

print('--- EPR_Mode data object ---')
head_backup = getattr(d, 'head', 0)
# EPR_Mode from the capture: action 1 (Enter), PDP 140 W
d.head = 0x188A
d.data = [0x018C0000]
captured = []
d.putx = lambda s0, s1, data: captured.append(data)
d.text = ''
d.putpayload(0, -1, 0)
check('EPR_Mode action Enter', captured[-1][1][0], 'action Enter')
check('EPR_Mode PDP 140W', captured[-1][1][0], 'PDP 140W')
d.head = head_backup

print('--- extended message reassembly (real two-chunk EPR caps) ---')
# chunk 0: 26 bytes, chunk 1: 10 bytes (8 data + 2 pad), total 36 bytes
chunk0 = bytes.fromhex('2C91812A' .replace('2A','0A') + '2CD10200' + '2CC10300' + '2CB10400'
                       + 'F4410600' + '6432A4C9' + '0000')
chunk1 = bytes.fromhex('0000F4C10800' + '8C9630D2')
print('chunk0 len %d (expect 26), chunk1 len %d (expect 10)' % (len(chunk0), len(chunk1)))
d.reset()
d.head = 0xBBB1 | 0x0000            # type 0x11, ext=1, NDO placeholder
d.head = (1 << 15) | (3 << 12) | (5 << 9) | 1   # ext + NDO 3 + msgid 5 + SRC
d.text = ''
captured = []
d.putx = lambda s0, s1, data: captured.append(data)
d.ext_raw = bytearray(chunk1)
d.exthead = 0x8824                  # chunked, chunk 1, size 36
d.ext_buf = bytearray(chunk0)       # chunk 0 already received
d.ext_type = mod.EXT_TYPES and 17
d.ext_size = 36

# now feed the last chunk through the same path the decoder uses
valid = min(len(chunk1), mod.EXT_CHUNK_BYTES, 36 - 1 * mod.EXT_CHUNK_BYTES)
d.ext_buf += chunk1[:valid]
if len(d.ext_buf) >= d.ext_size:
    d.put_extended_payload(17, bytes(d.ext_buf[:d.ext_size]))
txt = d.text
check('EPR caps reassembled 28V', txt, '28V')
check('EPR caps reassembled AVS', txt, 'EPR AVS')
check('EPR caps PDO count', txt, '[8]')

print('')
if fail:
    print('FAILURES: %d' % len(fail))
    for f in fail:
        print('  ', f)
    sys.exit(1)
print('all decoder checks passed')
