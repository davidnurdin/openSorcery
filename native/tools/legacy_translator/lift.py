#!/usr/bin/env python3
"""Z80 -> C++ static translator for the Sorcery+ game code.

pass 1:  lift.py scan  mem.bin entries.txt  addrs.txt      (finds code, writes "addr len" per instruction)
pass 2:  lift.py gen   mem.bin addrs.txt cyc.txt out.cpp   (emits C++)
"""
import re, sys

R8 = ['b', 'c', 'd', 'e', 'h', 'l', None, 'a']
RP = ['bc', 'de', 'hl', 'sp']
RP2 = ['bc', 'de', 'hl', 'af']
CC = ['!(C.f&FZ)', '(C.f&FZ)', '!(C.f&FC)', '(C.f&FC)', '!(C.f&FP)', '(C.f&FP)', '!(C.f&FS)', '(C.f&FS)']
CCN = ['nz', 'z', 'nc', 'c', 'po', 'pe', 'p', 'm']

# runtime-patched operand bytes (self modifying code): address -> read from memory at run time
SMC = set(list(range(0x8B6A, 0x8B6F)) + [0x8B70, 0x8B71] + list(range(0x8B7F, 0x8B84)) + [0x8B85, 0x8B86])


class Ins:
    def __init__(s, addr):
        s.addr = addr; s.len = 0; s.text = ''; s.body = []; s.kind = 'seq'; s.target = None
        s.cond = None; s.halt = False; s.ei = False; s.unknown = False; s.dynamic = False


