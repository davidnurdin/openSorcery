import sys,re; sys.path.insert(0,'tools'); import rec,z80dis
m=bytearray(open('out/mem1.bin','rb').read())
d=open('out/dec/MUCODE.BIN','rb').read(); m[0x4f00:0x4f00+len(d)]=d
dyn=[int(x,16) for x in open('out/dyn_cov.txt').read().split()]
def words(a,n): return [m[a+2*i]|m[a+2*i+1]<<8 for i in range(n)]
entries=[0x7000,0x8d55]+words(0x92e9,6)+words(0x92dd,6)+words(0x935e,8)+[a for a in dyn if 0x7000<=a<0x9300]
code,calls,fw=rec.recdis(m,entries,lo=0x7000,hi=0x9300)
strs=dict(rec.recdis.strs)
mu,mcalls,mfw=rec.recdis(m,[0x4f00,0x4f03,0x4f38,0x4f51]+[a for a in dyn if 0x4f00<=a<0x531c],lo=0x4f00,hi=0x531c)
mstrs=rec.recdis.strs
allcode={**code,**mu}; allstr={**strs,**mstrs}
labels=set(calls)|set(mcalls)|set(entries)
for a,(t,n) in allcode.items():
    mm=re.search(r'0x([0-9A-F]{4})$',t)
    if mm and re.match(r'(jp|jr|djnz|call)',t): labels.add(int(mm.group(1),16))
def emit(lo,hi,out):
    a=lo
    while a<hi:
        if a in allcode:
            t,n=allcode[a]
            if a in labels: out.append('L%04X:'%a)
            out.append('    %-28s ; %04X %s'%(t,a,bytes(m[a:a+n]).hex(' ')))
            a+=n
            if a in allstr:
                e=allstr[a]; out.append('    str %r'%bytes(m[a:e])); a=e
        else:
            j=a
            while j<hi and j not in allcode and j-a<16: j+=1
            if a in labels: out.append('L%04X:'%a)
            out.append('    db '+','.join('0x%02X'%b for b in m[a:j])+'   ; %04X '%a+''.join(chr(c) if 32<=c<127 else '.' for c in m[a:j]))
            a=j
out=['; ===== MUCODE 4F00-531B =====']
emit(0x4f00,0x531c,out)
out.append('; ===== CODE 7000-92FF =====')
emit(0x7000,0x9300,out)
open('out/full_list.txt','w').write('\n'.join(out))
print(len(out), len(allcode))
cov=bytearray(0x10000)
for a,(t,n) in allcode.items():
    for i in range(n): cov[a+i]=1
print('code bytes 7000-92FF:',sum(cov[0x7000:0x9300]),'of',0x2300)
