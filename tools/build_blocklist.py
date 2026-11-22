# Python port of tools/build_blocklist.cpp (same parsing, fxhash, v2 container),
# for hosts without a C++ compiler (e.g. Windows). Keep DEFAULT_SOURCES in sync.
#   python tools/build_blocklist.py blocklist.bin [SOURCES...]
#   python tools/build_blocklist.py --check blocklist.bin DOMAIN...   # look up domains
import sys, struct, zlib, urllib.request, bisect

M64 = (1 << 64) - 1
MASK = (1 << 40) - 1
BUCKETS = 1 << 16

DEFAULT_SOURCES = [
    "https://raw.githubusercontent.com/StevenBlack/hosts/master/hosts",
    "https://raw.githubusercontent.com/hagezi/dns-blocklists/main/wildcard/pro.plus-onlydomains.txt",
    "https://raw.githubusercontent.com/sjhgvr/oisd/main/domainswild2_big.txt",
]


def mulmix(a, b):
    p = (a * b) & ((1 << 128) - 1)  # same as the 32x32 schoolbook in fxhash.h
    return (p & M64) ^ (p >> 64)


def rd64(b, i):
    return int.from_bytes(b[i:i + 8], 'little')


def rd32(b, i):
    return int.from_bytes(b[i:i + 4], 'little')


def fxhash64(b):
    SEED1, SEED2, ANTIZ = 0x243f6a8885a308d3, 0x13198a2e03707344, 0xa4093822299f31d0
    s0, s1 = SEED1, SEED2
    n = len(b)
    if n <= 16:
        if n >= 8:
            s0 ^= rd64(b, 0); s1 ^= rd64(b, n - 8)
        elif n >= 4:
            s0 ^= rd32(b, 0); s1 ^= rd32(b, n - 4)
        elif n > 0:
            s0 ^= b[0]; s1 ^= (b[n - 1] << 8) | b[n // 2]
    else:
        i = 0
        while i + 16 <= n:
            x, y = rd64(b, i), rd64(b, i + 8)
            t = mulmix(s0 ^ x, ANTIZ ^ y)
            s0, s1 = s1, t
            i += 16
        s0 ^= rd64(b, n - 16); s1 ^= rd64(b, n - 8)
    h = mulmix(s0, s1) ^ n
    st = (h * 0xf1357aea2e62a9c5) & M64
    return ((st << 26) | (st >> 38)) & M64


def h40(d):
    return fxhash64(d) & MASK


def norm(s):
    d = s.lower()  # ASCII-only, like tolower() in the C locale
    a, b = 0, len(d)
    while a < b and d[a:a + 1] in (b'*', b'.'):
        a += 1
    while b > a and d[b - 1:b] == b'.':
        b -= 1
    d = d[a:b]
    if len(d) > 4 and d[:4] == b'www.':
        d = d[4:]
    return d


DCH = set(b'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-')


def adg_match(line):
    p = 0
    allow = False
    if line[:2] == b'@@':
        allow = True; p = 2
    if line[p:p + 2] != b'||':
        return None
    p += 2
    ds = p
    while p < len(line) and line[p] in DCH:
        p += 1
    if p == ds:
        return None
    de = p
    if p < len(line) and line[p:p + 1] == b'^':
        p += 1
    mod = False
    if p < len(line) and line[p:p + 1] == b'$':
        mod = True
    elif p != len(line):
        return None
    return allow, mod, line[ds:de]


def add_block(domains, d):
    if b'.' in d and b' ' not in d:
        domains.append(d)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else 'blocklist.bin'
    srcs = sys.argv[2:] or DEFAULT_SOURCES
    domains, allow, skipped = [], [], 0
    for src in srcs:
        print('  downloading', src, file=sys.stderr)
        if src.startswith(('http://', 'https://')):
            data = urllib.request.urlopen(src, timeout=120).read()
        else:
            data = open(src, 'rb').read()
        for raw in data.split(b'\n'):
            raw = raw.rstrip(b'\r \t')
            ls = len(raw) - len(raw.lstrip(b' \t'))
            abp = raw[ls:ls + 2] in (b'||', b'@@')
            if abp:
                line = raw[ls:]
            else:
                h = raw.find(b'#')
                line = (raw if h < 0 else raw[:h]).strip(b' \t')
            if not line or line[:1] in (b'!', b'/', b'['):
                continue
            m = adg_match(line)
            if m:
                a, mod, dom = m
                if mod:
                    skipped += 1; continue
                nd = norm(dom)
                if a:
                    allow.append(nd)
                else:
                    add_block(domains, nd)
                continue
            if line[:2] in (b'||', b'@@') or line[:1] == b'|' or b'^' in line or b'$' in line or b'*' in line:
                skipped += 1; continue
            parts = line.replace(b'\t', b' ').split()
            d = None
            if len(parts) >= 2 and parts[0] in (b'0.0.0.0', b'127.0.0.1', b'::1', b'::'):
                d = parts[1]
            elif len(parts) == 1:
                d = parts[0]
            if d is not None:
                add_block(domains, norm(d))

    allow_h = sorted(set(h40(a) for a in allow))
    domains = sorted(set(domains))
    hashes, removed = [], 0
    for d in domains:
        h = h40(d)
        i = bisect.bisect_left(allow_h, h)
        if i < len(allow_h) and allow_h[i] == h:
            removed += 1; continue
        hashes.append(h)
    distinct = len(hashes)
    hashes = sorted(set(hashes))
    cnt = len(hashes)

    off = [0] * (BUCKETS + 1)
    for h in hashes:
        off[(h >> 24) + 1] += 1
    for b in range(1, BUCKETS + 1):
        off[b] += off[b - 1]
    payload = bytearray(struct.pack('<%dI' % (BUCKETS + 1), *off))
    payload += b''.join((h & 0xFFFFFF).to_bytes(3, 'little') for h in hashes)
    crc = zlib.crc32(payload) & 0xFFFFFFFF
    hdr = b'C3BL' + bytes([2, 3, 0, 0]) + struct.pack('<II', cnt, crc)
    with open(out, 'wb') as f:
        f.write(hdr); f.write(payload)
    print('allowlisted removed:', removed, '| skipped rules:', skipped)
    print('source domains:', distinct, '| hash entries:', cnt, '| collisions:', distinct - cnt)
    print('flash blob: %d bytes (%.2f MB) -> %s' % (len(hdr) + len(payload), (len(hdr) + len(payload)) / 1048576, out))


def check(blob, doms):
    b = open(blob, 'rb').read()
    cnt = struct.unpack('<I', b[8:12])[0]
    off = struct.unpack('<%dI' % (BUCKETS + 1), b[16:16 + (BUCKETS + 1) * 4])
    base = 16 + (BUCKETS + 1) * 4
    for d in doms:
        h = h40(norm(d.encode()))
        bk, suf = h >> 24, h & 0xFFFFFF
        found = any(int.from_bytes(b[base + 3 * i:base + 3 * i + 3], 'little') == suf for i in range(off[bk], off[bk + 1]))
        print('%-28s %s' % (d, 'IN LIST' if found else 'not in list'))


if __name__ == '__main__':
    if len(sys.argv) > 2 and sys.argv[1] == '--check':
        check(sys.argv[2], sys.argv[3:])
    else:
        main()
