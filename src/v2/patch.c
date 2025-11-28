#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "v2/patch.h"
#include "v2/strings.h"
#include "memfile.h"
#include "v2/blueprint.h"
#include "log.h"
#include "v2/inst.h"

#define MAX_LINE     2048

typedef struct _TranslationRecord TranslationRecord;

typedef struct _TranslationRecord {
    char * key;
    char * value;
    char * value_raw;
    TranslationRecord * next;
} TranslationRecord;

static TranslationRecord * parse_translation_line(const char *line) {
    const char * eq = strchr(line, '=');
    if(!eq) {
        return NULL;
    }
    
    const char * key = line;
    uint32_t key_len = eq - line;
    
    const char * text = eq + 1;
    uint32_t text_len = strlen(text);
        
    if(text_len && text[text_len - 1] == '\n') {
        --text_len;
    }
    
    char * key_bytes = malloc(key_len + 1);
    memcpy(key_bytes, key, key_len);
    key_bytes[key_len] = 0;
    
    char * text_bytes = malloc(text_len + 1);
    memcpy(text_bytes, text, text_len);
    text_bytes[text_len] = 0;
    
    TranslationRecord * rec = malloc(sizeof(*rec));
    memset(rec, 0, sizeof(*rec));
    
    rec->key = key_bytes;
    rec->value = text_bytes;
    rec->value_raw = strdup(rec->value);
    string_decode(rec->value_raw, strlen(rec->value_raw));
    
    return rec;
}

static const char* data_to_hex(const void *data, uint32_t len) {
    static char tmp[1024];
    char * ptr = tmp;
    
    for(uint32_t i = 0; i < len; i++) {
        ptr += sprintf(ptr, "%02X ", ((uint8_t*)data)[i]);
    }
    
    return tmp;
}

