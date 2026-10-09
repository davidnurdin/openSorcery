# Compact Z80 disassembler
R=['b','c','d','e','h','l','(hl)','a']
RP=['bc','de','hl','sp']; RP2=['bc','de','hl','af']
CC=['nz','z','nc','c','po','pe','p','m']
ALU=['add a,','adc a,','sub ','sbc a,','and ','xor ','or ','cp ']
ROT=['rlc','rrc','rl','rr','sla','sra','sll','srl']
def dis(m,pc):
    """m: indexable memory (bytes); returns (text,len)"""
    o=pc
    def b():
        nonlocal o
        v=m[o&0xffff]; o+=1; return v
    def w():
        lo=b(); hi=b(); return lo|hi<<8
    def rel():
        d=b(); d=d-256 if d>127 else d
        return (o+d)&0xffff
    def S(n): return '%02x'%n if False else '0x%02X'%n
    def W(n): return '0x%04X'%n
    ix=None
    op=b()
    if op in (0xDD,0xFD):
        ix='ix' if op==0xDD else 'iy'
        op=b()
        if op in (0xDD,0xFD,0xED): return 'db 0x%02X'%m[pc],1
    def reg(i):
        if i==6 and ix:
            d=b(); d=d-256 if d>127 else d
            return '(%s%+d)'%(ix,d)
        if ix and i in (4,5): return ix+('h' if i==4 else 'l')
        return R[i]
    def hl(): return ix or 'hl'
    if op==0xCB:
        if ix:
            d=b(); d=d-256 if d>127 else d
            op=b(); t='(%s%+d)'%(ix,d)
        else:
            op=b(); t=R[op&7]
        x=op>>6;y=(op>>3)&7
        if x==0: s='%s %s'%(ROT[y],t)
        else: s='%s %d,%s'%(['','bit','res','set'][x],y,t)
        return s,o-pc
    if op==0xED:
        op=b(); x=op>>6;y=(op>>3)&7;z=op&7
        if x==1:
            if z==0: s='in %s,(c)'%('f' if y==6 else R[y])
            elif z==1: s='out (c),%s'%('0' if y==6 else R[y])
            elif z==2: s='%s hl,%s'%('sbc' if y&1==0 else 'adc',RP[y>>1])
            elif z==3:
                a=w(); s='ld (%s),%s'%(W(a),RP[y>>1]) if y&1==0 else 'ld %s,(%s)'%(RP[y>>1],W(a))
            elif z==4: s='neg'
            elif z==5: s='retn' if y!=1 else 'reti'
            elif z==6: s='im %s'%['0','0','1','2','0','0','1','2'][y]
            else: s=['ld i,a','ld r,a','ld a,i','ld a,r','rrd','rld','nop','nop'][y]
        elif x==2 and z<4 and y>=4:
            s=['ldi','cpi','ini','outi','ldd','cpd','ind','outd','ldir','cpir','inir','otir','lddr','cpdr','indr','otdr'][(y-4)*4+z]
        else: s='db 0xED,0x%02X'%op
        return s,o-pc
    x=op>>6;y=(op>>3)&7;z=op&7;p=y>>1;q=y&1
    if x==1:
        if op==0x76: s='halt'
        elif ix and (y==6 or z==6): 
            s='ld %s,%s'%(reg2(R,y,z,ix,b) )
        else: s='ld %s,%s'%(reg(y),reg(z))
    elif x==2:
        s=ALU[y]+reg(z)
    elif x==0:
        if z==0:
            if y==0: s='nop'
            elif y==1: s="ex af,af'"
            elif y==2: s='djnz '+W(rel())
            elif y==3: s='jr '+W(rel())
            else: s='jr %s,%s'%(CC[y-4],W(rel()))
        elif z==1:
            if q==0:
                r=hl() if p==2 else RP[p]; s='ld %s,%s'%(r,W(w()))
            else: s='add %s,%s'%(hl(),hl() if p==2 else RP[p])
        elif z==2:
            if q==0:
                if p==0: s='ld (bc),a'
                elif p==1: s='ld (de),a'
                elif p==2: s='ld (%s),%s'%(W(w()),hl())
                else: s='ld (%s),a'%W(w())
            else:
                if p==0: s='ld a,(bc)'
                elif p==1: s='ld a,(de)'
                elif p==2: s='ld %s,(%s)'%(hl(),W(w()))
                else: s='ld a,(%s)'%W(w())
        elif z==3:
            r=hl() if p==2 else RP[p]; s=('inc ' if q==0 else 'dec ')+r
        elif z==4: s='inc '+reg(y)
        elif z==5: s='dec '+reg(y)
        elif z==6:
            r=reg(y); s='ld %s,%s'%(r,S(b()))
        else: s=['rlca','rrca','rla','rra','daa','cpl','scf','ccf'][y]
    else:
        if z==0: s='ret '+CC[y]
        elif z==1:
            if q==0: s='pop '+(hl() if p==2 else RP2[p])
            else: s=['ret','exx','jp '+hl(),'ld sp,'+hl()][p]
            if p==2 and q==1: s='jp (%s)'%hl()
        elif z==2: s='jp %s,%s'%(CC[y],W(w()))
        elif z==3:
            if y==0: s='jp '+W(w())
            elif y==2: s='out (%s),a'%S(b())
            elif y==3: s='in a,(%s)'%S(b())
            elif y==4: s='ex (sp),'+hl()
            elif y==5: s='ex de,hl'
            elif y==6: s='di'
            else: s='ei'
        elif z==4: s='call %s,%s'%(CC[y],W(w()))
        elif z==5:
            if q==0: s='push '+(hl() if p==2 else RP2[p])
            else: s='call '+W(w())
        elif z==6: s=ALU[y]+S(b())
        else: s='rst 0x%02X'%(y*8)
    return s,o-pc
def reg2(R,y,z,ix,b):
    d=b(); d=d-256 if d>127 else d
    m='(%s%+d)'%(ix,d)
    if y==6: return m,R[z]
    return R[y],m
def listing(m,start,end,labels=None):
    pc=start; out=[]
    while pc<end:
        s,n=dis(m,pc)
        out.append('%04X  %-12s %s'%(pc,bytes(m[(pc+i)&0xffff] for i in range(n)).hex(' '),s)); pc+=n
    return out
