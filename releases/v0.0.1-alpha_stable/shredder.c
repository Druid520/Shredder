// NOTE: improve memory stack, clean up repeats, do/includ DRY, and clean up code overall
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

 // Configuration & Constants
#define MEMORY_SIZE       65536U    
#define STACK_SIZE        64U       
#define MAX_INSTRUCTIONS  1000000U  
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

 // Global VM State
static uint8_t  memory[MEMORY_SIZE];        // Unified 64K memory 
static uint16_t call_stack[STACK_SIZE];     // 16-bit return addresses 
static uint16_t stack_pointer = 0;          // Stack pointer 
static uint8_t  overflow_flag = 0;          // Arithmetic overflow flag 
static uint32_t instruction_count = 0;      // Instruction counter 
static int      debug_mode = 0;
static int      trace_mode = 0;

 // Helper: Check operand availability
 // Returns 1 if ip + needed <= MEMORY_SIZE
static int ensure_operands(uint32_t ip, uint32_t needed) {
    if (ip >= MEMORY_SIZE) return 0;
    if (needed > MEMORY_SIZE) return 0;
    return (ip + needed) <= MEMORY_SIZE;
}

 // Helper: Validate 16-bit address
static int is_valid_address(uint32_t addr) {
    return addr < MEMORY_SIZE;
}

 // Stack Operations
 static int push_stack(uint16_t return_addr) {
    if (stack_pointer >= STACK_SIZE) {
        fprintf(stderr, "CPU Fault: Stack overflow (max depth: %u) at instruction %u\n",
                (unsigned)STACK_SIZE, (unsigned)instruction_count);
        return 0;
    }
    call_stack[stack_pointer++] = return_addr;
    if (trace_mode) {
        printf("  [STACK] Push 0x%04X (SP=%u)\n", return_addr, (unsigned)stack_pointer);
    }
    return 1;
}

static int pop_stack(uint16_t *out_addr) {
    if (stack_pointer == 0) {
        fprintf(stderr, "CPU Fault: Stack underflow at instruction %u\n",
                (unsigned)instruction_count);
        return 0;
    }
    *out_addr = call_stack[--stack_pointer];
    if (trace_mode) {
        printf("  [STACK] Pop 0x%04X (SP=%u)\n", *out_addr, (unsigned)stack_pointer);
    }
    return 1;
}

 // Load Program from .shred file
 // Returns 0 on success, -1 on error
static int load_program(const char *filename) {
    if (!filename || strlen(filename) >= MAX_FILENAME_LEN) {
        fprintf(stderr, "Error: Invalid filename\n");
        return -1;
    }

    FILE *file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "Error: Cannot open file '%s'\n", filename);
        perror("fopen");
        return -1;
    }

    memset(memory, 0, MEMORY_SIZE);

    uint32_t addr = 0;
    int ch;
    int in_comment = 0;
    char hex_buf[3] = {0};
    int hex_pos = 0;
    uint32_t line = 1, col = 0;

    while ((ch = fgetc(file)) != EOF) {
        col++;

        // Handle comments 
        if (ch == ';' || ch == '#') {
            in_comment = 1;
            continue;
        }
        if (ch == '\n') {
            in_comment = 0;
            line++;
            col = 0;
            continue;
        }
        if (in_comment) continue;

        // skip whitespace
        if (isspace((unsigned char)ch)) continue;

        // process hex digits
        if (isxdigit((unsigned char)ch)) {
            hex_buf[hex_pos++] = (char)ch;

            if (hex_pos == 2) {
                hex_buf[2] = '\0';
                unsigned int byte_val = 0;
                if (sscanf(hex_buf, "%x", &byte_val) != 1 || byte_val > 0xFF) {
                    fprintf(stderr, "Error: Invalid hex '%s' at line %u, col %u\n",
                            hex_buf, (unsigned)line, (unsigned)col);
                    fclose(file);
                    return -1;
                }
                if (addr >= MEMORY_SIZE) {
                    fprintf(stderr, "Warning: Memory full at %u bytes, truncating\n",
                            (unsigned)MEMORY_SIZE);
                    break;
                }
                memory[addr++] = (uint8_t)byte_val;
                hex_pos = 0;
            }
        } else {
            fprintf(stderr, "Error: Invalid character 0x%02X at line %u, col %u\n",
                    (unsigned char)ch, (unsigned)line, (unsigned)col);
            fclose(file);
            return -1;
        }
    }

    fclose(file);

    if (hex_pos != 0) {
        fprintf(stderr, "Error: Incomplete hex byte at end of file\n");
        return -1;
    }

    if (debug_mode) {
        printf("Loaded %u bytes (0x%04X) from '%s'\n", (unsigned)addr, (unsigned)addr, filename);
    }

    return 0;
}

 // debug: print current instruction n stuf
