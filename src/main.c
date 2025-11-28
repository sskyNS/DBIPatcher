/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   main.c
 *
 * Created on 9. září 2025, 12:21
 */

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>
#include <signal.h>
#include <inttypes.h>
#include <sys/param.h>

#include "log.h"
#include "utils.h"
#include "v2/keys.h"
#include "v2/strings.h"
#include "v2/inst.h"
#include "v2/imm.h"
#include "v2/blueprint.h"
#include "v2/merge.h"
#include "v2/patch.h"
#include "v2/utf8.h"

#define APP         "dbipatcher"
#define ARRLEN(arr) (sizeof(arr)/sizeof(*arr))

typedef enum {
    CMD_NONE = 0,
    CMD_FIND_IMM,
    CMD_FIND_STR,
    CMD_FIND_KEYS,
    CMD_NEW_EN,
    CMD_NEW_RU,
    CMD_PARTIALS,
    CMD_DECODE,
    CMD_MERGE,
    CMD_SCAN,
    CMD_PATCH,
} Command;

typedef struct {
    Command command;
    char * command_name;
    char * needle;
    char * nro_path;
    char * dict_path;
    char * output_path;
    char * lang_path;
    char * blueprint_path;
    char * keygen_path;
    
    FILE * output_file;
    MemFile * nro_mf;
    
    int64_t keys;
    int64_t min_length;
    int64_t decode_addr;
    uint8_t help;
} Args;

typedef enum {
    ARG_TYPE_HELP = 'h',
    
    // no shortop
    ARG_TYPE_FIND_IMM = 1000,
    ARG_TYPE_FIND_STR,
    ARG_TYPE_FIND_KEYS,
    ARG_TYPE_KEYGEN,
    ARG_TYPE_NEW_EN,
    ARG_TYPE_NEW_RU,
    ARG_TYPE_PARTIALS,
    ARG_TYPE_DECODE,
    ARG_TYPE_MERGE,
    ARG_TYPE_SCAN,
    ARG_TYPE_PATCH,
    ARG_TYPE_NRO,
    ARG_TYPE_KEYS,
    ARG_TYPE_DICT,
    ARG_TYPE_OUT,
    ARG_TYPE_MIN,
    ARG_TYPE_MAX,
    ARG_TYPE_LANG,
} ArgType;

static Args args;

static struct option long_options[] = {
    {"find-imm", required_argument, 0, ARG_TYPE_FIND_IMM },
    {"find-str", required_argument, 0, ARG_TYPE_FIND_STR },
    {"find-keys", no_argument, 0, ARG_TYPE_FIND_KEYS },
    {"new-en", no_argument, 0, ARG_TYPE_NEW_EN },
    {"new-ru", no_argument, 0, ARG_TYPE_NEW_RU },
    {"partials", no_argument, 0, ARG_TYPE_PARTIALS },
    {"decode", required_argument, 0, ARG_TYPE_DECODE },
    {"merge", required_argument, 0, ARG_TYPE_MERGE },
    {"scan", no_argument, 0, ARG_TYPE_SCAN },
    {"patch", required_argument, 0, ARG_TYPE_PATCH },
    {"nro", required_argument, 0, ARG_TYPE_NRO },
    {"keys", required_argument, 0, ARG_TYPE_KEYS },
    {"dict", required_argument, 0, ARG_TYPE_DICT },
    {"out", required_argument, 0, ARG_TYPE_OUT },
    {"min", required_argument, 0, ARG_TYPE_MIN },
    {"lang", required_argument, 0, ARG_TYPE_LANG },
    {"keygen", required_argument, 0, ARG_TYPE_KEYGEN },
    {"help", no_argument, 0, ARG_TYPE_HELP },
    {0, 0, 0, 0},
};

