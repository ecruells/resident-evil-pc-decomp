#pragma once

// ============================================================================
// STR — PrintFormattedText compile-time string encoder
//
// Encodes readable ASCII strings into the RE1 8x14 font encoding at compile
// time. Full reference (character table, escapes, terminators, message-stream
// tags, and the encoded string tables in MenuData.cpp / Globals.cpp):
//   docs/TEXT_ENCODING.md
//
// Usage:
//   static constexpr auto s_pftHello = STR("HELLO WORLD");
//   PrintFormattedText(x, y, color, s_pftHello);
//
// Supported characters:
//   A-Z  a-z  0-9  space  ! " , . : ; ? ' ( ) - / \
//   Unmapped printable characters become '?' (0x1B).
//
// Controller symbols (8x14 font, indices 0x02-0x0B):
//   Use the STR_* named macros, or raw hex escapes \x02-\x0B:
//     STR("PRESS " STR_TRI " TO START")
//     STR("PRESS \x07 TO START")           // same thing
//
// Controller symbols (14x14 font, via 0xF8 escape):
//   Use STR_BTN_* macros for 14x14 button glyphs:
//     STR("USE " STR_BTN_L1 " TO AIM")
//
// The encoded data is terminated with 0x01 (end of text block).
//
// Escapes:
//   \n  0x02  newline          \p  0x03  page break (next char = delay operand)
//   \s  0x04  set char delay   \i  0x05 01 06 00 05 00  item-name placeholder
//   \c  0x08  yes/no prompt    \q  0x0A  square glyph
//   \o  0x78  opening double quote (plain " is the closing form, 0x19)
//   \xNN      raw hex byte (for message-stream tags the named escapes cannot
//             express, e.g. the CLUT brackets around a literal item name)
//   \d        auto-dismiss: the next char is encoded and written AFTER the
//             0x01 terminator, making the message clear itself after that many
//             frames instead of waiting for a button press. Omit it and the
//             terminator is followed by 0, which is the wait-for-input form.
//             \d\xNN writes the raw byte NN instead of an encoded char.
//
// NOTE on \i: the message system (UpdateMessageDisplay, 0x004557b0) has no
// single-byte "item name" code. The original message bytes (e.g. "You got the
// \i" at 0x004bf3f7) are 05 01 06 00 05 00: CLUT colour 1, then tag 06 with
// arg 0 (= use g_selectedItemId), then CLUT colour 0. A bare 0x05 would be
// read as a CLUT tag, swallow the following 0x01 terminator, and run the
// renderer into the next message's memory — the "all possible messages at
// once" symptom.
//
// NOTE on \p: the char right after \p in the source IS the delay operand. The
// original room/global texts use operand 0 ("\p " in source); a letter like
// "\pWill" makes the page break wait (0x33 << 1) frames instead of 0.
// ============================================================================

// --- 8x14 controller symbol constants (direct font indices) ---
// These are raw bytes in the 0x02-0x0B range. Use as string literal fragments.
#define STR_START  "\x02"   // ►  Start button
#define STR_L2     "\x03"   // L2 shoulder button
#define STR_R2     "\x04"   // R2 shoulder button
#define STR_L1     "\x05"   // L1 shoulder button
#define STR_R1     "\x06"   // R1 shoulder button
#define STR_TRI    "\x07"   // △  Triangle
#define STR_CIR    "\x08"   // ○  Circle
#define STR_CROSS  "\x09"   // ×  Cross
#define STR_SQR    "\x0A"   // □  Square
#define STR_SEL    "\x0B"   // ▼  Select button

