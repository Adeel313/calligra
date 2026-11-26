// SPDX-License-Identifier: LGPL-2.0-or-later
#ifndef CALLIGRA_SYMBOL_ENCODING_H
#define CALLIGRA_SYMBOL_ENCODING_H

#include <QtGlobal>

namespace Calligra
{
// Translate a byte from the legacy Windows Symbol encoding to Unicode.
inline ushort symbolByteToUnicode(ushort code)
{
    if (code >= 0x30 && code <= 0x39) {
        return code; // digits are the same in Symbol and Unicode
    }
    switch (code) {
    case 0x20: return 0x0020;
    case 0x21: return 0x0021;
    case 0x22: return 0x2200; // FOR ALL
    case 0x23: return 0x0023;
    case 0x24: return 0x2203; // THERE EXISTS
    case 0x25: return 0x0025;
    case 0x26: return 0x0026;
    case 0x27: return 0x220b; // CONTAINS AS MEMBER
    case 0x28: return 0x0028;
    case 0x29: return 0x0029;
    case 0x2a: return 0x2217; // ASTERISK OPERATOR
    case 0x2b: return 0x002b;
    case 0x2c: return 0x002c;
    case 0x2d: return 0x2212; // MINUS SIGN
    case 0x2e: return 0x002e;
    case 0x2f: return 0x002f;
    case 0x3a: return 0x003a;
    case 0x3b: return 0x003b;
    case 0x3c: return 0x003c;
    case 0x3d: return 0x003d;
    case 0x3e: return 0x003e;
    case 0x3f: return 0x003f;
    case 0x40: return 0x2245; // APPROXIMATELY EQUAL TO
    case 0x41: return 0x0391; // GREEK CAPITAL LETTER ALPHA
    case 0x42: return 0x0392;
    case 0x43: return 0x03a7;
    case 0x44: return 0x0394;
    case 0x45: return 0x0395;
    case 0x46: return 0x03a6;
    case 0x47: return 0x0393;
    case 0x48: return 0x0397;
    case 0x49: return 0x0399;
    case 0x4a: return 0x03d1; // GREEK THETA SYMBOL
    case 0x4b: return 0x039a;
    case 0x4c: return 0x039b;
    case 0x4d: return 0x039c;
    case 0x4e: return 0x039d;
    case 0x4f: return 0x039f;
    case 0x50: return 0x03a0;
    case 0x51: return 0x0398;
    case 0x52: return 0x03a1;
    case 0x53: return 0x03a3;
    case 0x54: return 0x03a4;
    case 0x55: return 0x03a5;
    case 0x56: return 0x03c2; // GREEK SMALL LETTER FINAL SIGMA
    case 0x57: return 0x03a9;
    case 0x58: return 0x039e;
    case 0x59: return 0x03a8;
    case 0x5a: return 0x0396;
    case 0x5b: return 0x005b;
    case 0x5c: return 0x2234; // THEREFORE
    case 0x5d: return 0x005d;
    case 0x5e: return 0x22a5; // UP TACK
    case 0x5f: return 0x005f;
    case 0x60: return 0x203e; // OVERLINE
    case 0x61: return 0x03b1; // GREEK SMALL LETTER ALPHA
    case 0x62: return 0x03b2;
    case 0x63: return 0x03c7;
    case 0x64: return 0x03b4;
    case 0x65: return 0x03b5;
    case 0x66: return 0x03c6;
    case 0x67: return 0x03b3;
    case 0x68: return 0x03b7;
    case 0x69: return 0x03b9;
    case 0x6a: return 0x03d5; // GREEK PHI SYMBOL
    case 0x6b: return 0x03ba;
    case 0x6c: return 0x03bb;
    case 0x6d: return 0x03bc; // GREEK SMALL LETTER MU
    case 0x6e: return 0x03bd;
    case 0x6f: return 0x03bf;
    case 0x70: return 0x03c0;
    case 0x71: return 0x03b8; // GREEK SMALL LETTER THETA
    case 0x72: return 0x03c1;
    case 0x73: return 0x03c3;
    case 0x74: return 0x03c4;
    case 0x75: return 0x03c5;
    case 0x76: return 0x03d6; // GREEK PI SYMBOL
    case 0x77: return 0x03c9;
    case 0x78: return 0x03be;
    case 0x79: return 0x03c8;
    case 0x7a: return 0x03b6;
    case 0x7b: return 0x007b;
    case 0x7c: return 0x007c;
    case 0x7d: return 0x007d;
    case 0x7e: return 0x223c; // TILDE OPERATOR
    case 0xa1: return 0x03d2; // GREEK UPSILON WITH HOOK SYMBOL
    case 0xa2: return 0x2032; // PRIME
    case 0xa3: return 0x2264; // LESS-THAN OR EQUAL TO
    case 0xa4: return 0x2215; // DIVISION SLASH
    case 0xa5: return 0x221e; // INFINITY
    case 0xa6: return 0x0192; // LATIN SMALL LETTER F WITH HOOK
    case 0xa7: return 0x2663; // BLACK CLUB SUIT
    case 0xa8: return 0x2666; // BLACK DIAMOND SUIT
    case 0xa9: return 0x2665; // BLACK HEART SUIT
    case 0xaa: return 0x2660; // BLACK SPADE SUIT
    case 0xab: return 0x2194; // LEFT RIGHT ARROW
    case 0xac: return 0x2190; // LEFTWARDS ARROW
    case 0xad: return 0x2191; // UPWARDS ARROW
    case 0xae: return 0x2192; // RIGHTWARDS ARROW
    case 0xaf: return 0x2193; // DOWNWARDS ARROW
    case 0xb0: return 0x00b0; // DEGREE SIGN
    case 0xb1: return 0x00b1; // PLUS-MINUS SIGN
    case 0xb2: return 0x2033; // DOUBLE PRIME
    case 0xb3: return 0x2265; // GREATER-THAN OR EQUAL TO
    case 0xb4: return 0x00d7; // MULTIPLICATION SIGN
    case 0xb5: return 0x221d; // PROPORTIONAL TO
    case 0xb6: return 0x2202; // PARTIAL DIFFERENTIAL
    case 0xb7: return 0x2219; // BULLET OPERATOR
    case 0xb8: return 0x00f7; // DIVISION SIGN
    case 0xb9: return 0x2260; // NOT EQUAL TO
    case 0xba: return 0x2261; // IDENTICAL TO
    case 0xbb: return 0x2248; // ALMOST EQUAL TO
    case 0xbc: return 0x22ef; // MIDLINE HORIZONTAL ELLIPSIS
    case 0xbf: return 0x21b5; // DOWNWARDS ARROW WITH CORNER LEFTWARDS
    case 0xc1: return 0x2131; // BLACK-LETTER CAPITAL I
    case 0xc2: return 0x211c; // BLACK-LETTER CAPITAL R
    case 0xc3: return 0x2118; // SCRIPT CAPITAL P
    case 0xc4: return 0x2297; // CIRCLED TIMES
    case 0xc5: return 0x2295; // CIRCLED PLUS
    case 0xc6: return 0x2205; // EMPTY SET
    case 0xc7: return 0x2229; // INTERSECTION
    case 0xc8: return 0x222a; // UNION
    case 0xc9: return 0x2283; // SUPERSET OF
    case 0xca: return 0x2287; // SUPERSET OF OR EQUAL TO
    case 0xcb: return 0x2284; // NOT A SUBSET OF
    case 0xcc: return 0x2282; // SUBSET OF
    case 0xcd: return 0x2286; // SUBSET OF OR EQUAL TO
    case 0xce: return 0x2208; // ELEMENT OF
    case 0xcf: return 0x2209; // NOT AN ELEMENT OF
    case 0xd0: return 0x2220; // ANGLE
    case 0xd1: return 0x2207; // NABLA
    case 0xd2: return 0x00ae; // REGISTERED SIGN
    case 0xd3: return 0x00a9; // COPYRIGHT SIGN
    case 0xd4: return 0x2122; // TRADE MARK SIGN
    case 0xd5: return 0x220f; // N-ARY PRODUCT
    case 0xd6: return 0x221a; // SQUARE ROOT
    case 0xd7: return 0x22c5; // DOT OPERATOR
    case 0xd8: return 0x00ac; // NOT SIGN
    case 0xd9: return 0x2227; // LOGICAL AND
    case 0xda: return 0x2228; // LOGICAL OR
    case 0xdb: return 0x21d4; // LEFT RIGHT DOUBLE ARROW
    case 0xdc: return 0x21d0; // LEFTWARDS DOUBLE ARROW
    case 0xdd: return 0x21d1; // UPWARDS DOUBLE ARROW
    case 0xde: return 0x21d2; // RIGHTWARDS DOUBLE ARROW
    case 0xdf: return 0x21d3; // DOWNWARDS DOUBLE ARROW
    case 0xe0: return 0x25ca; // LOZENGE
    case 0xe1: return 0x2329; // LEFT-POINTING ANGLE BRACKET
    case 0xe5: return 0x2211; // N-ARY SUMMATION
    case 0xe9: return 0x23a1; // LEFT SQUARE BRACKET UPPER CORNER
    case 0xea: return 0x23a2; // LEFT SQUARE BRACKET EXTENSION
    case 0xeb: return 0x23a3; // LEFT SQUARE BRACKET LOWER CORNER
    case 0xf9: return 0x23a4; // RIGHT SQUARE BRACKET UPPER CORNER
    case 0xfa: return 0x23a5; // RIGHT SQUARE BRACKET EXTENSION
    case 0xfb: return 0x23a6; // RIGHT SQUARE BRACKET LOWER CORNER
    case 0xf1: return 0x232a; // RIGHT-POINTING ANGLE BRACKET
    case 0xf2: return 0x222b; // INTEGRAL
    case 0xf3: return 0x2320; // TOP HALF INTEGRAL
    case 0xf5: return 0x2321; // BOTTOM HALF INTEGRAL
    default: return 0;
    }
}

/** Decode a Wingdings-encoded bullet character to Unicode. */
inline ushort wingdingsByteToUnicode(ushort code)
{
    switch (code) {
    case 0x6c: return 0x25cf; // BLACK CIRCLE
    case 0x6e: return 0x25a0; // BLACK SQUARE
    case 0x6f: return 0x25a1; // WHITE SQUARE
    case 0x70: return 0x25a1; // WHITE SQUARE
    case 0x71: return 0x2751; // LOWER RIGHT SHADOWED WHITE SQUARE
    case 0x72: return 0x2752; // UPPER RIGHT SHADOWED WHITE SQUARE
    case 0x75: return 0x25c6; // BLACK DIAMOND
    case 0x76: return 0x2756; // BLACK DIAMOND MINUS WHITE X
    case 0x77: return 0x2b29; // BLACK SMALL DIAMOND
    case 0x9f: return 0x2022; // BULLET
    case 0xa0: return 0x25aa; // BLACK SMALL SQUARE
    case 0xa1: return 0x25cb; // WHITE CIRCLE
    case 0xa7: return 0x25aa; // BLACK SMALL SQUARE
    case 0xa8: return 0x25a1; // WHITE SQUARE
    case 0xd8: return 0x27a2; // THREE-D TOP-LIGHTED RIGHTWARDS ARROWHEAD
    case 0xe8: return 0x2794; // WIDE RIGHTWARDS ARROW
    case 0xfb: return 0x2717; // BALLOT X
    case 0xfc: return 0x2714; // CHECK MARK
    default: return 0;
    }
}

} // namespace Calligra

#endif
