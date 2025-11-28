/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

#include <stddef.h>
#include <sys/param.h>
#include <string.h>
#include <stdio.h>

#include "v2/imm.h"
#include "v2/inst.h"
#include "log.h"
#include "utils.h"

#define MAX_IMMEDIATE_CNT   16

/*
    arm64_instr_type_t type;
    arm64_reg_t rd, rn, rm;
    uint64_t imm;
    uint32_t shift;
 */

#define IMM_CMP_TYPE        0x001
#define IMM_CMP_RD          0x002
#define IMM_CMP_RN          0x004
#define IMM_CMP_RM          0x008
#define IMM_CMP_IMM         0x010
#define IMM_CMP_SHIFT       0x020

#define IMM_COLLECT         0x040

typedef enum {
    IT_NULL = 0,
    IT_MATCH = 1, // require full match
    IT_MATCH_OR_START = 2, // no instruction - delimiter, choose 1 of nestend instructions
    IT_MATCH_OR_END = 3, // no instruction - delimiter end
    IT_LOOP_START = 4, // init loop
    IT_LOOP_END = 5, // return to previous IT_LOOP_START if iterations remain
    IT_SKIP = 6, // skip up to n instructions
} ImmediateType;

static const char * types[] = {
    "NULL", "MATCH", "OR_START", "OR_END", "LOOP_START", "LOOP_END", "SKIP"
};

#define INST_STEP_OR_START          { IT_MATCH_OR_START,    { .type = INSTR_DUMMY, }, 0, } 
#define INST_STEP_OR_END            { IT_MATCH_OR_END,      { .type = INSTR_DUMMY, }, 0, } 
#define INST_STEP_LOOP_START(cnt)   { IT_LOOP_START,        { .type = INSTR_DUMMY, }, (cnt), } 
#define INST_LOOP_END               { IT_LOOP_END,          { .type = INSTR_DUMMY, }, 0, } 
#define INST_STEP_ANY(cnt)          { IT_SKIP,              { .type = INSTR_DUMMY, }, (cnt) } 
#define INST_END                    { IT_NULL, } 

#define INST_OR(...)                INST_STEP_OR_START,\
                                    __VA_ARGS__,\
                                    INST_STEP_OR_END

#define INST_LOOP(cnt,...)          INST_STEP_LOOP_START(cnt),\
                                    __VA_ARGS__,\
                                    INST_LOOP_END

typedef struct {
    ImmediateType type;
    arm64_instr_t instr;
    uint32_t mask;
} ImmStep;

typedef struct {
    const ImmStep * cur;
    uint32_t counter_skip; // used for SKIP
    uint32_t counter_loop; // used for LOOP
    uint8_t collect;
    uint8_t matched;
} ImmState;

typedef struct {
    const ImmStep * init;
    const ImmStep * collect;
    const ImmStep * end;

    ImmState state_init;
    ImmState state_collect;
    ImmState state_end;
    
    uint8_t raw[MAX_IMMEDIATE_CNT * 2];
    uint32_t raw_idx;
    uint32_t offsets[MAX_IMMEDIATE_CNT];
    uint32_t offsets_idx;
    uint32_t immediate_start;
    
    ImmResult * result;
    uint32_t match_cnt;
} ImmParser;

static const ImmStep match_a_init[] = {
    { IT_MATCH,{
            .type = INSTR_ADRP,
            .rd = REG_X0,}, IMM_CMP_TYPE | IMM_CMP_RD},

    INST_END
};

static const ImmStep match_a_collect[] = {
        INST_STEP_ANY(4),
        
        { IT_MATCH,{ .type = INSTR_LDR, .rd = REG_XZR, .rn = REG_X0},
            IMM_CMP_TYPE | IMM_CMP_RD | IMM_CMP_RN},
            
        INST_LOOP(4,
            INST_STEP_ANY(4),
            INST_OR(
                // MOVZ
                { IT_MATCH,{ .type = INSTR_MOVZ, .rd = REG_W0},
                    IMM_CMP_TYPE | IMM_CMP_RD | IMM_COLLECT},
                // MOVN
                { IT_MATCH,{ .type = INSTR_MOVN, .rd = REG_W0},
                    IMM_CMP_TYPE | IMM_CMP_RD | IMM_COLLECT},
                // MOVK
                { IT_MATCH,{ .type = INSTR_MOVK, .rd = REG_W0},
                    IMM_CMP_TYPE | IMM_CMP_RD | IMM_COLLECT}
            )
        ),
            
        INST_STEP_ANY(4),
                        
        INST_END
};

