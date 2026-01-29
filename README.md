# Shredder ![Alt text](images/icon.png)
Shredder/SHVM - README
====================
Credits: Me/Druid520, developing the code, making the repo, etc. roboman126, testing code, helping out.

NOTE: Sorry for grammar issues if theres any and writing difference!! i wrote this in different periods and got burnt out before uploading to github!
 
Overview
--------
Shredder is a minimal hexadecimal virtual machine (VM) designed to explore low-level programming concepts,
self-modifying code, and stack-based execution. Programs are written in pure hexadecimal (.shred files),
allowing fine-grained control of memory, logic, and I/O.

The philosophy behind Shredder:
- Simplicity: Only the essential opcodes are implemented, focusing on computation, control flow, and I/O.
- Transparency: Programs can manipulate code and data in the same memory space (64K unified memory).
- Education: Encourages understanding of low-level instruction execution, stack operations, and arithmetic logic.

Key Features
------------
- Unified 64K memory (addresses 0x0000–0xFFFF)
- 64-level call stack for function calls and RUN/RET instructions
- Self-modifying code support
- Hexadecimal instruction format for clarity and precision
- Overflow detection on arithmetic instructions
- Full bounds checking to prevent memory faults
- I/O support for characters and numbers

Memory Model
------------
Shredder uses a single 64K memory array where code and data coexist. Instructions and data occupy the
same address space, allowing programs to modify their own code at runtime.

Stack
-----
- Maximum depth: 64 levels
- Stores 16-bit return addresses for RUN/RUN16 and RET instructions
- Stack overflow or underflow is trapped and reported

Execution
---------
- Programs start execution at address 0x0000 by default
- Execution continues until a HALT instruction or fatal error occurs
- Debug and trace modes provide instruction-level reporting

File Format
-----------
- Shredder programs must have a `.shred` extension
- Files contain hexadecimal characters (0–9, A–F) only, with optional comments
- Comments start with `;` or `#` and are ignored by the loader
