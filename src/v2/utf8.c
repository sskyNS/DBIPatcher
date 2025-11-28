/*
    MIT License

    Copyright (c) 2024 zahash

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.
 */

/*
    Okay, this is ugly. Basicaly just edit is_allowed_character when new char
    is found in russian strings.
 */

#include "v2/utf8.h"

#include <stdint.h>

// Helper function to extract Unicode codepoint from UTF-8 bytes
uint32_t extract_codepoint(const char* str, uint32_t offset, int byte_count) {
    uint32_t codepoint = 0;
    
    switch (byte_count) {
        case 1:
            codepoint = (uint8_t)str[offset] & 0x7F;
            break;
        case 2:
            codepoint = ((uint8_t)str[offset] & 0x1F) << 6;
            codepoint |= ((uint8_t)str[offset + 1] & 0x3F);
            break;
        case 3:
            codepoint = ((uint8_t)str[offset] & 0x0F) << 12;
            codepoint |= ((uint8_t)str[offset + 1] & 0x3F) << 6;
            codepoint |= ((uint8_t)str[offset + 2] & 0x3F);
            break;
        case 4:
            codepoint = ((uint8_t)str[offset] & 0x07) << 18;
            codepoint |= ((uint8_t)str[offset + 1] & 0x3F) << 12;
            codepoint |= ((uint8_t)str[offset + 2] & 0x3F) << 6;
            codepoint |= ((uint8_t)str[offset + 3] & 0x3F);
            break;
    }
    
    return codepoint;
}

// Character range validation function
int is_allowed_character(uint32_t codepoint) {
    // Allow specific control characters: \r (0x0D), \n (0x0A), ESC (0x1B)
    if (codepoint == 0x0A || codepoint == 0x0D || codepoint == 0x1B) {
        return UT_GENERIC;
    }
    
    // Allow ASCII printable characters (0x20-0x7E) and space (0x20)
    if (codepoint >= 0x20 && codepoint <= 0x7E) {
        if((codepoint >= 'a' && codepoint <= 'z') || (codepoint >= 'A' && codepoint <= 'Z')) {
            return UT_LETTER;
        }
        
        if(codepoint >= '0' && codepoint <= '9') {
            return UT_NUMBER;
        }
        
        /*switch(codepoint) {
            case '`':
            case '^':
            case '$':
                return UT_INVALID;
            default:
                break;
        }*/
        
        return UT_GENERIC;
    }
    
    // Allow common Unicode whitespace characters
    switch (codepoint) {
        case 0x0009: // HT (Horizontal Tab)
        case 0x000B: // VT (Vertical Tab) 
        case 0x000C: // FF (Form Feed)
        case 0x0020: // SPACE (already covered above but explicit)
        case 0x00A0: // NO-BREAK SPACE
        case 0x00B0: // DEGREE
        case 0x00AB: // 《 
        case 0x00BB: // 》
        case 0x1680: // OGHAM SPACE MARK  
        case 0x2000: // EN QUAD
        case 0x2001: // EM QUAD
        case 0x2002: // EN SPACE
        case 0x2003: // EM SPACE
        case 0x2004: // THREE-PER-EM SPACE
        case 0x2005: // FOUR-PER-EM SPACE
        case 0x2006: // SIX-PER-EM SPACE
        case 0x2007: // FIGURE SPACE
        case 0x2008: // PUNCTUATION SPACE
        case 0x2009: // THIN SPACE
        case 0x200A: // HAIR SPACE
        case 0x2026: // Horizontal Ellipsis
        case 0x2715: // Multiplication X
        case 0x202F: // NARROW NO-BREAK SPACE
        case 0x205F: // MEDIUM MATHEMATICAL SPACE
        case 0x3000: // IDEOGRAPHIC SPACE
        
        case 0x27A1: // BLACK RIGHTWARDS ARROW
        case 0x2B05: // LEFTWARDS BLACK ARROW
        case 0x2B06: // UPWARDS BLACK ARROW
        case 0x2B07: // DOWNWARDS BLACK ARROW
            return UT_GENERIC;
    }
    
    // Allow main Cyrillic block (U+0400–U+04FF)
    if (codepoint >= 0x0400 && codepoint <= 0x04FF) {        
        if(codepoint >= 0x0410 && codepoint <= 0x042F) {
            return UT_CYRILLIC;
        }
        
        if(codepoint >= 0x0430 && codepoint <= 0x044F) {
            return UT_CYRILLIC;
        }
        
        if(codepoint == 0x0401) {
            return UT_CYRILLIC;
        }
        
        if(codepoint == 0x0451) {
            return UT_CYRILLIC;
        }
        
        return UT_INVALID;
    }
    
    // Allow Cyrillic Supplement (U+0500–U+052F)
    if (codepoint >= 0x0500 && codepoint <= 0x052F) {
        return UT_INVALID;
    }
    
    // Allow Cyrillic Extended-A (U+2DE0–U+2DFF)
    if (codepoint >= 0x2DE0 && codepoint <= 0x2DFF) {
        return UT_INVALID;
    }
    
    // Allow Cyrillic Extended-B (U+A640–U+A69F)
    if (codepoint >= 0xA640 && codepoint <= 0xA69F) {
        return UT_INVALID;
    }
    
    // Allow Cyrillic Extended-C (U+1C80–U+1C8F)
    if (codepoint >= 0x1C80 && codepoint <= 0x1C8F) {
        return UT_INVALID;
    }
    
    // Reject all other characters
    return UT_INVALID;
}