// --- 14x14 controller symbol constants (0xF8 + index byte) ---
// These are two-byte sequences. Use as string literal fragments.
#define STR_BTN_L2     "\xF8\x00"   // L2 (14x14)
#define STR_BTN_R2     "\xF8\x01"   // R2 (14x14)
#define STR_BTN_L1     "\xF8\x02"   // L1 (14x14)
#define STR_BTN_R1     "\xF8\x03"   // R1 (14x14)
#define STR_BTN_TRI    "\xF8\x04"   // △  Triangle (14x14)
#define STR_BTN_CIR    "\xF8\x05"   // ○  Circle (14x14)
#define STR_BTN_CROSS  "\xF8\x06"   // ×  Cross (14x14)
#define STR_BTN_SQR    "\xF8\x07"   // □  Square (14x14)
#define STR_BTN_UP     "\xF8\x08"   // ↑  D-pad Up (14x14)
#define STR_BTN_RIGHT  "\xF8\x09"   // →  D-pad Right (14x14)
#define STR_BTN_DOWN   "\xF8\x0A"   // ↓  D-pad Down (14x14)
#define STR_BTN_LEFT   "\xF8\x0B"   // ←  D-pad Left (14x14)

// Japanese glyph table (generated). Declares pft_detail::kJpnGlyphs and
// pft_detail::jpnGlyphIndex, which STR_JP() below searches at compile time.
#include "JpnFontTable.h"

