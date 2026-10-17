# ASCTester

This is a test program for the Apple Sound Chip (ASC) and various variants used on classic Mac models from the '80s and '90s.

It is still a work in progress. The goal is to create some sanity tests so you can compare emulators and damaged machines against known-good behavior on good hardware, and also learn more about the behavior of the ASC and its interrupt.

## Build instructions

This is built using [Retro68](https://github.com/autc04/Retro68). To build, run the following commands:

- `export RETRO68="/path/to/Retro68-build/toolchain`
- `make`

Pre-built binaries are provided: ASCTester.dsk is an 800k disk image suitable for use in emulators, and ASCTester.bin is a MacBinary file.

## How to use

When you run this program, you should hear a a few blips from the speaker. It may take a while for the tests to complete. Don't move the mouse or press the keyboard during the tests. When it's finished, a window will pop up containing test results.

Only use this on a Mac that actually has an ASC or ASC variant. Note that it's very possible this program could hang your machine, so don't have anything important going on at the same time. Ideally, run it immediately after rebooting.

## What it prints out

- **BoxFlag** &mdash; an identifier of the Mac model
- **ASC Version** &mdash; the value in register $800 of the ASC containing its revision
- **System a.b.c** &mdash; the system version that the test is running, just for informational purposes
- **AddrMapFlags** &mdash; a value showing which peripherals are marked as available by the OS
- **F09 a (b)** &mdash; **a** is 1 if we detect that register $F09 actually accepts 1 and 0 as written values, so it probably exists. **b** is the value that register $F09 contains when we first look at it, if it exists.
- **F29 a (b)** &mdash; **a** is 1 if we detect that register $F29 actually accepts 1 and 0 as written values, so it probably exists. **b** is the value that register $F29 contains when we first look at it, if it exists.
- **804Idle** &mdash; the value that register $804 contains at idle while nothing sound-related is happening
- **M0, M1, M2 (a)** &mdash; 1 if 0, 1, and 2 are accepted as writes to register $801, respectively. **a** is the initial value of register $801.
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
  - **(a b)** &mdash; **a** is the number of samples written before FIFO A was marked as full. **b** is the same, but for FIFO B. This is a rough idea of the size of the FIFO, but it will always report too big because it empties as it's being filled. The numbers will not be exactly the same during every test.
- **VIA2 (a $xxxx) b** &mdash; **a** is 1 if the VIA2 readback was consistent twice in a row. **$xxxx** is a bitmask of which address bits from A0-A8 appear to be decoded by VIA2, inside of the first $200 bytes of space. If it's 0, it means it fully repeats inside of the first $200 bytes which likely indicates a "normal" VIA2 with a different register every $200 bytes. If it's nonzero, it's likely a pseudo-VIA that looks at a few of the lower address bits for decoding. **b** is 1 if the VIA2's address space mirroring works correctly with register $1C13 able to configure VIA2's IER regardless of whether it's actually mapped to $1C00 or $13.
- **Idle IRQ** &mdash; the results of several idle IRQ tests, in order:
  - 1 if the ASC flags an IRQ immediately upon enabling IRQs while idle, with register $F29 set to 1 if it exists
  - 1 if the ASC floods a bunch of IRQs immediately upon enabling IRQs while idle, with register $F29 set to 1 if it exists
  - 1 if the ASC IRQ flood prevents further ASCTester app instructions from executing, with register $F29 set to 1 if it exists
  - **(a)** indicates the number of IRQs we counted while idle with $F29 set to 1 if it exists (50000 means it flooded).
  - 1 if the ASC flags an IRQ immediately upon enabling IRQs while idle, with register $F29 set to 0 if it exists
  - 1 if the ASC floods a bunch of IRQs immediately upon enabling IRQs while idle, with register $F29 set to 0 if it exists
  - 1 if the ASC IRQ flood prevents further ASCTester app instructions from executing, with register $F29 set to 0 if it exists
  - **(b)** indicates the number of IRQs we counted while idle with $F29 set to 0 if it exists (50000 means it flooded).
  - 1 if toggling register $F29 to 1 and back to 0 while idle causes the ASC to flag another IRQ
  - 1 if toggling register $F29 to 1 and back to 0 while idle causes the ASC to flood a bunch of IRQs
  - 1 if toggling register $F29 to 1 and back to 0 while idle causes an ASC IRQ flood that prevents further instructions from running
- **FIFO IRQ** &mdash; the results of several IRQ tests while filling and emptying the FIFO, in order:
  - 1 if we actually decided to test a FIFO for IRQ. It might be 0 if we didn't find a sufficient FIFO to test
  - 1 if we used FIFO A status bits for the test, 0 if we used FIFO B status bits (B is preferred unless we detect that the B bits don't work)
  - 1 if the half empty IRQ was observed immediately after we filled the FIFO up, which shouldn't happen
  - 1 if the empty IRQ was observed immediately after we filled the FIFO up, which shouldn't happen
- **(a b), (c d), (e f), (g h), i** &mdash; IRQ counts during the FIFO test, in order (over a period of 4 seconds):
  - **a** is the number of FIFO full IRQs observed
  - **b** is the maximum increase in FIFO full IRQs that was observed by one loop iteration in the main program
  - **c** is the number of FIFO half empty IRQs observed
  - **d** is the maximum increase in FIFO half empty IRQs that was observed by one loop iteration in the main program
  - **e** is the number of FIFO empty IRQs observed
  - **f** is the maximum increase in FIFO empty IRQs that was observed by one loop iteration in the main program
  - **g** is the number of other IRQs observed
  - **h** is the maximum increase in other IRQs that was observed by one loop iteration in the main program
  - **i** is 1 if an IRQ fired after we toggled the IRQ off and back on just after the FIFO filled up
- **a IRQs (start time = b)** &mdash; detailed info about the IRQs that occurred during the FIFO IRQ test, and when. Limited to displaying info about the first 12 IRQs. **The displayed times will vary between runs. This is for diagnostics only.**
- **Cancel a b c d e f g** &mdash; test results from the IRQ cancel test:
  - **a** is 1 if the cancel test was performed. It's skipped if the FIFO IRQ wasn't tested, or if there was an IRQ flood
  - **b** is 1 if an ASC IRQ was initially signaled in VIA2's IFR before any samples were written to the chip
  - **c** is 1 if an ASC IRQ was signaled in the VIA2's IFR after writing a bunch of samples to the chip
  - **d** is 1 if an ASC IRQ was still signaled in the VIA2's IFR after attempting to clear it
  - **e** is 1 if an ASC IRQ was still signaled in the VIA2's IFR after interrupts were enabled
  - **f** is the value that was observed in ASC register $804 after the test finished
  - **g** is the total count of IRQs observed after enabling interrupts during the cancel test
- **Timer2: a (ACR $xx IER $xx)** &mdash; **a** is 1 if VIA1 timer 2 can be used as a stopwatch (it isn't in pulse counting mode). The VIA1 ACR and IER values are shown for information. Mac OS (7.1 at least) has the timer 2 interrupt enabled, so the tests below borrow the timer with interrupts off, mask its interrupt and leave it expired with its flag cleared.
- **StatusWrite a (b) repeat c, clear $xx $yy d** &mdash; tests whether writing to $804 raises the ASC interrupt again, as the Mac II's MMU mode switch code expects (it reads $804 and writes the flags back to get a new interrupt). Done in FIFO mode with an empty FIFO, the ASC interrupt masked in VIA2's IER, polling VIA2's IFR:
  - **a** is 1 if the test was performed
  - **b** is 1 if VIA2 flagged an ASC interrupt again right after clearing it, before any write (the line is held active at idle, e.g. V8)
  - **c** is 1 if writing $01 a second time, without reading $804 in between, flagged the interrupt again
  - **$xx** and **$yy** are two reads of $804 right after clearing the FIFO (bit 7 of $803), and **d** is 1 if VIA2 flagged an ASC interrupt after the clear
  - then for each value written to $804: **$vv: d $xx $yy e**, where **d** is 1 if VIA2 flagged an ASC interrupt after the write, **$xx** and **$yy** are two reads of $804 after it, and **e** is 1 if VIA2 flagged the interrupt again after clearing it, after those reads
- **MaskedLatch a: b $xx $ss c $yy n** &mdash; tests whether VIA2 (or the chip standing in for it; some are Apple ASIC reimplementations) latches the ASC interrupt in its IFR while it's masked in its IER, whether the flag stays once the ASC releases its line, and whether enabling the ASC afterwards delivers it. With the ASC masked and CPU interrupts off, fills the FIFO (on the original ASC, the full condition drives the line), reads $804 (which releases the line on the original ASC), then enables the ASC in the IER and turns CPU interrupts back on:
  - **a** is 1 if the test was performed
  - **b** is 1 if IFR bit 4 was set after the FIFO filled up
  - **$xx** is the raw IFR at that point (on a 6522, bit 7 only reflects enabled sources)
  - **$ss** is the $804 read that follows
  - **c** is 1 if IFR bit 4 was still set after that read
  - **$yy** is the raw IFR after enabling the ASC in the IER
  - **n** is the number of interrupts taken after turning CPU interrupts back on (the handler doesn't read $804)

  On the IIci (oscilloscope measurements by Doug Brown), the ASC holds its line until $804 is read and the RBV latches the falling edge whatever its IER says, so b, c and n are expected to be 1. Variants with a level interrupt (V8...) release the line only when the FIFO is above half empty, which the IRQLine test below covers.
- **IRQLine a: b c d e (t) $xx f** &mdash; tests whether the ASC interrupt input of VIA2 is edge or level triggered, and whether the ASC holds its line until $804 is read or pulses it for each condition. Fills the FIFO without reading $804, with the ASC masked in VIA2's IER, polling the IFR:
  - **a** is 1 if the test was performed
  - **b** is 1 if VIA2 flagged an ASC interrupt again right after clearing it, before filling the FIFO (idle level, e.g. V8)
  - **c** is 1 if VIA2 flagged an ASC interrupt after filling the FIFO
  - **d** is 1 if the flag came back right after clearing it, with $804 not read: the input is level triggered
  - **e** is 1 if the flag came back later, as the FIFO drained past half empty, with $804 still not read. With an edge triggered input, that means the ASC pulses its line (or drops it and raises it again) for each condition, rather than holding it until $804 is read
  - **t** is when that happened, in VIA ticks after clearing the flag (about 61440 means it didn't happen within about 80 ms)
  - **$xx** is $804 read after that
  - **f** is 1 if the flag came back after clearing it, after reading $804

All the results are also saved to a text file named "ASCTester Results" next to the application.

Recent versions of Retro68's RetroConsole link in most of libstdc++'s iostreams and locale support, which makes the application about 1.2M, more than the 1M partition Retro68's default SIZE resource asks for: under System 7 it then fails to load its code and stops with an "unimplemented trap" system error. `make SIMPLE_CONSOLE=1` builds a version without RetroConsole (about 70K) that shows the results in a plain window instead; click to quit.

## Expected results gathered from working hardware

Several tests are counts that display a number other than 0 or 1. **The large numbers in parentheses other than 50000 will vary between runs.** The important thing is that it's a value greater than 0 and much less than 50000.

### Mac IIci

```
ASCTester test version 4
BoxFlag: 5   ASC Version: $00   System 7.1.0
AddrMapFlags: $0000773F
F09: 0 ($00)  F29: 0 ($00)
804Idle: $00  M0: 1 M1: 1 M2: 1 ($00)
Mono: 1 1 Stereo: 1 1
Mono FIFO Tests:
0 0 1 0 1 0 1 0 1 0 0 0 (1105 0)
Stereo FIFO Tests:
0 0 1 1 1 1 1 1 1 1 0 0 (1116 1116)
VIA2 (1 $0013) 1
Idle IRQ 0 0 0 (0), 0 0 0 (0), 0 0 0
FIFO IRQ 1 0 0 0
(1 1), (1 1), (0 0), (0 0), 1
2 IRQs (start time = 2756):
$0A at t =  2756, $05 at t =  2757
Cancel 1 0 1 0 0 $0F 0
```

Note: Sometimes the second pair underneath FIFO IRQ is (2 1) instead of (1 1).

### LC

```
ASCTester test version 4
BoxFlag: 13   ASC Version: $E8   System 7.1.0
AddrMapFlags: $0000773F
F09: 0 ($00)  F29: 0 ($00)
804Idle: $03  M0: 0 M1: 1 M2: 0 ($01)
Mono: 1 1 Stereo: 0 0
Mono FIFO Tests:
0 0 1 0 1 0 1 0 1 0 1 0 (1190 0)
VIA2 (1 $0013) 1
Idle IRQ 1 1 1 (50000), 0 0 0 (0), 0 0 0
FIFO IRQ 1 1 0 0
(0 0), (528 528), (50000 50000), (0 0), 0
50528 IRQs (start time = 2497):
$01 at t =  2499, $01 at t =  2499, $01 at 4 =  2499, $01 at t =  2499,
$01 at t =  2499, $01 at t =  2499, $01 at 4 =  2499, $01 at t =  2499,
$01 at t =  2499, $01 at t =  2499, $01 at 4 =  2499, $01 at t =  2499, ...
Cancel 0 0 0 0 0 $00 0
```

### LC III

```
ASCTester test version 4
BoxFlag: 21   ASC Version: $BC   System 7.1.0
AddrMapFlags: $0000773F
F09: 1 ($01)  F29: 1 ($01)
804Idle: $0E  M0: 0 M1: 1 M2: 0 ($01)
Mono: 1 0 Stereo: 0 1
Stereo FIFO Tests:
1 0 1 1 1 1 0 1 0 1 1 1 (0 1136)
VIA2 (1 $001F) 1
Idle IRQ 0 0 0 (0), 1 1 1 (50000), 1 1 1
FIFO IRQ 1 0 0 0
(0 0), (1199 1199), (50000 50000), (0 0), 0
51199 IRQs (start time = 4577):
$06 at t =  4579, $06 at t =  4579, $06 at t =  4579, $06 at t =  4579,
$06 at t =  4579, $06 at t =  4579, $06 at t =  4579, $06 at t =  4579,
$06 at t =  4579, $06 at t =  4579, $06 at t =  4579, $06 at t =  4579, ...
Cancel 0 0 0 0 0 $00 0
```

### LC 475

```
ASCTester test version 4
BoxFlag: 83   ASC Version: $BB   System 7.5.3
AddrMapFlags: $0500183F
F09: 1 ($01)  F29: 1 ($01)
804Idle: $0E  M0: 1 M1: 1 M2: 0 ($01)
Mono: 1 0 Stereo: 0 1
Stereo FIFO Tests:
1 0 1 1 1 1 0 1 0 1 1 1 (0 1104)
VIA2 (1 $0000) 1
Idle IRQ 0 0 0 (0), 1 0 0 (1), 1 0 0
FIFO IRQ 1 0 0 0
(0 0), (1 1), (0 0), (0 0), 0
1 IRQs (start time = 2614):
$06 at t =  2615
Cancel 1 0 1 0 0 $0E 0
```

## Older results needing a re-test with the latest version

### Quadra 700

```
ASCTester test version 3
BoxFlag: 16   ASC Version: $B0   System 7.6.1
AddrMapFlags: $05A0183F
F09: 1 ($01)  F29: 1 ($01)
804Idle: $0F  M0: 1 M1: 1 M2: 0 ($01)
Mono: 1 0 Stereo: 0 1
Stereo FIFO Tests:
0 0 1 1 1 1 1 1 1 1 1 1 (1103 1103)
VIA2 (1 $0000) 1
Idle IRQ 0 0 0 (0), 1 0 0 (1), 1 0 0
FIFO IRQ 1 0 0 0
(0 0), (1 1), (0 0), (0 0), 0
ASC VBL Task was located and temporarily disabled during this test.
```

Note: depending on certain factors including whether a sound has already been played by the machine, the F09 and F29 tests may print `($00)` instead of `($01)`.

The special note at the bottom about the ASC VBL task indicates that ASCTester discovered a VBL task that toggles the ASC IRQ periodically, and disabled it. Apple has a special VBL task in the Sound Manager specifically targeted at the Quadra 700 and 900. It interferes with the tests, so ASCTester has to temporarily disable it in order to obtain real info about the hardware behavior.

### PowerBook Duo 210

```
ASCTester test version 3
BoxFlag: 23   ASC Version: $E9   System 7.1.1
AddrMapFlags: $0000773F
F09: 0 ($00)  F29: 0 ($00)
804Idle: $03  M0: 0 M1: 1 M2: 0 ($01)
Mono: 1 1 Stereo: 0 0
Mono FIFO Tests:
0 0 1 0 1 0 1 0 1 0 1 0 (1022 0)
VIA2 (1 $00FF) 1
Idle IRQ 1 1 1 (50000), 0 0 0 (0), 0 0 0
FIFO IRQ 1 1 0 0
(0 0), (1236 1236), (50000 50000), (0 0), 0
```

### Mac IIfx

```
ASCTester test version 2
BoxFlag: 7   ASC Version: $00
F29: 0 ($00)  804Idle: $00  M0: 1 M1: 1 M2: 1
Mono: 1 1 Stereo: 1 1
Mono FIFO Tests:
0 0 1 0 1 0 0 0 0 0 0 0
Stereo FIFO Tests:
0 0 0 1 0 1 1 1 1 1 0 0
VIA2 $000F 0
Idle IRQ 0 0 0, 0 0 0, 0 0 0
FIFO IRQ 1 0 0 0
(1 1), (1 1), (0 0), (0 0)
```

### Quadra 950

```
ASCTester test version 2
BoxFlag: 20   ASC Version: $B0
F29: 1 ($01)  804Idle: $0F  M0: 1 M1: 1 M2: 0
Mono: 1 0 Stereo: 0 1
Stereo FIFO Tests:
0 0 1 1 1 1 1 1 1 1 1 1
VIA2 $0000 1
Idle IRQ 0 0 0, 1 0 0, 1 0 0
FIFO IRQ 1 0 0 0
(0 0), (1 1), (0 0), (0 0)
```

Note: The bottom line will change if you are moving the mouse during the tests. This is due to something similar to what is mentioned about the Quadra 700, but the special interfering code is in the ROM this time, and it only runs if the mouse or keyboard is being used.

### Quadra 800

```
ASCTester test version 2
BoxFlag: 29   ASC Version: $BB
F29: 1 ($01)  804Idle: $0E  M0: 1 M1: 1 M2: 0
Mono: 1 0 Stereo: 0 1
Stereo FIFO Tests:
1 0 1 1 1 1 0 1 0 1 1 1
VIA2 $0000 1
Idle IRQ 0 0 0, 1 0 0, 1 0 0
FIFO IRQ 1 0 0 0
(0 0), (1 1), (0 0), (0 0)
```

### PowerBook 180

```
ASCTester test version 2
BoxFlag: 27   ASC Version: $B0
F29: 1 ($01)  804Idle: $0F  M0: 1 M1: 1 M2: 0
Mono: 1 0 Stereo: 0 1
Stereo FIFO Tests:
0 0 1 1 1 1 1 1 1 1 1 1
VIA2 $0000 1
Idle IRQ 0 0 0, 1 0 0, 1 0 0
FIFO IRQ 1 0 0 0
(0 0), (1 1), (0 0), (0 0)
```

### PowerBook Duo 280c

```
ASCTester test version 2
BoxFlag: 97   ASC Version: $E9
F29: 0 ($00)  804Idle: $03  M0: 0 M1: 1 M2: 0
Mono: 1 1 Stereo: 0 0
Mono FIFO Tests:
0 0 1 0 1 0 1 0 1 0 1 0
VIA2 $01FF 1
Idle IRQ 1 1 1, 0 0 0, 0 0 0
FIFO IRQ 1 1 0 0
(0 0), (4582 4582), (50000 50000), (0 0)
```

Note: The 4582 will likely vary on different runs. The important thing is that it's a value greater than 0 and much less than 50000.

### PowerBook 520c/540c

```
ASCTester test version 2
BoxFlag: 66   ASC Version: $BB
F29: 1 ($01)  804Idle: $0E  M0: 1 M1: 1 M2: 0
Mono: 1 0 Stereo: 0 1
Stereo FIFO Tests:
1 0 1 1 1 1 0 1 0 1 1 1
VIA2 $0000 1
Idle IRQ 0 0 0, 1 1 1, 1 1 1
FIFO IRQ 1 0 0 0
(0 0), (4086 4086), (50000 50000), (0 0)
```

Note: The 4086 will likely vary on different runs. The important thing is that it's a value greater than 0 and much less than 50000.


### Classic II

```
BoxFlag: 17   ASC Version: $E0
F29Exists: 0  804Idle: $03  M0: 0 M1: 1 M2: 0
Mono: 1 1 Stereo: 0 0
Mono FIFO Tests:
0 0 1 0 1 0 1 0 1 0 1 0
VIA2 $0013 1
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
VIA2 $0013 1
IRQ 1 1 1 0 0 0
FIFO IRQ 1 1 0 1 0 0 0 0
```

### LC 550

```
BoxFlag: 74   ASC Version: $BC
F29Exists: 1  804Idle: $0E  M0: 0 M1: 1 M2: 0
Mono: 1 0 Stereo: 0 1
Stereo FIFO Tests:
1 0 1 1 1 1 0 1 0 1 1 1
VIA2 $001F 1
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
VIA2 $001F 1
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
VIA2 $0000 1
IRQ 0 0 0 1 0 0
FIFO IRQ 1 0 0 1 0 0 0 0
```

### LC 630

```
BoxFlag: 92   ASC Version: $BB
F29Exists: 1  804Idle: $0E  M0: 1 M1: 1 M2: 0
Mono: 1 0 Stereo: 0 1
Stereo FIFO Tests:
1 0 1 1 1 1 0 1 0 1 1 1
VIA2 $0000 1
IRQ 0 0 0 1 0 0
FIFO IRQ 1 0 0 1 0 0 0 0
```
