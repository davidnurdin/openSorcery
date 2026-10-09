import sys; sys.path.insert(0,'tools')
import z80, z80dis
d=open('out/files/DISC2.BIN','rb').read()
mem=bytearray(65536); body=d[128:]; mem[0xa000:0xa000+len(body)]=body
c=z80.Z80(mem); c.pc=0xa000; c.sp=0xbfff
n=0
seen=set()
while n<3_000_000:
    if c.pc>=0xa018 and c.pc not in seen and c.pc<0xa700:
        seen.add(c.pc)
        if len(seen)>400: break
    if c.pc<0xa000 or c.pc>=0xa648+0x80:
        print('left code to %04x after %d steps; sp=%04x'%(c.pc,n,c.sp)); break
    c.step(); n+=1
print(n,hex(c.pc))
open('out/disc2_dec.bin','wb').write(bytes(mem[0xa000:0xa800]))
print('\n'.join(z80dis.listing(mem,0xa018,0xa140)))