static void debug_instruction(uint32_t ip, uint8_t opcode) {
    if (!debug_mode && !trace_mode) return;
    if (ip >= MEMORY_SIZE) {
        printf("[%04X] <OUT OF BOUNDS>\n", (unsigned)ip);
        return;
    }

    printf("[%04X] ", (unsigned)ip);
    uint32_t avail = MEMORY_SIZE - ip;
// possibly the worst code ive written ever {down arrow}
    switch (opcode) {
        case OP_NOP:     printf("NOP\n"); break;
        case OP_POKE:
            if (avail >= 3) printf("POKE [%02X] <- %02X\n", memory[ip+1], memory[ip+2]);
            else printf("POKE <truncated>\n");
            break;
        case OP_MOVE:
            if (avail >= 3) printf("MOVE [%02X] -> [%02X]\n", memory[ip+1], memory[ip+2]);
            else printf("MOVE <truncated>\n");
            break;
        case OP_NOT:
            if (avail >= 2) printf("NOT [%02X]\n", memory[ip+1]);
            else printf("NOT <truncated>\n");
            break;
        case OP_NAND:
            if (avail >= 4) printf("NAND [%02X] [%02X] -> [%02X]\n", memory[ip+1], memory[ip+2], memory[ip+3]);
            else printf("NAND <truncated>\n");
            break;
        case OP_JMP:
            if (avail >= 2) printf("JMP %02X\n", memory[ip+1]);
            else printf("JMP <truncated>\n");
            break;
        case OP_JZ:
            if (avail >= 3) printf("JZ %02X if [%02X]==0\n", memory[ip+1], memory[ip+2]);
            else printf("JZ <truncated>\n");
            break;
        case OP_RUN:
            if (avail >= 2) printf("RUN %02X\n", memory[ip+1]);
            else printf("RUN <truncated>\n");
            break;
        case OP_HALT:    printf("HALT\n"); break;
        case OP_AND:
            if (avail >= 4) printf("AND [%02X] [%02X] -> [%02X]\n", memory[ip+1], memory[ip+2], memory[ip+3]);
            else printf("AND <truncated>\n");
            break;
        case OP_OR:
            if (avail >= 4) printf("OR [%02X] [%02X] -> [%02X]\n", memory[ip+1], memory[ip+2], memory[ip+3]);
            else printf("OR <truncated>\n");
            break;
        case OP_XOR:
            if (avail >= 4) printf("XOR [%02X] [%02X] -> [%02X]\n", memory[ip+1], memory[ip+2], memory[ip+3]);
            else printf("XOR <truncated>\n");
            break;
        case OP_INC:
            if (avail >= 2) printf("INC [%02X]\n", memory[ip+1]);
            else printf("INC <truncated>\n");
            break;
        case OP_DEC:
            if (avail >= 2) printf("DEC [%02X]\n", memory[ip+1]);
            else printf("DEC <truncated>\n");
            break;
        case OP_CMP:
            if (avail >= 4) printf("CMP [%02X] [%02X] -> [%02X]\n", memory[ip+1], memory[ip+2], memory[ip+3]);
            else printf("CMP <truncated>\n");
            break;
        case OP_COMMENT:
            if (avail >= 2) printf("COMMENT (len=%u)\n", (unsigned)memory[ip+1]);
            else printf("COMMENT <truncated>\n");
            break;
        case OP_PUTC:
            if (avail >= 2) printf("PUTC [%02X]\n", memory[ip+1]);
            else printf("PUTC <truncated>\n");
            break;
        case OP_PUTN:
            if (avail >= 2) printf("PUTN [%02X]\n", memory[ip+1]);
            else printf("PUTN <truncated>\n");
            break;
        case OP_GETC:
            if (avail >= 2) printf("GETC -> [%02X]\n", memory[ip+1]);
            else printf("GETC <truncated>\n");
            break;
        case OP_RET:     printf("RET\n"); break;
        case OP_ADD:
            if (avail >= 4) printf("ADD [%02X] [%02X] -> [%02X]\n", memory[ip+1], memory[ip+2], memory[ip+3]);
            else printf("ADD <truncated>\n");
            break;
        case OP_SUB:
            if (avail >= 4) printf("SUB [%02X] [%02X] -> [%02X]\n", memory[ip+1], memory[ip+2], memory[ip+3]);
            else printf("SUB <truncated>\n");
            break;
        case OP_MUL:
            if (avail >= 4) printf("MUL [%02X] [%02X] -> [%02X]\n", memory[ip+1], memory[ip+2], memory[ip+3]);
            else printf("MUL <truncated>\n");
            break;
        case OP_DIV:
            if (avail >= 4) printf("DIV [%02X] [%02X] -> [%02X]\n", memory[ip+1], memory[ip+2], memory[ip+3]);
            else printf("DIV <truncated>\n");
            break;
        case OP_SHL:
            if (avail >= 4) printf("SHL [%02X] [%02X] -> [%02X]\n", memory[ip+1], memory[ip+2], memory[ip+3]);
            else printf("SHL <truncated>\n");
            break;
        case OP_SHR:
            if (avail >= 4) printf("SHR [%02X] [%02X] -> [%02X]\n", memory[ip+1], memory[ip+2], memory[ip+3]);
            else printf("SHR <truncated>\n");
            break;
        case OP_POKE16:
            if (avail >= 4) printf("POKE16 [%02X%02X] <- %02X\n", memory[ip+1], memory[ip+2], memory[ip+3]);
            else printf("POKE16 <truncated>\n");
            break;
        case OP_MOVE16:
            if (avail >= 5) printf("MOVE16 [%02X%02X] -> [%02X%02X]\n", memory[ip+1], memory[ip+2], memory[ip+3], memory[ip+4]);
            else printf("MOVE16 <truncated>\n");
            break;
        case OP_JMP16:
            if (avail >= 3) printf("JMP16 %02X%02X\n", memory[ip+1], memory[ip+2]);
            else printf("JMP16 <truncated>\n");
            break;
        case OP_JZ16:
            if (avail >= 4) printf("JZ16 %02X%02X if [%02X]==0\n", memory[ip+1], memory[ip+2], memory[ip+3]);
            else printf("JZ16 <truncated>\n");
            break;
        case OP_RUN16:
            if (avail >= 3) printf("RUN16 %02X%02X\n", memory[ip+1], memory[ip+2]);
            else printf("RUN16 <truncated>\n");
            break;
        default:
            printf("UNKNOWN 0x%02X\n", opcode);
            break;
    }
}

 // mem/addr Dump
