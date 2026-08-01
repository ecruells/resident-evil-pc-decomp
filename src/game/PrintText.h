#pragma once

// ============================================================================
// STR — PrintFormattedText compile-time string encoder
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
//   \s  0x04  set char delay   \i  0x05  item-name placeholder
//   \c  0x08  yes/no prompt    \q  0x0A  square glyph
//   \d        auto-dismiss: the next char is encoded and written AFTER the
//             0x01 terminator, making the message clear itself after that many
//             frames instead of waiting for a button press. Omit it and the
//             terminator is followed by 0, which is the wait-for-input form.
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

namespace pft_detail {

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
    unsigned char bytes[N];

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
                    case 'i': bytes[out++] = 0x05; i++; continue;
                    case 'c': bytes[out++] = 0x08; i++; continue;
                    case 'q': bytes[out++] = 0x0A; i++; continue;
                    case 'd':
                        // Auto-dismiss delay. Emits nothing here; the NEXT
                        // character is encoded (same operand convention as \p)
                        // and written after the 0x01 terminator. See the note
                        // on the terminator below.
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

} // namespace pft_detail

#define STR(str) (::pft_detail::Encoded<sizeof(str)>{str})