void print_help(const char *program_name) {
    printf("Usage: %s [OPTIONS]" CRLF, program_name);
    printf(CRLF "Options:" CRLF);
    printf("  --find-imm <needle> --nro <file> --keys <count>" CRLF);
    printf("  --find-str <needle> --nro <file> --keys <count>" CRLF);
    printf("  --find-keys <needle> --nro <file>" CRLF);
    printf("  --new-en --nro <file> --min <len> --keys <count> --dict <file>" CRLF);
    printf("  --new-ru --nro <file> --min <len> --keys <count> --dict <file>" CRLF);
    printf("  --partials --nro <file> --dict <file>" CRLF);
    printf("  --decode <addr> --nro <file> --keys <count>" CRLF);
    printf("  --merge <file> --dict <file>" CRLF);
    printf("  --scan --nro <file> --dict <file>" CRLF);
    printf("  --patch <blueprint> --nro <file> --lang <file> --out <file>"CRLF);
    printf("  --help" CRLF);
    printf(CRLF);
    printf("  --out <file> is supported by all commands to redirect output to file" CRLF);
    printf("  --keygen <file> is supported by all commands to provide alternate key sequence (--find-keys output)" CRLF);
}

static void free_args(Args * args) {
    if(args->command_name) {
        free(args->command_name);
    }
    
    if(args->needle) {
        free(args->needle);
    }
    
    if(args->nro_path) {
        free(args->nro_path);
    }
    
    if(args->keygen_path) {
        free(args->keygen_path);
    }
    
    if(args->dict_path) {
        free(args->dict_path);
    }
    
    if(args->output_path) {
        free(args->output_path);
    }
    
    if(args->lang_path) {
        free(args->lang_path);
    }
    
    if(args->blueprint_path) {
        free(args->blueprint_path);
    }
    
    memset(args, 0, sizeof(*args));
    
    if(args->output_file) {
        fflush(args->output_file);
        if(args->output_file != stdout) {
            fclose(args->output_file);
        }
        args->output_file = NULL;
    }
    
    if(args->nro_mf) {
        mf_free(args->nro_mf);
        args->nro_mf = NULL;
    }
}