static const ImmStep match_a_commit[] = {
    { IT_MATCH,{
            .type = INSTR_BL,}, IMM_CMP_TYPE},

    INST_END
};

// instruction matcher

static int match_ins(const arm64_instr_t *d, const arm64_instr_t *p, uint32_t m) {
    if((m & IMM_CMP_TYPE) && d->type != p->type) return 0;
    if((m & IMM_CMP_RD) && d->rd != p->rd) return 0;
    if((m & IMM_CMP_RN) && d->rn != p->rn) return 0;
    if((m & IMM_CMP_RM) && d->rm != p->rm) return 0;
    if((m & IMM_CMP_IMM) && d->imm != p->imm) return 0;
    if((m & IMM_CMP_SHIFT) && d->shift != p->shift) return 0;
    
    /*
    char inst1[256];
    snprintf(inst1, sizeof(inst1), "%s", instr_to_string(d, 0, 0));
    
    lf_d("match [%s] with [%s] by 0x%03X", inst1, instr_to_string(p, 0, 0), m);
     */
    
    return 1;
}

// Advance one state; return new state or NULL on failure

static ImmState step_state(const ImmStep * steps, ImmState st, const arm64_instr_t *d) {
    if(!st.cur || st.cur->type == IT_NULL) {
        return st;
    }
    
    st.collect = 0;
    st.matched = 0;
    
    uint8_t do_log = 0 && (steps >= match_a_collect && steps < match_a_collect + ARRLEN(match_a_collect));
    if(do_log) {
        lf_d("   EVAL %s(%u)", types[st.cur->type], (uint32_t)(st.cur - match_a_collect));
    }
    
    switch(st.cur->type) {
        case IT_SKIP: {
            // try match next step
            ImmState tmp = st;
            tmp.counter_skip = 0;
            tmp.cur++;
            
            if(st.counter_skip == 0) {
                st.counter_skip = st.cur->mask;
            }
            
            // attempt recurse, so we can handle loops and or
            tmp = step_state(steps, tmp, d);
            
            // resolved skip
            if(tmp.cur != NULL && tmp.cur != st.cur && tmp.matched) {
                return tmp;
            }
            // fallback: consume one skip
            if(--st.counter_skip == 0) {
                st.cur++;
            }

        }   return st;
        
        case IT_LOOP_START:
            if(st.counter_loop == 0) {
                st.counter_loop = st.cur->mask;
            }
            st.cur++;
            
            // recurse, else instruction would be wasted
            return step_state(steps, st, d);
            
        case IT_LOOP_END:
            if(st.counter_loop && --st.counter_loop > 0) {
                // rewind to after LOOP_START
                const ImmStep *p = st.cur;
                while(p > steps && p[-1].type != IT_LOOP_START) {
                    p--;
                }
                st.cur = p;
            } else {
                st.cur++;
            }
            // recurse, else instruction would be wasted
            return step_state(steps, st, d);
            
        case IT_MATCH_OR_START: {
            // try any branch
            const ImmStep *p = st.cur + 1;
            const ImmStep *end = p;
            while(end->type != IT_MATCH_OR_END) end++;
            for(; p < end; p++) {
                if(p->type == IT_MATCH && do_log) {
                    lf_t("   OR MATCHING [%s]", instr_to_string(&p->instr, 0, 0));
                }
                
                if(p->type == IT_MATCH && match_ins(d, &p->instr, p->mask)) {
                    st.matched = 1;
                    
                    if(p->mask & IMM_COLLECT) {
                        st.collect = 1;
                    }
                    
                    // advance to OR_END
                    while(st.cur->type != IT_MATCH_OR_END) {
                        st.cur++;
                    }
                    st.cur++;
                    return st;
                }
            }
            
            st.cur = NULL;
        }   return st;
        
        case IT_MATCH_OR_END: {
            st.cur++;
            // recurse, else instruction would be wasted
            return step_state(steps, st, d);
        }
        
        case IT_MATCH:
            if(match_ins(d, &st.cur->instr, st.cur->mask)) {
                st.matched = 1;
                
                if(st.cur->mask & IMM_COLLECT) {
                    st.collect = 1;
                }
                st.cur++;
                return st;
            }
            
            st.cur = NULL;
            return st;
            
        case IT_NULL:
            return st;
            
        default:
            return st;
    }
}

