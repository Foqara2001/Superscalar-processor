/*
 * SP ASM: DMA Background Overlap Copy Test
 *
 * Copies 50 words from address 50 → 60 (overlapping), verifies copy, sets r2=1 on pass, r2=0 on fail.
 */
#include <stdio.h>


#define ADD         0
#define SUB         1
#define LSF         2
#define RSF         3
#define AND         4
#define OR          5
#define XOR         6
#define LHI         7
#define LD          8
#define ST          9
#define DMA_START   10
#define DMA_STATUS  11
#define JLT         16
#define JLE         17
#define JEQ         18
#define JNE         19
#define JIN         20
#define HLT         24

#define MEM_SIZE_BITS  (16)
#define MEM_SIZE       (1 << MEM_SIZE_BITS)
unsigned int mem[MEM_SIZE];
int pc = 0;

// Emit an instruction word into mem[]
static void asm_cmd(int opcode, int dst, int src0, int src1, int imm) {
    int inst = ((opcode & 0x1F) << 25)
             | ((dst     & 0x07) << 22)
             | ((src0    & 0x07) << 19)
             | ((src1    & 0x07) << 16)
             | (imm       & 0xFFFF);
    mem[pc++] = inst;
}

static void assemble_program(const char *outname) {
    int i;

    // 1) Initialize memory and test pattern
    for (i = 0; i < MEM_SIZE; i++) mem[i] = 0;
    for (i = 0; i < 50; i++) mem[50 + i] = 0x100 + i;  // pattern at 50..99

    // 2) Set r2=50, r3=60, r4=50
    asm_cmd(LHI, 2, 1, 0, 0);
    asm_cmd(ADD, 2, 2, 1, 50);
    asm_cmd(LHI, 3, 1, 0, 0);
    asm_cmd(ADD, 3, 3, 1, 60);
    asm_cmd(LHI, 4, 1, 0, 0);
    asm_cmd(ADD, 4, 4, 1, 50);

    // 3) Start overlapping DMA 50→60
    asm_cmd(DMA_START, 0, 2, 3, 50);

    // 4) Poll until DMA finishes
    asm_cmd(DMA_STATUS, 5, 0, 0, 0);
    asm_cmd(JNE,        0, 0, 5, 5);  // jump back to DMA_STATUS at PC=5

    // 5) Verify: for i=0..49 check mem[60+i] == mem[50+i]
    // r6 = i index
    asm_cmd(LHI, 6, 1, 0, 0);            // r6 = 0

    // VERIFY_LOOP at PC=8
    asm_cmd(ADD, 7, 6, 1, 50);           // r7 = 50 + i
    asm_cmd(LD,  7, 0, 7, 0);            // r7 = mem[50+i]
    asm_cmd(ADD, 8, 6, 1, 60);           // r8 = 60 + i
    asm_cmd(LD,  8, 0, 8, 0);            // r8 = mem[60+i]
    asm_cmd(SUB, 9, 7, 8, 0);            // r9 = r7 - r8
    asm_cmd(JNE,  0, 9, 0, 18);          // on mismatch jump to FAIL at PC=18

    asm_cmd(ADD, 6, 6, 1, 1);            // i++
    asm_cmd(JLT,  0, 6, 4, 8);           // if i<50, loop back

    // PASS: set r2=1 and halt (PC=17)
    asm_cmd(ADD, 2, 1, 0, 1);
    asm_cmd(HLT,  0, 0, 0, 0);

    // FAIL: set r2=0 and halt (PC=18)
    asm_cmd(ADD, 2, 1, 0, 0);
    asm_cmd(HLT,  0, 0, 0, 0);

    // Dump codes + data up to address 110
    FILE *fp = fopen(outname, "w");
    for (i = 0; i <= 110; i++)
        fprintf(fp, "%08x\n", mem[i]);
    fclose(fp);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "usage: asm <output_sram.txt>\n");
        return 1;
    }
    assemble_program(argv[1]);
    return 0;
}
