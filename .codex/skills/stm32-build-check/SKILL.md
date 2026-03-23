# STM32 Build Check

## Purpose

Use this skill after meaningful firmware changes to perform a focused embedded sanity check.

It is not only about compilation. It is also about checking whether the change is risky for:

- clocks
- DMA
- NVIC
- UART transport
- OLED
- USB CDC logging

## Workflow

1. Identify changed files.
2. Classify the change:
   - pure logic
   - peripheral config
   - interrupt path
   - transport/protocol path
   - UI/display path
3. Check for high-risk patterns:
   - new blocking calls
   - new ISR work
   - DMA without matching IRQ/callback
   - transport changes without logging visibility
   - display updates inside critical communication paths
4. Compile changed source files if full build is not practical.
5. If transport or timing changed, verify:
   - baud
   - frame assumptions
   - DMA mode
   - IDLE/IRQ path
6. If UI/display changed, verify it does not create unnecessary loop pressure.
7. Summarize:
   - what was checked
   - what still needs on-hardware confirmation
   - what residual risk remains

## Mandatory self-check

- Did I check for newly introduced blocking behavior?
- Did I check DMA/IRQ coupling?
- Did I separate compile success from runtime correctness?
- Did I note what still requires hardware validation?
