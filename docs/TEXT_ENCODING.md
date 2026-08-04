# RE1 Text Encoding & the STR() Macro

This document describes how in-game text is represented and rendered in the
decomp, and how the compile-time `STR()` macro (`src/game/PrintText.h`) turns
readable ASCII source strings into the byte streams the game engine consumes.

## 1. Overview

RE1 text is **not** stored as ASCII. Every text glyph is a byte in the range
`0x00..0xFF`, where the low 144 values (`0x00..0x8F`) index a glyph in the
**8×14 game-text font** region of `fontus.tim`, and the rest are control codes
(`0xF8/0xF9/0xFA` extended characters) or the `0x02..0x0B` controller
symbols. See the font layout at the top of `src/game/PrintText.cpp`.

Two consumers interpret these bytes:

| Consumer | Function | Purpose |
|----------|----------|---------|
| Formatted text renderer | `PrintFormattedText` (0x00455190) | Debug / menu text |
| Message display | `UpdateMessageDisplay` (0x004557b0) | Room dialogue, item messages, prompts |

The decomp stores the actual text as readable C++ strings and converts them to
the font encoding **at compile time** with `STR()`:

```cpp
static constexpr auto s_pftHello = STR("HELLO WORLD");
PrintFormattedText(x, y, color, s_pftHello);
```

## 2. Character encoding

`pft_detail::encodeChar()` (`src/game/PrintText.h`) maps one ASCII character to
one font-index byte:

| ASCII | Font byte | | ASCII | Font byte |
|-------|-----------|-|-------|-----------|
| `A`–`Z` | `0x1D` + (c − 'A') | | `a`–`z` | `0x3D` + (c − 'a') |
| `0`–`9` | `0x0C` + (c − '0') | | ` ` (space) | `0x00` |
| `!` | `0x1A` | | `"` (closing) | `0x19` |
| `,` | `0x18` | | `.` | `0x79` |
| `:` | `0x16` | | `;` | `0x17` |
| `?` | `0x1B` | | `'` | `0x3A` |
| `(` | `0x37` | | `)` | `0x39` |
| `-` | `0x3B` | | `/` and `\` | `0x38` |
| `\x02..\x0B` (raw) | unchanged | | `\xF8..\xFF` (raw) | unchanged |

Notes:

- `0x02..0x0B` are the **8×14 controller symbols** (Start, L2/R2/L1/R1, △, ○,
  ×, □, ▼). They pass through `encodeChar` unchanged, so they can be embedded
  in a string literal with `\x02`..`\x0B` or the `STR_*` fragment macros.
- Bytes `≥ 0xF8` also pass through unchanged — they are `PrintFormattedText`
  control codes.
- Any unmapped printable character becomes `?` (`0x1B`).
- The 8×14 grid is 18 columns × 8 rows; a plain glyph byte `b` is drawn at
  `col = b % 18`, `row = b / 18 + 2` (the `+2` skips the control/symbol rows).

## 3. The STR() macro

Defined in `src/game/PrintText.h`:

```cpp
#define STR(str) (::pft_detail::Encoded<sizeof(str)>{str})
```

`Encoded<N>` is a `constexpr` struct holding `unsigned char bytes[N]`. Its
constructor walks the source literal (its `\` escapes first, then
`encodeChar()` per character) and writes the encoded bytes, then always appends
**one terminator byte** `0x01`. The result is exposed through `.bytes`
(implicitly convertible to `const unsigned char*`).

### Escapes

| Escape | Emits | Meaning |
|--------|-------|---------|
| `\n` | `0x02` | Newline (message: line break) |
| `\p` | `0x03` | Page break; the next char is a delay operand |
| `\s` | `0x04` | Set character delay |
| `\i` | `0x05` | Item-name placeholder (**see §6 — currently collides with tag 0x05**) |
| `\c` | `0x08` | Yes/No prompt |
| `\q` | `0x0A` | Square glyph |
| `\o` | `0x78` | Opening double quote (plain `"` is the closing form `0x19`) |
| `\d` | — | Auto-dismiss: the next character is encoded and written **after** the `0x01` terminator (see below) |
| `\\` | `0x38` | Backslash glyph |

### Terminator & the byte after it

The byte **following** the `0x01` terminator is read by the message state
machine and is part of the message, not padding:

- `0x00` → state 5: hold the text and wait for a button press.
- `N ≠ 0` → state 6: auto-dismiss after `N` frames.

`Encoded`'s zero-initialised tail gives the wait-for-input form (byte `0x00`)
by default. Messages that must clear themselves supply the frame count with
`\d` — in the original all four opening narrations do
(`0x004bf763/0x004bf795/0x004bf7e4/0x004bf836`).

### 8×14 controller glyph fragments

```cpp
STR_START "\x02"  STR_L2 "\x03"  STR_R2 "\x04"  STR_L1 "\x05"  STR_R1 "\x06"
STR_TRI  "\x07"  STR_CIR "\x08"  STR_CROSS "\x09"  STR_SQR "\x0A"  STR_SEL "\x0B"
```

14×14 symbols are two-byte sequences `\xF8` + index; use `STR_BTN_*`
(`STR_BTN_L1`, `STR_BTN_TRI`, …) for those.

## 4. PrintFormattedText (0x00455190)

Simple linear renderer over an encoded byte array:

