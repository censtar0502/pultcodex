# CubeIDE Safe Edit

## Purpose

Use this skill when editing STM32CubeIDE / CubeMX generated projects.

It is for:

- safe peripheral changes
- preserving user code across regeneration
- avoiding accidental damage to generated structure

## Project-specific rules

- Current repository is STM32CubeIDE-oriented.
- Generated files may be overwritten by CubeMX.
- Keep user logic either:
  - inside `USER CODE BEGIN/END`
  - or in separate non-generated files

## Workflow

1. Identify whether the target file is:
   - generated
   - partially generated
   - fully user-owned
2. If generated:
   - prefer `USER CODE` blocks
   - otherwise use the smallest possible surgical edit
3. If touching peripheral config, cross-check:
   - GPIO
   - DMA
   - NVIC
   - clock tree
   - interrupt handlers
4. Re-check compatibility with:
   - `.docs/DISC1_DEV_PINOUT.md`
   - `.docs/HARDWARE_AND_PINOUT_F407.md`
5. Watch for regeneration-sensitive files:
   - `main.c`
   - `gpio.c`
   - `spi.c`
   - `i2c.c`
   - `tim.c`
   - `usart.c`
   - `stm32f4xx_it.c`
   - `main.h`
6. Prefer moving custom logic into dedicated files when possible.
7. After editing, verify that the intended behavior still matches `.ioc` assumptions.

## Mandatory self-check

- Did I edit inside safe regions where possible?
- Did I accidentally rely on a generated setting that Cube may overwrite later?
- Did I change any IRQ/DMA/UART/GPIO relation without checking both code and `.ioc`?
- Would this survive one more Cube regeneration?

