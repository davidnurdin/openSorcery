import sys,re; sys.path.insert(0,'tools'); import z80dis
def recdis(m,entries,lo=0x7000,hi=0xa14a):
    code={}   # addr->(text,len)
    work=list(entries); calls=set(); fw={}; strs={}
    while work:
        a=work.pop()
        while lo<=a<hi and a not in code:
            t,n=z80dis.dis(m,a)
            if t.startswith('db '): break
            code[a]=(t,n)
            mm=re.match(r'(jp|call|jr|djnz)\s+(?:\w+,)?0x([0-9A-F]{4})$',t)
            if mm:
                tg=int(mm.group(2),16)
                if lo<=tg<hi: work.append(tg)
                elif tg>=0xb900 or tg<0x100: fw[tg]=fw.get(tg,0)+1
                if mm.group(1)=='call' and lo<=tg<hi: calls.add(tg)
            if t=='call 0x8CF6':
                # inline string: ctrl 0x1F x,y ; 0x1E n ; text; last char bit7
                p=a+n
                while True:
                    c=m[p]
                    if c==0x1F: p+=3
                    elif c==0x1E: p+=2
                    elif c&0x80: p+=1; break
                    else: p+=1
                strs[a+n]=p
                code[a]=(t,n)
                a=p; continue
            if t in('ret','reti','retn') or t=='jp 0x'+t[-4:] and t.startswith('jp 0x') or t.startswith('jr 0x') or t.startswith('jp (') :
                break
            if t.startswith('rst 0x18'):  # far call: 2-byte pointer follows
                pass
            a+=n
    recdis.strs=strs
    return code,calls,fw
if __name__=='__main__':
    m=open('out/mem1.bin','rb').read()
    code,calls,fw=recdis(m,[0x7000,0x8d55])
    print(len(code),'insns', len(calls),'subs')
    print(sorted((hex(k),v) for k,v in fw.items()))
    print(sum(n for t,n in code.values()),'bytes of code')
