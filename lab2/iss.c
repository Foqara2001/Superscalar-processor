#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define MEM_SIZE 65536
#define REG_COUNT 8

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

uint32_t mem[MEM_SIZE];
int32_t reg[REG_COUNT] = {0};
uint32_t pc = 0;

FILE *trace_fp, *sram_fp;

void trace_instruction(uint32_t inst, uint32_t current_pc) {
    // Decode the instruction fields.
    uint8_t opcode = (inst >> 25) & 0x1F;
    uint8_t dst    = (inst >> 22) & 0x07;
    uint8_t src0   = (inst >> 19) & 0x07;
    uint8_t src1   = (inst >> 16) & 0x07;
    uint16_t imm   = inst & 0xFFFF;
    
    // Map opcode numbers to mnemonic strings.
    const char *op_str;
    switch (opcode) {
        case 0:  op_str = "ADD"; break;
        case 1:  op_str = "SUB"; break;
        case 2:  op_str = "LSF"; break;
        case 3:  op_str = "RSF"; break;
        case 4:  op_str = "AND"; break;
        case 5:  op_str = "OR";  break;
        case 6:  op_str = "XOR"; break;
        case 7:  op_str = "LHI"; break;
        case 8:  op_str = "LD";  break;
        case 9:  op_str = "ST";  break;
        case 16: op_str = "JLT"; break;
        case 17: op_str = "JLE"; break;
        case 18: op_str = "JEQ"; break;
        case 19: op_str = "JNE"; break;
        case 20: op_str = "JIN"; break;
        case 24: op_str = "HLT"; break;
        default: op_str = "UNK"; break;
    }
    
    // Print the instruction and its decoded fields.
    fprintf(trace_fp, "PC: %04x, INST: %08x, opcode = %d (%s), dst = %d, src0 = %d, src1 = %d, immediate = %08x\n",
            current_pc, inst, opcode, op_str, dst, src0, src1, imm);
    
    // Print registers in two lines.
    fprintf(trace_fp, "r[0] = %08x r[1] = %08x r[2] = %08x r[3] = %08x\n",
            reg[0], reg[1], reg[2], reg[3]);
    fprintf(trace_fp, "r[4] = %08x r[5] = %08x r[6] = %08x r[7] = %08x\n",
            reg[4], reg[5], reg[6], reg[7]);
}



void dump_sram() {
    for (int i = 0; i < MEM_SIZE; i++) {
        fprintf(sram_fp, "%08x\n", mem[i]);
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s code.bin\n", argv[0]);
        return 1;
    }

    // Load memory
    FILE *input_fp = fopen(argv[1], "r");
    if (!input_fp) {
        perror("Error opening input file");
        return 1;
    }

    char line[64];
    for (int i = 0; i < MEM_SIZE && fgets(line, sizeof(line), input_fp); i++) {
        sscanf(line, "%x", &mem[i]);
    }
    fclose(input_fp);

    trace_fp = fopen("trace.txt", "w");
    sram_fp = fopen("sram_out.txt", "w");

    uint8_t halted = 0;
    while (!halted && pc < MEM_SIZE) {
        uint32_t current_pc = pc;  // Save current PC for trace
        uint32_t inst = mem[pc++];

        uint8_t opcode = (inst >> 25) & 0x1F;
        uint8_t dst = (inst >> 22) & 0x07;
        uint8_t src0 = (inst >> 19) & 0x07;
        uint8_t src1 = (inst >> 16) & 0x07;
        int16_t imm = (int16_t)(inst & 0xFFFF);

        int32_t val0 = src0 == 0 ? 0 : (src0 == 1 ? imm : reg[src0]);
        int32_t val1 = src1 == 0 ? 0 : (src1 == 1 ? imm : reg[src1]);

        switch (opcode) {
            case ADD: reg[dst] = val0 + val1; break;
            case SUB: reg[dst] = val0 - val1; break;
            case LSF: reg[dst] = val0 << val1; break;
            case RSF: reg[dst] = val0 >> val1; break;
            case AND: reg[dst] = val0 & val1; break;
            case OR:  reg[dst] = val0 | val1; break;
            case XOR: reg[dst] = val0 ^ val1; break;
            case LHI: reg[dst] = (val1 << 16) | (val0 & 0xFFFF); break;
            case LD:  reg[dst] = mem[val1 & 0xFFFF]; break;
            case ST:  mem[val1 & 0xFFFF] = reg[src0]; break;

            case JLT:
                if (val0 < val1) {
                    reg[7] = pc;  // Save pc into r7
                    pc = imm & 0xFFFF;
                }
                break;
            case JLE:
                if (val0 <= val1) {
                    reg[7] = pc;  // Save pc into r7
                    pc = imm & 0xFFFF;
                }
                break;
            case JEQ:
                if (val0 == val1) {
                    reg[7] = pc;  // Save pc into r7
                    pc = imm & 0xFFFF;
                }
                break;
            case JNE:
                if (val0 != val1) {
                    reg[7] = pc;  // Save pc into r7
                    pc = imm & 0xFFFF;
                }
                break;
            case JIN:
                reg[7] = pc;  // Save pc from reg[src0] into r7
                pc = reg[src0] & 0xFFFF;
                break;

            case HLT:
                halted = 1;
                break;

            default:
                break; // undefined op: do nothing
        }

        // Use the saved PC for correct trace output
        trace_instruction(inst, current_pc);
    }

    dump_sram();
    fclose(trace_fp);
    fclose(sram_fp);
    return 0;
}