static void dump_memory(uint32_t start, uint32_t end) {
    if (start >= MEMORY_SIZE) start = 0;
    if (end >= MEMORY_SIZE) end = MEMORY_SIZE - 1;
    if (start > end) {
        uint32_t tmp = start; start = end; end = tmp;
    }

    printf("\n--- Memory Dump (0x%04X-0x%04X) ---\n", (unsigned)start, (unsigned)end);
    for (uint32_t i = start; i <= end; i++) {
        if ((i - start) % 16 == 0) printf("\n%04X: ", (unsigned)i);
        printf("%02X ", memory[i]);
    }
    printf("\n");
}

 // da engine
static void execute(uint16_t start_addr) {
    if (!is_valid_address(start_addr)) {
        fprintf(stderr, "Error: Start address 0x%04X out of bounds\n", start_addr);
        return;
    }

    uint32_t ip = start_addr;
    int running = 1;

    while (running) {
        // instruction limit check
        if (++instruction_count > MAX_INSTRUCTIONS) {
            fprintf(stderr, "CPU Fault: Instruction limit exceeded (%u), possible infinite loop\n",
                    (unsigned)MAX_INSTRUCTIONS);
            return;
        }

        // bounds check
        if (!is_valid_address(ip)) {
            fprintf(stderr, "CPU Fault: IP 0x%04X out of bounds\n", (unsigned)ip);
            return;
        }

        uint8_t opcode = memory[ip];
        debug_instruction(ip, opcode);

        switch (opcode) {
            case OP_NOP:
                ip += 1;
                break;

            case OP_POKE: {
                if (!ensure_operands(ip, 3)) {
                    fprintf(stderr, "CPU Fault: POKE truncated at 0x%04X\n", (unsigned)ip);
                    running = 0; break;
                }
                uint8_t addr = memory[ip + 1];
                uint8_t value = memory[ip + 2];
                memory[addr] = value;
                ip += 3;
                break;
            }

            case OP_MOVE: {
                if (!ensure_operands(ip, 3)) {
                    fprintf(stderr, "CPU Fault: MOVE truncated at 0x%04X\n", (unsigned)ip);
                    running = 0; break;
                }
                uint8_t src = memory[ip + 1];
                uint8_t dest = memory[ip + 2];
                memory[dest] = memory[src];
                ip += 3;
                break;
            }

            case OP_NOT: {
                if (!ensure_operands(ip, 2)) {
                    fprintf(stderr, "CPU Fault: NOT truncated at 0x%04X\n", (unsigned)ip);
                    running = 0; break;
                }
                uint8_t addr = memory[ip + 1];
                memory[addr] = ~memory[addr];
                ip += 2;
                break;
            }

            case OP_NAND: {
                if (!ensure_operands(ip, 4)) {
                    fprintf(stderr, "CPU Fault: NAND truncated at 0x%04X\n", (unsigned)ip);
                    running = 0; break;
                }
                uint8_t a = memory[ip + 1];
                uint8_t b = memory[ip + 2];
                uint8_t dest = memory[ip + 3];
                memory[dest] = ~(memory[a] & memory[b]);
                ip += 4;
                break;
            }

            case OP_JMP: {
                if (!ensure_operands(ip, 2)) {
                    fprintf(stderr, "CPU Fault: JMP truncated at 0x%04X\n", (unsigned)ip);
                    running = 0; break;
                }
                uint8_t addr = memory[ip + 1];
                ip = addr;
                break;
            }

            case OP_JZ: {
                if (!ensure_operands(ip, 3)) {
                    fprintf(stderr, "CPU Fault: JZ truncated at 0x%04X\n", (unsigned)ip);
                    running = 0; break;
                }
                uint8_t addr = memory[ip + 1];
                uint8_t cond = memory[ip + 2];
                if (memory[cond] == 0) {
                    ip = addr;
                } else {
                    ip += 3;
                }
                break;
            }

            case OP_RUN: {
                if (!ensure_operands(ip, 2)) {
                    fprintf(stderr, "CPU Fault: RUN truncated at 0x%04X\n", (unsigned)ip);
                    running = 0; break;
                }
                uint8_t addr = memory[ip + 1];
                if (!push_stack((uint16_t)(ip + 2))) {
                    running = 0; break;
                }
                ip = addr;
                break;
            }

            case OP_HALT: {
                if (stack_pointer > 0) {
                    uint16_t ret_addr;
                    if (!pop_stack(&ret_addr)) {
                        running = 0; break;
                    }
                    ip = ret_addr;
                } else {
                    running = 0;
                }
                break;
            }

            case OP_AND: {
                if (!ensure_operands(ip, 4)) {
                    fprintf(stderr, "CPU Fault: AND truncated at 0x%04X\n", (unsigned)ip);
                    running = 0; break;
                }
                uint8_t a = memory[ip + 1];
                uint8_t b = memory[ip + 2];
                uint8_t dest = memory[ip + 3];
                memory[dest] = memory[a] & memory[b];
                ip += 4;
                break;
            }

            case OP_OR: {
                if (!ensure_operands(ip, 4)) {
                    fprintf(stderr, "CPU Fault: OR truncated at 0x%04X\n", (unsigned)ip);
                    running = 0; break;
                }
                uint8_t a = memory[ip + 1];
                uint8_t b = memory[ip + 2];
                uint8_t dest = memory[ip + 3];
                memory[dest] = memory[a] | memory[b];
                ip += 4;
                break;
            }

            case OP_XOR: {
                if (!ensure_operands(ip, 4)) {
                    fprintf(stderr, "CPU Fault: XOR truncated at 0x%04X\n", (unsigned)ip);
                    running = 0; break;
                }
                uint8_t a = memory[ip + 1];
                uint8_t b = memory[ip + 2];
                uint8_t dest = memory[ip + 3];
                memory[dest] = memory[a] ^ memory[b];
                ip += 4;
                break;
            }

            case OP_INC: {
                if (!ensure_operands(ip, 2)) {
                    fprintf(stderr, "CPU Fault: INC truncated at 0x%04X\n", (unsigned)ip);
                    running = 0; break;
                }
                uint8_t addr = memory[ip + 1];
                memory[addr]++;
                ip += 2;
                break;
            }

            case OP_DEC: {
                if (!ensure_operands(ip, 2)) {
                    fprintf(stderr, "CPU Fault: DEC truncated at 0x%04X\n", (unsigned)ip);
                    running = 0; break;
                }
                uint8_t addr = memory[ip + 1];
                memory[addr]--;
                ip += 2;
                break;
            }

            case OP_CMP: {
                if (!ensure_operands(ip, 4)) {
                    fprintf(stderr, "CPU Fault: CMP truncated at 0x%04X\n", (unsigned)ip);
                    running = 0; break;
                }
                uint8_t a = memory[ip + 1];
                uint8_t b = memory[ip + 2];
                uint8_t dest = memory[ip + 3];
                memory[dest] = (memory[a] == memory[b]) ? 1 : 0;
                ip += 4;
                break;
            }

            case OP_COMMENT: {
                if (!ensure_operands(ip, 2)) {
                    fprintf(stderr, "CPU Fault: COMMENT truncated at 0x%04X\n", (unsigned)ip);
                    running = 0; break;
                }
                uint8_t len = memory[ip + 1];
                uint32_t new_ip = ip + 2 + len;
                if (new_ip > MEMORY_SIZE) {
                    fprintf(stderr, "CPU Fault: COMMENT overflows memory at 0x%04X\n", (unsigned)ip);
                    running = 0; break;
                }
                ip = new_ip;
                break;
            }

            // io instructions

            case OP_PUTC: {
                if (!ensure_operands(ip, 2)) {
                    fprintf(stderr, "CPU Fault: PUTC truncated at 0x%04X\n", (unsigned)ip);
                    running = 0; break;
                }
                uint8_t addr = memory[ip + 1];
                putchar(memory[addr]);
                fflush(stdout);
                ip += 2;
                break;
            }

            case OP_PUTN: {
                if (!ensure_operands(ip, 2)) {
                    fprintf(stderr, "CPU Fault: PUTN truncated at 0x%04X\n", (unsigned)ip);
                    running = 0; break;
                }
                uint8_t addr = memory[ip + 1];
                printf("%d", memory[addr]);
                fflush(stdout);
                ip += 2;
                break;
            }

            case OP_GETC: {
                if (!ensure_operands(ip, 2)) {
                    fprintf(stderr, "CPU Fault: GETC truncated at 0x%04X\n", (unsigned)ip);
                    running = 0; break;
                }
                uint8_t addr = memory[ip + 1];
                int ch = getchar();
                memory[addr] = (ch == EOF) ? 0 : (uint8_t)ch;
                ip += 2;
                break;
            }

            case OP_RET: {
                if (stack_pointer > 0) {
                    uint16_t ret_addr;
                    if (!pop_stack(&ret_addr)) {
                        running = 0; break;
                    }
                    ip = ret_addr;
                } else {
                    fprintf(stderr, "CPU Fault: RET with empty stack at 0x%04X\n", (unsigned)ip);
                    running = 0;
                }
                break;
            }

            // arithmetic anstructions

            case OP_ADD: {
                if (!ensure_operands(ip, 4)) {
                    fprintf(stderr, "CPU Fault: ADD truncated at 0x%04X\n", (unsigned)ip);
                    running = 0; break;
                }
                uint8_t a = memory[ip + 1];
                uint8_t b = memory[ip + 2];
                uint8_t dest = memory[ip + 3];
                uint16_t result = (uint16_t)memory[a] + (uint16_t)memory[b];
                memory[dest] = (uint8_t)result;
                overflow_flag = (result > 255) ? 1 : 0;
                ip += 4;
                break;
            }

            case OP_SUB: {
                if (!ensure_operands(ip, 4)) {
                    fprintf(stderr, "CPU Fault: SUB truncated at 0x%04X\n", (unsigned)ip);
                    running = 0; break;
                }
                uint8_t a = memory[ip + 1];
                uint8_t b = memory[ip + 2];
                uint8_t dest = memory[ip + 3];
                int16_t result = (int16_t)memory[a] - (int16_t)memory[b];
                memory[dest] = (uint8_t)result;
                overflow_flag = (result < 0) ? 1 : 0;
                ip += 4;
                break;
            }

            case OP_MUL: {
                if (!ensure_operands(ip, 4)) {
                    fprintf(stderr, "CPU Fault: MUL truncated at 0x%04X\n", (unsigned)ip);
                    running = 0; break;
                }
                uint8_t a = memory[ip + 1];
                uint8_t b = memory[ip + 2];
                uint8_t dest = memory[ip + 3];
                uint16_t result = (uint16_t)memory[a] * (uint16_t)memory[b];
                memory[dest] = (uint8_t)result;
                overflow_flag = (result > 255) ? 1 : 0;
                ip += 4;
                break;
            }

            case OP_DIV: {
                if (!ensure_operands(ip, 4)) {
                    fprintf(stderr, "CPU Fault: DIV truncated at 0x%04X\n", (unsigned)ip);
                    running = 0; break;
                }
                uint8_t a = memory[ip + 1];
                uint8_t b = memory[ip + 2];
                uint8_t dest = memory[ip + 3];
                if (memory[b] == 0) {
                    fprintf(stderr, "CPU Fault: Division by zero at 0x%04X\n", (unsigned)ip);
                    running = 0; break;
                }
                memory[dest] = memory[a] / memory[b];
                ip += 4;
                break;
            }

            case OP_SHL: {
                if (!ensure_operands(ip, 4)) {
                    fprintf(stderr, "CPU Fault: SHL truncated at 0x%04X\n", (unsigned)ip);
                    running = 0; break;
                }
                uint8_t a = memory[ip + 1];
                uint8_t b = memory[ip + 2];
                uint8_t dest = memory[ip + 3];
                uint8_t shift = memory[b] & 0x07;  // Limit to 0-7
                memory[dest] = memory[a] << shift;
                ip += 4;
                break;
            }

            case OP_SHR: {
                if (!ensure_operands(ip, 4)) {
                    fprintf(stderr, "CPU Fault: SHR truncated at 0x%04X\n", (unsigned)ip);
                    running = 0; break;
                }
                uint8_t a = memory[ip + 1];
                uint8_t b = memory[ip + 2];
                uint8_t dest = memory[ip + 3];
                uint8_t shift = memory[b] & 0x07;  // Limit to 0-7
                memory[dest] = memory[a] >> shift;
                ip += 4;
                break;
            }

            // 16-bit addressing Instructions

            case OP_POKE16: {
                if (!ensure_operands(ip, 4)) {
                    fprintf(stderr, "CPU Fault: POKE16 truncated at 0x%04X\n", (unsigned)ip);
                    running = 0; break;
                }
                uint16_t addr = ((uint16_t)memory[ip + 1] << 8) | memory[ip + 2];
                uint8_t value = memory[ip + 3];
                if (!is_valid_address(addr)) {
                    fprintf(stderr, "CPU Fault: POKE16 address 0x%04X out of bounds at 0x%04X\n",
                            addr, (unsigned)ip);
                    running = 0; break;
                }
                memory[addr] = value;
                ip += 4;
                break;
            }

            case OP_MOVE16: {
                if (!ensure_operands(ip, 5)) {
                    fprintf(stderr, "CPU Fault: MOVE16 truncated at 0x%04X\n", (unsigned)ip);
                    running = 0; break;
                }
                uint16_t src = ((uint16_t)memory[ip + 1] << 8) | memory[ip + 2];
                uint16_t dest = ((uint16_t)memory[ip + 3] << 8) | memory[ip + 4];
                if (!is_valid_address(src) || !is_valid_address(dest)) {
                    fprintf(stderr, "CPU Fault: MOVE16 address out of bounds at 0x%04X\n", (unsigned)ip);
                    running = 0; break;
                }
                memory[dest] = memory[src];
                ip += 5;
                break;
            }

            case OP_JMP16: {
                if (!ensure_operands(ip, 3)) {
                    fprintf(stderr, "CPU Fault: JMP16 truncated at 0x%04X\n", (unsigned)ip);
                    running = 0; break;
                }
                uint16_t addr = ((uint16_t)memory[ip + 1] << 8) | memory[ip + 2];
                if (!is_valid_address(addr)) {
                    fprintf(stderr, "CPU Fault: JMP16 to 0x%04X out of bounds at 0x%04X\n",
                            addr, (unsigned)ip);
                    running = 0; break;
                }
                ip = addr;
                break;
            }

            case OP_JZ16: {
                if (!ensure_operands(ip, 4)) {
                    fprintf(stderr, "CPU Fault: JZ16 truncated at 0x%04X\n", (unsigned)ip);
                    running = 0; break;
                }
                uint16_t addr = ((uint16_t)memory[ip + 1] << 8) | memory[ip + 2];
                uint8_t cond = memory[ip + 3];
                if (!is_valid_address(addr)) {
                    fprintf(stderr, "CPU Fault: JZ16 to 0x%04X out of bounds at 0x%04X\n",
                            addr, (unsigned)ip);
                    running = 0; break;
                }
                if (memory[cond] == 0) {
                    ip = addr;
                } else {
                    ip += 4;
                }
                break;
            }

            case OP_RUN16: {
                if (!ensure_operands(ip, 3)) {
                    fprintf(stderr, "CPU Fault: RUN16 truncated at 0x%04X\n", (unsigned)ip);
                    running = 0; break;
                }
                uint16_t addr = ((uint16_t)memory[ip + 1] << 8) | memory[ip + 2];
                if (!is_valid_address(addr)) {
                    fprintf(stderr, "CPU Fault: RUN16 to 0x%04X out of bounds at 0x%04X\n",
                            addr, (unsigned)ip);
                    running = 0; break;
                }
                if (!push_stack((uint16_t)(ip + 3))) {
                    running = 0; break;
                }
                ip = addr;
                break;
            }

            default:
                fprintf(stderr, "CPU Fault: Unknown opcode 0x%02X at 0x%04X\n", opcode, (unsigned)ip);
                running = 0;
                break;
        }
    }

    if (debug_mode) {
        printf("\nExecution ended. Instructions executed: %u\n", (unsigned)instruction_count);
    }
}

 // Main Entry Point
 int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Shredder - A Minimal Hexadecimal Virtual Machine\n");
        printf("================================================\n");
        printf("Usage: %s [OPTIONS] <program.shred>\n\n", argv[0]);
        printf("Options:\n");
        printf("  -d, --debug      Enable debug mode\n");
        printf("  -t, --trace      Enable trace mode (verbose)\n");
        printf("  -m START:END     Dump memory range (hex, no 0x prefix)\n");
        printf("  -h, --help       Show this help\n\n");
        printf("Memory: 64K bytes (0x0000-0xFFFF)\n");
        printf("Stack:  64 levels\n");
        return EXIT_SUCCESS;
    }

    const char *filename = NULL;
    int dump_start = -1, dump_end = -1;

    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--debug") == 0) {
            debug_mode = 1;
        } else if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--trace") == 0) {
            trace_mode = 1;
            debug_mode = 1;
        } else if (strcmp(argv[i], "-m") == 0 && i + 1 < argc) {
            if (sscanf(argv[i + 1], "%x:%x", &dump_start, &dump_end) == 2) {
                i++;
            } else {
                fprintf(stderr, "Error: Invalid memory range. Use -m START:END (hex)\n");
                return EXIT_FAILURE;
            }
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            return EXIT_SUCCESS;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Error: Unknown option '%s'\n", argv[i]);
            return EXIT_FAILURE;
        } else {
            filename = argv[i];
        }
    }

    if (!filename) {
        fprintf(stderr, "Error: No .shred file specified\n");
        return EXIT_FAILURE;
    }

    // Check file extension
    const char *ext = strrchr(filename, '.');
    if (!ext || strcmp(ext, ".shred") != 0) {
        fprintf(stderr, "Warning: File '%s' doesn't have .shred extension\n", filename);
    }

    // Initialize VM
    memset(memory, 0, MEMORY_SIZE);
    memset(call_stack, 0, sizeof(call_stack));
    stack_pointer = 0;
    overflow_flag = 0;
    instruction_count = 0;

    // Load and execute
    if (load_program(filename) != 0) {
        return EXIT_FAILURE;
    }

    if (debug_mode) {
        printf("\n=== Starting execution ===\n\n");
    }

    execute(0);

    // Post-execution
    if (debug_mode) {
        if (dump_start < 0) dump_start = 0x00;
        if (dump_end < 0) dump_end = 0xFF;
        dump_memory((uint32_t)dump_start, (uint32_t)dump_end);

        if (overflow_flag) {
            printf("[!] Overflow flag is SET\n");
        }
        if (stack_pointer > 0) {
            printf("[!] Warning: Stack not empty (depth=%u)\n", (unsigned)stack_pointer);
        }
    }

    return EXIT_SUCCESS;
}
