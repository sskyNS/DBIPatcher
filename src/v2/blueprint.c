#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>

#include "v2/blueprint.h"
#include "v2/strings.h"

#define MAX_LINE_LENGTH 2048
#define UNPATCHABLE_ADDR 0xFFFFFFFF

static uint64_t parse_hex64(const char *str) {
    uint64_t value = 0;
    sscanf(str, "0x%llx", (unsigned long long *)&value);
    return value;
}

static uint32_t parse_hex32(const char *str) {
    uint32_t value = 0;
    sscanf(str, "0x%x", &value);
    return value;
}

uint32_t parse_decimal(const char *str) {
    return strtoul(str, NULL, 10);
}

static void add_patch_location(BlueprintRecord *record, PatchType type, uint32_t addr, 
                       uint32_t offset, uint32_t len, uint64_t imm) {
    PatchLocation *new_patch = (PatchLocation *)malloc(sizeof(PatchLocation));
    new_patch->type = type;
    new_patch->address = addr;
    new_patch->offset = offset;
    new_patch->len = len;
    new_patch->imm = imm;
    new_patch->next = NULL;
    
    if (record->patches == NULL) {
        record->patches = new_patch;
    } else {
        PatchLocation *current = record->patches;
        while (current->next != NULL) {
            current = current->next;
        }
        current->next = new_patch;
    }
}

static uint32_t calculate_max_string_length(PatchLocation *patches) {
    uint32_t max_len = 0;
    PatchLocation *current = patches;
    
    while (current != NULL) {
        uint32_t end_pos = current->offset + current->len;
        if (end_pos > max_len) {
            max_len = end_pos;
        }
        current = current->next;
    }
    
    return max_len;
}

static void reconstruct_expected_string(BlueprintRecord *record) {
    if (record->raw_len == 0) {
        record->encoded_string_raw = NULL;
        record->is_consistent = 1; 
        return;
    }
    
    record->encoded_string_raw = (char *)calloc(record->raw_len + 1, 1);
    record->plain_string_raw = (char *)calloc(record->raw_len + 1, 1);
    
    uint32_t bitmap_size = ((record->raw_len + 7) / 8);
    uint8_t *coverage_map = (uint8_t *)calloc(bitmap_size, 1);
    
    PatchLocation *current = record->patches;
    while (current != NULL) {
        uint64_t imm = current->imm;
        for (uint32_t i = 0; i < current->len; i++) {
            uint32_t byte_pos = current->offset + i;
            record->encoded_string_raw[byte_pos] = (char)((imm >> (i * 8)) & 0xFF);

            coverage_map[byte_pos / 8] |= (1 << (byte_pos % 8));
        }
        current = current->next;
    }
    
    for(uint32_t x = 0; x < record->raw_len; x++) {
        uint8_t xor = (record->key >> ((x & 7) * 8)) & 0xFF;
        record->plain_string_raw[x] = record->encoded_string_raw[x] ^ xor;
    }
    
    const char * encoded = string_encode(record->plain_string_raw, record->raw_len);
    uint32_t encoded_len = strlen(encoded) + 1;
    
    record->plain_string = (char *)calloc(encoded_len, 1);
    memcpy(record->plain_string, encoded, encoded_len);
    
    record->is_consistent = 1;
    for (uint32_t i = 0; i < record->raw_len; i++) {
        if (!(coverage_map[i / 8] & (1 << (i % 8)))) {
            record->is_consistent = 0;
        }
    }
    
    free(coverage_map);
}

static int parse_patch_line(const char *line, BlueprintRecord *current_record) {
    char type_str[4];
    char addr_str[32], offset_str[32], len_str[32], imm_str[32];
    
    if (line[0] != '\t') {
        return 0;
    }
    
    if (sscanf(line + 1, "%3[^=]=%[^;];offset=%[^;];len=%[^;];imm=%s",
               type_str, addr_str, offset_str, len_str, imm_str) != 5) {
        return 0;
    }
    
    PatchType type;
    if (strcmp(type_str, "mov") == 0) {
        type = PATCH_TYPE_MOV;
    } else if (strcmp(type_str, "dat") == 0) {
        type = PATCH_TYPE_DAT;
    } else {
        return 0;
    }
    
    uint32_t addr = parse_hex32(addr_str);
    uint32_t offset = parse_decimal(offset_str);
    uint32_t len = parse_decimal(len_str);        
    uint64_t imm = parse_hex64(imm_str);
    
    add_patch_location(current_record, type, addr, offset, len, imm);
    
    return 1;
}

