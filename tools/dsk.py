import struct,sys
def load(p):
    d=open(p,'rb').read()
    nt=d[0x30]; ns=d[0x31]
    sizes=d[0x34:0x34+nt*ns]
    off=0x100; tracks={}
    for t in range(nt):
        for s in range(ns):
            sz=sizes[t*ns+s]*256
            if sz==0: continue
            hdr=d[off:off+0x100]
            assert hdr[:10]==b'Track-Info',(t,off)
            nsec=hdr[0x15]
            secs={}
            p=off+0x100
            for i in range(nsec):
                e=hdr[0x18+i*8:0x20+i*8]
                C,H,R,N,s1,s2,ln=struct.unpack('<BBBBBBH',e)
                ln=ln or (128<<N)
                secs[R]=(C,H,N,d[p:p+ln]); p+=ln
            tracks[t]=secs
            off+=sz
    return tracks
if __name__=='__main__':
    tr=load(sys.argv[1])
    for t,s in tr.items(): print(t,sorted(s), [v[2] for v in s.values()][:1])
