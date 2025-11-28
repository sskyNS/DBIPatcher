#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/param.h>

#include "v2/merge.h"
#include "v2/strings.h"
#include "log.h"

#define MAX_LINE 1024
#define MAX_VALUE 768
#define MAX_PLACEHOLDERS 20

typedef enum {
    ISSUE_TYPE_NONE = 0,
    ISSUE_TYPE_MISMATCHED_PLACEHOLDERS,
    ISSUE_TYPE_MISSING_TRANSLATION,
    ISSUE_TYPE_TOO_LONG,
    ISSUE_TYPE_PARTIAL_USED,
} IssueType;

typedef struct {
    char *key;
    uint32_t id;
    char *value;
} KeyRecord;

typedef struct {
    char *key;
    char *value;
} TranslationRecord;

typedef struct {
    char *key;
    uint32_t id;
    char *original;
    char *translation;
    IssueType issue_type;
} IssueRecord;

// Extract placeholders from a string

int placeholder_extract(const char *str, Placeholder * placeholders, uint32_t placeholders_cnt) {
    int count = 0;
    const char *p = str;
    while(*p && count < placeholders_cnt) {
        if(*p == '{') {
            const char *start = p;
            p++;
            while(*p && *p != '}') {
                p++;
            }
            if(*p == '}') {
                int len = p - start + 1;
                snprintf(placeholders[count].value, MAX_PLACEHOLDER_LEN, "%.*s", len, start);
                count++;

                p++;
            }
        } else {
            p++;
        }
    }
    return count;
}

static int count_cr(const char *str) {
    int count = 0;
    for (const char *p = str; *p; p++) {
        if (*p == '\r') {
            count++;
        }
    }
    return count;
}

// Compare two sets of placeholders

uint8_t placeholder_compare(const char *str1, const char *str2) {
    if (count_cr(str1) != count_cr(str2)) {
        return 0;
    }
    
    Placeholder ph1[MAX_PLACEHOLDERS];
    Placeholder ph2[MAX_PLACEHOLDERS];

    int count1 = placeholder_extract(str1, ph1, MAX_PLACEHOLDERS);
    int count2 = placeholder_extract(str2, ph2, MAX_PLACEHOLDERS);

    if(count1 != count2) {
        return 0;
    }

    for(int i = 0; i < count1; i++) {
        if(strcmp(ph1[i].value, ph2[i].value) != 0) {
            return 0;
        }
    }

    return 1;
}

// Parse key.txt line: KEY;ID;VALUE

static uint8_t parse_key_line(const char *line, KeyRecord *record) {
    char temp[MAX_LINE];
    strncpy(temp, line, MAX_LINE - 1);
    temp[MAX_LINE - 1] = '\0';

    temp[strcspn(temp, "\r\n")] = '\0';
    char *token = strtok(temp, ";");
    if(!token) {
        return 0;
    }
    record->key = strdup(token);

    token = strtok(NULL, ";");
    if(!token) {
        return 0;
    }
    record->id = strtoul(token, NULL, 10);

    token = strtok(NULL, "");
    if(!token) {
        return 0;
    }
    
    const char * encoded = string_encode(token, -1);
    record->value = strdup(encoded);

    return 1;
}

// Parse translation.txt line: KEY=VALUE

static uint8_t parse_translation_line(const char *line, TranslationRecord *record) {
    char temp[MAX_LINE];
    char *token;
    
    strncpy(temp, line, MAX_LINE - 1);
    temp[MAX_LINE - 1] = '\0';
    temp[strcspn(temp, "\r\n")] = '\0';
    
    record->key = NULL;
    record->value = NULL;
    
    // Check for translation.txt format: KEY=VALUE
    char *eq = strchr(temp, '=');
    if (eq) {
        char * iter = temp;
        while(iter != eq) {
            switch(*iter) {
                case 'a'...'z':
                case 'A'...'Z':
                case '0'...'9':
                case '_':
                    iter++;
                    break;
                default:
                    goto not_found;
            }
        }
        
        *eq = '\0';
        record->key = strdup(temp);
        const char *encoded = string_encode(eq + 1, -1);
        record->value = strdup(encoded);
        return 1;
    }
    
not_found:
    
    // Check for key.txt format: KEY;ID;VALUE
    token = strtok(temp, ";");
    if (!token) return 0;
    record->key = strdup(token);
    
    // Second token: id
    token = strtok(NULL, ";");
    if (!token) {
        free(record->key);
        return 0;
    }
    
    // Third token: value (rest of string)
    token = strtok(NULL, "");
    if (!token) {
        free(record->key);
        return 0;
    }
    
    const char *encoded = string_encode(token, -1);
    record->value = strdup(encoded);
    
    return 1;
}

