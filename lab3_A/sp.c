#define _CRT_SECURE_NO_WARNINGS
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#ifdef __unix__
#include <sys/socket.h>
#include <netinet/in.h>
#endif
#include "llsim.h"
static const char* SP_BUILD_TAG = "SP tracev2 " __DATE__ " " __TIME__;

#define sp_printf(fmt, ...) \
    do { \
        llsim_printf("sp: clock %d: ", llsim->clock); \
        llsim_printf(fmt, ##__VA_ARGS__); \
    } while (0)

int nr_simulated_instructions = 0;
FILE* inst_trace_fp = NULL, * cycle_trace_fp = NULL;

// Opcodes
#define ADD 0
#define SUB 1
#define LSF 2
#define RSF 3
#define AND 4
#define OR  5
#define XOR 6
#define LHI 7
#define LD  8
#define ST  9
#define JLT 16
#define JLE 17
#define JEQ 18
#define JNE 19
#define JIN 20
#define HLT 24

static char opcode_name[32][4] = { "ADD", "SUB", "LSF", "RSF", "AND", "OR", "XOR", "LHI",
                 "LD", "ST", "U", "U", "U", "U", "U", "U",
                 "JLT", "JLE", "JEQ", "JNE", "JIN", "U", "U", "U",
                 "HLT", "U", "U", "U", "U", "U", "U", "U" };

typedef struct sp_registers_s {
    // 6 32 bit registers (r[0], r[1] don't exist)
    int r[8];

    // 32 bit cycle counter
    int cycle_counter;

    // fetch0
    int fetch0_active; // 1 bit
    int fetch0_pc; // 16 bits

    // fetch1
    int fetch1_active; // 1 bit
    int fetch1_pc; // 16 bits
    int fetch1_inst; // 32 bits

    // dec0
    int dec0_active; // 1 bit
    int dec0_pc; // 16 bits
    int dec0_inst; // 32 bits

    // dec1
    int dec1_active; // 1 bit
    int dec1_pc; // 16 bits
    int dec1_inst; // 32 bits
    int dec1_opcode; // 5 bits
    int dec1_src0; // 3 bits
    int dec1_src1; // 3 bits
    int dec1_dst; // 3 bits
    int dec1_immediate; // 32 bits
    int dec1_alu0; // 32 bits
    int dec1_alu1; // 32 bits

    // exec0
    int exec0_active; // 1 bit

    int exec0_pc; // 16 bits
    int exec0_inst; // 32 bits
    int exec0_opcode; // 5 bits
    int exec0_src0; // 3 bits
    int exec0_src1; // 3 bits
    int exec0_dst; // 3 bits
    int exec0_immediate; // 32 bits
    int exec0_alu0; // 32 bits
    int exec0_alu1; // 32 bits
    int exec0_aluout; // 32 bits

    // exec1
    int exec1_active; // 1 bit
    int exec1_pc; // 16 bits
    int exec1_inst; // 32 bits
    int exec1_opcode; // 5 bits
    int exec1_src0; // 3 bits
    int exec1_src1; // 3 bits
    int exec1_dst; // 3 bits
    int exec1_immediate; // 32 bits
    int exec1_alu0; // 32 bits
    int exec1_alu1; // 32 bits
    int exec1_aluout; // 32 bits

    // DMA registers
    int dma_start;
    int dma_len;
    int dma_busy;
    int dma_done;
    int dma_src_addr;
    int dma_dst_addr;
    int dma_counter;
} sp_registers_t;

/*
 * Master structure
 */
typedef struct sp_s {
    // local srams
#define SP_SRAM_HEIGHT    64 * 1024
    llsim_memory_t* srami, * sramd;

    unsigned int memory_image[SP_SRAM_HEIGHT];
    int memory_image_size;

    int start;

    sp_registers_t* spro, * sprn;

    // Hazard detection signals
    int stall;
    int flush;

    // Branch prediction
    int branch_taken;
    int branch_target;

    // Forwarding signals
    int forward_alu0;
    int forward_alu1;
    int forward_value_alu0;
    int forward_value_alu1;
} sp_t;

// New tracer with operand overrides so the EXEC line shows the *actual* ALU inputs
static void trace_instruction(uint32_t inst, uint32_t current_pc, int32_t* regs,
    int use_exec_overrides, int32_t exec_opA, int32_t exec_opB)
{
    uint8_t  opcode = (inst >> 25) & 0x1F;
    uint8_t  dst = (inst >> 22) & 0x07;
    uint8_t  src0 = (inst >> 19) & 0x07;
    uint8_t  src1 = (inst >> 16) & 0x07;
    uint16_t imm16 = (uint16_t)(inst & 0xFFFF);
    int32_t  imm32 = (int32_t)(int16_t)imm16;

    const char* op_str;
    switch (opcode) {
    case 0:  op_str = "ADD"; break; case 1:  op_str = "SUB"; break;
    case 2:  op_str = "LSF"; break; case 3:  op_str = "RSF"; break;
    case 4:  op_str = "AND"; break; case 5:  op_str = "OR";  break;
    case 6:  op_str = "XOR"; break; case 7:  op_str = "LHI"; break;
    case 8:  op_str = "LD";  break; case 9:  op_str = "ST";  break;
    case 16: op_str = "JLT"; break; case 17: op_str = "JLE"; break;
    case 18: op_str = "JEQ"; break; case 19: op_str = "JNE"; break;
    case 20: op_str = "JIN"; break; case 24: op_str = "HLT"; break;
    default: op_str = "UNK"; break;
    }

    extern int nr_simulated_instructions;
    extern FILE* inst_trace_fp;
    fprintf(inst_trace_fp,
        "--- instruction %d (%04x) @ PC %d (%04d) -----------------------------------------------------------\n",
        nr_simulated_instructions, nr_simulated_instructions, current_pc, current_pc);

    fprintf(inst_trace_fp,
        "pc = %04d, inst = %08x, opcode = %d (%s), dst = %d, src0 = %d, src1 = %d, immediate = %08x\n",
        current_pc, inst, opcode, op_str, dst, src0, src1, (unsigned)imm16);

    int32_t regs_for_print[8];
    memcpy(regs_for_print, regs, sizeof(regs_for_print));
    if (src0 == 1 || src1 == 1 ||
        opcode == JEQ || opcode == JNE || opcode == JLT || opcode == JLE || opcode == JIN) {
        regs_for_print[1] = imm32;
    }

    fprintf(inst_trace_fp,
        "r[0] = %08x r[1] = %08x r[2] = %08x r[3] = %08x \n",
        (uint32_t)regs_for_print[0], (uint32_t)regs_for_print[1],
        (uint32_t)regs_for_print[2], (uint32_t)regs_for_print[3]);
    fprintf(inst_trace_fp,
        "r[4] = %08x r[5] = %08x r[6] = %08x r[7] = %08x \n\n",
        (uint32_t)regs_for_print[4], (uint32_t)regs_for_print[5],
        (uint32_t)regs_for_print[6], (uint32_t)regs_for_print[7]);

    int32_t opA = (src0 == 1) ? (int32_t)(int16_t)imm16 : regs[src0];
    int32_t opB = (src1 == 1) ? (int32_t)(int16_t)imm16 : regs[src1];
    if (use_exec_overrides) { opA = exec_opA; opB = exec_opB; }

    switch (opcode) {
    case 0: case 1: case 2: case 3: case 4: case 5: case 6:
        fprintf(inst_trace_fp, ">>>> EXEC: R[%d] = %d %s %d <<<<\n\n", dst, opA, op_str, opB);
        break;

    case 7:
        fprintf(inst_trace_fp,
            ">>>> EXEC: R[%d] = (0x%04x << 16) | (R[%d] & 0xFFFF) <<<<\n\n",
            dst, (unsigned)imm16, dst);
        break;

    case 8: {
        int addr = regs[src1];
        static llsim_memory_t* sramd_mem = NULL;
        if (!sramd_mem) {
            llsim_unit_t* u = llsim_find_unit("sp");
            for (llsim_memory_t* m = u ? u->mems : NULL; m; m = m->next)
                if (m->name && strcmp(m->name, "sramd") == 0) { sramd_mem = m; break; }
        }
        uint32_t mem_val = (uint32_t)regs[dst];
        if (sramd_mem) mem_val = (uint32_t)llsim_mem_extract(sramd_mem, addr, 31, 0);
        fprintf(inst_trace_fp, ">>>> EXEC: R[%d] = MEM[%d] = %08x <<<<\n\n", dst, addr, mem_val);
        break;
    }

    case 9:
        fprintf(inst_trace_fp, ">>>> EXEC: MEM[%d] = R[%d] = %08x <<<<\n\n",
            regs[src1], src0, (uint32_t)regs[src0]);
        break;

    case 16: {
        int taken = (regs[src0] < regs[src1]);
        int nextpc = taken ? (int)(uint16_t)imm16 : ((current_pc + 1) & 0xFFFF);
        fprintf(inst_trace_fp, ">>>> EXEC: JLT %d, %d, %d <<<<\n\n", regs[src0], regs[src1], nextpc); break;
    }
    case 17: {
        int taken = (regs[src0] <= regs[src1]);
        int nextpc = taken ? (int)(uint16_t)imm16 : ((current_pc + 1) & 0xFFFF);
        fprintf(inst_trace_fp, ">>>> EXEC: JLE %d, %d, %d <<<<\n\n", regs[src0], regs[src1], nextpc); break;
    }
    case 18: {
        int taken = (regs[src0] == regs[src1]);
        int nextpc = taken ? (int)(uint16_t)imm16 : ((current_pc + 1) & 0xFFFF);
        fprintf(inst_trace_fp, ">>>> EXEC: JEQ %d, %d, %d <<<<\n\n", regs[src0], regs[src1], nextpc); break;
    }
    case 19: {
        int taken = (regs[src0] != regs[src1]);
        int nextpc = taken ? (int)(uint16_t)imm16 : ((current_pc + 1) & 0xFFFF);
        fprintf(inst_trace_fp, ">>>> EXEC: JNE %d, %d, %d <<<<\n\n", regs[src0], regs[src1], nextpc); break;
    }
    case 20: {
        int nextpc = regs[dst];
        fprintf(inst_trace_fp, ">>>> EXEC: JIN %d, %d, %d <<<<\n\n", regs[src0], regs[src1], nextpc); break;
    }

    case 24:
        fprintf(inst_trace_fp, ">>>> EXEC: HALT at PC %04x<<<<\n", current_pc);
        fprintf(inst_trace_fp, "sim finished at pc %d, %d instructions", current_pc, ++nr_simulated_instructions);
        break;

    default:
        fprintf(inst_trace_fp, ">>>> EXEC: (unknown) <<<<\n\n");
        break;
    }


    nr_simulated_instructions++;
}






static void dump_sram(sp_t* sp, char* name, llsim_memory_t* sram)
{
    FILE* fp;
    int i;

    fp = fopen(name, "w");
    if (fp == NULL) {
        printf("couldn't open file %s\n", name);
        exit(1);
    }
    for (i = 0; i < SP_SRAM_HEIGHT; i++)
        fprintf(fp, "%08x\n", llsim_mem_extract(sram, i, 31, 0));
    fclose(fp);
}

static void sp_reset(sp_t* sp)
{
    printf("%s\n", SP_BUILD_TAG);
    sp_registers_t* sprn = sp->sprn;

    memset(sprn, 0, sizeof(*sprn));
    sprn->fetch0_pc = 0;
    sprn->fetch0_active = 0;
}

static void detect_hazards(sp_t* sp)
{
    sp_registers_t* s = sp->spro;

    // Reset all hazard/forwarding signals for this cycle
    sp->stall = 0;
    sp->flush = 0;

    sp->forward_alu0 = 0;
    sp->forward_alu1 = 0;
    sp->forward_value_alu0 = 0;
    sp->forward_value_alu1 = 0;

    // --------------------------
    // Data hazards (DEC1 consumers vs. EXEC0/EXEC1 producers)
    // --------------------------
    if (s->dec1_active) {

        // ---------- Forward from EXEC1 (ALU + LD) — PRIORITY #1 ----------
        // For LD, the value is available on the dataout port *this* cycle.
        if (s->exec1_active && s->exec1_dst >= 2) {
            int fwd1 = (s->exec1_opcode == LD)
                ? llsim_mem_extract_dataout(sp->sramd, 31, 0)
                : s->exec1_aluout;

            if (s->exec1_dst == s->dec1_src0) {
                sp->forward_alu0 = 1;
                sp->forward_value_alu0 = fwd1;
            }
            if (s->exec1_dst == s->dec1_src1) {
                sp->forward_alu1 = 1;
                sp->forward_value_alu1 = fwd1;
            }
        }

        // ---------- Forward from EXEC0 (ALU-only) — PRIORITY #2 / fallback ----------
        // Recompute the ALU result here (exec0_aluout of the *old* state is not final yet).
        int ex0_can_fw = 0;
        int ex0_res = 0;

        if (s->exec0_active && s->exec0_dst >= 2) {
            switch (s->exec0_opcode) {
            case ADD: ex0_can_fw = 1; ex0_res = s->exec0_alu0 + s->exec0_alu1; break;
            case SUB: ex0_can_fw = 1; ex0_res = s->exec0_alu0 - s->exec0_alu1; break;
            case LSF: ex0_can_fw = 1; ex0_res = (int)((uint32_t)s->exec0_alu0 << (s->exec0_alu1 & 31)); break;
            case RSF: ex0_can_fw = 1; ex0_res = (int)((uint32_t)s->exec0_alu0 >> (s->exec0_alu1 & 31)); break;
            case AND: ex0_can_fw = 1; ex0_res = s->exec0_alu0 & s->exec0_alu1; break;
            case OR:  ex0_can_fw = 1; ex0_res = s->exec0_alu0 | s->exec0_alu1; break;
            case XOR: ex0_can_fw = 1; ex0_res = s->exec0_alu0 ^ s->exec0_alu1; break;
            case LHI:
                ex0_can_fw = 1;
                ex0_res = (((uint32_t)s->exec0_immediate & 0xFFFF) << 16) |
                    (s->exec0_alu0 & 0xFFFF);
                break;
            default:
                // No EXEC0 forwarding for LD/ST/branches
                break;
            }
        }

        if (ex0_can_fw) {
            if (!sp->forward_alu0 && s->exec0_dst == s->dec1_src0) {
                sp->forward_alu0 = 1;
                sp->forward_value_alu0 = ex0_res;
            }
            if (!sp->forward_alu1 && s->exec0_dst == s->dec1_src1) {
                sp->forward_alu1 = 1;
                sp->forward_value_alu1 = ex0_res;
            }
        }

        // ---------- Load-use stall (producer LD still in EXEC0) ----------
        // When LD is in EXEC0, its data is NOT ready this cycle -> must stall the consumer.
        if (s->exec0_active && s->exec0_opcode == LD && s->exec0_dst >= 2) {
            if (s->exec0_dst == s->dec1_src0 || s->exec0_dst == s->dec1_src1)
                sp->stall = 1;
        }
    }

    // --------------------------
    // Control hazards (branch resolved in EXEC0)
    // --------------------------
    sp->branch_taken = 0;
    sp->branch_target = 0;

    if (s->exec0_active) {
        switch (s->exec0_opcode) {
        case JLT:
            if (s->exec0_alu0 < s->exec0_alu1) {
                sp->branch_taken = 1;
                sp->branch_target = s->exec0_immediate & 0xFFFF;
            }
            break;
        case JLE:
            if (s->exec0_alu0 <= s->exec0_alu1) {
                sp->branch_taken = 1;
                sp->branch_target = s->exec0_immediate & 0xFFFF;
            }
            break;
        case JEQ:
            if (s->exec0_alu0 == s->exec0_alu1) {
                sp->branch_taken = 1;
                sp->branch_target = s->exec0_immediate & 0xFFFF;
            }
            break;
        case JNE:
            if (s->exec0_alu0 != s->exec0_alu1) {
                sp->branch_taken = 1;
                sp->branch_target = s->exec0_immediate & 0xFFFF;
            }
            break;
        case JIN:
            sp->branch_taken = 1;
            sp->branch_target = s->exec0_alu0;  // jump to value in R[dst]
            break;
        default:
            break;
        }

        if (sp->branch_taken)
            sp->flush = 1;
    }
}




static void sp_ctl(sp_t* sp)
{
    sp_registers_t* spro = sp->spro;
    sp_registers_t* sprn = sp->sprn;
    int i;

    fprintf(cycle_trace_fp, "cycle %d\n", spro->cycle_counter);
    fprintf(cycle_trace_fp, "cycle_counter %08x\n", spro->cycle_counter);
    for (i = 2; i <= 7; i++)
        fprintf(cycle_trace_fp, "r%d %08x\n", i, spro->r[i]);

    fprintf(cycle_trace_fp, "fetch0_active %08x\n", spro->fetch0_active);
    fprintf(cycle_trace_fp, "fetch0_pc %08x\n", spro->fetch0_pc);

    fprintf(cycle_trace_fp, "fetch1_active %08x\n", spro->fetch1_active);
    fprintf(cycle_trace_fp, "fetch1_pc %08x\n", spro->fetch1_pc);


    fprintf(cycle_trace_fp, "dec0_active %08x\n", spro->dec0_active);
    fprintf(cycle_trace_fp, "dec0_pc %08x\n", spro->dec0_pc);
    fprintf(cycle_trace_fp, "dec0_inst %08x\n", spro->dec0_inst);

    fprintf(cycle_trace_fp, "dec1_active %08x\n", spro->dec1_active);
    fprintf(cycle_trace_fp, "dec1_pc %08x\n", spro->dec1_pc);
    fprintf(cycle_trace_fp, "dec1_inst %08x\n", spro->dec1_inst);
    fprintf(cycle_trace_fp, "dec1_opcode %08x\n", spro->dec1_opcode);
    fprintf(cycle_trace_fp, "dec1_src0 %08x\n", spro->dec1_src0);
    fprintf(cycle_trace_fp, "dec1_src1 %08x\n", spro->dec1_src1);
    fprintf(cycle_trace_fp, "dec1_dst %08x\n", spro->dec1_dst);
    fprintf(cycle_trace_fp, "dec1_immediate %08x\n", spro->dec1_immediate);


    fprintf(cycle_trace_fp, "exec0_active %08x\n", spro->exec0_active);
    fprintf(cycle_trace_fp, "exec0_pc %08x\n", spro->exec0_pc);
    fprintf(cycle_trace_fp, "exec0_inst %08x\n", spro->exec0_inst);
    fprintf(cycle_trace_fp, "exec0_opcode %08x\n", spro->exec0_opcode);
    fprintf(cycle_trace_fp, "exec0_src0 %08x\n", spro->exec0_src0);
    fprintf(cycle_trace_fp, "exec0_src1 %08x\n", spro->exec0_src1);
    fprintf(cycle_trace_fp, "exec0_dst %08x\n", spro->exec0_dst);
    fprintf(cycle_trace_fp, "exec0_immediate %08x\n", spro->exec0_immediate);
    fprintf(cycle_trace_fp, "exec0_alu0 %08x\n", spro->exec0_alu0);
    fprintf(cycle_trace_fp, "exec0_alu1 %08x\n", spro->exec0_alu1);


    fprintf(cycle_trace_fp, "exec1_active %08x\n", spro->exec1_active);
    fprintf(cycle_trace_fp, "exec1_pc %08x\n", spro->exec1_pc);
    fprintf(cycle_trace_fp, "exec1_inst %08x\n", spro->exec1_inst);
    fprintf(cycle_trace_fp, "exec1_opcode %08x\n", spro->exec1_opcode);
    fprintf(cycle_trace_fp, "exec1_src0 %08x\n", spro->exec1_src0);
    fprintf(cycle_trace_fp, "exec1_src1 %08x\n", spro->exec1_src1);
    fprintf(cycle_trace_fp, "exec1_dst %08x\n", spro->exec1_dst);
    fprintf(cycle_trace_fp, "exec1_immediate %08x\n", spro->exec1_immediate);
    fprintf(cycle_trace_fp, "exec1_alu0 %08x\n", spro->exec1_alu0);
    fprintf(cycle_trace_fp, "exec1_alu1 %08x\n", spro->exec1_alu1);
    fprintf(cycle_trace_fp, "exec1_aluout %08x\n", spro->exec1_aluout);


    fprintf(cycle_trace_fp, "\n");
    fprintf(cycle_trace_fp, "\n");
    fprintf(cycle_trace_fp, "\n");

    // Update cycle counter
    sprn->cycle_counter = spro->cycle_counter + 1;

    // ------------------------------
// Stage EXEC1 (Write-back) — TRACE BEFORE COMMIT
// ------------------------------
    if (spro->exec1_active) {
        // Show actual ALU operands (already forwarded) in the EXEC line for ALU ops
        int use_ovr = 0, oA = 0, oB = 0;
        switch (spro->exec1_opcode) {
        case ADD: case SUB: case LSF: case RSF:
        case AND: case OR:  case XOR:
            use_ovr = 1;
            oA = spro->exec1_alu0;   // DEC1 applied forwarding before latching into EXEC0
            oB = spro->exec1_alu1;
            break;
        default:
            break; // LD/ST/branches/LHI keep their standard trace
        }

        // TRACE (pre-commit dump) + EXEC line with operand overrides for ALU ops
        trace_instruction(spro->exec1_inst, spro->exec1_pc, spro->r, use_ovr, oA, oB);

        // Now commit results to NEXT state
        switch (spro->exec1_opcode) {
        case ADD: case SUB: case LSF: case RSF:
        case AND: case OR:  case XOR: case LHI:
            if (spro->exec1_dst >= 2)
                sprn->r[spro->exec1_dst] = spro->exec1_aluout;
            break;

        case LD: {
            int wb_val = llsim_mem_extract_dataout(sp->sramd, 31, 0);
            if (spro->exec1_dst >= 2)
                sprn->r[spro->exec1_dst] = wb_val;
            break;
        }

        case ST:
            break;

        case JLT: {
            int taken = (spro->exec1_alu0 < spro->exec1_alu1);
            if (taken) sprn->r[7] = spro->exec1_pc;   // save FROM address
            break;
        }
        case JLE: {
            int taken = (spro->exec1_alu0 <= spro->exec1_alu1);
            if (taken) sprn->r[7] = spro->exec1_pc;
            break;
        }
        case JEQ: {
            int taken = (spro->exec1_alu0 == spro->exec1_alu1);
            if (taken) sprn->r[7] = spro->exec1_pc;
            break;
        }
        case JNE: {
            int taken = (spro->exec1_alu0 != spro->exec1_alu1);
            if (taken) sprn->r[7] = spro->exec1_pc;
            break;
        }
        case JIN:
            // indirect jump is always taken
            sprn->r[7] = spro->exec1_pc;
            break;


        case HLT:
            llsim_stop();
            dump_sram(sp, "srami_out.txt", sp->srami);
            dump_sram(sp, "sramd_out.txt", sp->sramd);
            break;
        }
    }




    // ------------------------------
    // Stage EXEC0 (ALU / MEM access)
    // IMPORTANT: Always promote EXEC0 -> EXEC1, even on stall.
    // The stall only prevents DEC1 -> EXEC0.
    // ------------------------------
// -------- Stage EXEC0 (ALU/MEM) -> EXEC1 (WB) --------
// Always advance EXEC0 to EXEC1, even on stall.
    sprn->exec1_active = spro->exec0_active;
    sprn->exec1_pc = spro->exec0_pc;
    sprn->exec1_inst = spro->exec0_inst;
    sprn->exec1_opcode = spro->exec0_opcode;
    sprn->exec1_src0 = spro->exec0_src0;
    sprn->exec1_src1 = spro->exec0_src1;
    sprn->exec1_dst = spro->exec0_dst;
    sprn->exec1_immediate = spro->exec0_immediate;
    sprn->exec1_alu0 = spro->exec0_alu0;
    sprn->exec1_alu1 = spro->exec0_alu1;
    sprn->exec1_aluout = spro->exec0_aluout; // may be overwritten below

    if (spro->exec0_active) {
        switch (spro->exec0_opcode) {
        case ADD: sprn->exec1_aluout = spro->exec0_alu0 + spro->exec0_alu1; break;
        case SUB: sprn->exec1_aluout = spro->exec0_alu0 - spro->exec0_alu1; break;
        case LSF: sprn->exec1_aluout = (int)((uint32_t)spro->exec0_alu0 << (spro->exec0_alu1 & 31)); break;
        case RSF: sprn->exec1_aluout = (int)((uint32_t)spro->exec0_alu0 >> (spro->exec0_alu1 & 31)); break;
        case AND: sprn->exec1_aluout = spro->exec0_alu0 & spro->exec0_alu1; break;
        case OR:  sprn->exec1_aluout = spro->exec0_alu0 | spro->exec0_alu1; break;
        case XOR: sprn->exec1_aluout = spro->exec0_alu0 ^ spro->exec0_alu1; break;
        case LHI:
            sprn->exec1_aluout = (((uint32_t)spro->exec0_immediate & 0xFFFF) << 16) |
                (spro->exec0_alu0 & 0xFFFF);
            break;
        case LD:
            // Issue a proper read for address in alu1 (R[src1])
            llsim_mem_read(sp->sramd, spro->exec0_alu1);
            break;

        case ST:
            // Write R[src0] (alu0) to MEM[R[src1]] (alu1)
            llsim_mem_set_datain(sp->sramd, spro->exec0_alu0, 31, 0);
            llsim_mem_write(sp->sramd, spro->exec0_alu1);
            break;

        }

    }
    detect_hazards(sp);

    /* Note: The stall handling that prevents DEC1 -> EXEC0 should be in the DEC1 block.
       Do NOT zero exec1 on stall; otherwise you’ll “skip” the instruction already in EXEC0. */


       // ------------------------------
       // Stage DEC1 (operand prep → EXEC0 latch)
       // ------------------------------
    if (!sp->stall) {
        if (sp->flush) {
            sprn->exec0_active = 0;
        }
        else {
            sprn->exec0_active = spro->dec1_active;
            sprn->exec0_pc = spro->dec1_pc;
            sprn->exec0_inst = spro->dec1_inst;
            sprn->exec0_opcode = spro->dec1_opcode;
            sprn->exec0_src0 = spro->dec1_src0;
            sprn->exec0_src1 = spro->dec1_src1;
            sprn->exec0_dst = spro->dec1_dst;
            sprn->exec0_immediate = spro->dec1_immediate;

            // Build fresh operands now (AFTER write-back of prior cycle and AFTER detect_hazards)
            int a0 = (spro->dec1_src0 == 0) ? 0 :
                (spro->dec1_src0 == 1) ? spro->dec1_immediate :
                spro->r[spro->dec1_src0];

            int a1 = (spro->dec1_src1 == 0) ? 0 :
                (spro->dec1_src1 == 1) ? spro->dec1_immediate :
                spro->r[spro->dec1_src1];

            // Apply existing forwarding decisions (already computed in detect_hazards)
            if (sp->forward_alu0) {
                sp_printf("DEC1 FW src0: %d -> %d\n", a0, sp->forward_value_alu0);
                a0 = sp->forward_value_alu0;
            }
            if (sp->forward_alu1) {
                sp_printf("DEC1 FW src1: %d -> %d\n", a1, sp->forward_value_alu1);
                a1 = sp->forward_value_alu1;
            }

            sprn->exec0_alu0 = a0;
            sprn->exec0_alu1 = a1;


        }
    }
    else {
        sprn->exec0_active = 0;
    }


    // ------------------------------
    // Stage DEC0 (decode + build operands)
    // ------------------------------
    if (!sp->stall) {
        if (sp->flush) {
            sprn->dec1_active = 0;
        }
        else {
            sprn->dec1_active = spro->dec0_active;
            sprn->dec1_pc = spro->dec0_pc;
            sprn->dec1_inst = spro->dec0_inst;

            if (spro->dec0_active) {
                sprn->dec1_opcode = (spro->dec0_inst >> 25) & 0x1F;
                sprn->dec1_dst = (spro->dec0_inst >> 22) & 0x07;
                sprn->dec1_src0 = (spro->dec0_inst >> 19) & 0x07;
                sprn->dec1_src1 = (spro->dec0_inst >> 16) & 0x07;
                sprn->dec1_immediate = (int16_t)(spro->dec0_inst & 0xFFFF);

                switch (sprn->dec1_opcode) {
                case LD:
                    // address from R[src1]
                    sprn->dec1_alu0 = 0;                               // unused
                    sprn->dec1_alu1 = spro->r[sprn->dec1_src1];        // <-- r2 here (may be stale; will be forwarded)
                    break;

                case ST:
                    // data = R[src0], address = R[src1]
                    sprn->dec1_alu0 = spro->r[sprn->dec1_src0];        // data
                    sprn->dec1_alu1 = spro->r[sprn->dec1_src1];        // address
                    break;

                case JLT: case JLE: case JEQ: case JNE:
                    sprn->dec1_alu0 = spro->r[sprn->dec1_src0];
                    sprn->dec1_alu1 = spro->r[sprn->dec1_src1];
                    break;

                case JIN:
                    sprn->dec1_alu0 = spro->r[sprn->dec1_dst];
                    sprn->dec1_alu1 = 0;
                    break;

                default:
                    // ALU ops: src==1 means “use immediate”
                    sprn->dec1_alu0 = (sprn->dec1_src0 == 0) ? 0 :
                        (sprn->dec1_src0 == 1) ? sprn->dec1_immediate :
                        spro->r[sprn->dec1_src0];
                    sprn->dec1_alu1 = (sprn->dec1_src1 == 0) ? 0 :
                        (sprn->dec1_src1 == 1) ? sprn->dec1_immediate :
                        spro->r[sprn->dec1_src1];
                    break;
                }
            }
        }
    }


    // ------------------------------
    // Stage FETCH1 (sample inst)
    // ------------------------------
    if (!sp->stall) {
        if (sp->flush) {
            sprn->dec0_active = 0;
        }
        else {
            sprn->dec0_active = spro->fetch1_active;
            sprn->dec0_pc = spro->fetch1_pc;
            sprn->dec0_inst = spro->fetch1_inst;
        }
    }
    else {
        // Stall: keep DEC0 latched
        sprn->dec0_active = spro->dec0_active;
        sprn->dec0_pc = spro->dec0_pc;
        sprn->dec0_inst = spro->dec0_inst;
    }

    // ------------------------------
    // Stage FETCH0 (issue fetch)
    // ------------------------------
    if (!sp->stall) {
        if (sp->flush) {
            sprn->fetch1_active = 0;
        }
        else {
            sprn->fetch1_active = spro->fetch0_active;
            sprn->fetch1_pc = spro->fetch0_pc;

            if (spro->fetch0_active) {
                sprn->fetch1_inst = llsim_mem_extract(sp->srami, spro->fetch0_pc, 31, 0);
            }
        }

        // Next PC
        int next_pc;
        if (spro->fetch0_active) {
            next_pc = sp->branch_taken ? sp->branch_target : (spro->fetch0_pc + 1);
        }
        else {
            next_pc = 0;  // Start from PC=0 if not active
        }

        sprn->fetch0_active = 1;
        sprn->fetch0_pc = next_pc;

        // Issue read for next instruction
        sp->srami->read = 1;
        sp->srami->read_addr = next_pc;
        if (spro->fetch0_active) {
            sp->srami->read = 1;
            sp->srami->read_addr = spro->fetch0_pc;
        }
        else {
            sp->srami->read = 0;
        }
    }
    else {
        // Stall: keep FETCH state
        sprn->fetch1_active = spro->fetch1_active;
        sprn->fetch1_pc = spro->fetch1_pc;
        sprn->fetch1_inst = spro->fetch1_inst;
        sprn->fetch0_active = spro->fetch0_active;
        sprn->fetch0_pc = spro->fetch0_pc;
    }

    // ------------------------------
    // DMA (unchanged)
    // ------------------------------
    if (spro->dma_busy && !spro->dma_done) {
        if (spro->dma_counter < spro->dma_len) {
            int data = llsim_mem_extract(sp->sramd, spro->dma_src_addr + spro->dma_counter, 31, 0);
            llsim_mem_inject(sp->sramd, spro->dma_dst_addr + spro->dma_counter, data, 31, 0);
            sprn->dma_counter = spro->dma_counter + 1;
        }
        else {
            sprn->dma_done = 1;
            sprn->dma_busy = 0;
        }
    }

    // ------------------------------
    // Cycle trace (debug)
    // ------------------------------


    sp_printf("cycle_counter %08x\n", spro->cycle_counter);
    sp_printf("r2 %08x, r3 %08x\n", spro->r[2], spro->r[3]);
    sp_printf("r4 %08x, r5 %08x, r6 %08x, r7 %08x\n", spro->r[4], spro->r[5], spro->r[6], spro->r[7]);
    sp_printf("fetch0_active %d, fetch1_active %d, dec0_active %d, dec1_active %d, exec0_active %d, exec1_active %d\n",
        spro->fetch0_active, spro->fetch1_active, spro->dec0_active, spro->dec1_active, spro->exec0_active, spro->exec1_active);
    sp_printf("fetch0_pc %d, fetch1_pc %d, dec0_pc %d, dec1_pc %d, exec0_pc %d, exec1_pc %d\n",
        spro->fetch0_pc, spro->fetch1_pc, spro->dec0_pc, spro->dec1_pc, spro->exec0_pc, spro->exec1_pc);
}

static void sp_run(llsim_unit_t* unit)
{
    sp_t* sp = (sp_t*)unit->private;

    if (llsim->reset) {
        sp_reset(sp);
        return;
    }

    sp->srami->read = 0;
    sp->srami->write = 0;
    sp->sramd->read = 0;
    sp->sramd->write = 0;

    sp_ctl(sp);
}

static void sp_generate_sram_memory_image(sp_t* sp, char* program_name)
{
    FILE* fp;
    int addr, i;

    fp = fopen(program_name, "r");
    if (fp == NULL) {
        printf("couldn't open file %s\n", program_name);
        exit(1);
    }
    addr = 0;
    while (addr < SP_SRAM_HEIGHT) {
        fscanf(fp, "%08x\n", &sp->memory_image[addr]);
        addr++;
        if (feof(fp))
            break;
    }
    sp->memory_image_size = addr;

    fprintf(inst_trace_fp, "program %s loaded, %d lines\n\n", program_name, addr);

    for (i = 0; i < sp->memory_image_size; i++) {
        llsim_mem_inject(sp->srami, i, sp->memory_image[i], 31, 0);
        // Initialize data memory with the same content for simplicity
        llsim_mem_inject(sp->sramd, i, sp->memory_image[i], 31, 0);
    }
}

void sp_init(char* program_name)
{
    llsim_unit_t* llsim_sp_unit;
    llsim_unit_registers_t* llsim_ur;
    sp_t* sp;

    llsim_printf("initializing sp unit\n");

    inst_trace_fp = fopen("inst_trace.txt", "w");
    if (inst_trace_fp == NULL) {
        printf("couldn't open file inst_trace.txt\n");
        exit(1);
    }

    cycle_trace_fp = fopen("cycle_trace.txt", "w");
    if (cycle_trace_fp == NULL) {
        printf("couldn't open file cycle_trace.txt\n");
        exit(1);
    }

    llsim_sp_unit = llsim_register_unit("sp", sp_run);
    llsim_ur = llsim_allocate_registers(llsim_sp_unit, "sp_registers", sizeof(sp_registers_t));
    sp = llsim_malloc(sizeof(sp_t));
    llsim_sp_unit->private = sp;
    sp->spro = llsim_ur->old;
    sp->sprn = llsim_ur->new;

    sp->srami = llsim_allocate_memory(llsim_sp_unit, "srami", 32, SP_SRAM_HEIGHT, 0);
    sp->sramd = llsim_allocate_memory(llsim_sp_unit, "sramd", 32, SP_SRAM_HEIGHT, 0);
    sp_generate_sram_memory_image(sp, program_name);

    sp->start = 1;

    // Initialize DMA registers
    sp->sprn->dma_busy = 0;
    sp->sprn->dma_done = 0;
    sp->sprn->dma_src_addr = 0;
    sp->sprn->dma_dst_addr = 0;
    sp->sprn->dma_counter = 0;
    sp->sprn->dma_len = 0;
}