utf8_char_validity utf8_check_char(const char* str, uint32_t offset) {
    int type = UT_INVALID;
    // Single-byte UTF-8 characters have the form 0xxxxxxx
    if (((uint8_t)str[offset] & 0b10000000) == 0b00000000) {
        uint32_t codepoint = extract_codepoint(str, offset, 1);
        if ((type = is_allowed_character(codepoint)) == UT_INVALID) {
            return (utf8_char_validity) { .valid = type, .next_offset = offset };
        }
        return (utf8_char_validity) { .valid = type, .next_offset = offset + 1 };
    }

    // Two-byte UTF-8 characters have the form 110xxxxx 10xxxxxx
    if (((uint8_t)str[offset + 0] & 0b11100000) == 0b11000000 &&
        ((uint8_t)str[offset + 1] & 0b11000000) == 0b10000000) {

        // Check for overlong encoding
        if (((uint8_t)str[offset] & 0b00011111) < 0b00000010) {
            return (utf8_char_validity) { .valid = 0, .next_offset = offset };
        }
        
        uint32_t codepoint = extract_codepoint(str, offset, 2);
        if ((type = is_allowed_character(codepoint)) == UT_INVALID) {
            return (utf8_char_validity) { .valid = type, .next_offset = offset };
        }
        return (utf8_char_validity) { .valid = type, .next_offset = offset + 2 };
    }

    // Three-byte UTF-8 characters have the form 1110xxxx 10xxxxxx 10xxxxxx
    if (((uint8_t)str[offset + 0] & 0b11110000) == 0b11100000 &&
        ((uint8_t)str[offset + 1] & 0b11000000) == 0b10000000 &&
        ((uint8_t)str[offset + 2] & 0b11000000) == 0b10000000) {

        // Check for overlong encoding
        if (((uint8_t)str[offset + 0] & 0b00001111) == 0b00000000 &&
            ((uint8_t)str[offset + 1] & 0b00111111) < 0b00100000) {
            return (utf8_char_validity) { .valid = 0, .next_offset = offset };
        }

        // Reject UTF-16 surrogates (U+D800 to U+DFFF)
        if ((uint8_t)str[offset + 0] == 0b11101101 &&
            (uint8_t)str[offset + 1] >= 0b10100000 &&
            (uint8_t)str[offset + 1] <= 0b10111111) {
            return (utf8_char_validity) { .valid = 0, .next_offset = offset };
        }
        
        uint32_t codepoint = extract_codepoint(str, offset, 3);
        if ((type = is_allowed_character(codepoint)) == UT_INVALID) {
            return (utf8_char_validity) { .valid = type, .next_offset = offset };
        }
        return (utf8_char_validity) { .valid = type, .next_offset = offset + 3 };
    }

    // Four-byte UTF-8 characters have the form 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
    if (((uint8_t)str[offset + 0] & 0b11111000) == 0b11110000 &&
        ((uint8_t)str[offset + 1] & 0b11000000) == 0b10000000 &&
        ((uint8_t)str[offset + 2] & 0b11000000) == 0b10000000 &&
        ((uint8_t)str[offset + 3] & 0b11000000) == 0b10000000) {

        // Check for overlong encoding
        if (((uint8_t)str[offset + 0] & 0b00000111) == 0b00000000 &&
            ((uint8_t)str[offset + 1] & 0b00111111) < 0b00010000) {
            return (utf8_char_validity) { .valid = 0, .next_offset = offset };
        }
        
        uint32_t codepoint = extract_codepoint(str, offset, 4);
        if ((type = is_allowed_character(codepoint)) == UT_INVALID) {
            return (utf8_char_validity) { .valid = type, .next_offset = offset };
        }
        return (utf8_char_validity) { .valid = type, .next_offset = offset + 4 };
    }

    return (utf8_char_validity) { .valid = UT_INVALID, .next_offset = offset };
}

