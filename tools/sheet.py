#!/usr/bin/env python3
"""Contact sheet: sheet.py out.png in1.png in2.png ...  (half-size tiles, 3 columns)"""
import sys, zlib, struct
def readpng(p):
    d=open(p,'rb').read(); i=8; idat=b''
    while i<len(d):
        n,t=struct.unpack('>I4s',d[i:i+8]); c=d[i+8:i+8+n]; i+=12+n
        if t==b'IHDR': w,h=struct.unpack('>II',c[:8])
        elif t==b'IDAT': idat+=c
    raw=zlib.decompress(idat); rows=[]
    for y in range(h):
        base=y*(1+w*3)+1; rows.append(raw[base:base+w*3])
    return w,h,rows
def writepng(p,w,h,rows):
    raw=b''.join(b'\0'+r for r in rows)
    def ch(t,c): return struct.pack('>I',len(c))+t+c+struct.pack('>I',zlib.crc32(t+c))
    open(p,'wb').write(b'\x89PNG\r\n\x1a\n'+ch(b'IHDR',struct.pack('>IIBBBBB',w,h,8,2,0,0,0))+ch(b'IDAT',zlib.compress(raw,1))+ch(b'IEND',b''))
out=sys.argv[1]; ins=sys.argv[2:]
tiles=[readpng(p) for p in ins]
tw,th=tiles[0][0]//2,tiles[0][1]//2; cols=3; rows_n=(len(tiles)+cols-1)//cols
W=tw*cols; H=th*rows_n
canvas=[bytearray(W*3) for _ in range(H)]
for k,(w,h,rows) in enumerate(tiles):
    ox=(k%cols)*tw; oy=(k//cols)*th
    for y in range(th):
        r=rows[y*2]
        for x in range(tw):
            canvas[oy+y][(ox+x)*3:(ox+x)*3+3]=r[x*6:x*6+3]
writepng(out,W,H,[bytes(r) for r in canvas])
