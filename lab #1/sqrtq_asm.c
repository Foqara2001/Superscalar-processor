/*
 * SP ASM: Integer Square Root Calculator (FIXED VERSION)
 *
 * This program calculates the integer square root of a number
 * Input number is at memory location 1000
 * Output is stored at memory location 1001
 */
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>

#define ADD 0
#define SUB 1
#define LSF 2
#define RSF 3
#define AND 4
#define OR  5
#define XOR 6
#define LHI 7
#define LD 8
#define ST 9
#define JLT 16
#define JLE 17
#define JEQ 18
#define JNE 19
#define JIN 20
#define HLT 24

#define MEM_SIZE_BITS	(16)
#define MEM_SIZE	(1 << MEM_SIZE_BITS)
#define MEM_MASK	(MEM_SIZE - 1)

unsigned int mem[MEM_SIZE];
int pc = 0;

static void asm_cmd(int opcode, int dst, int src0, int src1, int immediate)
{
    int inst;
    inst = ((opcode & 0x1f) << 25) | ((dst & 7) << 22) | ((src0 & 7) << 19) | ((src1 & 7) << 16) | (immediate & 0xffff);
    mem[pc++] = inst;
}

static void assemble_program(char* program_name)
{
    FILE* fp;
    int addr, last_addr;

    // Clear memory and reset program counter
    for (addr = 0; addr < MEM_SIZE; addr++)
        mem[addr] = 0;

    pc = 0;

    // Step 1: Load input N from memory[1000] into r2
    asm_cmd(LD, 2, 0, 1, 1000);  // r2 = N

    // Handle special case: if N == 0, result = 0
    asm_cmd(JEQ, 0, 2, 0, pc + 20); // if N == 0, jump to store result

    // Step 2: Initialize result = 0 in r4
    asm_cmd(ADD, 4, 0, 0, 0);

    // Step 3: Initialize bit = 1 in r3
    asm_cmd(ADD, 3, 1, 0, 1);

    // ---------- Bitloop: Find highest power of 4 <= N ----------
    // Fixed: Use safety counter to prevent infinite loops
    asm_cmd(ADD, 7, 1, 0, 16);   // r7 = safety counter (16 bits max)

    int bitloop_start = pc;
    asm_cmd(LSF, 5, 3, 1, 2);     // r5 = bit * 4
    asm_cmd(JLT, 0, 5, 2, pc + 4); // if (bit*4) <= N, continue shifting
    asm_cmd(JEQ, 0, 0, 0, pc + 5); // else jump out of loop

    asm_cmd(LSF, 3, 3, 1, 1);     // bit <<= 1
    asm_cmd(SUB, 7, 7, 1, 1);     // decrement safety counter
    asm_cmd(JNE, 0, 7, 0, bitloop_start); // loop if counter != 0

    // ---------- Mainloop ----------
    int mainloop_addr = pc;
    asm_cmd(JEQ, 0, 3, 0, pc + 12);  // if bit == 0 → done

    asm_cmd(ADD, 6, 4, 3, 0);         // r6 = result + bit
    asm_cmd(SUB, 6, 2, 6, 0);         // r6 = N - (res + bit)
    asm_cmd(JLT, 0, 6, 0, pc + 6);    // if N < res + bit → jump to else

    // if branch: result = (res >> 1) + bit, N -= result + bit
    asm_cmd(SUB, 2, 2, 4, 0);         // N -= result
    asm_cmd(SUB, 2, 2, 3, 0);         // N -= bit
    asm_cmd(RSF, 4, 4, 1, 1);         // result >>= 1
    asm_cmd(ADD, 4, 4, 3, 0);         // result += bit
    asm_cmd(JEQ, 0, 0, 0, pc + 2);    // skip else

    // else block
    asm_cmd(RSF, 4, 4, 1, 1);         // result >>= 1

    // shift bit
    asm_cmd(RSF, 3, 3, 1, 2);         // bit >>= 2

    // jump to mainloop
    asm_cmd(JEQ, 0, 0, 0, mainloop_addr);

    // ---------- Done ----------
    asm_cmd(ST, 0, 4, 1, 1001);       // store result to mem[1001]
    asm_cmd(HLT, 0, 0, 0, 0);         // halt

    // Set test input value
    mem[1000] = 3000;  // Test input
    mem[1001] = 0;     // Clear output

    // Output the memory to file
    last_addr = 1002;
    fp = fopen(program_name, "w");
    if (fp == NULL) {
        printf("couldn't open file %s\n", program_name);
        exit(1);
    }

    for (addr = 0; addr <= last_addr; addr++) {
        fprintf(fp, "%08x\n", mem[addr]);
    }

    fclose(fp);
}

int main(int argc, char* argv[])
{
    if (argc != 2) {
        printf("usage: asm program_name\n");
        return -1;
    }
    else {
        assemble_program(argv[1]);
        printf("SP assembler generated machine code and saved it as %s\n", argv[1]);
        return 0;
    }
}