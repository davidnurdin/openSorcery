#!/usr/bin/env python3
"""Extract + decrypt every game asset from the DSK and export PNG/raw files.
usage: extract_assets.py Sorcery.dsk outdir"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
import dsk as D, pnglib as P
src, out = sys.argv[1], sys.argv[2]
for d in ('raw', 'png', 'png/rooms', 'png/sprites', 'png/tiles', 'png/screens', 'png/font'): os.makedirs(f'{out}/{d}', exist_ok=True)
tr = D.load(src)
img = b''.join(tr[t][r][3] for t in sorted(tr) for r in sorted(tr[t]))
def dec(b):
    a = (-b) & 255; a = ((a >> 1) | (a << 7)) & 255; return (a - 0x25) & 255
T = bytes(dec(i) for i in range(256))
dirb = img[:2048]; files = {}
for i in range(0, 2048, 32):
    e = dirb[i:i+32]
    if e[0] != 0 or e[1] < 0x20: continue
    nm = bytes(b & 0x7f for b in e[1:9]).decode().rstrip(); ex = bytes(b & 0x7f for b in e[9:12]).decode().rstrip()
    files.setdefault(nm + '.' + ex, []).append((e[12], [b for b in e[16:] if b]))
F = {}
for k, ents in files.items():
    data = b''.join(img[b*1024:(b+1)*1024] for _, bl in sorted(ents) for b in bl)
    ln = struct.unpack('<H', data[24:26])[0]
    F[k] = data[128:128+ln].translate(T)
    open(f'{out}/raw/{k}', 'wb').write(F[k])
# palettes (hardware colours) captured from the running game
PAL_TITLE = [0x14,0x0A,0x0E,0x0C,0x13,0x0B,0x1F,0x0F,0x12,0x06,0x1C,0x16,0x03,0x1F,0x00,0x1E]
PAL_GAME = PAL_TITLE
pal = [P.hw_rgb(h) for h in PAL_TITLE]
def screen(data, w_bytes=80, lines=200, mode=0):
    rows = []
    for y in range(lines):
        base = (y >> 3) * w_bytes + (y & 7) * 0x800
        r = []
        for i in range(w_bytes):
            b = data[base + i] if base + i < len(data) else 0
            if mode == 0: r.extend(P.mode0_pix(b))
            else: r.extend(P.mode1_pix(b))
        rows.append(r)
    return rows
def save_screen(name, data, pal, mode=0, scale=(4, 2)):
    rows = screen(data, mode=mode); W = len(rows[0]) * scale[0]; H = len(rows) * scale[1]
    cv = P.canvas(W, H); P.blit(cv, W, 0, 0, rows, 0, pal, *scale); P.save(f'{out}/png/screens/{name}.png', cv, W, H)
save_screen('title_strange', F['STRANGE.SCN'], pal)
# tiles (planar 16 x 256 bytes: tile t byte j at j*256+t, 4x8 px mode 0)
def tile_pix(ts, t):
    rows = []
    for y in range(8):
        a, b = ts[(2*y) * 256 + t], ts[(2*y+1) * 256 + t]
        rows.append(list(P.mode0_pix(a)) + list(P.mode0_pix(b)))
    return rows
def tileset_png(name, ts, pal):
    W, H = 16 * 4 * 3, 16 * 8 * 3; cv = P.canvas(W, H)
    for t in range(256): P.blit(cv, W, (t % 16) * 12, (t // 16) * 24, tile_pix(ts, t), 0, pal, 3, 3)
    P.save(f'{out}/png/tiles/{name}.png', cv, W, H)
for n in ('CONSET1.BIN', 'CONSET2.BIN'): tileset_png(n[:-4], F[n], pal)
tp = F['TITLEP.BIN']
tileset_png('TITLEP_tiles', tp, pal)
# title picture 'presented by' tilemap: 720 tiles at +0x1000
def render_map(tm, ts, pal):
    W, H = 160, 144; cv = P.canvas(W * 2, H * 2)
    for i in range(720):
        P.blit(cv, W, (i % 40) * 4 * 2, (i // 40) * 8 * 2, tile_pix(ts, tm[i]), 0, pal, 2, 2)
    return cv, W * 2, H * 2
cv, w, h = render_map(tp[0x1000:0x12d0], tp, pal); P.save(f'{out}/png/screens/amsoft_presents.png', cv, w, h)
cv, w, h = render_map(F['TISCRN.BIN'], F['CONSET1.BIN'], pal); P.save(f'{out}/png/screens/tiscrn.png', cv, w, h)
# font: 16 bytes/glyph (8 rows x 2 bytes), mode 1 pixels
fo = F['CONALP.BIN']; ng = len(fo) // 16
W, H = 16 * 8 * 3, ((ng + 15) // 16) * 8 * 3; cv = P.canvas(W, H); fpal = [(0,0,0), (255,255,0), (0,255,255), (255,255,255)]
for g in range(ng):
    rows = [P.mode1_pix(fo[g*16 + y*2]) + P.mode1_pix(fo[g*16 + y*2 + 1]) for y in range(8)]
    P.blit(cv, W, (g % 16) * 24, (g // 16) * 24, rows, 0, fpal, 3, 3)
P.save(f'{out}/png/font/font.png', cv, W, H)
# sprites: 144 bytes = 24 rows x 6 bytes (12 px mode 0)
for n in ('SPRITES1.BIN', 'SPRITES2.BIN'):
    sd = F[n]; nf = len(sd) // 144; cols = 12
    W, H = cols * 12 * 3, ((nf + cols - 1) // cols) * 24 * 3; cv = P.canvas(W, H)
    for f in range(nf):
        rows = []
        for y in range(24):
            r = []
            for x in range(6): r.extend(P.mode0_pix(sd[f*144 + y*6 + x]))
            rows.append(r)
        P.blit(cv, W, (f % cols) * 36, (f // cols) * 72, rows, 0, pal, 3, 3)
    P.save(f'{out}/png/sprites/{n[:-4]}.png', cv, W, H)
# rooms: sector per room on tracks 31..39
def read_room(n):
    t, r = 0x1F + n // 9, 0xC1 + n % 9
    if t not in tr or r not in tr[t]: return None
    s = tr[t][r][3]
    if s[:3] != b'DC ' or s[3] != n: return None
    out_, i = [], 4
    while i < 512:
        a = s[i]
        if a == 0xFF: break
        if a == 0: i += 1; out_ += [0] * (s[i] or 256)
        else: out_.append(a)
        i += 1
    return (out_ + [0] * 720)[:720], s[i+1:]
nrooms = 0
for n in range(80):
    r = read_room(n)
    if not r: continue
    tm, extra = r
    open(f'{out}/raw/room{n:02d}.tiles', 'wb').write(bytes(tm)); open(f'{out}/raw/room{n:02d}.extra', 'wb').write(bytes(extra))
    cv, w, h = render_map(tm, F['CONSET1.BIN'], pal); P.save(f'{out}/png/rooms/room{n:02d}.png', cv, w, h); nrooms += 1
print('files', len(F), 'rooms', nrooms)
