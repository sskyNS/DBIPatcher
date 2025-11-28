/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

#include <stdint.h>
#include <stdio.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <sys/param.h>
#include <ctype.h>

#include "log.h"
#include "v2/keys.h"
#include "v2/utf8.h"
#include "v2/imm.h"
#include "v2/strings.h"
#include "utils.h"

#define MAX_STRING_LEN      2048

typedef struct _DataRef DataRef;

typedef struct _DataRef {
    uint32_t offset;
    uint32_t len;
    DataRef * next;
} DataRef;

typedef struct _TextReference {
    char name[16];
    uint32_t key_idx;
    uint64_t key;
    char * text;
    uint32_t text_length;
    
    uint8_t duplicate;
    uint32_t match_full;
    uint32_t match_partial;
    
    DataRef * data;
    uint32_t data_cnt;
} TextReference;

typedef struct _MemArea MemArea;

typedef struct _MemArea {
    uint8_t * start;
    uint32_t len;
    MemArea * next;
} MemArea;

MemArea * memarea_link(MemArea ** first, MemArea ** last, void * start, uint32_t len) {
    MemArea * next = calloc(1, sizeof(MemArea));
    next->start = start;
    next->len = len;
    
    if(*last) {
        (*last)->next = next;
    } else {
        *first = next;
    }
    
    *last = next;
    
    return next;
}

void memarea_free_chain(MemArea * start) {
    while(start) {
        MemArea * cur = start;
        start = start->next;
        free(cur);
    }
}

char nibble_to_char(uint8_t val) {
    val &= 0x0F;
    
    switch(val) {
        case 0 ... 9:
            return '0' + val;
        default:
            return 'a' + (val - 10);
    }
}

const char * string_encode(const char * src, uint32_t len) {
    if(len == UINT32_MAX) {
        len = strlen(src);
    }
    
    static char encode[MAX_STRING_LEN * 2];
    char * ptr_w = encode;
    
    // TODO: yea, would be better to handle all unprintable characters
    for(uint32_t i = 0; i < len; i++) {
        switch(src[i]) {
            case 0:
                goto end;
            case '\n':
                *(ptr_w++) = '\\';
                *(ptr_w++) = 'n';
                break;
            case '\r':
                *(ptr_w++) = '\\';
                *(ptr_w++) = 'r';
                break;
            default:
                if((uint8_t)src[i] < 0x20) {
                    *(ptr_w++) = '\\';
                    *(ptr_w++) = 'x';
                    *(ptr_w++) = nibble_to_char(src[i] >> 4);
                    *(ptr_w++) = nibble_to_char(src[i] & 0xF);
                } else {
                    *(ptr_w++) = src[i];
                }
                break;
        }
    }
    
end:
    if(ptr_w - encode == sizeof(encode)) {
        --ptr_w;
    }
    
    *(ptr_w) = 0;
    
    return encode;
}

static int8_t hex_digit_to_value(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    return -1;
}

void string_decode(void *data, uint32_t len) {
    uint8_t *src = data;
    uint8_t *dst = data;
    uint8_t *end = src + len;

    while (src < end && *src) {
        if (*src != '\\') {
            *dst++ = *src++;
            continue;
        }
        src++;
        if (src >= end) {
            *dst++ = '\\';
            break;
        }
        switch (*src) {
            case 'r':
                *dst++ = '\r';
                src++;
                break;
            case 'n':
                *dst++ = '\n';
                src++;
                break;
            case 'x': {
                // Expect two hex digits after '\x'
                if (src + 2 < end) {
                    int8_t hi = hex_digit_to_value(src[1]);
                    int8_t lo = hex_digit_to_value(src[2]);
                    if (hi >= 0 && lo >= 0) {
                        *dst++ = (hi << 4) | lo;
                        src += 3;
                        break;
                    }
                }
                // Fallback: output literal "\x" if invalid/incomplete
                *dst++ = '\\';
                *dst++ = 'x';
                src++;
                break;
            }
            default:
                // Unknown escape: emit literally
                *dst++ = '\\';
                *dst++ = *src++;
                break;
        }
    }
    *dst = '\0';
}

static void data_ref_free(DataRef * iter) {
    while(iter) {
        DataRef * m = iter;
        iter = iter->next;
        free(m);
    }
}

static void data_ref_append(TextReference * ref, DataRef * data) {
    data->next = ref->data;
    ref->data = data;
    ref->data_cnt++;
}

static DataRef * data_ref_init(uint32_t offset, uint32_t len) {
    DataRef * res = malloc(sizeof(*res));
    memset(res, 0, sizeof(*res));
    
    res->offset = offset;
    res->len = len;
    
    return res;
}

static void text_reference_free(TextReference * ref, uint32_t ref_len) {
    free(ref);
}

static int text_reference_load(const char * file, TextReference ** dst, uint32_t * len) {
    FILE * f = fopen(file, "r");
    if(!f) {
        return EXIT_FAILURE;
    }
    
    uint32_t ref_cnt = 1024;
    uint32_t ref_idx = 0;
    TextReference * refs = calloc(ref_cnt, sizeof(TextReference));
    
    char * line = NULL;
    size_t line_len = 0;
    ssize_t ret;
    while((ret = getline(&line, &line_len, f)) > 0) {
        if(line[ret - 1] == '\n') {
            line[--ret] = 0;
        }
        
        char * name = line; 
        char * end_name = strchr(name, ';');
        int len_name = end_name - name;
        
        if(!end_name) {
            //lf_e("fmt error %s", line);
            continue;
        }
        
        char * id = end_name + 1;
        char * end_id = strchr(id, ';');
        int len_id = end_id - id;
        
        if(!end_id) {
            //lf_e("fmt error %s", line);
            continue;
        }
        
        char * text = end_id + 1;
        char * end_text = text + strlen(text);
        int len_text = end_text - text;
        
        if(ref_cnt == ref_idx) {
            uint32_t prev_cnt = ref_cnt;
            TextReference * prev_ref = refs;
            
            ref_cnt *= 2;
            refs = calloc(ref_cnt, sizeof(TextReference));
            memcpy(refs, prev_ref, sizeof(TextReference) * prev_cnt);
        }
        
        char tmp[16];
        snprintf(tmp, sizeof(tmp), "%.*s", len_id, id);
        
        uint32_t key = strtoul(tmp, NULL, 10);
        
        TextReference * ref = &refs[ref_idx++];
        ref->key_idx = key;
        ref->key = get_key(key);
        ref->text = malloc(len_text + 1);

        snprintf(ref->text, len_text + 1, "%.*s", len_text, text);
        string_decode(ref->text, len_text);
        
        snprintf(ref->name, sizeof(ref->name), "%.*s", len_name, name);
        
        ref->text_length = strlen(ref->text) + 1;
        
        //lf_e("loaded %s key %u 0x%016" PRIx64, ref->name, ref->key_idx, ref->key);
    }
    
    *dst = refs;
    *len = ref_idx;
    
    if(line) {
        free(line);
    }
    fclose(f);
    
    return 0;
}

