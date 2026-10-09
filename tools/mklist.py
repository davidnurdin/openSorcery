import sys,re; sys.path.insert(0,'tools'); import rec,z80dis
m=open('out/mem1.bin','rb').read()
code,calls,fw=rec.recdis(m,[0x7000,0x8d55])
# gather branch targets as labels
labels=set(calls)|{0x7000,0x8d55}
for a,(t,n) in code.items():
    mm=re.search(r'0x([0-9A-F]{4})$',t)
    if mm and re.match(r'(jp|jr|djnz|call)',t): labels.add(int(mm.group(1),16))
out=[]; a=0x7000
while a<0xa14a:
    if a in code:
        t,n=code[a]
        if a in labels: out.append('L%04X:'%a)
        out.append('    %-28s ; %04X %s'%(t,a,m[a:a+n].hex(' ')))
        a+=n
        if a in rec.recdis.strs:
            e=rec.recdis.strs[a]
            out.append('    str %r'%bytes(mm for mm in m[a:e]))
            a=e
    else:
        j=a
        while j<0xa14a and j not in code and j-a<16: j+=1
        if a in labels: out.append('L%04X:'%a)
        out.append('    db '+','.join('0x%02X'%b for b in m[a:j])+'   ; %04X '%a+''.join(chr(c) if 32<=c<127 else '.' for c in m[a:j]))
        a=j
open('out/code_list.txt','w').write('\n'.join(out))
print(len(out))