utf8_char_validity utf8_check_char_unchecked(const char* str, uint32_t offset) {
    int type = UT_INVALID;
    // Single-byte UTF-8 characters have the form 0xxxxxxx
    if (((uint8_t)str[offset] & 0b10000000) == 0b00000000) {
        uint32_t codepoint = extract_codepoint(str, offset, 1);
        if ((type = UT_GENERIC && codepoint != 0) == UT_INVALID) {
            return (utf8_char_validity) { .valid = type, .next_offset = offset };
        }
        return (utf8_char_validity) { .valid = type, .next_offset = offset + 1 };
    }

    // Two-byte UTF-8 characters have the form 110xxxxx 10xxxxxx
    if (((uint8_t)str[offset + 0] & 0b11100000) == 0b11000000 &&
        ((uint8_t)str[offset + 1] & 0b11000000) == 0b10000000) {

        // Check for overlong encoding
        if (((uint8_t)str[offset] & 0b00011111) < 0b00000010) {
            return (utf8_char_validity) { .valid = 0, .next_offset = offset };
        }
        
        uint32_t codepoint = extract_codepoint(str, offset, 2);
        if ((type = UT_GENERIC && codepoint != 0) == UT_INVALID) {
            return (utf8_char_validity) { .valid = type, .next_offset = offset };
        }
        return (utf8_char_validity) { .valid = type, .next_offset = offset + 2 };
    }

    // Three-byte UTF-8 characters have the form 1110xxxx 10xxxxxx 10xxxxxx
    if (((uint8_t)str[offset + 0] & 0b11110000) == 0b11100000 &&
        ((uint8_t)str[offset + 1] & 0b11000000) == 0b10000000 &&
        ((uint8_t)str[offset + 2] & 0b11000000) == 0b10000000) {

        // Check for overlong encoding
        if (((uint8_t)str[offset + 0] & 0b00001111) == 0b00000000 &&
            ((uint8_t)str[offset + 1] & 0b00111111) < 0b00100000) {
            return (utf8_char_validity) { .valid = 0, .next_offset = offset };
        }

        // Reject UTF-16 surrogates (U+D800 to U+DFFF)
        if ((uint8_t)str[offset + 0] == 0b11101101 &&
            (uint8_t)str[offset + 1] >= 0b10100000 &&
            (uint8_t)str[offset + 1] <= 0b10111111) {
            return (utf8_char_validity) { .valid = 0, .next_offset = offset };
        }
        
        uint32_t codepoint = extract_codepoint(str, offset, 3);
        if ((type = UT_GENERIC && codepoint != 0) == UT_INVALID) {
            return (utf8_char_validity) { .valid = type, .next_offset = offset };
        }
        return (utf8_char_validity) { .valid = type, .next_offset = offset + 3 };
    }

    // Four-byte UTF-8 characters have the form 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
    if (((uint8_t)str[offset + 0] & 0b11111000) == 0b11110000 &&
        ((uint8_t)str[offset + 1] & 0b11000000) == 0b10000000 &&
        ((uint8_t)str[offset + 2] & 0b11000000) == 0b10000000 &&
        ((uint8_t)str[offset + 3] & 0b11000000) == 0b10000000) {

        // Check for overlong encoding
        if (((uint8_t)str[offset + 0] & 0b00000111) == 0b00000000 &&
            ((uint8_t)str[offset + 1] & 0b00111111) < 0b00010000) {
            return (utf8_char_validity) { .valid = 0, .next_offset = offset };
        }
        
        uint32_t codepoint = extract_codepoint(str, offset, 4);
        if ((type = UT_GENERIC && codepoint != 0) == UT_INVALID) {
            return (utf8_char_validity) { .valid = type, .next_offset = offset };
        }
        return (utf8_char_validity) { .valid = type, .next_offset = offset + 4 };
    }

    return (utf8_char_validity) { .valid = UT_INVALID, .next_offset = offset };
}