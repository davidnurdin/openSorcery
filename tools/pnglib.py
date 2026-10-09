import zlib, struct
def write_png(path, w, h, rows):
    """rows: list of bytes (RGB, w*3 each)"""
    raw = b''.join(b'\0' + r for r in rows)
    def ch(t, c): return struct.pack('>I', len(c)) + t + c + struct.pack('>I', zlib.crc32(t + c))
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + ch(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) +
                           ch(b'IDAT', zlib.compress(raw, 9)) + ch(b'IEND', b''))
LV = (0, 128, 255)
# CPC hardware colour number -> RGB
FW2HW = [0x14,0x04,0x15,0x1C,0x18,0x1D,0x0C,0x05,0x0D,0x16,0x06,0x17,0x1E,0x00,0x1F,0x0E,0x07,0x0F,0x12,0x02,0x13,0x1A,0x19,0x1B,0x0A,0x03,0x0B]
HW = {}
for ink, hw in enumerate(FW2HW):
    HW[hw] = (LV[(ink//3)%3], LV[ink//9], LV[ink%3])
HW[1] = HW[0]; HW[8] = HW[5]; HW[9] = HW[3]; HW[0x10] = HW[4]; HW[0x11] = HW[2]
def fw_rgb(ink): return HW[FW2HW[ink % 27]]
def hw_rgb(h): return HW[h & 0x1F]
def mode0_pix(b):
    return ((b>>7&1)|(b>>3&1)<<1|(b>>5&1)<<2|(b>>1&1)<<3, (b>>6&1)|(b>>2&1)<<1|(b>>4&1)<<2|(b&1)<<3)
def mode1_pix(b):
    return [((b>>(3-p))&1)|((b>>(7-p))&1)<<1 for p in range(4)]
def blit(canvas, W, x, y, pixels, w, pal, scale_x=1, scale_y=1):
    """pixels: list of rows of pen numbers; canvas: list of bytearrays"""
    for r, row in enumerate(pixels):
        for c, pen in enumerate(row):
            rgb = bytes(pal[pen])
            for sy in range(scale_y):
                line = canvas[y + r * scale_y + sy]
                for sx in range(scale_x):
                    o = (x + c * scale_x + sx) * 3
                    line[o:o+3] = rgb
def canvas(w, h): return [bytearray(w * 3) for _ in range(h)]
def save(path, cv, w, h): write_png(path, w, h, [bytes(r) for r in cv])
