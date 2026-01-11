# ASCTester

This is a test program for the Apple Sound Chip (ASC) and various variants used on classic Mac models from the '80s and '90s.

It is still a work in progress. The goal is to create some sanity tests so you can compare emulators and damaged machines against known-good behavior on good hardware, and also learn more about the behavior of the ASC and its interrupt.

## Build instructions

This is built using [Retro68](https://github.com/autc04/Retro68). To build, run the following commands:

- `export RETRO68="/path/to/Retro68-build/toolchain`
- `make`

## How to use

When you run this program, you should hear a quick blip or two from the speaker, followed by a delay, followed by another blip, followed by another short delay. Then a window will pop up containing test results.

Only use this on a Mac that actually has an ASC or ASC variant. Note that it's very possible this program could hang your machine, so don't have anything important going on at the same time. Ideally, run it immediately after rebooting.

## What it prints out

- **BoxFlag** &mdash; an identifier of the Mac model
- **ASC Version** &mdash; the value in register $800 of the ASC containing its revision
- **F29Exists** &mdash; 1 if we detect that register $F29 actually accepts 1 and 0 as written values, so it probably exists
- **804Idle** &mdash; the value that register $804 contains at idle while nothing sound-related is happening
- **M0, M1, M2** &mdash; 1 if 0, 1, and 2 are accepted as writes to register $801, respectively
- **Mono: a b** &mdash; **a** is 1 if register $802 accepts a write of bit 2 = 0; **b** is 1 if ASCTester thinks it should actually test mono
- **Stereo: a b** &mdash; **a** is 1 if register $802 accepts a write of bit 2 = 1; **b** is 1 if ASCTester thinks it should actually test stereo
  - (The reason we might not test based on the bit's response is because the bit lost meaning on newer ASC variants)
- **Mono/Stereo FIFO Tests** &mdash; the results of several FIFO register $804 tests in mono/stereo mode, in order:
  - 1 if bit 1 (full/empty A) was 1 immediately after we wrote some samples, so it's probably not a valid playback status bit and the rest of the tests related to FIFO A status bits should be ignored.
  - 1 if bit 3 (full/empty B) was 1 immediately after we wrote some samples, so it's probably not a valid playback status bit and the rest of the tests related to FIFO B status bits should be ignored.
  - 1 if bit 1 (full/empty A) is eventually recognized as 1 = full while writing samples
  - 1 if bit 3 (full/empty B) is eventually recognized as 1 = full while writing samples
  - 1 if bit 0 (half empty A) is 0 when we recognize FIFO A is full
  - 1 if bit 2 (half empty B) is 0 when we recognize FIFO B is full
  - 1 if bit 0 (half empty A) is eventually recognized as 1 = half empty while waiting after filling it up
  - 1 if bit 2 (half empty B) is eventually recognized as 1 = half empty while waiting after filling it up
  - 1 if bit 1 (full/empty A) is 0 when we first notice bit 0 (half empty A) become 1
  - 1 if bit 3 (full/empty B) is 0 when we first notice bit 2 (half empty B) become 1
  - 1 if bit 1 (full/empty A) is eventually recognized as 1 = empty while waiting after filling it up
  - 1 if bit 3 (full/empty B) is eventually recognized as 1 = empty while waiting after filling it up
- **VIA2 $xxxx a** &mdash; **$xxxx** is how often the VIA2 address space repeats in bytes, or $FFFF if it doesn't repeat after $200 bytes which likely indicates a "normal" VIA2 with a different register every $200 bytes. **a** is 1 if the VIA2's address space mirroring works correctly with register $1C13 able to configure VIA2's IER regardless of whether it's actually mapped to $1C00 or $13.
- **IRQ** &mdash; the results of several idle IRQ tests, in order:
  - 1 if the ASC flags an IRQ immediately upon enabling IRQs while idle, without changing register $F29
  - 1 if the ASC floods a bunch of IRQs immediately upon enabling IRQs while idle, without changing register $F29
  - 1 if the ASC IRQ flood prevents further ASCTester app instructions from executing (again, no $F29 involved)
  - 1 if the ASC flags an IRQ immediately upon enabling IRQs while idle, with register $F29 set to 0
  - 1 if the ASC floods a bunch of IRQs immediately upon enabling IRQs while idle, with register $F29 set to 0
  - 1 if the ASC IRQ flood prevents further ASCTester app instructions from executing (again, no $F29 involved)
- **FIFO IRQ** &mdash; the results of several IRQ tests while filling and emptying the FIFO, in order:
  - 1 if we actually decided to test a FIFO for IRQ. It might be 0 if we didn't find a sufficient FIFO to test
  - 1 if we used FIFO A status bits for the test, 0 if we used FIFO B status bits (B is preferred unless we detect that the B bits don't work)
  - 1 if we observed an IRQ when the FIFO filled up
  - 1 if we observed an IRQ when the FIFO reached half empty after filling up
  - 1 if the half empty IRQ was observed immediately after we filled the FIFO up, which shouldn't happen
  - 1 if we observed an IRQ when the FIFO reached empty
  - 1 if the empty IRQ was observed immediately after we filled the FIFO up, which shoiuldn't happen
  - 1 if we received any other IRQs during the FIFO test that we didn't expect

## Expected results gathered from working hardware

### Mac IIci

```
BoxFlag: 5   ASC Version: $00
F29Exists: 0  804Idle: $00  M0: 1 M1: 1 M2: 1
Mono: 1 1 Stereo: 1 1
Mono FIFO Tests:
0 0 1 0 1 0 1 0 1 0 0 0
Stereo FIFO Tests:
0 0 1 1 1 1 1 1 1 1 0 0
VIA2 $0004 1
IRQ 0 0 0 0 0 0
FIFO IRQ 1 0 1 1 0 0 0 0
```

### Classic II

```
BoxFlag: 17   ASC Version: $E0
F29Exists: 0  804Idle: $03  M0: 0 M1: 1 M2: 0
Mono: 1 1 Stereo: 0 0
Mono FIFO Tests:
0 0 1 0 1 0 1 0 1 0 1 0
VIA2 $0004 1
IRQ 1 1 1 0 0 0
FIFO IRQ 1 1 0 1 0 0 0 0
```

### LC

```
BoxFlag: 13   ASC Version: $E8
F29Exists: 0  804Idle: $03  M0: 0 M1: 1 M2: 0
Mono: 1 1 Stereo: 0 0
Mono FIFO Tests:
0 0 1 0 1 0 1 0 1 0 1 0
VIA2 $0004 1
IRQ 1 1 1 0 0 0
FIFO IRQ 1 1 0 1 0 0 0 0
```

### LC II

```
BoxFlag: 31   ASC Version: $E8
F29Exists: 0  804Idle: $03  M0: 0 M1: 1 M2: 0
Mono: 1 1 Stereo: 0 0
Mono FIFO Tests:
0 0 1 0 1 0 1 0 1 0 1 0
VIA2 $0004 1
IRQ 1 1 1 0 0 0
FIFO IRQ 1 1 0 1 0 0 0 0
```

### LC III

```
BoxFlag: 21   ASC Version: $BC
F29Exists: 1  804Idle: $0E  M0: 0 M1: 1 M2: 0
Mono: 1 0 Stereo: 0 1
Stereo FIFO Tests:
1 0 1 1 1 1 0 1 0 1 1 1
VIA2 $0020 1
IRQ 0 0 0 1 1 1
FIFO IRQ 1 0 0 1 0 0 0 0
```

### LC 475

```
BoxFlag: 83   ASC Version: $BB
F29Exists: 1  804Idle: $0E  M0: 1 M1: 1 M2: 0
Mono: 1 0 Stereo: 0 1
Stereo FIFO Tests:
1 0 1 1 1 1 0 1 0 1 1 1
VIA2 $FFFF 1
IRQ 0 0 0 1 0 0
FIFO IRQ 1 0 0 1 0 0 0 0
```

### LC 550

```
BoxFlag: 74   ASC Version: $BC
F29Exists: 1  804Idle: $0E  M0: 0 M1: 1 M2: 0
Mono: 1 0 Stereo: 0 1
Stereo FIFO Tests:
1 0 1 1 1 1 0 1 0 1 1 1
VIA2 $0020 1
IRQ 0 0 0 1 1 1
FIFO IRQ 1 0 0 1 0 0 0 0
```

### Color Classic

```
BoxFlag: 43   ASC Version: $E8
F29Exists: 0  804Idle: $03  M0: 0 M1: 1 M2: 0
Mono: 1 1 Stereo: 0 0
Mono FIFO Tests:
0 0 1 0 1 0 1 0 1 0 1 0
VIA2 $0020 1
IRQ 1 1 1 0 0 0
FIFO IRQ 1 1 0 1 0 0 0 0
```

### Centris 610

```
BoxFlag: 46   ASC Version: $BB
F29Exists: 1  804Idle: $0E  M0: 1 M1: 1 M2: 0
Mono: 1 0 Stereo: 0 1
Stereo FIFO Tests:
1 0 1 1 1 1 0 1 0 1 1 1
VIA2 $FFFF 1
IRQ 0 0 0 1 0 0
FIFO IRQ 1 0 0 1 0 0 0 0
```