def decode(m, a):
    i = Ins(a)
    p = a

    def b():
        nonlocal p
        v = m[p & 0xFFFF]; p += 1; return v
    prefix = None
    op = b()
    if op in (0xDD, 0xFD):
        prefix = 'ix' if op == 0xDD else 'iy'
        op = b()
        if op in (0xDD, 0xFD, 0xED):
            i.unknown = True; i.len = 1; return i
    def imm8():
        at = p; v = b()
        return f'M[0x{at:04X}]' if at in SMC else f'0x{v:02X}'
    def imm16():
        at = p; lo = b(); hi = b()
        if at in SMC or at + 1 in SMC: return f'(uint16_t)(M[0x{at:04X}]|M[0x{at+1:04X}]<<8)'
        return f'0x{hi<<8|lo:04X}'
    def lit16():
        at = p; lo = b(); hi = b(); return hi << 8 | lo, at in SMC or at + 1 in SMC, at
    def disp():
        d = b(); return d - 256 if d > 127 else d
    HL = f'C.{prefix}' if prefix else 'HL()'
    def setHL(v): return f'C.{prefix}={v};' if prefix else f'SETHL({v});'
    def ea_idx():
        d = disp(); return f'(uint16_t)(C.{prefix}+({d}))', d
    def get8(n, ea=None):
        if n == 6: return f'M[{ea}]'
        if prefix and n == 4: return f'(uint8_t)(C.{prefix}>>8)'
        if prefix and n == 5: return f'(uint8_t)(C.{prefix}&0xFF)'
        return f'C.{R8[n]}'
    def set8(n, v, ea=None):
        if n == 6: return f'M[{ea}]={v};'
        if prefix and n == 4: return f'C.{prefix}=(uint16_t)((C.{prefix}&0xFF)|(({v})<<8));'
        if prefix and n == 5: return f'C.{prefix}=(uint16_t)((C.{prefix}&0xFF00)|(uint8_t)({v}));'
        return f'C.{R8[n]}={v};'
    def rp16(n):  # bc de hl/ix sp
        return ['BC()', 'DE()', HL, 'C.sp'][n]
    def setrp(n, v):
        return [f'SETBC({v});', f'SETDE({v});', setHL(v), f'C.sp={v};'][n]
    body = i.body

    if op == 0xCB:
        if prefix:
            ea, d = ea_idx(); op = b()
            x, y, z = op >> 6, (op >> 3) & 7, op & 7
            body.append(f'{{ uint16_t ea={ea}; uint8_t v=M[ea];')
            if x == 1: body.append(f'  lift::bit(C,{y},v,(uint8_t)(ea>>8)); }}')
            else:
                if x == 0: body.append(f'  uint8_t r=lift::rot(C,{y},v);')
                elif x == 2: body.append(f'  uint8_t r=v&~{1<<y};')
                else: body.append(f'  uint8_t r=v|{1<<y};')
                body.append('  M[ea]=r;' + (f' C.{R8[z]}=r;' if z != 6 else '') + ' }')
            i.text = f'cb {prefix}'
        else:
            op = b(); x, y, z = op >> 6, (op >> 3) & 7, op & 7
            src = 'M[HL()]' if z == 6 else f'C.{R8[z]}'
            if x == 1:
                xy = '(uint8_t)(HL()>>8)' if z == 6 else src
                body.append(f'lift::bit(C,{y},{src},{xy});')
            elif x == 0: body.append(f'{src}=lift::rot(C,{y},{src});')
            elif x == 2: body.append(f'{src}={src}&~{1<<y};')
            else: body.append(f'{src}={src}|{1<<y};')
            i.text = 'cb'
        i.len = p - a; return i

    if op == 0xED and not prefix:
        op = b(); x, y, z = op >> 6, (op >> 3) & 7, op & 7; pp, q = y >> 1, y & 1
        i.text = 'ed'
        if x == 1:
            if z == 0 or z == 1: i.unknown = True
            elif z == 2:
                body.append(f'SETHL(lift::{"adc16" if q else "sbc16"}(C,HL(),{rp16(pp).replace("HL()","HL()")}));')
            elif z == 3:
                at = imm16()
                if q == 0: body.append(f'WW({at},{rp16(pp)});')
                else: body.append(setrp(pp, f'RW({at})'))
            elif z == 4: body.append('{ uint8_t o=C.a; C.a=0; lift::alu(C,2,o); C.a=(uint8_t)(-o); }')
            elif z == 5:
                body.append('C.iff1=C.iff2; C.pc=POP16(); goto dispatch;'); i.kind = 'ret'
            elif z == 6: body.append(f'C.im={[0,0,1,2,0,0,1,2][y]};')
            else:
                if y == 0: body.append('C.i=C.a;')
                elif y == 1: body.append('C.r=C.a;')
                elif y == 2: body.append('C.a=C.i; C.f=(C.f&FC)|(C.a&(FS|FY|FX))|(C.a?0:FZ)|(C.iff2?FP:0);')
                elif y == 3: body.append('C.a=C.r; C.f=(C.f&FC)|(C.a&(FS|FY|FX))|(C.a?0:FZ)|(C.iff2?FP:0);')
                elif y == 4: body.append('{ uint8_t v=M[HL()]; M[HL()]=(uint8_t)((C.a<<4)|(v>>4)); C.a=(C.a&0xF0)|(v&15); C.f=(C.f&FC)|(C.a&(FS|FY|FX))|(C.a?0:FZ)|PAR[C.a]; }')
                elif y == 5: body.append('{ uint8_t v=M[HL()]; M[HL()]=(uint8_t)((v<<4)|(C.a&15)); C.a=(C.a&0xF0)|(v>>4); C.f=(C.f&FC)|(C.a&(FS|FY|FX))|(C.a?0:FZ)|PAR[C.a]; }')
                else: pass
        elif x == 2 and y >= 4 and z < 2:
            dec = y & 1; rep = y >= 6
            dd = '-1' if dec else '+1'
            if z == 0:
                body.append(f'{{ uint8_t v=M[HL()]; M[DE()]=v; SETHL(HL(){dd}); SETDE(DE(){dd}); SETBC(BC()-1); uint8_t n=v+C.a;'
                            f' C.f=(C.f&(FS|FZ|FC))|(n&FX)|((n<<4)&FY)|(BC()?FP:0);')
                if rep: body.append('  if (BC()) { cyc+=EXTRA; goto LSELF; } }')
                else: body.append('}')
            else:
                body.append(f'{{ uint8_t v=M[HL()]; uint8_t res=C.a-v; uint8_t hf=((C.a^v^res)&FH); SETHL(HL(){dd}); SETBC(BC()-1);'
                            f' uint8_t n=res-(hf?1:0); C.f=(C.f&FC)|(res&FS)|(res?0:FZ)|hf|(BC()?FP:0)|FN|(n&FX)|((n<<4)&FY);')
                if rep: body.append('  if (BC() && res) { cyc+=EXTRA; goto LSELF; } }')
                else: body.append('}')
        else:
            i.unknown = True
        i.len = p - a
        if i.unknown: i.len = 2
        return i

    x, y, z = op >> 6, (op >> 3) & 7, op & 7; pp, q = y >> 1, y & 1
    i.text = f'op{op:02X}'
    if x == 1:
        if op == 0x76:
            i.halt = True; body.append('C.halted=true;')
        elif prefix and (y == 6 or z == 6):
            ea, d = ea_idx()
            if y == 6: body.append(f'M[{ea}]=C.{R8[z]};')
            else: body.append(f'C.{R8[y]}=M[{ea}];')
        elif y == 6: body.append(f'M[HL()]=C.{R8[z]};')
        elif z == 6: body.append(f'C.{R8[y]}=M[HL()];')
        else: body.append(set8(y, get8(z)))
    elif x == 2:
        if z == 6:
            if prefix: ea, d = ea_idx(); body.append(f'lift::alu(C,{y},M[{ea}]);')
            else: body.append(f'lift::alu(C,{y},M[HL()]);')
        else: body.append(f'lift::alu(C,{y},{get8(z)});')
    elif x == 0:
        if z == 0:
            if y == 0: pass
            elif y == 1: body.append("{ uint8_t t=C.a; C.a=C.a_; C.a_=t; t=C.f; C.f=C.f_; C.f_=t; }")
            else:
                d = disp(); t = (p + d) & 0xFFFF; i.target = t
                if y == 2: i.kind = 'cjump'; i.cond = ('--C.b', True)  # djnz
                elif y == 3: i.kind = 'jump'
                else: i.kind = 'cjump'; i.cond = (CC[y - 4], False)
        elif z == 1:
            if q == 0: body.append(setrp(pp, imm16()))
            else: body.append(setHL(f'lift::add16(C,{HL},{rp16(pp)})'))
        elif z == 2:
            if q == 0:
                if pp == 0: body.append('M[BC()]=C.a;')
                elif pp == 1: body.append('M[DE()]=C.a;')
                elif pp == 2: body.append(f'WW({imm16()},{HL});')
                else: body.append(f'M[{imm16()}]=C.a;')
            else:
                if pp == 0: body.append('C.a=M[BC()];')
                elif pp == 1: body.append('C.a=M[DE()];')
                elif pp == 2: body.append(setHL(f'RW({imm16()})'))
                else: body.append(f'C.a=M[{imm16()}];')
        elif z == 3:
            dd = '-1' if q else '+1'
            body.append(setrp(pp, f'(uint16_t)({rp16(pp)}{dd})'))
        elif z in (4, 5):
            fn = 'inc8' if z == 4 else 'dec8'
            if y == 6:
                if prefix: ea, d = ea_idx(); body.append(f'{{ uint16_t ea={ea}; M[ea]=lift::{fn}(C,M[ea]); }}')
                else: body.append(f'M[HL()]=lift::{fn}(C,M[HL()]);')
            else: body.append(set8(y, f'lift::{fn}(C,{get8(y)})'))
        elif z == 6:
            if y == 6:
                if prefix: ea, d = ea_idx(); body.append(f'M[{ea}]={imm8()};')
                else: body.append(f'M[HL()]={imm8()};')
            else: body.append(set8(y, imm8()))
        else:
            body.append([
                '{ uint8_t c0=C.a>>7; C.a=(uint8_t)((C.a<<1)|c0); C.f=(C.f&(FS|FZ|FP))|(C.a&(FY|FX))|c0; }',
                '{ uint8_t c0=C.a&1; C.a=(uint8_t)((C.a>>1)|(c0<<7)); C.f=(C.f&(FS|FZ|FP))|(C.a&(FY|FX))|c0; }',
                '{ uint8_t c0=C.a>>7; C.a=(uint8_t)((C.a<<1)|(C.f&FC)); C.f=(C.f&(FS|FZ|FP))|(C.a&(FY|FX))|c0; }',
                '{ uint8_t c0=C.a&1; C.a=(uint8_t)((C.a>>1)|((C.f&FC)<<7)); C.f=(C.f&(FS|FZ|FP))|(C.a&(FY|FX))|c0; }',
                'lift::daa(C);',
                'C.a=(uint8_t)~C.a; C.f=(C.f&(FS|FZ|FP|FC))|(C.a&(FY|FX))|FH|FN;',
                'C.f=(C.f&(FS|FZ|FP))|(C.a&(FY|FX))|FC;',
                'C.f=(C.f&(FS|FZ|FP))|(C.a&(FY|FX))|((C.f&FC)?FH:0)|((C.f&FC)^FC);'][y])
    else:
        if z == 0:
            i.kind = 'cret'; i.cond = (CC[y], False)
        elif z == 1:
            if q == 0:
                v = 'POP16()'
                if pp == 0: body.append(f'SETBC({v});')
                elif pp == 1: body.append(f'SETDE({v});')
                elif pp == 2: body.append(setHL(v))
                else: body.append(f'SETAF({v});')
            else:
                if pp == 0: i.kind = 'ret'
                elif pp == 1: body.append('{ uint8_t t; t=C.b;C.b=C.b_;C.b_=t; t=C.c;C.c=C.c_;C.c_=t; t=C.d;C.d=C.d_;C.d_=t; t=C.e;C.e=C.e_;C.e_=t; t=C.h;C.h=C.h_;C.h_=t; t=C.l;C.l=C.l_;C.l_=t; }')
                elif pp == 2: i.kind = 'jpind'; body.append(f'C.pc={HL};')
                else: body.append(f'C.sp={HL};')
        elif z == 2:
            t, dyn, at = lit16(); i.kind = 'cjump'; i.target = t; i.cond = (CC[y], False)
            if dyn: i.dynamic = at
        elif z == 3:
            if y == 0:
                t, dyn, at = lit16(); i.kind = 'jump'; i.target = t
                if dyn: i.dynamic = at
            elif y in (2, 3): i.unknown = True; imm8()
            elif y == 4: body.append(f'{{ uint16_t v=RW(C.sp); WW(C.sp,{HL}); {setHL("v")} }}')
            elif y == 5: body.append('{ uint16_t t=DE(); SETDE(HL()); SETHL(t); }')
            elif y == 6: body.append('C.iff1=C.iff2=false;')
            else: body.append('C.iff1=C.iff2=true; C.eiPending=true;'); i.ei = True
        elif z == 4:
            t, dyn, at = lit16(); i.kind = 'ccall'; i.target = t; i.cond = (CC[y], False)
            if dyn: i.dynamic = at
        elif z == 5:
            if q == 0:
                v = [ 'BC()', 'DE()', HL, 'AF()'][pp]
                body.append(f'PUSH16({v});')
            else:
                t, dyn, at = lit16(); i.kind = 'call'; i.target = t
                if dyn: i.dynamic = at
        elif z == 6: body.append(f'lift::alu(C,{y},{imm8()});')
        else:
            i.kind = 'call'; i.target = y * 8
    i.len = p - a
    i.text = i.text
    return i


