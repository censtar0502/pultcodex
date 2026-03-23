# Protocol Frame Audit

## Purpose

Use this skill when checking or changing dispenser protocol code.

It is for:

- frame packing
- frame parsing
- CRC/XOR logic
- response timing assumptions
- status-to-event mapping
- protocol adapter boundaries

## Project-specific rules

- Never let raw protocol details leak into the universal FSM.
- Command bytes, frame layout, CRC/XOR, and raw status codes must stay in protocol adapters.
- Validate both directions:
  - request frame generation
  - response parsing

## Workflow

1. Identify all affected files:
   - adapter `.c/.h`
   - transport layer
   - status mapping docs
   - FSM integration points
2. Write down the exact frame structure:
   - prefix/start byte
   - address/device/channel fields
   - command byte
   - payload
   - checksum/CRC range
3. Verify checksum/XOR over the exact intended bytes.
4. Verify field lengths and units:
   - volume units
   - money units
   - transaction identifier format
5. Verify timing expectations:
   - response timeout
   - inter-byte timeout
   - polling interval assumptions
6. Verify status mapping:
   - raw protocol status
   - abstract event returned to FSM
7. Check that logging of `TRK1/TRK2/BOTH` can be done without contaminating protocol logic.
8. Output only concrete mismatches, risks, and the smallest safe patch.

## Mandatory self-check

- Did I confirm the frame byte-by-byte?
- Did I confirm checksum/XOR byte range exactly?
- Did I keep protocol specifics out of universal FSM?
- Did I verify status-to-event mapping, not just frame syntax?
- Did I check whether blocking behavior was introduced into protocol flow?