static uint32_t * init_matches_cnt(uint32_t cnt) {
    return calloc(cnt, sizeof(uint32_t));
}

static int free_matches_cnt(uint32_t * matches, uint32_t matches_len) {
    uint32_t matches_total = 0;
    for(uint32_t i = 0; i < matches_len; i++) {
        matches_total += matches[i];
        
        if(matches[i]) {
            lf_i("found %u matches uisng key %u", matches[i], i);
        }
    }
    
    free(matches);
    
    if(matches_total) {
        lf_i("found total of %u matches", matches_total);
    } else {
        lf_e("no matches found");
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
} 

uint32_t scan_get_matched_length(TextReference * matched, char * start, uint32_t len) {
    /*char tmp[MAX_STRING_LEN];
    
    memcpy(tmp, start, len);
    uint64_t key = matched->key;

    for(uint32_t i = 0; i < len; i++) {
        uint8_t xor = (key >> ((i & 7) * 8)) & 0xFF;
        tmp[i] ^= xor;
    }*/

    uint32_t matched_len;
    if(matched->text_length <= 8) {
        matched_len = matched->text_length;
    } else {
        matched_len = 8;

        while(matched_len != matched->text_length) {
            if(matched->text[matched_len] != start[matched_len]) {
                break;
            }
            matched_len++;
        }
    }
    
    return matched_len;
}

/** This function is used to get full text of partial references. It requires
 * partial reference to be already present in key file - you may get that by
 * running scan_english / scan_russian and manually adding it.
 * 
 * For each partial reference, it displays candidates that produced utf8 which
 * somewhat makes sense. This needs to be manualy evaluated.
 * 
 * This function should catch vast majority of partial immediates, with minimum
 * false detects, because compiler seems to produce pretty consistent code for 
 * those.
 */
int fscan_partials(FILE * f, const MemFile * mf, TextReference * refs, uint32_t ref_cnt) {
    ImmResult * res = imm_scan(mf->data, mf->len);
    char tmp[MAX_STRING_LEN];
    
    uint32_t matches_total = 0;
    
    fprintf(f, "// scanning partials" CRLF);
    
    for(uint32_t i = 0; i < ref_cnt; i++) {
        TextReference * ref = &refs[i];
        
        // dont want to rewrite it, there should be only partials with those
        // length constraints anyway
        if(ref->match_partial && ref->text_length > 8 && ref->text_length < 16) {
            memcpy(tmp, ref->text, 8);
            
            fprintf(f, "%-16s key 0x%-20" PRIx64 " / %-5u [%-9u]: '%s'\n", ref->name, ref->key, ref->key_idx, ref->text_length, ref->text);
            
            ImmResult * iter = res;
            uint8_t matched = 0;
            while(iter) {                
                memcpy(tmp + 8, iter->raw, iter->raw_len);
                uint32_t len = 8 + iter->raw_len;
                
                for(uint32_t x = 8; x < len; x++) {
                    uint8_t xor = (ref->key >> ((x & 7) * 8)) & 0xFF;
                    tmp[x] ^= xor;
                }
                
                uint32_t offset = 0;
                while(offset < len) {
                    utf8_char_validity val = utf8_check_char(tmp, offset);
                    if(val.valid) {
                        offset = val.next_offset;
                    } else {
                        break;
                    }
                }
                
                if(tmp[offset] == 0) {
                    matched = 1;
                    offset++;

                    // get rid of null terminator matching pretty much everything
                    if(offset > 0 && (ref->text_length <= 9 || tmp[8] != 0)) {
                        fprintf(f, "%20s 0x%-20x   %5s [%-3u / %-3u]: '%s'\n", "at", iter->matches->offsets[0], "", offset, len, string_encode(tmp, offset));
                        matches_total++;
                    }
                }

                iter = iter->next;
            }
            
            if(matched) {
                fprintf(f, "\n");
            }
        } 
    }
    
    imm_result_free(res);
    
    if(matches_total) {
        lf_i("found total of %u matches", matches_total);
    } else {
        lf_e("no matches found");
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
}

/** Use this function to match unknown cyrillic strings. It should be called 
 * after keys were loaded, as that shrinks down the search space.
 * 
 * Found strings have to be manualy evaluated and added to key file.
 */
int fscan_russian(FILE * out, const MemFile * mf, uint32_t min_cyrillic, uint32_t key_cnt, MemArea * mem_iter) {
    char tmp[MAX_STRING_LEN];
    
    KeySet * ks = gen_key_set(key_cnt);

    uint32_t * matches = init_matches_cnt(ks->key_cnt);
    
    while(mem_iter) {
        fprintf(out, "// scanning 0x%08X to 0x%08X (%u B)" CRLF, (uint32_t)(mem_iter->start - mf->data), (uint32_t)((mem_iter->start - mf->data) + mem_iter->len), mem_iter->len);

        uint32_t mem_start = mem_iter->start - mf->data;
        uint32_t max_len = mem_start + (mem_iter->len / 8) * 8;

        for(uint32_t i = mem_start; i < max_len;) {
            uint32_t matched_cyrillic = 0;
            uint64_t matched_key = 0;
            uint32_t matched_key_idx = 0;

            uint32_t rem = max_len - i;
            uint32_t len = MIN(rem, MAX_STRING_LEN);
            char * start = (char*)(mf->data + i);

            for(uint32_t k = 0; k < ks->key_cnt; k++) {
                memcpy(tmp, start, 8);
                uint64_t key = ks->keys[k];
                
                // key_cnt 0 means we want explicitely to check unxored strings
                if(key == 0 && key_cnt != 0) {
                    continue;
                }

                for(uint32_t x = 0; x < 8; x++) {
                    uint8_t xor = (key >> ((x & 7) * 8)) & 0xFF;
                    tmp[x] ^= xor;
                }

                if(tmp[0] == 0) {
                    continue;
                }

                uint32_t cur_cyrillic = 0;
                uint32_t offset = 0;
                while(offset < 8) {
                    utf8_char_validity val = utf8_check_char(tmp, offset);
                    if(val.valid) {
                        offset = val.next_offset;

                        if(val.valid == UT_CYRILLIC) {
                            ++cur_cyrillic;
                            break;
                        }
                    } else {
                        if(tmp[offset] == 0) {
                            offset++;
                        }
                        break;
                    }
                }

                uint8_t is_space = 0;
                if(isspace(tmp[0]) && isspace(tmp[1]) && isspace(tmp[2]) && isspace(tmp[3])) {
                    is_space = 1;
                }

                if(cur_cyrillic || is_space) {
                    cur_cyrillic = 0;

                    memcpy(tmp, start, len);

                    for(uint32_t x = 0; x < len; x++) {
                        uint8_t xor = (key >> ((x & 7) * 8)) & 0xFF;
                        tmp[x] ^= xor;
                    }

                    uint32_t offset = 0;
                    while(offset < len) {
                        utf8_char_validity val = utf8_check_char(tmp, offset);
                        if(val.valid) {
                            offset = val.next_offset;

                            if(val.valid == UT_CYRILLIC) {
                                ++cur_cyrillic;
                            }
                        } else {
                            if(tmp[offset] == 0) {
                                offset++;
                            }
                            break;
                        }
                    }

                    if(cur_cyrillic >= min_cyrillic && cur_cyrillic > matched_cyrillic && offset >= 7) {
                        matched_cyrillic = cur_cyrillic;
                        matched_key = key;
                        matched_key_idx = k;
                    }
                } 
            }

            if(matched_cyrillic) {
                memcpy(tmp, start, len);
                uint64_t key = matched_key;

                for(uint32_t x = 0; x < len; x++) {
                    uint8_t xor = (key >> ((x & 7) * 8)) & 0xFF;
                    tmp[x] ^= xor;
                }

                uint32_t offset = 0;
                while(offset < len) { 
                    utf8_char_validity val = utf8_check_char(tmp, offset);
                    if(val.valid) {
                        offset = val.next_offset;
                    } else {
                        if(tmp[offset] == 0) {
                            offset++;
                        }
                        break;
                    }
                }

                const char * encode = string_encode(tmp, offset);

                char buf[256];
                snprintf(buf, sizeof(buf), "   at 0x%08X key 0x%016" PRIx64 " / %-3u", i, matched_key, matched_key_idx);
                fprintf(out, "%-50s [%-3u]: '%s'" CRLF, buf, offset, encode);
                
                matches[matched_key_idx]++;
                // TODO: output to file in slightly nicer format
                //lf_w("[SP %-4u B at 0x%08X  key: 0x%016" PRIx64 "  ;%u;%s\n", offset, i, matched_key, matched_key_idx, encode);

                i += ((offset + 7) / 8) * 8;
                //i += 8;
            } else {
                i += 8;
            }
        }

        mem_iter = mem_iter->next;
    }

    int ret = free_matches_cnt(matches, ks->key_cnt);
    free_key_set(ks);
    return ret;
}

/** Use this function to match unknown english strings. It should be called 
 * after keys were loaded, as that shrinks down the search space. Also make sure
 * to first resolve russian strings, because there will be a lot false positives.
 * 
 * Found strings have to be manualy evaluated and added to key file.
 */
int fscan_english(FILE * out, const MemFile * mf, uint32_t min_offset, uint32_t key_cnt, MemArea * mem_iter) {
    char tmp[MAX_STRING_LEN * 2];
    
    KeySet * ks = gen_key_set(key_cnt);
    
    // get rid of keys producing mostly valid ascii
    for(uint32_t i = 1; i < ks->key_cnt; i++) {
        uint32_t ascii = 0;
        uint8_t * key = (uint8_t*)&ks->keys[i];
        for(uint32_t i = 0; i < 8; i++) {
            if(key[i] <= 127) {
                ascii++;
            }
        }
        
        if(ascii > 6) {
            ks->keys[i] = 0;
        }
    }

    uint32_t * matches = init_matches_cnt(ks->key_cnt);
    
    while(mem_iter) {
        fprintf(out, "// scanning 0x%08X to 0x%08X (%u B)" CRLF, (uint32_t)(mem_iter->start - mf->data), (uint32_t)((mem_iter->start - mf->data) + mem_iter->len), mem_iter->len);

        uint32_t mem_start = mem_iter->start - mf->data;
        uint32_t max_len = mem_start + (mem_iter->len / 8) * 8;

        for(uint32_t i = mem_start; i < max_len;) {
            uint32_t matched_offset = 0;
            uint64_t matched_key = 0;
            uint32_t matched_key_idx = 0;

            uint32_t rem = max_len - i;
            uint32_t len = MIN(rem, MAX_STRING_LEN);
            char * start = (char*)(mf->data + i);

            for(uint32_t k = 0; k < ks->key_cnt; k++) {
                memcpy(tmp, start, 8);
                uint64_t key = ks->keys[k];
                
                // key_cnt 0 means we want explicitely to check unxored strings
                if(key == 0 && key_cnt != 0) {
                    continue;
                }

                for(uint32_t x = 0; x < 8; x++) {
                    uint8_t xor = (key >> ((x & 7) * 8)) & 0xFF;
                    tmp[x] ^= xor;
                }

                if(tmp[0] == 0) {
                    continue;
                }

                uint32_t offset = 0;
                while(offset < 8) {
                    utf8_char_validity val = utf8_check_char(tmp, offset);
                    if(val.valid) {
                        offset = val.next_offset;
                    } else {
                        if(tmp[offset] == 0) {
                            offset++;
                        }
                        break;
                    }
                }

                uint8_t is_space = 0;
                if(isspace(tmp[0]) && isspace(tmp[1]) && isspace(tmp[2]) && isspace(tmp[3])) {
                    is_space = 1;
                }

                if(offset == 8 || is_space) {
                    memcpy(tmp, start, len);

                    for(uint32_t x = 0; x < len; x++) {
                        uint8_t xor = (key >> ((x & 7) * 8)) & 0xFF;
                        tmp[x] ^= xor;
                    }

                    uint8_t zero_term = 0;
                    uint32_t offset = 0;
                    while(offset < len) {
                        utf8_char_validity val = utf8_check_char(tmp, offset);
                        if(val.valid) {
                            offset = val.next_offset;
                        } else {
                            if(tmp[offset] == 0) {
                                zero_term = 1;
                                offset++;
                                break;
                            }
                            break;
                        }
                    }
                    
                    // either possible partial or full
                    if((offset >= 6 && offset <= 8) || zero_term) {
                        if(offset >= min_offset && offset > matched_offset) {
                            matched_offset = offset;
                            matched_key = key;
                            matched_key_idx = k;
                        }
                    }
                } 
            }

            if(matched_offset) {
                memcpy(tmp, start, len);
                uint64_t key = matched_key;

                for(uint32_t x = 0; x < len; x++) {
                    uint8_t xor = (key >> ((x & 7) * 8)) & 0xFF;
                    tmp[x] ^= xor;
                }

                uint32_t offset = 0;
                while(offset < len) { 
                    utf8_char_validity val = utf8_check_char(tmp, offset);
                    if(val.valid) {
                        offset = val.next_offset;
                    } else {
                        if(tmp[offset] == 0) {
                            offset++;
                        }
                        break;
                    }
                }

                const char * encode = string_encode(tmp, offset);

                char buf[256];
                snprintf(buf, sizeof(buf), "   at 0x%08X key 0x%016" PRIx64 " / %-3u", i, matched_key, matched_key_idx);
                fprintf(out, "%-50s [%-3u]: '%s'" CRLF, buf, offset, encode);
                
                matches[matched_key_idx]++;
                
                // TODO: output to file in slightly nicer format
                //lf_w("[SP %-4u B at 0x%08X  key: 0x%016" PRIx64 "  ;%u;%s\n", offset, i, matched_key, matched_key_idx, encode);

                i += ((offset + 7) / 8) * 8;
                //i += 8;
            } else {
                i += 8;
            }
        }

        mem_iter = mem_iter->next;
    }

    int ret = free_matches_cnt(matches, ks->key_cnt);
    free_key_set(ks);
    return ret;
}

/** This function is used to resolve specific string by trying all possible keys
 * on each candidate slot.
 * 
 * Reports all (at least partial) matches.
 */
int fscan_string(FILE * f, const MemFile * mf, uint32_t key_cnt, const char * target) {
    char tmp[MAX_STRING_LEN];
        
    fprintf(f, "// %-47s [%-3u]: '%s'\n", "lookup string", (uint32_t)strlen(target) + 1, target);
    
    char target2[strlen(target) + 1];
    snprintf(target2, sizeof(target2), "%s", target);
    string_decode(target2, sizeof(target2) + 1);
    
    
    KeySet * ks = gen_key_set(key_cnt);
    ks->keys[0] = 0;
    
    uint32_t mem_start = 0;
    uint32_t max_len = (mf->len / 8) * 8;
    
    uint32_t tgt_len = strlen(target2);
    uint32_t tgt_len_compare = MIN(tgt_len, 8);

    uint32_t * matches = init_matches_cnt(ks->key_cnt);
    
    for(uint32_t i = mem_start; i < max_len;) {
        uint32_t rem = max_len - i;
        uint32_t len = MIN(rem, MAX_STRING_LEN);
        char * start = (char*)(mf->data + i);

        for(uint32_t k = key_cnt ? 1 : 0; k < ks->key_cnt; k++) {
            memcpy(tmp, start, 8);
            uint64_t key = ks->keys[k];

            for(uint32_t x = 0; x < 8; x++) {
                uint8_t xor = (key >> ((x & 7) * 8)) & 0xFF;
                tmp[x] ^= xor;
            }

            if(memcmp(tmp, target2, tgt_len_compare) == 0) {
                memcpy(tmp, start, len);

                for(uint32_t x = 0; x < len; x++) {
                    uint8_t xor = (key >> ((x & 7) * 8)) & 0xFF;
                    tmp[x] ^= xor;
                }
                
                uint32_t offset = 0;
                while(offset < len) {
                    utf8_char_validity val = utf8_check_char_unchecked(tmp, offset);
                    if(val.valid) {
                        offset = val.next_offset;
                    } else {
                        if(tmp[offset] == 0) {
                            offset++;
                        }
                        break;
                    }
                }
                
                const char * encode = string_encode(tmp, offset);

                char buf[256];
                snprintf(buf, sizeof(buf), "   at 0x%08X key 0x%016" PRIx64 " / %-3u", i, key, k);
                fprintf(f, "%-50s [%-3u]: '%s'\n", buf, offset, encode);
                
                matches[k]++;
            }
        }

        i += 8;
    }
     
    int ret = free_matches_cnt(matches, ks->key_cnt);
    free_key_set(ks);
    return ret;
}

static void print_data_ref(FILE * f, TextReference * ref, DataRef * iter) {
    uint8_t raw[ref->text_length];
    
    const uint8_t * src = (const uint8_t*)ref->text;
    uint8_t * dst = raw;
    for(uint32_t x = 0; x < ref->text_length; x++) {
        uint8_t xor = (ref->key >> ((x & 7) * 8)) & 0xFF;
        *(dst++) = src[x] ^ xor;
    }
    
    while(iter) {
        uint32_t iter_cnt = (iter->len + 7) / 8;
        
        fprintf(f, "// at 0x%08X diff=%u\n", iter->offset, iter->len);

        for(uint32_t i = 0; i < iter_cnt; i++) {
            uint32_t raw_idx = i * 8;
            uint32_t rem = iter->len - raw_idx;
            rem = MIN(rem, 8);

            fprintf(f, "\tdat=0x%08X;offset=%u;len=%u;imm=0x", iter->offset + raw_idx, raw_idx, rem);

            for(uint32_t j = rem; j --> 0;) {
                fprintf(f, "%02X", raw[raw_idx + j]);
            }
            fprintf(f, "\n");
        }

        iter = iter->next;
    }
}

MemArea * text_reference_match_full(TextReference * refs, uint32_t ref_len, const MemFile * mf, uint32_t mem_start, uint32_t mem_len) {
    //char tmp[MAX_STRING_LEN];
    // 0x005A0000 0x38000
    
    // now, this is malloced, so it should be ok
    if((uintptr_t)mf->data % sizeof(uint64_t) != 0) {
        lf_e("data not aligned to %u B", (uint32_t)sizeof(uint64_t));
    }
    
    MemArea * memarea_start = NULL;
    MemArea * memarea_last = NULL;
    
    mem_start = (mem_start / 8) * 8;
    
    uint32_t mem_end = mf->len;
    mem_end = MIN(mem_end, mem_start + mem_len);
    mem_end = (mem_end / 8) * 8;
    
    // lets precompute everything, this should speed things up nicely 
    uint64_t cmp_mask[ref_len];
    memset(cmp_mask, 0, sizeof(cmp_mask));
    
    uint64_t cmp_val[ref_len];
    memset(cmp_val, 0, sizeof(cmp_val));
    
    for(uint32_t i = 0; i < ref_len; i++) {
        TextReference * ref = &refs[i];

        uint32_t compare_len = MIN(8, ref->text_length);
        memcpy(&cmp_val[i], ref->text, compare_len);
        memset(&cmp_mask[i], 0xFF, compare_len);
    }
    
    for(uint32_t i = mem_start; i < mem_end;) {
        uint32_t rem = mem_end - i;
        uint32_t len = MIN(rem, MAX_STRING_LEN);
        char * start = (char*)(mf->data + i);
        
        TextReference * matched = NULL;
        uint32_t max_matched_length = 0;
        
        for(uint32_t i = 0; i < ref_len; i++) {
            TextReference * ref = &refs[i];
            
            // partials have very specific length requirements, however texts
            // without key seems to be stored fully
            if(ref->text_length < 16 && ref->key != 0) {
                continue;
            }
                                    
            // honestly, no need to decode utf8 in first pass, just comparing
            const uint64_t * cur_val = (const uint64_t*)start;
            if((*cur_val & cmp_mask[i]) != cmp_val[i]) {
                continue;
            }

            uint32_t matched_length = scan_get_matched_length(ref, start, len);
            
            if(matched_length == ref->text_length && matched_length > max_matched_length) {
                max_matched_length = matched_length;
                matched = ref;
            }
            
            /*// prefer probable partials on collision
            // - no matches yet
            // - current is longer
            // - previously matched something which should not be partial, but resolved as one and current IS partial
            if(!matched || 
                matched_length > max_matched_length || 
                (matched_length < 16 && max_matched_length < 16 && matched->text_length >= 16 && ref->text_length < 16)
            ) {
                // further filtering:
                // - no matches yet
                // - NOT: already have partial matched, while current is NOT partial
                if(!matched || !(max_matched_length < 16 && matched->text_length < 16 && ref->text_length >= 16)) {
                    max_matched_length = matched_length;
                    matched = ref;
                }
            }*/
        }
        
        if(matched) {
            DataRef * ref;
            if(max_matched_length == matched->text_length) {
                matched->match_full++;
                
                if(matched->match_partial) {
                    lf_e("E3");
                }
            } else {
                lf_e("matched partial from full");
                matched->match_partial++;
                
                if(matched->match_full) {
                    lf_e("E2");
                }
            }
            
            ref = data_ref_init(i, max_matched_length);
            data_ref_append(matched, ref);

            if(mem_start != i) {
                memarea_link(&memarea_start, &memarea_last, mf->data + mem_start, i - mem_start);
            }
            
            i += ((max_matched_length + 7) / 8) * 8;
            mem_start = i;
        } else {
            i += 8;
        }
    }
        
    if(mem_start != mem_end) {
        memarea_link(&memarea_start, &memarea_last, mf->data + mem_start, mem_end - mem_start);
    }

    return memarea_start;
}

MemArea * text_reference_match_partial(TextReference * refs, uint32_t ref_len, const MemFile * mf, MemArea * area) {
    // now, this is malloced, so it should be ok
    if((uintptr_t)mf->data % sizeof(uint64_t) != 0) {
        lf_e("nro data not aligned to %u B", (uint32_t)sizeof(uint64_t));
        return NULL;
    }
    
    MemArea * memarea_start = NULL;
    MemArea * memarea_last = NULL;
        
    // lets precompute everything, this should speed things up nicely 
    uint64_t cmp_mask[ref_len];
    memset(cmp_mask, 0, sizeof(cmp_mask));
    
    uint64_t cmp_val[ref_len];
    memset(cmp_val, 0, sizeof(cmp_val));
    
    for(uint32_t i = 0; i < ref_len; i++) {
        TextReference * ref = &refs[i];

        uint32_t compare_len = MIN(8, ref->text_length);
        memcpy(&cmp_val[i], ref->text, compare_len);
        memset(&cmp_mask[i], 0xFF, compare_len);
    }
    
    MemArea * iter = area;
    while(iter) {
        uint32_t mem_start = iter->start - mf->data;
        uint32_t mem_end = mem_start + iter->len;
        
        for(uint32_t i = mem_start; i < mem_end;) {
            //uint32_t rem = mem_end - i;
            //uint32_t len = MIN(rem, MAX_STRING_LEN);
            char * start = (char*)(mf->data + i);

            TextReference * matched = NULL;
            uint32_t max_matched_length = 0;
            
            for(uint32_t i = 0; i < ref_len; i++) {
                TextReference * ref = &refs[i];

                // partials have very specific length requirements
                if(ref->text_length >= 16 || ref->text_length < 8) {
                    continue;
                }

                // honestly, no need to decode utf8 in first pass, just comparing
                const uint64_t * cur_val = (const uint64_t*)start;
                if((*cur_val & cmp_mask[i]) != cmp_val[i]) {
                    continue;
                }

                // no way to distinguish multiple partial matches
                max_matched_length = 8;
                matched = ref;
                break;
            }

            if(matched) {
                DataRef * ref = data_ref_init(i, max_matched_length);
                data_ref_append(matched, ref);
                
                if(max_matched_length == matched->text_length) {
                    matched->match_full++;
                    
                    if(matched->match_partial) {
                        lf_e("E2");
                    }
                } else {
                    //lf_e("matched partial");
                    matched->match_partial++;
                    
                    if(matched->match_full) {
                        lf_e("E1");
                    }
                }

                if(mem_start != i) {
                    memarea_link(&memarea_start, &memarea_last, mf->data + mem_start, i - mem_start);
                }
                
                i += ((max_matched_length + 7) / 8) * 8;
                mem_start = i;
            } else {
                i += 8;
            }
        }

        if(mem_start != mem_end) {
            memarea_link(&memarea_start, &memarea_last, mf->data + mem_start, mem_end - mem_start);
        }
        
        iter = iter->next;
    }
    
    memarea_free_chain(area);
    return memarea_start;
}

MemArea * text_reference_match(TextReference * refs, uint32_t ref_len, const MemFile * mf, uint32_t mem_start, uint32_t mem_len) {
    // encrypt texts to speed up search
    for(uint32_t i = 0; i < ref_len; i++) {
        TextReference * ref = &refs[i];
        
        for(uint32_t i = 0; i < ref->text_length; i++) {
            uint8_t xor = (ref->key >> ((i & 7) * 8)) & 0xFF;
            ref->text[i] ^= xor;
        }
    }
    
    MemArea * result = text_reference_match_full(refs, ref_len, mf, mem_start, mem_len);
    result = text_reference_match_partial(refs, ref_len, mf, result);
    
    // decrypt texts again
    for(uint32_t i = 0; i < ref_len; i++) {
        TextReference * ref = &refs[i];

        for(uint32_t i = 0; i < ref->text_length; i++) {
            uint8_t xor = (ref->key >> ((i & 7) * 8)) & 0xFF;
            ref->text[i] ^= xor;
        }
    }
    
    for(uint32_t i = 0; i < ref_len; i++) {
        TextReference * ref_cur = &refs[i];
        
        for(uint32_t j = i + 1; j < ref_len; j++) {
            TextReference * ref_check = &refs[j];
            
            if(!(ref_cur->duplicate || ref_check->duplicate) && !(ref_check->match_full || ref_check->match_partial)) {
                if(ref_cur->key == ref_check->key 
                        && ref_cur->text_length == ref_check->text_length
                        && memcmp(ref_cur->text, ref_check->text, ref_cur->text_length) == 0) {
                    
                    ref_check->duplicate = 1;
                }
            }
        }
    }
    
    return result;
}

int scan_string(const ScanStringArgs * args) {        
    FILE * out = stdout;
    
    if(args->out != NULL) {
        out = args->out;
    }

    int ret = fscan_string(out, args->dbi_mf, args->key_cnt, args->lookup);
        
    return ret;
}

int scan_strings_type(const ScanTypeArgs * args) {
    // 0x005A0000, 0x38000
    
    TextReference * refs;
    uint32_t ref_len;
    
    uint32_t len = UINT32_MAX;
    uint32_t start = 0;
    
    const MemFile * mf = args->dbi_mf;
    if(!mf) {
        return EXIT_FAILURE;
    }
    
    if(text_reference_load(args->keys, &refs, &ref_len) != EXIT_SUCCESS) {
        lf_e("failed to load \"%s\"", args->keys);
        return EXIT_FAILURE;
    } else {
        lf_i("using dictionary file \"%s\"", args->keys);
    }
    
    if(len == UINT32_MAX) {
        len = mf->len - start;
    }
    
    FILE * out = stdout;
    if(args->out != NULL) {
        out = args->out;
    }
    
    MemArea * memarea_start = text_reference_match(refs, ref_len, mf, start, len);
    int ret;
    
    if(args->type == SCAN_RUSSIAN) {
        ret = fscan_russian(out, mf, args->min_match, args->key_cnt, memarea_start);  
    } else {
        ret = fscan_english(out, mf, args->min_match, args->key_cnt, memarea_start);    
    }
    
    memarea_free_chain(memarea_start);
    text_reference_free(refs, ref_len);
        
    return ret;
}

int scan_partials(const ScanPartialsArgs * args) {
    
    TextReference * refs;
    uint32_t ref_len;
    
    uint32_t len = UINT32_MAX;
    uint32_t start = 0;
    
    const MemFile * mf = args->dbi_mf;
    if(!mf) {
        return EXIT_FAILURE;
    }
    
    if(text_reference_load(args->keys, &refs, &ref_len) != 0) {
        lf_e("failed to load \"%s\"", args->keys);
        return EXIT_FAILURE;
    } else {
        lf_i("using dictionary file \"%s\"", args->keys);
    }
            
    FILE * out = stdout;
    if(args->out != NULL) {
        out = args->out;
    }
    
    MemArea * memarea_start = text_reference_match(refs, ref_len, mf, start, len);
    
    // basically let run imm_scanner and try matching all partials,
    // trying to verify valid utf8 string afterwards
    int ret = fscan_partials(out, mf, refs, ref_len);
    
    memarea_free_chain(memarea_start);
    text_reference_free(refs, ref_len);

    return ret;
}

int scan_blueprint(const ScanBlueprintArgs * args) {
    TextReference * refs;
    uint32_t ref_len;
    
    uint32_t len = UINT32_MAX;
    uint32_t start = 0;
    
    const MemFile * mf = args->dbi_mf;
    if(!mf) {
        return EXIT_FAILURE;
    }
    
    if(text_reference_load(args->keys, &refs, &ref_len) != 0) {
        lf_e("failed to load \"%s\"", args->keys);
        return EXIT_FAILURE;
    } else {
        lf_i("using dictionary file \"%s\"", args->keys);
    }
    
    lf_i("loaded %u references", ref_len);
    
    FILE * out = stdout;
    
    if(args->out != NULL) {
        out = args->out;
    }
        
    //--------------------------------------------------------------------------
    // FIRST PASS - match known key-string combo
    //--------------------------------------------------------------------------
    MemArea * memarea_start = text_reference_match(refs, ref_len, mf, start, len);
        
    //--------------------------------------------------------------------------
    // SECOND PASS - match partials
    //--------------------------------------------------------------------------

    fprintf(out, "//------------------------" CRLF);
    fprintf(out, "// [MATCHED LONG]" CRLF);
    fprintf(out, "//------------------------" CRLF);
    fprintf(out, CRLF);
    
    uint32_t cnt_matched_full = 0;
    for(uint32_t i = 0; i < ref_len; i++) {
        TextReference * ref = &refs[i];
        
        if(ref->match_full) {
            //lf_i("%-10s[k=0x%016" PRIx64 " / %-3u  len=%-5u  full=%-3u  partial=%-3u]:%s", ref->name, ref->key, ref->key_idx, ref->text_length, ref->match_full, ref->match_partial, string_encode(ref->text, -1));
            // 14:14:40  [I] APP055    [k=0xdfafb5419f490ddb / 59   len=15     full=0    partial=1  ]:LFS мод.: -
            //// at 0x005A1998 diff=8
            //  dat=0x005A1998;offset=0;len=8;imm=0x617F0991BF1A4B97
            
            fprintf(out, "key=0x%016" PRIx64 ";id=%-40s", ref->key, ref->name);
            fprintf(out, "// [%-3u]: '%s'" CRLF, ref->text_length, string_encode(ref->text, -1));
            
            print_data_ref(out, ref, ref->data);
            
            fprintf(out, "\n");
            
            cnt_matched_full++;
        }
    }
    
    fprintf(out, "//------------------------" CRLF);
    fprintf(out, "// [MATCHED PARTIAL]" CRLF);
    fprintf(out, "//------------------------" CRLF);
    fprintf(out, CRLF);
    
    uint32_t cnt_matched_partial = 0;
    uint32_t cnt_unmatched_partial = 0;
    for(uint32_t i = 0; i < ref_len; i++) {
        TextReference * ref = &refs[i];
        
        if(ref->duplicate || ref->text_length >= 16 || ref->text_length <= 8) {
            continue;
        }
        
        if(ref->match_partial) {
            uint8_t found = 0;
            //lf_i("%-10s[k=0x%016" PRIx64 " / %-3u  len=%-5u  full=%-3u  partial=%-3u]:%s", ref->name, ref->key, ref->key_idx, ref->text_length, ref->match_full, ref->match_partial, string_encode(ref->text, -1));
            
            fprintf(out, "key=0x%016" PRIx64 ";id=%-40s", ref->key, ref->name);
            fprintf(out, "// [%-3u]: '%s'" CRLF, ref->text_length, string_encode(ref->text, -1));
            
            print_data_ref(out, ref, ref->data);
            
            ImmDefine d = {
                .key = ref->key,
                .data = ref->text,
                .len = strlen(ref->text) + 1,
                .offset = 8,
            };
            
            // not really interested in final null terminator
            if(ref->text_length > 9) {
                // first try matching whole partial
                ImmResult * res = imm_lookup(mf->data, mf->len, &d, 4);
                
                // now, I have seen single partial, which was missing its null
                // terminator - so, lets try matching without null terminator, 
                // as long as there is sufficient length available - matching
                // single byte is obviously not viable approach.
                //
                // matches found in this way will need to be manualy evaluated!
                if(res->matches_cnt == 0 && ref->text_length > 10) {
                    d.len--;
                    res = imm_lookup(mf->data, mf->len, &d, 4);
                    d.len++;
                } 
                
                if(res->matches_cnt != 0) {
                    found = 1;
                    
                    imm_print(out, &d, res);
                    
                    if(d.len != res->raw_len + d.offset) {
                        d.offset += res->raw_len;
                        imm_print_dummy(out, &d);
                    }
                }
                
                imm_result_free(res);
            } else if(ref->text_length == 9) {
                found = 1;
                imm_print_dummy(out, &d);
            }
            
            fprintf(out, CRLF);
            
            if(found) {
                for(uint32_t j = i + 1; j < ref_len; j++) {
                    TextReference * ref_check = &refs[j];

                    if(ref->text_length == ref_check->text_length && ref->key == ref_check->key && memcmp(ref->text, ref_check->text, ref->text_length) == 0) {
                        ref_check->duplicate = 1;
                    }
                }
                
                
                cnt_matched_partial++;
            } else {
                lf_w("unmatched found key=0x%016" PRIx64 ";id=%-40s [%-3u]: '%s'", ref->key, ref->name, ref->text_length, string_encode(ref->text, -1));
                cnt_unmatched_partial++;
            }
        } else if (!ref->match_full) {
            //lf_w("unmatched not found key=0x%016" PRIx64 ";id=%-40s [%-3u]:'%s'", ref->key, ref->name, ref->text_length, string_encode(ref->text, -1));
            cnt_unmatched_partial++;
        }
    }
    
    fprintf(out, "//------------------------" CRLF);
    fprintf(out, "// [MATCHED SHORT]" CRLF);
    fprintf(out, "//------------------------" CRLF);
    fprintf(out, CRLF);
    
    uint32_t cnt_unmatched_short = 0;
    for(uint32_t i = 0; i < ref_len; i++) {
        TextReference * ref = &refs[i];
        
        if(ref->duplicate) {
            continue;
        }
        
        if(!(ref->match_full || ref->match_partial) && ref->text_length <= 8) {
            //lf_i("%-10s[k=0x%016" PRIx64 " / %-3u  len=%-5u]:%s", ref->name, ref->key, ref->key_idx, ref->text_length, string_encode(ref->text, -1));
            //lf_e("trying to match %s", ref->text);

            ImmDefine d = {
                .key = ref->key,
                .data = ref->text,
                .len = strlen(ref->text) + 1,
                .offset = 0,
            };
            
            ImmResult * res = imm_lookup(mf->data, mf->len, &d, 4);
            
            if(res->matches_cnt != 0) {
                ref->match_partial = res->matches_cnt;
                
                fprintf(out, "key=0x%016" PRIx64 ";id=%-40s", ref->key, ref->name);
                fprintf(out, "// [%-3u]: '%s'" CRLF, ref->text_length, string_encode(ref->text, -1));
            
                imm_print(out, &d, res);
                
                fprintf(out, CRLF);
            } else {
                //imm_print_dummy(out, &d);
                cnt_unmatched_short++;
            }
            
            imm_result_free(res);
        }
    }
    
    fprintf(out, CRLF);
    fprintf(out, "//------------------------" CRLF);
    fprintf(out, "// [UNMATCHED LONG]" CRLF);
    fprintf(out, "//------------------------" CRLF);
    fprintf(out, CRLF);
    
    uint32_t cnt_unmatched_long = 0;
    for(uint32_t i = 0; i < ref_len; i++) {
        TextReference * ref = &refs[i];
        
        if(ref->duplicate) {
            continue;
        }
        
        if(!(ref->match_full || ref->match_partial) && ref->text_length >= 16) {
            fprintf(out, "// %-10s[k=0x%016" PRIx64 " / %-3u  len=%-5u]: '%s'" CRLF, ref->name, ref->key, ref->key_idx, ref->text_length, string_encode(ref->text, -1));
            cnt_unmatched_long++;
        }
    }
    
    fprintf(out, CRLF);
    fprintf(out, "//------------------------" CRLF);
    fprintf(out, "// [UNMATCHED PARTIAL]" CRLF);
    fprintf(out, "//------------------------" CRLF);
    fprintf(out, CRLF);
    
    //uint32_t cnt_unmatched_long = 0;
    for(uint32_t i = 0; i < ref_len; i++) {
        TextReference * ref = &refs[i];
        
        if(ref->duplicate) {
            continue;
        }
        
        if(!(ref->match_full || ref->match_partial) && ref->text_length > 8 && ref->text_length < 16) {
            fprintf(out, "// %-10s[k=0x%016" PRIx64 " / %-3u  len=%-5u]: '%s'" CRLF, ref->name, ref->key, ref->key_idx, ref->text_length, string_encode(ref->text, -1));
            //cnt_unmatched_long++;
        }
    }
    
    fprintf(out, CRLF);
    fprintf(out, "//------------------------" CRLF);
    fprintf(out, "// [UNMATCHED SHORT]" CRLF);
    fprintf(out, "//------------------------" CRLF);
    fprintf(out, CRLF);
    
    //uint32_t cnt_unmatched_long = 0;
    for(uint32_t i = 0; i < ref_len; i++) {
        TextReference * ref = &refs[i];
        
        if(ref->duplicate) {
            continue;
        }
        
        if(!(ref->match_full || ref->match_partial) && ref->text_length <= 8) {
            fprintf(out, "// %-10s[k=0x%016" PRIx64 " / %-3u  len=%-5u]: '%s'" CRLF, ref->name, ref->key, ref->key_idx, ref->text_length, string_encode(ref->text, -1));
            //cnt_unmatched_long++;
        }
    }

    fprintf(out, CRLF);
    fprintf(out, "//------------------------" CRLF);
    fprintf(out, "// [DUPLICATES]" CRLF);
    fprintf(out, "//------------------------" CRLF);
    fprintf(out, CRLF);
    
    uint32_t cnt_duplicate = 0;
    for(uint32_t i = 0; i < ref_len; i++) {
        TextReference * ref = &refs[i];
        
        if(ref->duplicate) {
            fprintf(out, "// %-10s[k=0x%016" PRIx64 " / %-3u  len=%-5u]: '%s'" CRLF, ref->name, ref->key, ref->key_idx, ref->text_length, string_encode(ref->text, -1));
            cnt_duplicate++;
        }
    }
    
    fprintf(out, CRLF);
    fprintf(out, "//------------------------" CRLF);
    fprintf(out, "// [MISMATCHED]" CRLF);
    fprintf(out, "//------------------------" CRLF);
    fprintf(out, CRLF);
    
    uint32_t cnt_mismatched = 0;
    for(uint32_t i = 0; i < ref_len; i++) {
        TextReference * ref = &refs[i];
        
        if(ref->match_full && ref->match_partial) {
            fprintf(out, "// %-10s[k=0x%016" PRIx64 " / %-3u  p=%u  f=%u  len=%-5u]: '%s'" CRLF, ref->name, ref->key, ref->key_idx, ref->match_partial, ref->match_full, ref->text_length, string_encode(ref->text, -1));
            cnt_mismatched++;
        }
    }

    fprintf(out, CRLF);
    fprintf(out, "//------------------------" CRLF);
    fprintf(out, "// [MEMORY AREAS]" CRLF);
    fprintf(out, "//------------------------" CRLF);
    fprintf(out, CRLF);
    
    MemArea * iter = memarea_start;
    while(iter) {
        fprintf(out, "// 0x%08X;%u" CRLF, (uint32_t)(iter->start - mf->data), iter->len);
        iter = iter->next;
    }
    
    fprintf(out, CRLF);
    fprintf(out, "//------------------------" CRLF);
    fprintf(out, "// [STATS]" CRLF);
    fprintf(out, "//------------------------" CRLF);
    fprintf(out, CRLF);
    
    #define PRINT_BOTH(fmt, ...) do {\
        lf_i(fmt, __VA_ARGS__);\
        fprintf(out, "//");\
        fprintf(out, fmt, __VA_ARGS__);\
        fprintf(out, CRLF);\
    } while(0)
    
    lf_i("stats:");
    PRINT_BOTH(" matched_full:      %u", cnt_matched_full);
    PRINT_BOTH(" matched_partial:   %u", cnt_matched_partial);
    PRINT_BOTH(" unmatched_long:    %u", cnt_unmatched_long);
    PRINT_BOTH(" unmatched_partial: %u", cnt_unmatched_partial);
    PRINT_BOTH(" unmatched_short:   %u", cnt_unmatched_short);
    PRINT_BOTH(" duplicates:        %u", cnt_duplicate);
    PRINT_BOTH(" mismatched:        %u", cnt_mismatched);
        
    for(uint32_t i = 0; i < ref_len; i++) {
        TextReference * ref = &refs[i];
        data_ref_free(ref->data);
    }
    free(refs);
    
    return 0;
}