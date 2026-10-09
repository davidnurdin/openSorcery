# Sorcery+ (CPC 6128, French disk) reverse-engineering notes
- DSK: extended, 40 tracks x 9 sectors (IDs C1..C9), CP/M data format, 1 KB blocks, directory in blocks 0-1.
- Files have AMSDOS headers; bodies are scrambled: plain = ror1(-b) - 0x25 (loader at 0x4000, hidden track 3).
- Loader loads CONALP->0100 (font), CODE->7000 (game), TISCRN->69C0, SOUND.DAT->A600, then jp 7000.
  CODE later loads (lists at 94A5/94B5/94D0/94D6): CONSET1/2->5800, SMESSC->0A00, MUCODE->4F00, SPRITES1/2->0A00, LOCDATA->4000.
- Rooms: 79 rooms, RLE-compressed, one sector each on tracks 31..39 (track=0x1F+n/9, id=0xC1+n%9), magic "DC ".
- Hidden AMSDOS BIOS RSX names 0x81..0x89 (set message, setup, select format, read sector, write sector, ..., move track).
- Variables: IY=6800 (game flags), 9300-9400 misc, text print inline strings (call 8CF6), interrupt phases tables 92DD/92E9,
  entity state table 935E, XOR sprite blitter with self-modifying code at 8B6A/8B7F.
- Firmware used: KM TEST KEY/JOYSTICK, SCR/MC ink+mode (raster split via 6 interrupts/frame), CAS in, sound manager, KL events.
