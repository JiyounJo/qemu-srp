/*
 *  SRP translation
 *
 *  Copyright (c) 2011 Kevin Hjc
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "cpu.h"
#include "exec-all.h"
#include "disas.h"
#include "tcg-op.h"
#include "qemu-log.h"

#include "helper.h"
#define GEN_HELPER 1
#include "helper.h"


/* internal defines */
typedef struct DisasContext {
    target_ulong pc;
    int is_jmp;
    /* Nonzero if this instruction has been conditionally skipped.  */
    int condjmp;
    /* The label that will be jumped to when the instruction is skipped.  */
    int condlabel;
    struct TranslationBlock *tb;
    int singlestep_enabled;

#if !defined(CONFIG_USER_ONLY)
    int user;
#endif
} DisasContext;

#if defined(CONFIG_USER_ONLY)
#define IS_USER(s) 1
#else
#define IS_USER(s) (s->user)
#endif

static TCGv_i32 cpu_R[60];

static const char *regnames[] =
    { "R00", "R04", "R08", "R0c", "R10", "R14", "R18", "R1c","R20", "R24", "R28", "R2c",
       "R30", "R34", "R38", "R3c", "R30", "R44", "R48", "R4c","R40", "R54", "R58", "R5c",
       "R60", "R64", "R68", "R6c", "R70", "R74", "R78", "R7c","R80", "R84", "R88", "R8c",
       "R90", "R94", "R98", "R9c", "Ra0", "Ra4", "Ra8", "Rac","Rb0", "Rb4", "Rb8", "Rbc",
       "Rc0", "Rc4", "Rc8", "Rcc", "Rd0", "Rd4", "Rd8", "Rdc","Re0", "Re4", "Re8", "Rec",
       "IRQ", "PSW", "SP", "PC"};


static int num_temps;

/* Allocate a temporary variable.  */
static TCGv_i32 new_tmp(void)
{
    num_temps++;
    return tcg_temp_new_i32();
}

/* Release a temporary variable.  */
static void dead_tmp(TCGv tmp)
{
    tcg_temp_free(tmp);
    num_temps--;
}


/* Set a variable to the value of a CPU register.  */
static void load_reg_var(DisasContext *s, TCGv var, int reg)
{
     tcg_gen_mov_i32(var, cpu_R[reg]);
}

/* Create a new temporary and set it to the value of a CPU register.  */
static inline TCGv load_reg(DisasContext *s, int reg)
{
    TCGv tmp = new_tmp();
    load_reg_var(s, tmp, reg);
    return tmp;
}

/* Set a CPU register.  The source must be a temporary and will be
   marked as dead.  */
static void store_reg(DisasContext *s, int reg, TCGv var)
{
    tcg_gen_mov_i32(cpu_R[reg], var);
    dead_tmp(var);
}

/* To store into memory*/
static inline void gen_st8(TCGv val, TCGv addr, int index)
{
    tcg_gen_qemu_st8(val, addr, index);
    dead_tmp(val);
}



static inline void gen_st32(TCGv val, TCGv addr, int index)
{
    tcg_gen_qemu_st32(val, addr, index);
    dead_tmp(val);
}

/*To load from memory*/
static inline TCGv gen_ld8s(TCGv addr, int index)
{
    TCGv tmp = new_tmp();
    tcg_gen_qemu_ld8s(tmp, addr, index);
    return tmp;
}



static inline TCGv gen_ld32(TCGv addr, int index)
{
    TCGv tmp = new_tmp();
    tcg_gen_qemu_ld32u(tmp, addr, index);
    return tmp;
}

static unsigned int get_insn_length(unsigned int  insn)
{
	unsigned int length;

	switch(insn >> 28) {
		case 0x00:
			length = 1;
			break;
		case 0x01:
		case 0x02:
		case 0x03:
		case 0x0C:
			length = 3;
			break;
		case 0x04:
		case 0x05:
			length = 6;
			break;
		case 0x06:
		case 0x0E:
		case 0x0F:
			length = 2;
			break;
		case 0x0B:
			length = 4;
			break;
		case 0x0D:
			length = 5;
			break;
		default:
			length = 0;
			break;
	}

	return length;
}