int main(int argc, char** argv) {    
    int ret = EXIT_SUCCESS;
        
    memset(&args, 0, sizeof(args));
    args.decode_addr = -1;
    args.keys = -1;
    args.min_length = -1;
    args.output_file = stdout;
    
    const char * program_name = argc ? argv[0] : APP;
    
    log_init(program_name);
    
    char short_options[ARRLEN(long_options) * 2 + 1];
    char * short_ptr = short_options;
    
    for(uint32_t i = 0; i < ARRLEN(long_options); i++) {
        struct option * opt = &long_options[i];
        
        if(opt->val < 128) {
            *(short_ptr++) = (char)opt->val;
            if(opt->has_arg == required_argument) {
                *(short_ptr++) = ':';
            }
        }
    }
    
    int opt_cnt = 0;
    int opt;
    int opt_idx = 0;
    while ((opt = getopt_long(argc, argv, short_options, long_options, &opt_idx)) != -1) {
        ++opt_cnt;
        
        Command cmd_prev = args.command;

        switch (opt) {
            case ARG_TYPE_FIND_IMM:   
                args.command = CMD_FIND_IMM;
                args.needle  = strdup(optarg);               
                break;
                
            case ARG_TYPE_FIND_STR:   
                args.command = CMD_FIND_STR;
                args.needle  = strdup(optarg);               
                break;
                
            case ARG_TYPE_KEYGEN:   
                args.keygen_path  = strdup(optarg);               
                break;
                
            case ARG_TYPE_FIND_KEYS:   
                args.command = CMD_FIND_KEYS;           
                break;
                
            case ARG_TYPE_NEW_EN:   
                args.command = CMD_NEW_EN;             
                break;
                
            case ARG_TYPE_NEW_RU:   
                args.command = CMD_NEW_RU;             
                break;
                
            case ARG_TYPE_PARTIALS:   
                args.command = CMD_PARTIALS;             
                break;
                
            case ARG_TYPE_DECODE:   
                args.command = CMD_DECODE;
                if(strncmp(optarg, "0x", 2) == 0 || strncmp(optarg, "0X", 2) == 0) {
                    args.decode_addr = strtoul(optarg + 2, NULL, 16);
                } else {
                    args.decode_addr = strtoul(optarg, NULL, 10);
                }
                break;
                
            case ARG_TYPE_MERGE:   
                args.command = CMD_MERGE;
                args.lang_path  = strdup(optarg);               
                break;
                
            case ARG_TYPE_SCAN:   
                args.command = CMD_SCAN;              
                break;
                
            case ARG_TYPE_PATCH:   
                args.command = CMD_PATCH;
                args.blueprint_path  = strdup(optarg);               
                break;
                
            case ARG_TYPE_NRO:   
                args.nro_path  = strdup(optarg);               
                break;
                
            case ARG_TYPE_KEYS:   
                if(strncmp(optarg, "0x", 2) == 0 || strncmp(optarg, "0X", 2) == 0) {
                    args.keys = strtoul(optarg + 2, NULL, 16);
                } else {
                    args.keys = strtoul(optarg, NULL, 10);
                }             
                break;
                
            case ARG_TYPE_DICT:   
                args.dict_path  = strdup(optarg);               
                break;
                
            case ARG_TYPE_OUT:   
                args.output_path  = strdup(optarg);               
                break;
                
            case ARG_TYPE_MIN:   
                if(strncmp(optarg, "0x", 2) == 0 || strncmp(optarg, "0X", 2) == 0) {
                    args.min_length = strtoul(optarg + 2, NULL, 16);
                } else {
                    args.min_length = strtoul(optarg, NULL, 10);
                }             
                break;
                
            case ARG_TYPE_LANG:   
                args.lang_path  = strdup(optarg);               
                break;
                
            case ARG_TYPE_HELP:   
                
            case '?':
                // already reports nice error
                goto exit_failure;
                
            default:    args.help       = 1;                            break;
        }
        
        if(cmd_prev != args.command) {
            const char * command_name = "?";
            struct option * iter = long_options;
                while(iter->name) {
                    if(iter->val == opt) {
                        command_name = iter->name;
                        break;
                    }
                    iter++;
                }
            
            if(cmd_prev != CMD_NONE) {
                lf_e("unable to use --%s with --%s", command_name, args.command_name);
                goto exit_failure;
            }
            
            args.command_name = strdup(command_name);
        }
    }
    
    if(opt_cnt == 0) {
        args.help = 1;
    }
    
    if(args.help) {
        print_help(program_name);
        goto exit;
    }
    
    if(args.output_path) {
        FILE * tmp = args.output_file;
        
        if(mkpath(0755, "%s", args.output_path) != 0) {
            lf_e("failed to open \"%s\" for writing", args.output_path);
            goto exit_failure;
        }
        
        // should make windows happy
        args.output_file = fopen(args.output_path, "w+b");
        
        if(!args.output_file) {
            args.output_file = tmp;
            lf_e("failed to open \"%s\" for writing", args.output_path);
            goto exit_failure;
        } else {
            lf_i("using output file \"%s\"", args.output_path);
        }
    } else {
        args.output_file = stdout;
    }
    
    if(args.keygen_path) {
        if(set_keygen(args.keygen_path) != 0) {
            lf_e("failed to load \"%s\"", args.keygen_path);
            goto exit_failure;
        } else {
            lf_i("using keygen file \"%s\"", args.keygen_path);
        }
    }
    
    if(args.nro_path) {
        args.nro_mf = mf_init_path(args.nro_path);
        
        if(args.nro_mf == NULL) {
            lf_e("failed to load \"%s\"", args.nro_path);
            goto exit_failure;
        } else {
            lf_i("using nro file \"%s\"", args.nro_path);
        }
    }
    
    switch(args.command) {
        case CMD_FIND_IMM:
        case CMD_FIND_STR:
            if (!args.needle || !args.nro_mf || args.keys < 0) {
                lf_e("--%s requires --nro, and --keys", args.command_name);
                goto exit_failure;
            } else {
                if(args.command == CMD_FIND_IMM) { 
                    uint32_t needle_len = strlen(args.needle);
                    if(needle_len < 8) {
                        lf_i("searching for immediate \"%s\" using %u keys", args.needle, (uint32_t)args.keys);
                        uint32_t cnt_total = 0;
                        
                        MemFile * mf = args.nro_mf;
                        
                        const char * encode = string_encode(args.needle, needle_len);
                        
                        fprintf(args.output_file, "// %-47s [%-3u]: '%s'\n", "lookup immediate", needle_len + 1, args.needle);
                                                
                        // dont forgot to handle 0 as well as special case
                        for(uint32_t i = args.keys ? 1 : 0; i <= args.keys; i++) {
                            uint32_t cnt_matches = 0;
                            
                            ImmDefine d = {
                                .key = get_key(i),
                                .data = args.needle,
                                .len = needle_len,
                                .offset = 0,
                            };

                            ImmResult * iter_start = imm_lookup(mf->data, mf->len, &d, 10);
                            ImmResult * iter = iter_start;

                            if(iter->matches_cnt != 0) {
                                while(iter) {
                                    ImmMatch * m = iter->matches;
                                    while(m) {
                                        char buf[256];
                                        snprintf(buf, sizeof(buf), "   at 0x%08X key 0x%016" PRIx64 " / %-3u", m->offsets[0], d.key, i);
                                        fprintf(args.output_file, "%-50s [%-3u]: '%s'\n", buf, needle_len + 1, encode);
                                        
                                        cnt_matches++;
                                        m = m->next;
                                    }
                                    iter = iter->next;
                                }
                            }
                            
                            imm_result_free(iter_start);
                            
                            if(cnt_matches) {
                                cnt_total += cnt_matches;
                                lf_i("found %u matches using key %u", cnt_matches, i);
                            }
                        }
                                                
                        if(cnt_total) {
                            lf_i("found total of %u matches", cnt_total);
                        } else {
                            lf_e("no matches found");
                            goto exit_failure;
                        }
                    } else {
                        lf_e("--%s lookup too long", args.command_name);
                        goto exit_failure;
                    }
                } else {
                    ScanStringArgs scan_args = {
                        .dbi_mf = args.nro_mf,
                        .lookup = args.needle,
                        .out = args.output_file,
                        .key_cnt = args.keys,
                    };
                    
                    lf_i("searching for string \"%s\" using %u keys", args.needle, (uint32_t)args.keys);
                    ret = scan_string(&scan_args);
                }
            }
            break;
            
        case CMD_FIND_KEYS:
            if (!args.nro_path) {
                lf_e("--%s requires --nro", args.command_name);
                goto exit_failure;
            } else {
                lf_i("searching for keys");
                KeySet * ks = get_key_set(args.nro_mf);
                
                if(ks->key_cnt > 1) {
                    lf_i("found total of %u matches", ks->key_cnt);
                    
                    for(uint32_t i = 0; i < ks->key_cnt; i++) {
                        fprintf(args.output_file, "0x%016" PRIX64 CRLF, ks->keys[i]);
                    }
                } else {
                    lf_e("no matches found");
                    ret = EXIT_FAILURE;
                }
                free_key_set(ks);
            }
            break;
            
        case CMD_NEW_EN:
        case CMD_NEW_RU:
            if (!args.nro_path || args.min_length < 0 || args.keys < 0 || !args.dict_path) {
                lf_e("--%s requires --nro, --min, --keys, and --dict", args.command_name);
                goto exit_failure;
            } else {
                ScanType scan_type = SCAN_ENGLISH;
                const char * scan_lang = "en";
                
                if(args.command == CMD_NEW_RU) {
                    scan_type = SCAN_RUSSIAN;
                    scan_lang = "ru";
                }
                
                ScanTypeArgs scan_args = {
                    .type = scan_type,
                    .dbi_mf = args.nro_mf,
                    .keys = args.dict_path,
                    .out = args.output_file,
                    .key_cnt = args.keys,
                    .min_match = args.min_length,
                };
                
                lf_i("searching for new %s strings >= %u characters using %u keys", scan_lang, (uint32_t)args.min_length, (uint32_t)args.keys);
                ret = scan_strings_type(&scan_args);
            }
            break;
        
        case CMD_PARTIALS:
            if (!args.nro_path || !args.dict_path) {
                lf_e("--%s  requires --nro and --dict", args.command_name);
                goto exit_failure;
            } else {
                ScanPartialsArgs scan_args = {
                    .dbi_mf = args.nro_mf,
                    .keys = args.dict_path,
                    .out = args.output_file,
                };

                lf_i("searching for partial string candidates");
                ret = scan_partials(&scan_args);
            }
            break;
            
        case CMD_DECODE:
            if (args.decode_addr < 0 || !args.nro_path || args.keys < 0) {
                lf_e("--%s requires valid address, --nro, and --keys", args.command_name);
                goto exit_failure;
            } else {
                char tmp[2048];
                MemFile * mf = args.nro_mf;
                
                lf_i("decoding string at 0x%08X using %u keys", (uint32_t)args.decode_addr, (uint32_t)args.keys);
                fprintf(args.output_file, "// decoding string at 0x%08X" CRLF, (uint32_t)args.decode_addr);
                
                if(mf->len <= args.decode_addr) {
                    lf_e("provided address out of range");
                    goto exit_failure;
                }
                
                uint32_t cnt_total = 0;
                uint32_t len = mf->len - args.decode_addr;
                len = MIN(len, sizeof(tmp));
                
                KeySet * ks = gen_key_set(args.keys);
                
                for(uint32_t i = args.keys > 0 ? 1 : 0; i < ks->key_cnt; i++) {
                    uint64_t key = ks->keys[i];
                    
                    memcpy(tmp, mf->data + args.decode_addr, len);
                    
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
                            break;
                        }
                    }
                    
                    if(offset < len && offset != 0 && tmp[offset] == 0) {
                        const char * encode = string_encode(tmp, offset);
                        
                        if(tmp[offset] == 0) {
                            offset++;
                        }
                        
                        char buf[256];
                        snprintf(buf, sizeof(buf), "   at 0x%08X key 0x%016" PRIx64 " / %-3u", (uint32_t)args.decode_addr, key, i);
                        fprintf(args.output_file, "%-50s [%-3u]: '%s'\n", buf, offset, encode);

                        cnt_total++;
                    }
                }
                
                if(cnt_total) {
                    lf_i("found total of %u matches", cnt_total);
                } else {
                    lf_e("no matches found");
                    goto exit_failure;
                }
            }
            break;
            
        case CMD_MERGE:
            if (!args.lang_path || !args.dict_path) {
                lf_e("--%s requires input file and --dict", args.command_name);
                goto exit_failure;
            } else {
                MergeArgs merge_args = {
                    .keys = args.dict_path,
                    .translation = args.lang_path,
                    .out = args.output_file,
                };

                lf_i("merging translation file with dictionary");
                ret = merge(&merge_args);
            }
            break;
            
        case CMD_SCAN:
            if (!args.nro_path || !args.dict_path) {
                lf_e("--%s requires --nro and --dict", args.command_name);
                return 0;
            } else {
                ScanBlueprintArgs scan_args = {
                    .dbi_mf = args.nro_mf,
                    .keys = args.dict_path,
                    .out = args.output_file,
                };

                lf_i("creating blueprint");
                ret = scan_blueprint(&scan_args);
            }
            break;
            
        case CMD_PATCH:
            if (!args.blueprint_path || !args.nro_path || !args.lang_path || !args.output_path) {
                lf_e("--%s requires blueprint, --nro, --lang, and --out", args.command_name);
                return 0;
            } else {
               PatchArgs patch_args = {
                    .dbi_mf = args.nro_mf,
                    .translation = args.lang_path,
                    .blueprint = args.blueprint_path,
                    .out = args.output_file,
                };

                lf_i("patching nro");
                ret = patch(&patch_args);
            }
            break;

        default:
            lf_e("command not specified");
            goto exit_failure;
    }
   
    fflush(stdout);
    sleep(1);
    
exit:
    free_args(&args);
    return ret;
    
exit_failure:
    free_args(&args);
    return (EXIT_FAILURE);
}

