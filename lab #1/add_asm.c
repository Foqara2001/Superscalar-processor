/*
 * SP ASM: Sign Magnitude Addition
 *
 * This program adds two numbers in sign magnitude format
 * Input numbers are at memory locations 1000 and 1001
 * Output is stored at memory location 1002
 */
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
    for (addr = 0; addr < MEM_SIZE; addr++)
        mem[addr] = 0;
    pc = 0;


     // === Initialize constants ===
     asm_cmd(LHI, 5, 1, 0, 0x8000);                                 // 0x00
     asm_cmd(ADD, 5, 5, 1, 0);                                      // 0x01

     // === Load operand A from memory[1000] into r2 ===
     asm_cmd(LHI, 6, 1, 0, 0);                                     // 0x02
     asm_cmd(ADD, 6, 6, 1, 1000);                                   // 0x03
     asm_cmd(LD, 2, 0, 6, 0); // r2 = operand A                     // 0x04

     // === Load operand B from memory[1001] into r3 ===
     asm_cmd(LHI, 6, 1, 0, 0);                                     // 0x05
     asm_cmd(ADD, 6, 0, 1, 1001);                                   // 0x06
     asm_cmd(LD, 3, 0, 6, 0); // r3 = operand B                     // 0x07

     // === Extract signs ===
     asm_cmd(AND, 6, 2, 5, 0); // r6 = sign of A                    // 0x08
     asm_cmd(AND, 7, 3, 5, 0); // r7 = sign of B                    // 0x09

     // === If same sign, jump to ADD_CASE ===
     asm_cmd(SUB, 4, 6, 7, 0);                                     // 0x0A
     asm_cmd(JEQ, 0, 4, 0,pc+19); // if signs equal, jump to ADD_CASE // 0x0B

     // === Get magnitude of A ===
     asm_cmd(JEQ, 0, 6, 0, pc+3); // if A is positive, skip SUB     // 0x0C
     asm_cmd(SUB, 6, 2, 5, 0); // r6 = magA                         // 0x0D
     asm_cmd(JEQ, 0, 0, 0, pc+2);                                   // 0x0E
     asm_cmd(ADD, 6, 2, 1, 0); // r6 = magA (if positive)           // 0x0F

     // === Get magnitude of B ===
     asm_cmd(JEQ, 0, 7, 0, pc+3); // if B is positive, skip SUB     // 0x10
     asm_cmd(SUB, 7, 3, 5, 0); // r7 = magB                         // 0x11
     asm_cmd(JEQ, 0, 0, 0, pc+2);                                  // 0x12
     asm_cmd(ADD, 7, 3, 1, 0); // r7 = magB (if positive)           // 0x13

     // === Compare magnitudes ===
     asm_cmd(SUB, 4, 4, 0, 0); // r4 = magA - magB                  // 0x14
     asm_cmd(JLT, 0, 6, 7, pc+5); // if magA < magB -> B - A        // 0x15
     asm_cmd(SUB, 4, 6, 7, 0); // result = A - B                    // 0x16
     asm_cmd(AND,5,2,5,0);//extract sign of A                       // 0x17
     asm_cmd(ADD, 4, 4, 5, 0); // set sign                         // 0x18
     asm_cmd(JEQ, 0, 0, 0,pc+17); // jump FINISH                    // 0x19

     // === B - A case ===
     asm_cmd(SUB, 4, 7, 6, 0);                                     // 0x1A
     asm_cmd(AND,5,3,5,0);//extract sign of B                      // 0x1B
     asm_cmd(ADD, 4, 4, 5, 0); // set sign                         // 0x1C
     asm_cmd(JEQ, 0, 0, 0,pc+13); // jump FINISH                    // 0x1D

     // === ADD_CASE: signs are equal ===
     // reuse r6 and r7 to hold magnitudes
     asm_cmd(JEQ, 0, 6, 0,pc+3); // if A is positive               // 0x1E
     asm_cmd(SUB, 6, 2, 5, 0); // r6 = magA                         // 0x1F
     asm_cmd(JEQ, 0, 0, 0,pc+2);                                   // 0x20
     asm_cmd(ADD, 6, 2, 1, 0); // r6 = magA                         // 0x21

     asm_cmd(JEQ, 0, 7, 0,pc+3); // if B is positive               // 0x22
     asm_cmd(SUB, 7, 3, 5, 0); // r7 = magB                         // 0x23
     asm_cmd(JEQ, 0, 0, 0,pc+2);                                   // 0x24
     asm_cmd(ADD, 7, 3, 1, 0); // r7 = magB                         // 0x25

     asm_cmd(ADD, 4, 6, 7, 0); // result = magA + magB             // 0x26
     asm_cmd(JEQ, 0, 6, 0,pc+2); // if A was negative              // 0x27
     // === Extract signs again ===
     asm_cmd(AND, 7, 2, 5, 0); // r6 = sign of A                    // 0x28
     asm_cmd(ADD, 4, 4, 7, 0); // set sign                         // 0x29

     // === FINISH: store result ===
     asm_cmd(LHI, 6, 0, 0, 0);                                     // 0x2A
     asm_cmd(ADD, 6, 0, 1, 1002);                                   // 0x2B
     asm_cmd(ST, 0, 4, 6, 0); // memory[1002] = result             // 0x2C
     asm_cmd(HLT, 0, 0, 0, 0);                                     // 0x2D


    mem[1000] = 0x00000120; 
    mem[1001] = 0x80000140;
    
    last_addr = pc + 3;  // Add a few more addresses to include our test data
    fp = fopen(program_name, "w");
    if (fp == NULL) {
        printf("couldn't open file %s\n", program_name);
        exit(1);
    }
    addr = 0;
    // Write the memory locations explicitly
    while (addr < 1003) {
        fprintf(fp, "%08x\n", mem[addr]);
        addr++;
    }
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