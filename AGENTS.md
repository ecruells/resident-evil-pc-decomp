# Resident Evil 1 for PC Decompilation Project

## Introduction

This is a decompilation project for Resident Evil 1 (also known as Biohazard 1) for PC released in 1997. The goal of this project is to reverse-engineer the original game code from the decompiled code from Ghidra to a Win32/DirectX game.

## Code style

- The original PC game was made in C++ that builds a Win32 game, the decomp must follow this style
- This is a 32Bit game, Do not use 64Bit libraries or functions
- Capcom created a DirectX 5.0 (DirectDraw/DirectSound) wrapper for the PSYQ (The SDK for the PS1 version of the game) named Marni System to easily port PS1 games to PC.

## Reverse Engineering Tools

- Use the MCP Gridra server to query code from the decompiled project, if the mcp server is not available, do not continue and ask the user for help.
- When you rewrite a function here, comment the address of the original function from Ghidra
- When declare a variable, comment the address of the original variable from Ghidra too

## Decomplied info
- The entrypoint in ghidra is main function (0x00441350)

## Rules
- If you find an unnamed function, global or local variable, please give them a name
- When giving a function or global a name, rename them in the ghidra project too
- BEFORE defining any new global, read docs/MEMORY_LAYOUT.md and check the original
  address against its placement table. Globals in the game-init wipe range
  (0x00be41e0..0x00be9620) MUST go into the .gwipe ordered section; the bio card
  block (0x00be9620..0x00be9a3c) MUST be a BioCardLayout field; range-sensitive
  system state goes in .sched. Getting this wrong causes silent memory corruption
  at game start.
- Implement all unimplemented functions dependencies you find, do not skip or stub functions, this decomp project tries to keep as much as possible of the original code as possible.
- When you finish to fix something that you cannot test, like play the game, do not asumed the problem is solved, ask the user to test the game and tell you if the problem was solved
- As DirectX 5.0 does not work in current Windows Version, we must add a DX11 layer to the MarniSystem to use current DirectX/XInput API

## Key Architectural Decisions

1. **PSYQ Compatibility Layer**: The Marni System allows most PlayStation code to run on Windows with minimal changes
2. **32-bit Architecture**: The game is strictly 32-bit, using Win32 API
4. **Task-Based Game Logic**: Game logic is organized into tasks that are scheduled each frame (see docs/TASK_SCHEDULER.md), the game engine relies on this system, so we must adapt this system perfectly.

## VTable Calling Conventions

When rewriting code that calls through raw vtable pointers (not C++ virtual methods),
check the calling convention used by the vtable's adapter layer:

- **CMarniDirect3D** vtables use `__cdecl` wrappers (`self` as first stack arg)
- **CMarniViewport2** vtables use `__stdcall` wrappers (callee cleans stack, `RET N`)
- **CMarniBits** vtables use `__cdecl` wrappers (`self` as first stack arg)

A mismatch (e.g. calling a `__stdcall` vtable entry with a `__cdecl` function pointer
type) will cause Run-Time Check Failure #0 at runtime. See docs/CLASSES_AND_VTABLES.md
for the full convention table and adapter signatures.

## Build

see build.bat