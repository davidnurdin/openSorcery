# Resources of the native port

The native version (`sorcery-native`) reads everything it needs from this folder; the original `.dsk` is **not** needed.
It looks for the folder with `--res DIR`, `$SORCERY_RES`, `./resources`, next to the executable, `../resources`.

| path | content |
|---|---|
| `graphics/sprites1.png`, `sprites2.png` | sprite sheets, 12 frames per row, each frame 12x24 pixels (mode 0, 16 pens) - set 1 = normal game, set 2 = second set |
| `graphics/tiles1.png`, `tiles2.png` | the 256 background tiles of 4x8 pixels in a 16x16 grid (64x128 px) |
| `graphics/font.png` | text font, 8x8 glyphs, 16 per row, 4 pens (mode 1) |
| `graphics/title.png` | the 160x200 title picture (mode 0) |
| `data/*.bin, *.dat` | unscrambled data files of the original: `CODE.bin` (game tables and texts; its Z80 code part is unused), `MUCODE.bin` (music data), `SOUND.dat` (envelopes and effects), `SMESSC.bin` (messages), `TISCRN.bin` (title tile map) |
| `rooms/roomNN.txt` | the 79 rooms as **text files**: `map` = 40x18 hex tile ids (`..` = empty; tile id = row*16+column in `tiles1.png`) and `record` = the 64 byte room record (doors, objects, pickup ... with notes) |
| `data/cutscene.bin`, `data/halloffame.bin` | the two other disc sectors (ending cutscene sprites, default all-time hall of fame) |
| `halloffame.sav` | written by the game when a name is entered; delete it to reset the table |

## Editing rooms
Open `rooms/roomNN.txt` in any editor: change tile ids in the `map` block (exactly 720 tiles, `ff` is reserved) and the `record` bytes (a door is
`y x destination-room flags`, `ff` = none). Text after `;` is a comment. The game reports the file and line of an error when it starts.
A room map is stored compressed in a 512 byte sector by the original format, so a very busy map can be refused ("too complex").
Regenerating the folder with `make_resources` overwrites your edits.

## Editing the graphics
The PNGs are **indexed** images: the pixel value is the CPC *pen number* (0-15, or 0-3 for the font), not a colour. The game applies
its own palette per room (the palette in the PNG is only a preview), so keep the palette order when editing (Aseprite, GIMP "indexed
mode", ...). RGB/RGBA PNGs are accepted too: each pixel is mapped to the nearest preview colour. Sizes must stay unchanged.
The limits of the CPC apply: tiles are 4 pixels wide, sprites 12x24.

Regenerate this folder from the original disc: `make make_resources && ./make_resources ../Sorcery.dsk resources`.