static BlueprintRecord* parse_key_line(const char *line) {
    char key_str[64], id_str[64];
    
    // parse: key=0x...;id=...
    if (sscanf(line, "key=%[^;];id=%s", key_str, id_str) != 2) {
        return NULL;
    }
    
    BlueprintRecord *record = (BlueprintRecord *)malloc(sizeof(BlueprintRecord));
    memset(record, 0, sizeof(*record));
    
    record->key = parse_hex64(key_str);
    strncpy(record->id, id_str, sizeof(record->id) - 1);
    record->id[sizeof(record->id) - 1] = '\0';

    
    return record;
}

BlueprintRecord* blueprint_load(const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        fprintf(stderr, "Error: Cannot open file '%s'\n", filename);
        return NULL;
    }
    
    BlueprintRecord *head = NULL;
    BlueprintRecord *tail = NULL;
    BlueprintRecord *current_record = NULL;
    char line[MAX_LINE_LENGTH];
    
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\r\n")] = '\0';
        
        // skip empty lines and comments
        if (line[0] == '\0') {
            continue;
        }
        
        char * cmnt = strstr(line, "//");
        if(cmnt) {
            char * iter = line;
            while(iter != cmnt) {
                if(!isspace(*iter)) {
                    break;
                }
                iter++;
            }
            
            if(iter == cmnt) {
                continue;
            }
        }
 
        if (strncmp(line, "key=", 4) == 0) {
            if (current_record != NULL) {
                current_record->raw_len = calculate_max_string_length(current_record->patches);
                reconstruct_expected_string(current_record);
            }
            
            current_record = parse_key_line(line);
            if (current_record == NULL) {
                continue;
            }

            if (head == NULL) {
                head = current_record;
                tail = current_record;
            } else {
                tail->next = current_record;
                tail = current_record;
            }
        }

        else if (line[0] == '\t' && current_record != NULL) {
            parse_patch_line(line, current_record);
        }
    }
    
    if (current_record != NULL) {
        current_record->raw_len = calculate_max_string_length(current_record->patches);
        reconstruct_expected_string(current_record);
    }
    
    fclose(fp);
    return head;
}

void blueprint_free(BlueprintRecord *head) {
    while (head != NULL) {
        BlueprintRecord *next_record = head->next;
        
        PatchLocation *patch = head->patches;
        while (patch != NULL) {
            PatchLocation *next_patch = patch->next;
            free(patch);
            patch = next_patch;
        }
        
        if (head->encoded_string_raw) {
            free(head->encoded_string_raw);
        }
        
        if(head->plain_string) {
            free(head->plain_string);
        }
        
        if(head->plain_string_raw) {
            free(head->plain_string_raw);
        }
        
        free(head);
        head = next_record;
    }
}

void blueprint_print(BlueprintRecord *head) {
    BlueprintRecord *current = head;
    int record_num = 1;
    
    while (current != NULL) {
        printf("\n=== Record %d ===\n", record_num++);
        printf("Key: 0x%016llx\n", (unsigned long long)current->key);
        printf("ID: %s\n", current->id);
        printf("Consistent: %c\n", current->is_consistent ? 'Y' : 'N');
        printf("Max String Length: %u\n", current->raw_len);
        
        printf("Expected String (hex): ");
        for (uint32_t i = 0; i < current->raw_len; i++) {
            printf("%02X ", (unsigned char)current->encoded_string_raw[i]);
        }
        printf("\n");
        printf("Expected String (dec): %s\n", current->plain_string);
        
        printf("Patches:\n");
        PatchLocation *patch = current->patches;
        while (patch != NULL) {
            printf("  %s: addr=0x%08X offset=%u len=%u imm=0x%llX",
                   patch->type == PATCH_TYPE_MOV ? "MOV" : "DAT",
                   patch->address,
                   patch->offset,
                   patch->len,
                   (unsigned long long)patch->imm);
            
            if (patch->address == UNPATCHABLE_ADDR) {
                printf(" [UNPATCHABLE]");
            }
            printf("\n");
            
            patch = patch->next;
        }
        
        current = current->next;
    }
}