namespace pft_detail {

constexpr int pft_hex(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

constexpr unsigned char encodeChar(unsigned char c)
{
    // Raw control bytes pass through unchanged:
    // 0x02-0x0B = 8x14 controller symbols
    // 0xF8-0xFF = PrintFormattedText control codes (0xF8+byte, 0xFB, 0xFF, etc.)
    if (c >= 0x02 && c <= 0x0B) return c;
    if (c >= 0xF8) return c;

    // ASCII printable → font index
    if (c >= 'A' && c <= 'Z') return (unsigned char)(c - 'A' + 0x1D);
    if (c >= 'a' && c <= 'z') return (unsigned char)(c - 'a' + 0x3D);
    if (c >= '0' && c <= '9') return (unsigned char)(c - '0' + 0x0C);
    if (c == ' ')  return 0x00;
    if (c == '!')  return 0x1A;
    if (c == '"')  return 0x19;
    if (c == ',')  return 0x18;
    if (c == '.')  return 0x79;
    if (c == ':')  return 0x16;
    if (c == ';')  return 0x17;
    if (c == '?')  return 0x1B;
    if (c == '\'') return 0x3A;
    if (c == '(')  return 0x37;
    if (c == ')')  return 0x39;
    if (c == '-')  return 0x3B;
    if (c == '/')  return 0x38;
    if (c == '\\') return 0x38;
    return 0x1B;
}

template <int N>
struct Encoded {
    // Sized for worst-case expansion: \i is 2 source chars -> 6 bytes (3x),
    // plus the 0x01 terminator and the optional auto-dismiss operand.
    unsigned char bytes[N * 3 + 2];

    constexpr Encoded() : bytes{} {}

    constexpr Encoded(const char (&str)[N]) : bytes{}
    {
        int out = 0;
        unsigned char dismissDelay = 0;
        for (int i = 0; i < N - 1; i++) {
            unsigned char c = (unsigned char)str[i];
            if (c == 0x5C && i + 1 < N - 1) {
                switch ((unsigned char)str[i + 1]) {
                    case 'n': bytes[out++] = 0x02; i++; continue;
                    case 'p': bytes[out++] = 0x03; i++; continue;
                    case 's': bytes[out++] = 0x04; i++; continue;
                    case 'i':
                        // Message-stream item-name sequence (see the header note):
                        // CLUT 1, name tag (arg 0 = selected item), CLUT 0.
                        bytes[out++] = 0x05; bytes[out++] = 0x01;
                        bytes[out++] = 0x06; bytes[out++] = 0x00;
                        bytes[out++] = 0x05; bytes[out++] = 0x00;
                        i++; continue;
                    case 'c': bytes[out++] = 0x08; i++; continue;
                    case 'q': bytes[out++] = 0x0A; i++; continue;
                    case 'o': bytes[out++] = 0x78; i++; continue;
                    case 'x':
                        // \xNN - raw hex byte (see the header note).
                        if (i + 3 < N) {
                            int hi = pft_hex(str[i + 2]);
                            int lo = pft_hex(str[i + 3]);
                            if (hi >= 0 && lo >= 0) {
                                bytes[out++] = (unsigned char)((hi << 4) | lo);
                                i += 3;
                                continue;
                            }
                        }
                        break;
                    case 'd':
                        // Auto-dismiss delay. Emits nothing here; the operand
                        // (\xNN raw, or an encoded char, same convention as
                        // \p) is written after the 0x01 terminator. See the
                        // note on the terminator below.
                        if (i + 5 < N && str[i + 2] == '\\' && str[i + 3] == 'x') {
                            int hi = pft_hex(str[i + 4]);
                            int lo = pft_hex(str[i + 5]);
                            if (hi >= 0 && lo >= 0) {
                                dismissDelay = (unsigned char)((hi << 4) | lo);
                                i += 5;
                                continue;
                            }
                            i++;
                            continue;
                        }
                        if (i + 2 < N - 1) {
                            dismissDelay = encodeChar((unsigned char)str[i + 2]);
                            i += 2;
                        } else {
                            i++;
                        }
                        continue;
                    case 0x5C: bytes[out++] = encodeChar(0x5C); i++; continue;
                    default: break;
                }
            }
            bytes[out++] = encodeChar(c);
        }
        // The byte FOLLOWING the 0x01 terminator is read by the message state
        // machine (UpdateMessageDisplay / FUN_004557b0, tag-1 handler) and is
        // part of the message, not padding:
        //     next == 0  -> state 5: hold the text and wait for a button press
        //     next != 0  -> state 6: auto-dismiss after `next` frames
        // Zero-initialised `bytes` gives the wait-for-input form by default,
        // which is right for the item/door texts. Messages that must play and
        // clear themselves supply the frame count with \d — in the original all
        // four opening narrations do (0x004bf763/95/e4, 0x004bf836).
        bytes[out++] = 0x01;
        if (dismissDelay != 0) bytes[out] = dismissDelay;
    }

    operator const unsigned char*() const { return bytes; }
};

// ------------------------------------------------------------------------
// EncodedJp — the same encoder for the Japanese font (data\FONT.TIM).
//
// The Japanese release keeps the identical message protocol and the identical
// escape set; only the glyph sheet differs. Its 14x14 game-text font spans two
// 256-wide texture pages of the 768x256 TIM, 18 columns each:
//
//   plain byte b (0x0C..0xF7)  left page,  row b/18,       col b%18
//   0xF8 nn                    left page,  row nn/18 + 13, col nn%18
//   0xF9 nn                    right page, row nn/18,      col nn%18
//   0xFA nn                    right page, row nn/18 + 14, col nn%18
//
// so a glyph is one or two bytes and the mapping is not derivable from ASCII.
// The source text is therefore written as UTF-8 and looked up by codepoint in
// the generated kJpnGlyphs table:
//
//   static constexpr auto s_msg = STR_JP(u8"カギがかかっている");
//
// The literal MUST be u8"" and the file MUST be UTF-8 (with BOM, so MSVC reads
// it as such) — a narrow literal is converted to the execution code page first
// and the codepoints never arrive.
//
// The left page's first five rows are the same character table fontus.tim has,
// so plain ASCII encodes to the same bytes as STR() with three exceptions the
// table carries: '.' is the two-byte 0xF8 0x1C (index 121 is a kana here),
// and ',' / ';' both become the ideographic comma, which is what the original
// Japanese text uses.
// ------------------------------------------------------------------------
template <int N>
struct EncodedJp {
    // Same worst case as Encoded: \i is 2 source chars -> 6 bytes. A UTF-8
    // Japanese character is 3 source bytes and at most 2 encoded bytes, so
    // multi-byte text only ever shrinks.
    unsigned char bytes[N * 3 + 2];

    constexpr EncodedJp() : bytes{} {}

    constexpr EncodedJp(const char (&str)[N]) : bytes{}
    {
        int out = 0;
        unsigned char dismissDelay = 0;
        for (int i = 0; i < N - 1; i++) {
            unsigned char c = (unsigned char)str[i];
            if (c == 0x5C && i + 1 < N - 1) {
                switch ((unsigned char)str[i + 1]) {
                    case 'n': bytes[out++] = 0x02; i++; continue;
                    case 'p': bytes[out++] = 0x03; i++; continue;
                    case 's': bytes[out++] = 0x04; i++; continue;
                    case 'i':
                        bytes[out++] = 0x05; bytes[out++] = 0x01;
                        bytes[out++] = 0x06; bytes[out++] = 0x00;
                        bytes[out++] = 0x05; bytes[out++] = 0x00;
                        i++; continue;
                    case 'c': bytes[out++] = 0x08; i++; continue;
                    case 'q': bytes[out++] = 0x0A; i++; continue;
                    case 'x':
                        if (i + 3 < N) {
                            int hi = pft_hex(str[i + 2]);
                            int lo = pft_hex(str[i + 3]);
                            if (hi >= 0 && lo >= 0) {
                                bytes[out++] = (unsigned char)((hi << 4) | lo);
                                i += 3;
                                continue;
                            }
                        }
                        break;
                    case 'd':
                        if (i + 5 < N && str[i + 2] == '\\' && str[i + 3] == 'x') {
                            int hi = pft_hex(str[i + 4]);
                            int lo = pft_hex(str[i + 5]);
                            if (hi >= 0 && lo >= 0) {
                                dismissDelay = (unsigned char)((hi << 4) | lo);
                                i += 5;
                                continue;
                            }
                            i++;
                            continue;
                        }
                        i++;
                        continue;
                    case 0x5C:
                        // "\\" is one backslash glyph, as in STR(); without
                        // this both source characters fall through to the
                        // table and draw it twice.
                        bytes[out++] = 0x38; i++; continue;
                    default: break;
                }
            }

            // Decode one UTF-8 sequence (the table covers the BMP only, which
            // is every glyph the font has).
            unsigned int cp = c;
            if (c >= 0xE0 && i + 2 < N - 1) {
                cp = ((unsigned int)(c & 0x0F) << 12)
                   | ((unsigned int)(str[i + 1] & 0x3F) << 6)
                   |  (unsigned int)(str[i + 2] & 0x3F);
                i += 2;
            } else if (c >= 0xC0 && i + 1 < N - 1) {
                cp = ((unsigned int)(c & 0x1F) << 6)
                   |  (unsigned int)(str[i + 1] & 0x3F);
                i += 1;
            }

            int gi = jpnGlyphIndex(cp);
            if (gi < 0) {
                bytes[out++] = 0x1B;    // '?' — same fallback STR() uses
                continue;
            }
            // Length comes from the table, never from "b1 is zero": a second
            // byte of zero is just column 0 of a row, so U+58CA encodes to the
            // two bytes F9 00 and U+4E45 to FA 00.
            bytes[out++] = kJpnGlyphs[gi].b0;
            if (kJpnGlyphs[gi].n > 1) bytes[out++] = kJpnGlyphs[gi].b1;
        }
        bytes[out++] = 0x01;
        if (dismissDelay != 0) bytes[out] = dismissDelay;
    }

    operator const unsigned char*() const { return bytes; }
};

} // namespace pft_detail

#define STR(str) (::pft_detail::Encoded<sizeof(str)>{str})

// Japanese counterpart of STR(). The literal must be u8"" — see EncodedJp.
#define STR_JP(str) (::pft_detail::EncodedJp<sizeof(str)>{str})
