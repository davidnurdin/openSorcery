# Minimal but accurate-enough Z80 emulator (documented flags + F3/F5 copy)
PAR=[0]*256
for i in range(256): PAR[i]=4 if bin(i).count('1')%2==0 else 0
class Z80:
    def __init__(s,mem=None):
        s.m=mem if mem is not None else bytearray(65536)
        s.a=s.f=s.b=s.c=s.d=s.e=s.h=s.l=0
        s.a_=s.f_=s.b_=s.c_=s.d_=s.e_=s.h_=s.l_=0
        s.ix=s.iy=0; s.sp=0xC000; s.pc=0; s.i=s.r=0; s.iff=0; s.im=0; s.halt=False
        s.t=0; s.io_in=lambda p:0xff; s.io_out=lambda p,v:None
        s.hooks={}   # pc -> fn(cpu) returns True to skip normal exec
    # pair helpers
    @property
    def bc(s): return s.b<<8|s.c
    @bc.setter
    def bc(s,v): s.b=v>>8&255; s.c=v&255
    @property
    def de(s): return s.d<<8|s.e
    @de.setter
    def de(s,v): s.d=v>>8&255; s.e=v&255
    @property
    def hl(s): return s.h<<8|s.l
    @hl.setter
    def hl(s,v): s.h=v>>8&255; s.l=v&255
    @property
    def af(s): return s.a<<8|s.f
    @af.setter
    def af(s,v): s.a=v>>8&255; s.f=v&255
    def rb(s,a): return s.m[a&0xffff]
    def wb(s,a,v): s.m[a&0xffff]=v&255
    def rw(s,a): return s.m[a&0xffff]|s.m[(a+1)&0xffff]<<8
    def ww(s,a,v): s.m[a&0xffff]=v&255; s.m[(a+1)&0xffff]=v>>8&255
    def fetch(s):
        v=s.m[s.pc]; s.pc=(s.pc+1)&0xffff; return v
    def fetchw(s):
        v=s.m[s.pc]|s.m[(s.pc+1)&0xffff]<<8; s.pc=(s.pc+2)&0xffff; return v
    def push(s,v): s.sp=(s.sp-2)&0xffff; s.ww(s.sp,v)
    def pop(s): v=s.rw(s.sp); s.sp=(s.sp+2)&0xffff; return v
    # flags: S Z Y H X P N C = 80 40 20 10 08 04 02 01
    def getr(s,i):
        if i==0: return s.b
        if i==1: return s.c
        if i==2: return s.d
        if i==3: return s.e
        if i==4: return s.h
        if i==5: return s.l
        if i==6: return s.rb(s.hl)
        return s.a
    def setr(s,i,v):
        v&=255
        if i==0: s.b=v
        elif i==1: s.c=v
        elif i==2: s.d=v
        elif i==3: s.e=v
        elif i==4: s.h=v
        elif i==5: s.l=v
        elif i==6: s.wb(s.hl,v)
        else: s.a=v
    def alu(s,op,v):
        a=s.a; c=s.f&1
        if op in (1,3): pass
        else: c0=0
        if op==0 or op==1:
            cc=c if op==1 else 0
            r=a+v+cc; res=r&255
            s.f=(res&0xA8)|(0x40 if res==0 else 0)|((a^v^res)&0x10)|(4 if ((a^v^0x80)&(a^res)&0x80) else 0)|(r>>8&1)
            s.a=res
        elif op in (2,3,7):
            cc=c if op==3 else 0
            r=a-v-cc; res=r&255
            f=(res&0x80)|(0x40 if res==0 else 0)|((a^v^res)&0x10)|(4 if ((a^v)&(a^res)&0x80) else 0)|2|(r>>8&1)&1
            f|=(v&0x28) if op==7 else (res&0x28)
            s.f=f
            if op!=7: s.a=res
        elif op==4:
            res=a&v; s.a=res; s.f=(res&0xA8)|(0x40 if res==0 else 0)|0x10|PAR[res]
        elif op==5:
            res=a^v; s.a=res; s.f=(res&0xA8)|(0x40 if res==0 else 0)|PAR[res]
        else:
            res=a|v; s.a=res; s.f=(res&0xA8)|(0x40 if res==0 else 0)|PAR[res]
    def inc8(s,v):
        r=(v+1)&255
        s.f=(s.f&1)|(r&0xA8)|(0x40 if r==0 else 0)|(0x10 if (v&15)==15 else 0)|(4 if v==0x7f else 0)
        return r
    def dec8(s,v):
        r=(v-1)&255
        s.f=(s.f&1)|(r&0xA8)|(0x40 if r==0 else 0)|(0x10 if (v&15)==0 else 0)|(4 if v==0x80 else 0)|2
        return r
    def add16(s,x,y):
        r=x+y
        s.f=(s.f&0xC4)|((r>>8)&0x28)|(((x^y^r)>>8)&0x10)|(r>>16&1)
        return r&0xffff
    def adc16(s,x,y):
        c=s.f&1; r=x+y+c; res=r&0xffff
        s.f=((res>>8)&0xA8)|(0x40 if res==0 else 0)|(((x^y^res)>>8)&0x10)|(4 if ((x^y^0x8000)&(x^res)&0x8000) else 0)|(r>>16&1)
        return res
    def sbc16(s,x,y):
        c=s.f&1; r=x-y-c; res=r&0xffff
        s.f=((res>>8)&0xA8)|(0x40 if res==0 else 0)|(((x^y^res)>>8)&0x10)|(4 if ((x^y)&(x^res)&0x8000) else 0)|2|(r>>16&1)
        return res
    def cond(s,c):
        f=s.f
        return [not f&0x40, f&0x40, not f&1, f&1, not f&4, f&4, not f&0x80, f&0x80][c]
    def rot(s,k,v):
        c=s.f&1
        if k==0: r=((v<<1)|(v>>7))&255; co=v>>7
        elif k==1: r=((v>>1)|(v<<7))&255; co=v&1
        elif k==2: r=((v<<1)|c)&255; co=v>>7
        elif k==3: r=((v>>1)|(c<<7))&255; co=v&1
        elif k==4: r=(v<<1)&255; co=v>>7
        elif k==5: r=(v>>1)|(v&0x80); co=v&1
        elif k==6: r=((v<<1)|1)&255; co=v>>7
        else: r=v>>1; co=v&1
        s.f=(r&0xA8)|(0x40 if r==0 else 0)|PAR[r]|co
        return r
    def step(s):
        h=s.hooks.get(s.pc)
        if h and h(s): return
        s.r=(s.r&0x80)|((s.r+1)&0x7f)
        op=s.fetch()
        pre=None
        if op==0xDD or op==0xFD:
            pre='ix' if op==0xDD else 'iy'
            op=s.fetch()
            if op in (0xDD,0xFD,0xED): s.pc-=1; return
        if op==0xCB: return s.cb(pre)
        if op==0xED: return s.ed()
        x=op>>6;y=(op>>3)&7;z=op&7;p=y>>1;q=y&1
        if pre:
            ixv=getattr(s,pre)
            def idx():
                d=s.fetch(); d=d-256 if d>127 else d; return (ixv+d)&0xffff
            def gr(i,a=None):
                if i==4: return ixv>>8
                if i==5: return ixv&255
                if i==6: return s.rb(a)
                return s.getr(i)
            def sr(i,v,a=None):
                v&=255
                if i==4: setattr(s,pre,(v<<8)|(ixv&255))
                elif i==5: setattr(s,pre,(ixv&0xff00)|v)
                elif i==6: s.wb(a,v)
                else: s.setr(i,v)
            hlv=ixv
        else:
            hlv=None
        def HL(): return getattr(s,pre) if pre else s.hl
        def setHL(v):
            if pre: setattr(s,pre,v&0xffff)
            else: s.hl=v
        if x==1:
            if op==0x76: s.halt=True; s.pc-=1; return
            if pre and (y==6 or z==6):
                a=idx()
                if y==6: s.wb(a,s.getr(z))
                else: s.setr(y,s.rb(a))
            elif pre:
                sr(y,gr(z))
            else: s.setr(y,s.getr(z))
            return
        if x==2:
            if pre:
                if z==6: v=s.rb(idx())
                else: v=gr(z)
            else: v=s.getr(z)
            s.alu(y,v); return
        if x==0:
            if z==0:
                if y==0: return
                if y==1:
                    s.a,s.a_=s.a_,s.a; s.f,s.f_=s.f_,s.f; return
                d=s.fetch(); d=d-256 if d>127 else d
                if y==2:
                    s.b=(s.b-1)&255
                    if s.b: s.pc=(s.pc+d)&0xffff
                elif y==3: s.pc=(s.pc+d)&0xffff
                elif s.cond(y-4): s.pc=(s.pc+d)&0xffff
                return
            if z==1:
                if q==0:
                    v=s.fetchw()
                    if p==0: s.bc=v
                    elif p==1: s.de=v
                    elif p==2: setHL(v)
                    else: s.sp=v
                else:
                    rp=[s.bc,s.de,HL(),s.sp][p]
                    setHL(s.add16(HL(),rp))
                return
            if z==2:
                if q==0:
                    if p==0: s.wb(s.bc,s.a)
                    elif p==1: s.wb(s.de,s.a)
                    elif p==2: s.ww(s.fetchw(),HL())
                    else: s.wb(s.fetchw(),s.a)
                else:
                    if p==0: s.a=s.rb(s.bc)
                    elif p==1: s.a=s.rb(s.de)
                    elif p==2: setHL(s.rw(s.fetchw()))
                    else: s.a=s.rb(s.fetchw())
                return
            if z==3:
                d=1 if q==0 else -1
                if p==0: s.bc=(s.bc+d)&0xffff
                elif p==1: s.de=(s.de+d)&0xffff
                elif p==2: setHL(HL()+d)
                else: s.sp=(s.sp+d)&0xffff
                return
            if z==4 or z==5:
                f=s.inc8 if z==4 else s.dec8
                if pre:
                    if y==6: a=idx(); s.wb(a,f(s.rb(a)))
                    else: sr(y,f(gr(y)))
                else: s.setr(y,f(s.getr(y)))
                return
            if z==6:
                if pre and y==6:
                    a=idx(); s.wb(a,s.fetch())
                elif pre: sr(y,s.fetch())
                else: s.setr(y,s.fetch())
                return
            a=s.a
            if y==0:
                s.a=((a<<1)|(a>>7))&255; s.f=(s.f&0xC4)|(s.a&0x28)|(a>>7)
            elif y==1:
                s.a=((a>>1)|(a<<7))&255; s.f=(s.f&0xC4)|(s.a&0x28)|(a&1)
            elif y==2:
                s.a=((a<<1)|(s.f&1))&255; s.f=(s.f&0xC4)|(s.a&0x28)|(a>>7)
            elif y==3:
                s.a=((a>>1)|((s.f&1)<<7))&255; s.f=(s.f&0xC4)|(s.a&0x28)|(a&1)
            elif y==4:
                f=s.f; adj=0; c=f&1
                if (f&0x10) or (a&15)>9: adj|=6
                if c or a>0x99: adj|=0x60; c=1
                if f&2: r=(a-adj)&255; h=0x10 if (f&0x10) and (a&15)<6 else 0
                else: r=(a+adj)&255; h=0x10 if (a&15)>9 else 0
                s.a=r; s.f=(r&0xA8)|(0x40 if r==0 else 0)|h|PAR[r]|(f&2)|c
            elif y==5:
                s.a=a^255; s.f=(s.f&0xC5)|(s.a&0x28)|0x12
            elif y==6: s.f=(s.f&0xC4)|(a&0x28)|1
            else: s.f=(s.f&0xC4)|(a&0x28)|((s.f&1)<<4)|((s.f&1)^1)
            return
        # x==3
        if z==0:
            if s.cond(y): s.pc=s.pop()
            return
        if z==1:
            if q==0:
                v=s.pop()
                if p==0: s.bc=v
                elif p==1: s.de=v
                elif p==2: setHL(v)
                else: s.af=v
            else:
                if p==0: s.pc=s.pop()
                elif p==1:
                    s.b,s.b_=s.b_,s.b; s.c,s.c_=s.c_,s.c; s.d,s.d_=s.d_,s.d; s.e,s.e_=s.e_,s.e; s.h,s.h_=s.h_,s.h; s.l,s.l_=s.l_,s.l
                elif p==2: s.pc=HL()
                else: s.sp=HL()
            return
        if z==2:
            a=s.fetchw()
            if s.cond(y): s.pc=a
            return
        if z==3:
            if y==0: s.pc=s.fetchw()
            elif y==2: s.io_out(s.a<<8|s.fetch(),s.a)
            elif y==3: s.a=s.io_in(s.a<<8|s.fetch())&255
            elif y==4:
                v=s.rw(s.sp); s.ww(s.sp,HL()); setHL(v)
            elif y==5: s.de,s.hl=s.hl,s.de
            elif y==6: s.iff=0
            else: s.iff=1
            return
        if z==4:
            a=s.fetchw()
            if s.cond(y): s.push(s.pc); s.pc=a
            return
        if z==5:
            if q==0:
                v=[s.bc,s.de,HL(),s.af][p]; s.push(v)
            else:
                a=s.fetchw(); s.push(s.pc); s.pc=a
            return
        if z==6: s.alu(y,s.fetch()); return
        s.push(s.pc); s.pc=y*8
    def cb(s,pre):
        if pre:
            d=s.fetch(); d=d-256 if d>127 else d
            a=(getattr(s,pre)+d)&0xffff
            op=s.fetch(); v=s.rb(a)
        else:
            op=s.fetch(); v=s.getr(op&7)
        x=op>>6;y=(op>>3)&7;z=op&7
        if x==0: r=s.rot(y,v)
        elif x==1:
            r=None; res=v&(1<<y)
            s.f=(s.f&1)|0x10|(0x40|4 if res==0 else 0)|(0x80 if res&0x80 else 0)|(v&0x28)
        elif x==2: r=v&~(1<<y)&255
        else: r=v|(1<<y)
        if r is not None:
            if pre:
                s.wb(a,r)
                if z!=6: s.setr(z,r)
            else: s.setr(z,r)
    def ed(s):
        op=s.fetch(); x=op>>6;y=(op>>3)&7;z=op&7;p=y>>1;q=y&1
        if x==1:
            if z==0:
                v=s.io_in(s.bc)&255
                s.f=(s.f&1)|(v&0xA8)|(0x40 if v==0 else 0)|PAR[v]
                if y!=6: s.setr(y,v)
            elif z==1: s.io_out(s.bc,0 if y==6 else s.getr(y))
            elif z==2:
                rp=[s.bc,s.de,s.hl,s.sp][p]
                s.hl=s.sbc16(s.hl,rp) if q==0 else s.adc16(s.hl,rp)
            elif z==3:
                a=s.fetchw()
                if q==0: s.ww(a,[s.bc,s.de,s.hl,s.sp][p])
                else:
                    v=s.rw(a)
                    if p==0: s.bc=v
                    elif p==1: s.de=v
                    elif p==2: s.hl=v
                    else: s.sp=v
            elif z==4:
                a=s.a; s.a=0; s.alu(7,a); s.a=(-a)&255
            elif z==5: s.pc=s.pop()
            elif z==6: s.im=[0,0,1,2,0,0,1,2][y]
            else:
                if y==0: s.i=s.a
                elif y==1: s.r=s.a
                elif y==2:
                    s.a=s.i; s.f=(s.f&1)|(s.a&0xA8)|(0x40 if s.a==0 else 0)|(4 if s.iff else 0)
                elif y==3:
                    s.a=s.r; s.f=(s.f&1)|(s.a&0xA8)|(0x40 if s.a==0 else 0)|(4 if s.iff else 0)
                elif y==4:
                    v=s.rb(s.hl); s.wb(s.hl,((s.a&15)<<4)|(v>>4)); s.a=(s.a&0xf0)|(v&15)
                    s.f=(s.f&1)|(s.a&0xA8)|(0x40 if s.a==0 else 0)|PAR[s.a]
                elif y==5:
                    v=s.rb(s.hl); s.wb(s.hl,((v<<4)&0xf0)|(s.a&15)); s.a=(s.a&0xf0)|(v>>4)
                    s.f=(s.f&1)|(s.a&0xA8)|(0x40 if s.a==0 else 0)|PAR[s.a]
            return
        if x==2 and y>=4:
            k=(y-4)*4+z
            rep=y>=6; dec=(y&1)
            if z==0: # ldi/ldd
                v=s.rb(s.hl); s.wb(s.de,v)
                dd=-1 if dec else 1
                s.hl=(s.hl+dd)&0xffff; s.de=(s.de+dd)&0xffff; s.bc=(s.bc-1)&0xffff
                n=(v+s.a)&255
                s.f=(s.f&0xC1)|(n&8)|((n<<4)&0x20)|(4 if s.bc else 0)
                if rep and s.bc: s.pc=(s.pc-2)&0xffff
            elif z==1: # cpi/cpd
                v=s.rb(s.hl); dd=-1 if dec else 1
                r=(s.a-v)&255; hf=1 if (s.a&15)<(v&15) else 0
                s.hl=(s.hl+dd)&0xffff; s.bc=(s.bc-1)&0xffff
                n=(r-hf)&255
                s.f=(s.f&1)|(r&0x80)|(0x40 if r==0 else 0)|(hf<<4)|(4 if s.bc else 0)|2|(n&8)|((n<<4)&0x20)
                if rep and s.bc and r!=0: s.pc=(s.pc-2)&0xffff
            elif z==2: # ini/ind
                v=s.io_in(s.bc)&255; s.wb(s.hl,v); s.b=(s.b-1)&255
                s.hl=(s.hl+(-1 if dec else 1))&0xffff
                s.f=(s.f&1)|(s.b&0xA8)|(0x40 if s.b==0 else 0)|2
                if rep and s.b: s.pc=(s.pc-2)&0xffff
            else: # outi/outd
                v=s.rb(s.hl); s.b=(s.b-1)&255; s.io_out(s.bc,v)
                s.hl=(s.hl+(-1 if dec else 1))&0xffff
                s.f=(s.f&1)|(s.b&0xA8)|(0x40 if s.b==0 else 0)|2
                if rep and s.b: s.pc=(s.pc-2)&0xffff
            return