def scan(m, entries):
    seen = {}
    work = list(entries)
    while work:
        a = work.pop()
        while 0 <= a < 0x10000 and a not in seen:
            ins = decode(m, a)
            if ins.unknown: break
            seen[a] = ins
            nxt = a + ins.len
            if ins.target is not None and not ins.dynamic and ins.target not in seen:
                tg = ins.target
                if not (tg >= 0xB900 or tg < 0x100):
                    work.append(tg)
            if ins.kind == 'call' and ins.target == 0x18:
                work.append(nxt + 2); break
            if ins.kind == 'call' and ins.target == 0x08:
                break
            if ins.kind == 'call' and ins.target == 0x8CF6:
                q = nxt
                while True:
                    ch = m[q]
                    if ch == 0x1F: q += 3
                    elif ch == 0x1E: q += 2
                    elif ch & 0x80: q += 1; break
                    else: q += 1
                work.append(q); break
            if ins.kind in ('jump', 'ret', 'jpind'):
                break
            if ins.halt: pass
            a = nxt
    return seen


def main():
    mode = sys.argv[1]
    m = open(sys.argv[2], 'rb').read()
    if mode == 'scan':
        entries = [int(x, 16) for x in open(sys.argv[3]).read().split()]
        seen = scan(m, entries)
        open(sys.argv[4], 'w').write(''.join('%04X %d\n' % (a, i.len) for a, i in sorted(seen.items())))
        print(len(seen), 'instructions')
        return
    addrs = {}
    for l in open(sys.argv[3]):
        a, n = l.split(); addrs[int(a, 16)] = int(n)
    cyc = {}
    for l in open(sys.argv[4]):
        a, base, extra = l.split(); cyc[int(a, 16)] = (int(base), int(extra))
    import os
    ovr = set()
    if len(sys.argv) > 6:
        ovr = {int(x, 16) for x in open(sys.argv[6]).read().split()}
    ins = {a: decode(m, a) for a in addrs}
    out = []
    w = out.append
    w('// GENERATED by native/tools/lift.py from the decrypted game code - do not edit.')
    w('#include "lift_rt.h"')
    w('using namespace lift;')
    w('#define HL() ((uint16_t)(C.h<<8|C.l))')
    w('#define BC() ((uint16_t)(C.b<<8|C.c))')
    w('#define DE() ((uint16_t)(C.d<<8|C.e))')
    w('#define AF() ((uint16_t)(C.a<<8|C.f))')
    w('#define SETHL(v) do{uint16_t _v=(v);C.h=_v>>8;C.l=(uint8_t)_v;}while(0)')
    w('#define SETBC(v) do{uint16_t _v=(v);C.b=_v>>8;C.c=(uint8_t)_v;}while(0)')
    w('#define SETDE(v) do{uint16_t _v=(v);C.d=_v>>8;C.e=(uint8_t)_v;}while(0)')
    w('#define SETAF(v) do{uint16_t _v=(v);C.a=_v>>8;C.f=(uint8_t)_v;}while(0)')
    w('#define RW(a) ((uint16_t)(M[(uint16_t)(a)]|M[(uint16_t)((a)+1)]<<8))')
    w('#define WW(a,v) do{uint16_t _a=(a),_v=(v);M[_a]=(uint8_t)_v;M[(uint16_t)(_a+1)]=_v>>8;}while(0)')
    w('#define PUSH16(v) do{uint16_t _v=(v);C.sp-=2;M[C.sp]=(uint8_t)_v;M[(uint16_t)(C.sp+1)]=_v>>8;}while(0)')
    w('#define POP16() ([&]{uint16_t _v=RW(C.sp);C.sp+=2;return _v;}())')
    w('#ifdef LIFT_PROFILE\nextern uint32_t gLiftProf[65536];\n#define Y(a) do{gLiftProf[a]++;C.eiPending=false;if(cyc>=limit){C.pc=(a);return false;}}while(0)\n#else\n#define Y(a) do{C.eiPending=false;if(cyc>=limit){C.pc=(a);return false;}}while(0)\n#endif')
    w('#define YE(a) do{if(cyc>=limit){C.pc=(a);return false;}}while(0)')
    w('bool lifted_run(Z80& C, uint8_t* M, int64_t& cyc, int64_t limit, LiftEnv& env, uint16_t sentinel, int bypass) {')
    w('  dispatch:')
    w('  if (C.pc == sentinel) return true;')
    w('  switch (C.pc) {')
    for a in sorted(addrs):
        if a in ovr:
            w(f'  case 0x{a:04X}: if (bypass == 0x{a:04X}) {{ bypass = -1; goto L{a:04X}; }} if (env.native(0x{a:04X})) goto dispatch; goto L{a:04X};')
        else:
            w(f'  case 0x{a:04X}: goto L{a:04X};')
    w('  default: {')
    w('    if (C.pc >= 0xB900 || C.pc < 0x40) { if (env.hook(C.pc)) goto dispatch; }')
    w('    return false; }')   # unknown address: caller falls back to the interpreter
    w('  }')
    after_ei = set()
    for a, i in ins.items():
        if i.ei: after_ei.add(a + i.len)
    for a in sorted(addrs):
        i = ins[a]
        base, extra = cyc.get(a, (4, 0))
        nxt = a + i.len
        w(f'L{a:04X}:')
        w(f'  {"YE" if a in after_ei else "Y"}(0x{a:04X});')
        if i.unknown:
            w(f'  C.pc=0x{a:04X}; return false;  // not translated'); continue
        body = [l.replace('EXTRA', str(base + extra)).replace('LSELF', f'L{a:04X}') for l in i.body]
        for l in body: w('  ' + l)
        w(f'  cyc+={base};')
        def target_goto(t, dyn):
            if dyn is not False and dyn:
                return f'C.pc=(uint16_t)(M[0x{dyn:04X}]|M[0x{dyn+1:04X}]<<8); goto dispatch;'
            if t in ovr: return f'C.pc=0x{t:04X}; goto dispatch;'
            if t in addrs: return f'goto L{t:04X};'
            return f'C.pc=0x{t:04X}; goto dispatch;'
        def call_code(t, dyn):
            code = f'PUSH16(0x{nxt:04X}); '
            if dyn: return code + target_goto(t, dyn)
            if t >= 0xB900 or t < 0x100:
                return code + f'C.pc=0x{t:04X}; goto dispatch;'
            return code + target_goto(t, False)
        if i.kind == 'jump':
            w('  ' + target_goto(i.target, i.dynamic))
        elif i.kind == 'cjump':
            if i.cond[1]:   # djnz
                w(f'  if (--C.b) {{ cyc+={extra}; {target_goto(i.target, False)} }}')
            else:
                w(f'  if ({i.cond[0]}) {{ cyc+={extra}; {target_goto(i.target, i.dynamic)} }}')
        elif i.kind == 'call':
            w('  ' + call_code(i.target, i.dynamic))
        elif i.kind == 'ccall':
            w(f'  if ({i.cond[0]}) {{ cyc+={extra}; {call_code(i.target, i.dynamic)} }}')
        elif i.kind == 'ret':
            w('  C.pc=POP16(); goto dispatch;') if not any('goto dispatch' in b for b in body) else None
        elif i.kind == 'cret':
            w(f'  if ({i.cond[0]}) {{ cyc+={extra}; C.pc=POP16(); goto dispatch; }}')
        elif i.kind == 'jpind':
            w('  goto dispatch;')
        elif i.halt:
            w(f'  C.pc=0x{nxt:04X}; return false;')
        else:
            if nxt not in addrs:
                w(f'  C.pc=0x{nxt:04X}; goto dispatch;')
    w('}')
    open(sys.argv[5], 'w').write('\n'.join(out) + '\n')
    print('generated', len(addrs), 'instructions')

main()