static void disas_srp_insn(CPUState * env, DisasContext *s)
{
	unsigned int insn, rs, rd, iLength;
	int32_t imm32;

	TCGv tmp1 = new_tmp();
    TCGv tmp2 = new_tmp();
    TCGv addr = new_tmp();

	insn = ldl_code(s->pc);
	iLength = get_insn_length(insn);	

	if (iLength == 2) {
		rd =  (insn >> 16) & 0xff;

	} else if(iLength == 3) {
		switch(insn >> 24) {
		    	case 0x17:
				rd = (insn >> 16) & 0xff;
				rs = (insn >> 8) & 0xff;
				tmp1 = load_reg(s, rs);
				tcg_gen_andi_i32(tmp1, tmp1, 0xff);
				tcg_gen_mov_i32(cpu_R[rd], tmp1);
				break;
			case 0x1C:   
			case 0x1E:
				rd = (insn >> 16) & 0xff;
				rs = (insn >> 8) & 0xff;
				addr = load_reg(s, rs);
				tmp1 = gen_ld8s(addr, IS_USER(s));
				store_reg(s, rd, tmp1);
				break;
			case 0x1D:
			case 0x1F:
				rs = (insn >> 16) & 0xff;	
				rd = (insn >> 8) & 0xff;	
				tmp1 = load_reg(s, rs);	
				addr = load_reg(s, rd);	
				gen_st8(tmp1, addr, IS_USER(s));	
				break;

			case 0x27:	
				rd = (insn >> 16) & 0xff;	
				rs = (insn >> 8) & 0xff;	
				tcg_gen_mov_tl(cpu_R[rd], cpu_R[rs]);	
				break;				
			case 0x2C:	
			case 0x2E:	
				rd = (insn >> 16) & 0xff;	
				rs = (insn >> 8) & 0xff;	
				addr = load_reg(s, rs);	
				tmp1 = gen_ld32(addr, IS_USER(s));	
				store_reg(s, rd, tmp1);	
				break;			
			case 0x2D:	
			case 0x2F:	
				rs = (insn >> 16) & 0xff;	
				rd = (insn >> 8) & 0xff;	
				tmp1 = load_reg(s, rs);	
				addr = load_reg(s, rd);	
				gen_st32(tmp1, addr, IS_USER(s));	
				break;
				
			case 0x37:                            	
				rd = (insn >> 16) & 0xff;	
				imm32 = (insn >> 8) & 0xff;            	
				tcg_gen_movi_i32(cpu_R[rd], imm32);	
				break;	
			case 0x38:	
			case 0x3A:	
				rd = (insn >> 16) & 0xff;	
				imm32 = (insn >> 8) & 0xff;	
				tcg_gen_movi_i32(tmp1, imm32);
				tcg_gen_movi_i32(addr, rd);
				gen_st8(tmp1, addr, IS_USER(s));	
				break;
			case 0x39:	
			case 0x3B:	
				rd = (insn >> 16) & 0xff;	
				imm32 = (insn >> 8) & 0xff;	
				tcg_gen_movi_i32(tmp1, imm32);
				addr = load_reg(s, rd);	
				gen_st8(tmp1, addr, IS_USER(s));	
				break;
			case 0x3C:	
			case 0x3E:	
				rd = (insn >> 16) & 0xff;	
				rs = (insn >> 8) & 0xff;	
				tcg_gen_movi_i32(addr, rs);
				tmp1 = gen_ld32(addr, IS_USER(s));	
				store_reg(s, rd, tmp1);	
				break;
			case 0x3D:	
			case 0x3F:	
				rs = (insn >> 16) & 0xff;	
				rs = (insn >> 8) & 0xff;	
				tcg_gen_movi_i32(addr, rs);
				tmp1 = load_reg(s, rs);	
				gen_st32(tmp1, addr, IS_USER(s));	
				break;
			default:
				break;
		}		
	} else if(iLength == 4) {
		rd = (insn >> 16) & 0xff;
		rs = insn & 0xffff;

	} else if(iLength == 5) {
	} else {
		
		switch(insn >> 24) {
			case 0x47:	
				rd = (insn >> 16) & 0xff;
				imm32 = ldl_code(s->pc + 2);
				tcg_gen_movi_i32(cpu_R[rd], imm32);
				break;	
			case 0x48:		
			case 0x4A:	
				rd = (insn >> 16) & 0xff;		
				tcg_gen_movi_i32(addr, rd);
				imm32 = ldl_code(s->pc + 2);
				tcg_gen_movi_i32(tmp1, imm32);		
				gen_st32(tmp1, addr, IS_USER(s));		
				break;	
			case 0x49:		
			case 0x4B:		
				rd = (insn >> 16) & 0xff;		
				imm32 = ldl_code(s->pc + 2);		
				tcg_gen_movi_i32(tmp1, imm32);		
				gen_st32(tmp1, addr, IS_USER(s));		
				break;
			case 0x4C:		
			case 0x4E:		
				rd = (insn >> 16) & 0xff;		
				rs = ldl_code(s->pc + 2);
				tcg_gen_movi_i32(addr,rs);		
				tmp1 = gen_ld32(addr, IS_USER(s));		
				store_reg(s, rd, tmp1);		
				break;
			case 0x4D:		
			case 0x4F:		
				rs = (insn >> 16) & 0xff;		
				rs = ldl_code(s->pc + 2);
				tcg_gen_movi_i32(addr,rs);	
				tmp1 = load_reg(s, rs);		
				gen_st32(tmp1, addr, IS_USER(s));		
				break;
				
			case 0x5C:		
			case 0x5E:		
				rd = (insn >> 16) & 0xff;		
				rs = ldl_code(s->pc + 2);
				tcg_gen_movi_i32(addr,rs);		
				tmp1 = gen_ld8s(addr, IS_USER(s));		
				store_reg(s, rd, tmp1);		
				break;	
			case 0x5D:		
			case 0x5F:		
				rs = (insn >> 16) & 0xff;		
				rs = ldl_code(s->pc + 2);
				tcg_gen_movi_i32(addr,rs);		
				tmp1= load_reg(s, rs);		
				gen_st8(tmp1, addr, IS_USER(s));		
				break;

			default:
				break;
		}
		
	}

	dead_tmp(tmp1);
    dead_tmp(tmp2);
    dead_tmp(addr);
		
	s->pc += iLength;	
}

