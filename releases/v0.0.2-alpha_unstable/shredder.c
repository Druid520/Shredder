#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

 // Configuration & Constants
#define MEMORY_SIZE       65536U    /* 64K unified memory */
#define STACK_SIZE        64U       /* 64-level call stack per spec */
#define MAX_INSTRUCTIONS  1000000U  /* Infinite loop protection */
#define MAX_FILENAME_LEN  256U

// Core Opcodes (0x00-0x0F) 
#define OP_NOP      0x00
#define OP_POKE     0x01
#define OP_MOVE     0x02
#define OP_NOT      0x03
#define OP_NAND     0x04
#define OP_JMP      0x05
#define OP_JZ       0x06
#define OP_RUN      0x07
#define OP_HALT     0x08
#define OP_AND      0x09
#define OP_OR       0x0A
#define OP_XOR      0x0B
#define OP_INC      0x0C
#define OP_DEC      0x0D
#define OP_CMP      0x0E
#define OP_COMMENT  0x0F

// I/O Opcodes (0x10-0x13)
#define OP_PUTC     0x10
#define OP_PUTN     0x11
#define OP_GETC     0x12
#define OP_RET      0x13

// Arithmetic & Shift Opcodes (0x14-0x19) 
#define OP_ADD      0x14
#define OP_SUB      0x15
#define OP_MUL      0x16
#define OP_DIV      0x17
#define OP_SHL      0x18
#define OP_SHR      0x19

// 16-bit Addressing Opcodes (0x1A-0x1E)
#define OP_POKE16   0x1A
#define OP_MOVE16   0x1B
#define OP_JMP16    0x1C
#define OP_JZ16     0x1D
#define OP_RUN16    0x1E

// Stack Operations (0x1F-0x20)
#define OP_PUSH     0x1F
#define OP_POP      0x20

// Boolean Operations (0x21-0x24)
#define OP_BAND     0x21
#define OP_BOR      0x22
#define OP_BXOR     0x23
#define OP_BNOT     0x24

// Comparison Operations (0x25-0x26)
#define OP_LT       0x25
#define OP_GT       0x26

// Additional Boolean Operation (0x27)
#define OP_BNAND    0x27

// 16-bit Stack Operations (0x28-0x29)
#define OP_PUSH16_MEM   0x28
#define OP_POP16_MEM    0x29

// 16-bit Boolean Operations (0x2A-0x2E)
#define OP_BAND16   0x2A
#define OP_BOR16    0x2B
#define OP_BXOR16   0x2C
#define OP_BNOT16   0x2D
#define OP_BNAND16  0x2E

// 16-bit Comparison Operations (0x2F-0x30)
#define OP_LT16_CMP     0x2F
#define OP_GT16_CMP     0x30

// Control Flow Operations (0x31-0x36)
#define OP_IF       0x31
#define OP_THEN     0x32
#define OP_ELSE     0x33
#define OP_ELSEIF   0x34
#define OP_FOR      0x35
#define OP_ERROR    0x36
//TODO: finish defining and actually code