| Byte | Meaning |
|------|---------|
| `0x00` | Advance 8 px (space) |
| `0x01` / `0x07` | End of text (return) |
| `0xF8 nn` | 14×14 controller symbol (index `nn`) |
| `0xF9 nn` | 8×14 character, depth 31 |
| `0xFA nn` | 8×14 character, depth 31, row offset +14 |
| `0xFB` | No-op (advance) |
| `0xFF` | Half-width advance (4 px) |
| *default* | Render glyph `nn` (depth 30) |

Used by `LoadSaveGameState` and other menu rendering.

## 5. Message display state machine (UpdateMessageDisplay, 0x004557b0)

Processes the message byte stream character-by-character with reveal timing.
The tag set (verified against the original binary via Ghidra):

| Tag | Meaning | Operand |
|-----|---------|---------|
| `0x01` | End-of-text marker | next byte: `0` = wait for input, `N` = auto-dismiss after N frames |
| `0x02` | Next page / skip | — |
| `0x03` | Newline (line break) | optional page-delay byte |
| `0x04` | Skip embedded tags | — |
| `0x05` | Set text CLUT color | 1 byte, color index |
| `0x06` | **Item-name lookup** | 1 byte: item id (`0` = selected item) |
| `0x07` | Return from item name | — |
| `0x08` | Yes/No prompt | — |
| `0xF8/0xF9/0xFA` | Extended characters | 1 byte each |

Plain glyph bytes (`0x0C..0x8F`, i.e. `0x0B < b < 0xF8`) render as characters.
The message is selected by `set_message_display()` (`src/game/RoomInit.cpp`):
`msg_id & 0x40` chooses between RDT room messages and the `global_messages[]`
table, `msg_id & 0x3F` is the index.

### Item-name substitution

When the state machine hits tag `0x06`, it sets the read pointer to
`message_item_name_lookup(itemId)` (`src/game/Rendering.cpp`, 0x00455140). The
name string bytes are then processed like normal message characters until the
name's **`0x07` terminator**, which doubles as the "return from item name"
tag (case 7) and jumps back to the message right after the `0x06`/operand
bytes. Item-name strings are therefore terminated with `0x07`, not `0x01`.

The original binary wraps the name in a CLUT color change so the name is
highlighted:

```
...the  05 01  06 00  <name bytes> 07  05 00  ...rest
       └─color 1──┘  └─item name─┘  └─color 0─┘
```

## 6. Encoded string tables in the decomp

### Item names — `src/game/MenuData.cpp` (0x07-terminated)

Originally one flat 984-byte block at `0x004BECC8`; each name terminated by
`0x07`. Now 78 `s_item*` constants, one per unique name:

```cpp
static constexpr auto s_itemCombatKnife = STR("COMBAT KNIFE\x07");  // +0x000
```

Consumed through two pointer tables:

- `g_ItemNamePointers[77]` (0x004BF0A0) — indexed by `itemId - 1`;
  `message_item_name_lookup()` returns one of these.
- `g_UnknownItemNamePointers[16]` (0x004BF260) — generic names shown for
  unexamined items, by item-category.

Each entry's comment records the original offset inside the flat block, so the
pointer-to-string relationship is preserved even though the block is gone.

### Global messages — `src/Globals.cpp` (0x01-terminated)

`global_messages[64]` (0x004BFC58) holds pointers to `s_gm00..s_gm62` STR
constants. These are displayed by `set_message_display()` when `msg_id` bit 6
is set; index is `msg_id & 0x3F`. All four opening narrations end with `\d` so
they auto-dismiss.

### Item descriptions — `src/Globals.cpp` (0x01-terminated)

`g_ItemDescriptions[79]` holds pointers to `s_idesc*` STR constants, shown by
the item viewer (`set_item_description_message`, 0x00455730). These live in
their own table — they are neither RDT nor global messages.

### RDT room messages — raw bytes

Room-specific dialogue lives inside `.rdt` files (see `docs/RDT_FILE_FORMAT.md`)
and is **not** STR-encoded; it is already in font-encoding form when loaded.

### ASCII file names & paths — plain string literals, never STR()

Not every byte string in the menu tables is rendered text. Filenames and paths
are **plain ASCII** and must stay as ordinary C string literals — do **not**
wrap them in `STR()` (that would font-encode them and break file loading):

- `g_ItemModelFileNames[75][8]` — 8-byte null-padded model file names
  (`"i05v"`, `"i00v"`, …), `src/game/MenuData.cpp`.
- `g_ItemModelFileNameING` / `g_ItemModelFileNameMINI` — `"ING"` / `"MINI"`.
- `g_ItemModelExtIVM` — `".ivm"`; `g_ItemModelDir` — `"./usa/item_m2/"`;
  `g_ItemMixPixPath` — `".\usa\data\item_mix.pix"`.

These are consumed via `strcat`/`LoadFile` (e.g. `menu_load_item_model`), not
by the font renderer.

## 7. Known issue: the `\i` escape vs. tag 0x06

The message protocol's item-name tag is `0x06` (§5), but the current `\i`
escape emits a single `0x05`, which is the **set-CLUT** tag. The original
message bytes for e.g. "You got the [item]." are
`...the 05 01 06 00 <name> 07 05 00 . 01 00` — the `05 01`/`05 00` pairs are
the CLUT highlight around the name and `06 00` is the actual item-name lookup.
A STR string such as `STR("You got the \\i")` currently encodes only
`...the 05 01` (the trailing `01` being the terminator), so the item name is
not inserted and the CLUT color is clobbered.

Making `\i` produce the correct two-byte `06 00` sequence also requires the
`Encoded<N>` sizing to account for the extra byte (a message ending in `\i`
would otherwise overflow `bytes[N]`). This is left as an explicit
reconciliation task; it needs in-game verification once changed.