static void free_key_record(KeyRecord* r) {
    free(r->key);
    free(r->value);
}

static void free_translation_record(TranslationRecord* r) {
    free(r->key);
    free(r->value);
}

static void free_issue_record(IssueRecord* r) {
    free(r->key);
    free(r->original);
    free(r->translation);
}

int merge(const MergeArgs * args) {
    FILE *key_file = fopen(args->keys, "r");
    FILE *trans_file = fopen(args->translation, "r");
    FILE *out;
        
    if(!key_file) {
        lf_e("failed to load \"%s\"", args->keys);
        return EXIT_FAILURE;
    } else {
        lf_i("using dictionary file \"%s\"", args->keys);
    }
    
    if(!key_file) {
        fclose(key_file);
        lf_e("failed to load \"%s\"", args->translation);
        return EXIT_FAILURE;
    } else {
        lf_i("using translation file \"%s\"", args->translation);
    }
            
    out = stdout;
    if(args->out != NULL) {
        out = args->out;
    }

    size_t key_slots = 32, key_count = 0;
    KeyRecord *keys = malloc(key_slots * sizeof (KeyRecord));
    char line[MAX_LINE];

    // Read all key records
    while(fgets(line, MAX_LINE, key_file)) {
        if(key_count >= key_slots) {
            key_slots *= 2;
            keys = realloc(keys, key_slots * sizeof (KeyRecord));
        }
        if(parse_key_line(line, &keys[key_count])) {
            key_count++;
        }
    }
    fclose(key_file);

    // Read all translation records
    size_t trans_slots = 32, trans_count = 0;
    TranslationRecord *translations = malloc(trans_slots * sizeof (TranslationRecord));

    while(fgets(line, MAX_LINE, trans_file)) {
        TranslationRecord temp;
        if(parse_translation_line(line, &temp)) {
            // Check if key already exists
            int existing = -1;
            for(size_t i = 0; i < trans_count; i++) {
                if(strcmp(translations[i].key, temp.key) == 0) {
                    existing = i;
                    break;
                }
            }
            if(existing >= 0) {
                free(translations[existing].value);
                translations[existing].value = temp.value;
                free(temp.key);
            } else {
                if(trans_count >= trans_slots) {
                    trans_slots *= 2;
                    translations = realloc(translations, trans_slots * sizeof (TranslationRecord));
                }
                translations[trans_count].key = temp.key;
                translations[trans_count].value = temp.value;
                trans_count++;
            }
        }
    }
    fclose(trans_file);

    // Prepare buffer for issues
    size_t issue_slots = 16, issue_count = 0;
    uint32_t warning_count = 0;
    IssueRecord *issues = malloc(issue_slots * sizeof (IssueRecord));

    // Process each key record and match translations
    for(size_t i = 0; i < key_count; i++) {
        char *final_value = NULL;
        uint8_t found = 0;
        IssueType issue_type = ISSUE_TYPE_NONE;

        // Find translation
        for(size_t j = 0; j < trans_count; j++) {
            if(strcmp(keys[i].key, translations[j].key) == 0) {
                found = 1;
                
                uint32_t len = strlen(keys[i].value);
                if(len >= 8 && len < 16) {
                    if(strlen(translations[j].value) > 7 && strcmp(keys[i].value, translations[j].value) != 0) {
                        // this is just a warning that gets overwritten by any
                        // real issue
                        issue_type = ISSUE_TYPE_PARTIAL_USED;
                    }
                }
                
                if(strlen(keys[i].value) < strlen(translations[j].value)) {
                    final_value = strdup(translations[j].value);
                    issue_type = ISSUE_TYPE_TOO_LONG;
                    break;
                }
    
                // Check placeholders
                if(placeholder_compare(keys[i].value, translations[j].value)) {
                    final_value = strdup(translations[j].value);
                } else {
                    final_value = strdup(translations[j].value);
                    issue_type = ISSUE_TYPE_MISMATCHED_PLACEHOLDERS;
                }
                break;
            }
        }
        
        if(!found) {
            final_value = strdup(keys[i].value);
            issue_type = ISSUE_TYPE_MISSING_TRANSLATION;
        }
        
        fprintf(out, "%s=%s" CRLF, keys[i].key, final_value);

        // Save issue
        if(issue_type != ISSUE_TYPE_NONE) {
            if(issue_count >= issue_slots) {
                issue_slots *= 2;
                issues = realloc(issues, issue_slots * sizeof (IssueRecord));
            }
            issues[issue_count].key = strdup(keys[i].key);
            issues[issue_count].id = keys[i].id;
            issues[issue_count].original = strdup(keys[i].value);
            issues[issue_count].translation = strdup(final_value);
            issues[issue_count].issue_type = issue_type;
            issue_count++;
        }

        free(final_value);
    }

    // Write issues if any
    if(issue_count > 0) {
        fprintf(out, CRLF);
        
        //---------------------------------------------------------------------- 
        fprintf(out, "//------------------------" CRLF);
        fprintf(out, "// [MISSING_TRANSLATION]" CRLF);
        fprintf(out, "//------------------------" CRLF);
        fprintf(out, "//" CRLF);
        
        uint32_t cur_count = 0;
        for(size_t i = 0; i < issue_count; i++) {

            if(issues[i].issue_type != ISSUE_TYPE_MISSING_TRANSLATION) {
                continue;
            }
            
            fprintf(out, "// %s;%s" CRLF, issues[i].key, issues[i].original);
            cur_count++;
        }
        
        if(cur_count) {
            lf_i("found %u MISSING_TRANSLATION issues", cur_count);
        }
        
        //----------------------------------------------------------------------
        fprintf(out, "//" CRLF);
        fprintf(out, "//------------------------" CRLF);
        fprintf(out, "// [MISMATCHED_PLACEHOLDERS]" CRLF);
        fprintf(out, "//------------------------" CRLF);
        fprintf(out, "//" CRLF);
        
        cur_count = 0;
        for(size_t i = 0; i < issue_count; i++) {

            if(issues[i].issue_type != ISSUE_TYPE_MISMATCHED_PLACEHOLDERS) {
                continue;
            }
            
            fprintf(out, "// %-20s;%s" CRLF, issues[i].key, issues[i].original);
            fprintf(out, "// %-20s;%s" CRLF, "", issues[i].translation);
            fprintf(out, "//" CRLF);
            cur_count++;
        }
        
        if(cur_count) {
            lf_i("found %u MISMATCHED_PLACEHOLDERS issues", cur_count);
        }
        
        //---------------------------------------------------------------------- 
        fprintf(out, "//" CRLF);
        fprintf(out, "//------------------------" CRLF);
        fprintf(out, "// [TOO_LONG]" CRLF);
        fprintf(out, "//------------------------" CRLF);
        fprintf(out, "//" CRLF);
        
        cur_count = 0;
        for(size_t i = 0; i < issue_count; i++) {

            if(issues[i].issue_type != ISSUE_TYPE_TOO_LONG) {
                continue;
            }
            
            fprintf(out, "// %-20s;%s" CRLF, issues[i].key, issues[i].original);
            fprintf(out, "// %-20s;%s" CRLF, "", issues[i].translation);
            fprintf(out, "//" CRLF);
            cur_count++;
        }
        
        if(cur_count) {
            lf_i("found %u TOO_LONG issues", cur_count);
        }
        
        //---------------------------------------------------------------------- 
        fprintf(out, "//" CRLF);
        fprintf(out, "//------------------------" CRLF);
        fprintf(out, "// [PARTIAL_USED]" CRLF);
        fprintf(out, "//------------------------" CRLF);
        fprintf(out, "//" CRLF);
        
        cur_count = 0;
        for(size_t i = 0; i < issue_count; i++) {

            if(issues[i].issue_type != ISSUE_TYPE_PARTIAL_USED) {
                continue;
            }
            
            fprintf(out, "// %-17s%-3u;%s" CRLF, issues[i].key, (uint32_t)strlen(issues[i].original), issues[i].original);
            fprintf(out, "// %-17s%-3u;%s" CRLF, "", (uint32_t)strlen(issues[i].translation), issues[i].translation);
            fprintf(out, "//" CRLF);
            cur_count++;
        }
        
        fprintf(out, "//------------------------" CRLF);
        
        if(cur_count) {
            warning_count += cur_count;
            issue_count -= cur_count;
            
            lf_i("found %u PARTIAL_USED warnings", cur_count);
        }
    }

    // Free all memory
    for(size_t i = 0; i < key_count; i++) {
        free_key_record(&keys[i]);
    }
    
    for(size_t i = 0; i < trans_count; i++) {
        free_translation_record(&translations[i]);
    }
    
    for(size_t i = 0; i < issue_count; i++) {
        free_issue_record(&issues[i]);
    }
    
    free(keys);
    free(translations);
    free(issues);

    lf_i("processed %u language records for %u keys", (uint32_t)trans_count, (uint32_t) key_count);
    
    int ret = EXIT_SUCCESS;
    
    if(warning_count) {
        lf_e("found total of %u warnings", warning_count);
    }
    
    if(issue_count) {
        lf_e("found total of %u issues", issue_count);
        ret = EXIT_FAILURE;
    }
    
    if(issue_count || warning_count) {
        lf_i("see output file for details");
    }

    return ret;
}
