/*
 * SP ASM: DMA Background Copy Test
 *
 * This program tests the DMA engine by copying 4 words from [100..103] to [200..203]
 * It then polls the DMA status and halts once the copy completes.
 */
#include <stdio.h>
#include <stdlib.h>

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
#define MEM_MASK       (MEM_SIZE - 1)

unsigned int mem[MEM_SIZE];
int pc = 0;

static void asm_cmd(int opcode, int dst, int src0, int src1, int immediate)
{
    int inst = ((opcode & 0x1F) << 25)  \
             | ((dst     & 0x07) << 22)  \
             | ((src0    & 0x07) << 19)  \
             | ((src1    & 0x07) << 16)  \
             | (immediate & 0xFFFF);
    mem[pc++] = inst;
}

static void assemble_program(char* program_name)
{
    FILE* fp;
    int addr, last_addr;

    // clear memory
    for (addr = 0; addr < MEM_SIZE; addr++)
        mem[addr] = 0;
    pc = 0;

    // 0: r2 = 100 (source base)
    asm_cmd(ADD, 2, 1, 0, 100);
    // 1: r3 = 200 (destination base)
    asm_cmd(ADD, 3, 1, 0, 200);
    // 2: kick off DMA copy of 4 words
    asm_cmd(DMA_START, 0, 2, 3, 4);
    // 3: poll DMA status into r4
    asm_cmd(DMA_STATUS, 4, 0, 0, 0);
    // 4: if r4 != 0, jump back to instruction 3
    asm_cmd(JNE,       0, 0, 4, 3);
    // 5: halt when done
    asm_cmd(HLT,       0, 0, 0, 0);

    // initialize source data at addresses 100..103
    mem[100] = 0xDEADBEEF;
    mem[101] = 0xCAFEBABE;
    mem[102] = 0x12345678;
    mem[103] = 0x87654321;

    // write out memory image up to address 204 (cover [200..203])
    last_addr = 204;
    fp = fopen(program_name, "w");
    if (!fp) {
        printf("couldn't open file %s\n", program_name);
        exit(1);
    }
    for (addr = 0; addr < last_addr; addr++) {
        fprintf(fp, "%08x\n", mem[addr] & 0xFFFFFFFF);
    }
    fclose(fp);
}

int main(int argc, char* argv[])
{
    if (argc != 2) {
        printf("usage: asm program_name\n");
        return -1;
    }
    assemble_program(argv[1]);
    printf("SP assembler generated %s (DMA test)\n", argv[1]);
    return 0;
}