static inline void imm_state_reset(ImmState * s, const ImmStep * start) {
    s->cur = start;
    s->counter_skip = 0;
    s->counter_loop = 0;
} 

static inline void imm_immediate_reset(ImmParser * P) {
    P->raw_idx = 0;
    P->offsets_idx = 0;
    P->immediate_start = 0;
}

static ImmMatch * imm_match_init(uint32_t len) {
    ImmMatch * res = malloc(sizeof(*res));
    memset(res, 0, sizeof(*res));
    
    res->cnt = len;
    res->offsets = malloc(sizeof(*res->offsets) * len);
    memset(res->offsets, 0, sizeof(*res->offsets) * len);
    
    return res;
}

// Outer loop using your step_state
void parse_with_patterns(ImmParser *P, uint32_t *insts, size_t n, uint64_t base_pc) {
    // Initialize states
    ImmState si = P->state_init;
    ImmState sc = P->state_collect;
    ImmState se = P->state_end;
    
    for (size_t i = 0; i < n; i++) {
        uint32_t pc = base_pc + i * 4;
        arm64_instr_t d;
        instr_decode(insts[i], &d, pc);

        // check if we have overlaping INIT
        ImmState ni;
        imm_state_reset(&ni, P->init);
        ni = step_state(P->init, ni, &d);
        
        uint8_t si_reached = 0;
        if(ni.cur) {
            //lf_e("INIT overlap");
            
            // INIT overlaps, treat as first match
            si = ni;
            
            // now, matching single instruction from INIT does not neccessarily 
            // mean that COLLECT should fail, but for now its good enough
            imm_state_reset(&sc, P->collect);
            imm_immediate_reset(P);
            
            P->immediate_start = pc;
            
            if (si.cur->type == IT_NULL) {
                si_reached = 1;
                //lf_w("INIT done 0x%08X [%s]", pc, instr_to_string(&d, insts[i], pc));
            }
        } else if(si.cur->type != IT_NULL) {
            // only handle if INIT not done yet
            si = step_state(P->init, si, &d);
            
            if(!si.cur) {
                // failed to match, reset
                imm_state_reset(&si, P->init);
            }
            
            if (si.cur && si.cur->type == IT_NULL) {
                si_reached = 1;
                //lf_w("INIT done 0x%08X [%s]", pc, instr_to_string(&d, insts[i], pc));
            }
        }
        
        if(si_reached) {
            // do something
            //lf_e("INIT start");
        }
        
        if(si.cur->type == IT_NULL) {
            //lf_t("is init");
        } else {
            //lf_e("is not init");
        }
        
        // INIT done, advance COLLECT
        if(si.cur->type == IT_NULL) {
                        
            ImmState nc = step_state(P->collect, sc, &d);
             
            // either failed, or ended without matching END pattern
            if (!nc.cur || nc.cur->type == IT_NULL) {
                // matched end, reset all
                imm_state_reset(&si, P->init);
                imm_state_reset(&sc, P->collect);
                imm_state_reset(&se, P->end);
                imm_immediate_reset(P);
            } else {
                if(nc.collect) {                   
                    uint16_t imm = d.imm & 0xFFFF;
                                        
                    if(P->offsets_idx != ARRLEN(P->offsets)) {
                        //lf_e("offset %u=0x%08x", P->offsets_idx, pc);
                        P->offsets[P->offsets_idx++] = pc;
                    }
                    
                    for(uint32_t i = 0; i < sizeof(imm); i++) {
                        if(P->raw_idx != ARRLEN(P->raw)) {
                            P->raw[P->raw_idx++] = imm & 0xFF;
                            imm >>= 8;
                        }
                    }
                }
                
                sc = nc;
            }   
        }
        
        // check if we have overlaping END
        ImmState ne;
        imm_state_reset(&ne, P->end);
        ne = step_state(P->end, ne, &d);
        
        uint8_t se_reached = 0;
        if(ne.cur) {
            // END overlaps, treat as first match
            se = ne;
            
            if(se.cur->type == IT_NULL) {
                se_reached = 1;
            }
        } else if(se.cur->type != IT_NULL) {
            se = step_state(P->end, se, &d);
                
            if(!se.cur) {
                // failed to match, reset END
                imm_state_reset(&se, P->end);
            }
            
            if(se.cur->type == IT_NULL) {
                se_reached = 1;
            }
        }
        
        if(se_reached) {
            //lf_e("END reached");
            // was previously INIT
            if(si.cur->type == IT_NULL) {
                //lf_e("was init");
                // TODO: check if immediate was previously found and 
                // perform callback

                if(P->raw_idx) {
                    //printf("collected %-3u 0x%08X [%u]\n", P->raw_idx, P->immediate_start, (uint32_t)(pc - P->immediate_start));
                    ImmResult * res = malloc(sizeof(*res));
                    memset(res, 0, sizeof(*res));
                    
                    ImmMatch * match = malloc(sizeof(*match));
                    memset(match, 0, sizeof(*match));
                    
                    uint32_t offsets_len = sizeof(*match->offsets) * P->offsets_idx;
                    match->cnt = P->offsets_idx;
                    match->offsets = malloc(offsets_len);
                    memcpy(match->offsets, P->offsets, offsets_len);
                    
                    res->matches_cnt = 1;
                    res->matches = match;
                    res->raw_len = P->raw_idx;
                    res->raw = malloc(P->raw_idx);
                    
                    memcpy(res->raw, P->raw, P->raw_idx);
                    
                    if(P->result) {
                        res->next = P->result;
                    } 
                    
                    P->result = res;
                }
            }

            // matched end, reset all
            imm_state_reset(&si, P->init);
            imm_state_reset(&sc, P->collect);
            imm_state_reset(&se, P->end);
            imm_immediate_reset(P);
        }

    }
    
    P->state_init = si;
    P->state_collect = sc;
    P->state_end = se;
}