int patch(const PatchArgs * args) {
    
    TranslationRecord * trans = NULL;
    
    const MemFile * dbi = args->dbi_mf;
    BlueprintRecord * bp = blueprint_load(args->blueprint);
    FILE * trans_file = fopen(args->translation, "r");
    
    if(!trans_file || !bp || !dbi) {
        if(bp) {
            blueprint_free(bp);
        }
        
        if(trans_file) {
            fclose(trans_file);
        }
        return EXIT_FAILURE;
    }
    
    //blueprint_print(bp);
    
    char line[MAX_LINE];
    while(fgets(line, MAX_LINE, trans_file)) {
        TranslationRecord * rec;
        if((rec = parse_translation_line(line)) != NULL) {
            rec->next = trans;
            trans = rec;
        }
    }
    
    uint32_t issue_count = 0;
    
    BlueprintRecord * bp_iter = bp;
    while(bp_iter) {
        BlueprintRecord * bp_cur = bp_iter;
        bp_iter = bp_iter->next;
        
        if(!bp_cur->is_consistent) {
            lf_e("not consistent %s;%s", bp_cur->id, bp_cur->plain_string);
            continue;
        }
        
        //lf_d("patching [%-4u] %s:%s", bp_cur->raw_len, bp_cur->id, bp_cur->plain_string);
        
        // 1. start with original unxored string
        
        
        if(bp_cur->key != 0) {
            memcpy(line, bp_cur->plain_string_raw, bp_cur->raw_len);
        } else {
            // those seem to parsed char by char, need to somehow hide unused chars
            memset(line, '\x1a', bp_cur->raw_len);
            line[bp_cur->raw_len - 1] = 0;
        }

        TranslationRecord * trans_iter = trans;
        while(trans_iter) {
            if(strcmp(trans_iter->key, bp_cur->id) == 0) {
                break;
            }
            trans_iter = trans_iter->next;
        }

        if(!trans_iter) {
            lf_e("missing translation %s;%s", bp_cur->id, bp_cur->plain_string);
            issue_count++;
            continue;
        }
        
        // 2. overwrite with translation
        int len = snprintf(line, sizeof(line), "%s", trans_iter->value_raw) + 1;

        // 3. xor whole payload
        for(uint32_t i = 0; i < bp_cur->raw_len; i++) {
            uint8_t xor = (bp_cur->key >> ((i & 7) * 8)) & 0xFF;
            line[i] ^= xor;
        }

        if(len > bp_cur->raw_len) {
            lf_e("translation too long [%-4u] %s;%s", bp_cur->raw_len, bp_cur->id, bp_cur->plain_string);
            lf_e("                     [%-4u] %s;%s", len, bp_cur->id, trans_iter->value);
            
            issue_count++;
            continue;
        }
        
        /*if(memcmp(line, bp_cur->encoded_string_raw, bp_cur->raw_len) == 0) {
            lf_d("equals [%-4u] %s:%s", bp_cur->raw_len, bp_cur->id, bp_cur->plain_string);
            continue;
        }*/
        
        PatchLocation * patch_iter = bp_cur->patches;
        while(patch_iter) {
            PatchLocation * patch_cur = patch_iter;
            patch_iter = patch_iter->next;
            
            if(patch_cur->address + patch_cur->len > dbi->len) {
                lf_e("out of range %s;%s", bp_cur->id, bp_cur->plain_string);
                
                issue_count++;
                continue;
            }
            
            if(memcmp(bp_cur->encoded_string_raw + patch_cur->offset, line + patch_cur->offset, patch_cur->len) != 0) {
                if(patch_cur->address == UINT32_MAX) {
                    lf_e("unpatchable difference %s  %s", data_to_hex(bp_cur->encoded_string_raw, bp_cur->raw_len), bp_cur->plain_string);
                    lf_e("                       %s  %s", data_to_hex(line,  bp_cur->raw_len), trans_iter->value);
                    
                    issue_count++;
                    continue;
                }
                
                // this can be patched, lets roll
            } else {
                continue;
            }
                
            
            switch(patch_cur->type) {
                case PATCH_TYPE_DAT:
                    if(memcmp(dbi->data + patch_cur->address, bp_cur->encoded_string_raw + patch_cur->offset, patch_cur->len) != 0) {
                        lf_e("dat patch mismatch %s", data_to_hex(dbi->data + patch_cur->address, patch_cur->len));
                        lf_e("                   %s", data_to_hex(bp_cur->encoded_string_raw + patch_cur->offset, patch_cur->len));
                        
                        issue_count++;
                        continue;
                    }
                    
                    memcpy(dbi->data + patch_cur->address, line + patch_cur->offset, patch_cur->len);
                    break;
                case PATCH_TYPE_MOV: {
                    uint32_t * instr = (uint32_t*)(dbi->data + patch_cur->address);
                    uint16_t imm_new = 0;
                    memcpy(&imm_new, line + patch_cur->offset, patch_cur->len);
                    
                    int ret_patch = inst_patch_mov(instr, patch_cur->imm, imm_new, patch_cur->len);
                    if(ret_patch != ERR_MOV_OK) {
                        arm64_instr_t decoded;
                        instr_decode(*instr, &decoded, 0);
      
                        
                        lf_e("imm patch error at 0x%08X [%d, expected=0x%04X] %s;%s", patch_cur->address, ret_patch, (uint16_t)patch_cur->imm, bp_cur->id, bp_cur->plain_string);
                        lf_e("     %s", instr_to_string(&decoded, *instr, patch_cur->address));
                        
                        issue_count++;
                        continue;
                    }
                }   break;
                default:
                    lf_e("unknown patch type %s;%s", bp_cur->id, bp_cur->plain_string);
                    issue_count++;
                    continue;
            }
        }
    }
    
    fwrite(dbi->data, 1, dbi->len, args->out);
         
    if(bp) {
        blueprint_free(bp);
    }

    if(trans_file) {
        fclose(trans_file);
    }
    
    if(issue_count) {
        lf_e("found total of %u issues", issue_count);
        return EXIT_FAILURE;
    } else {
        lf_i("done");
    }
    
    return EXIT_SUCCESS;
}