/* generate intermediate code in gen_opc_buf and gen_opparam_buf for
   basic block 'tb'. If search_pc is TRUE, also generate PC
   information for each intermediate instruction. */
static inline void gen_intermediate_code_internal(CPUState *env,
                                                  TranslationBlock *tb,
                                                  int search_pc)
{
	DisasContext dc1, *dc = &dc1;
	target_ulong pc_start;

	pc_start = tb->pc;

	dc->tb = tb;
	dc->is_jmp = DISAS_NEXT;
	dc->condjmp = 0;
	dc->pc = pc_start;
	dc->singlestep_enabled = env->singlestep_enabled;
#if !defined(CONFIG_USER_ONLY)
    dc->user = 0;
#endif
	disas_srp_insn(env, dc);
}

void gen_intermediate_code(CPUState *env, TranslationBlock *tb)
{
    gen_intermediate_code_internal(env, tb, 0);
}

void gen_intermediate_code_pc(CPUState *env, TranslationBlock *tb)
{
    gen_intermediate_code_internal(env, tb, 1);
}

void cpu_dump_state(CPUState *env, FILE *f,
                    int (*cpu_fprintf)(FILE *f, const char *fmt, ...),
                    int flags)
{
    int i;

    for(i=0;i<SRP_REGS;i++) {
        cpu_fprintf(f, "%s=%08x", regnames[i], env->regs[i]);
        ((i % 4) == 0) ? cpu_fprintf(f, "\n") : cpu_fprintf(f, " ");
    }
   
    cpu_fprintf(f, "PSW=%08x,  SP=%08x,  PC=%08x\n",
                env->psw,
                env->sp,
                env->pc);

}


void gen_pc_load(CPUState *env, TranslationBlock *tb,
                unsigned long searched_pc, int pc_pos, void *puc)
{
    env->pc = gen_opc_pc[pc_pos];
}