/** I have expected to match other patterns as well, however as it turns out,
 * partial matches are the only thing predictable enough to match it this way.
 * 
 * Might use it for language code matching, but I have a feeling it will be 
 * doable with finder.
 */
ImmResult * imm_scan(const void * data, uint32_t len) {
    const uint8_t * data_u8 = data;
    
    ImmParser P = {
        .init = match_a_init,
        .collect = match_a_collect,
        .end = match_a_commit,
    };
        
    imm_state_reset(&P.state_init, P.init);
    imm_state_reset(&P.state_collect, P.collect);
    imm_state_reset(&P.state_end, P.end);
    imm_immediate_reset(&P);
    
    uint32_t offset = 0;
    
    while(len) {
        uint32_t tmp[256];
        uint32_t mod = MIN(sizeof(tmp), len);
        len -= mod;
        
        memcpy(tmp, data_u8, mod);
        data_u8 += mod;
        
        parse_with_patterns(&P, tmp, (mod / sizeof(*tmp)), offset);
        offset += mod;
    }

    return P.result;
}



/*
 
002409d0 00 1b 00        adrp         x0,s_Address_already_in_use_71005a10   = "Address already in use"
                        b0
002409d4 f8 7f 17         stp           x24,xzr,[sp, #local_1f0]
                 a9
002409d8 1f f4 44         ldr            d31,[x0, #0x9e8]=>DAT_71005a19e8      = 489A9D633572259Dh
                 fd
002409dc a0 53 80        mov         w0,#0x29d
                 52
002409e0 60 ae a0         movk       w0,#0x573, LSL #16
                 72
002409e4 ff 03 06         strb          wzr,[sp, #local_1e0]
                 39
002409e8 e0 3b 02        str            w0,[sp, #local_130[8]]
                 b9
002409ec a0 b3 8b        mov         w0,#0xffffa262
                 12
002409f0 f3 87 01         str            x19,[sp, #local_58]
                 f9
002409f4 e0 7b 04        strh          w0,[sp, #local_130[12]]
                 79
002409f8 60 09 80        mov         w0,#0x4b
                 52
002409fc e0 fb 08         strb          w0,[sp, #local_130[14]]
                 39
00240a00 82 09 09        bl             ivar1_plus_1f8                                            undefined ivar1_plus_1f8()
 
 */