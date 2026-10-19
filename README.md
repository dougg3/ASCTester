# ASCTester

This is a test program for the Apple Sound Chip (ASC) and various variants used on classic Mac models from the '80s and '90s.

It is still a work in progress. The goal is to create some sanity tests so you can compare emulators and damaged machines against known-good behavior on good hardware, and also learn more about the behavior of the ASC and its interrupt.

## Build instructions

This is built using [Retro68](https://github.com/autc04/Retro68). To build, run the following commands:

- `export RETRO68="/path/to/Retro68-build/toolchain`
- `make`

Pre-built binaries are provided: ASCTester.dsk is an 800k disk image suitable for use in emulators, and ASCTester.bin is a MacBinary file.

## How to use

When you run this program, you should hear a a few blips from the speaker. It may take a while for the tests to complete. Don't move the mouse or press the keyboard during the tests. When it's finished, a window will pop up containing test results. It also writes the results to a file called "ASCTester Results" next to the app. As the results have gotten bigger, they might not fully fit on screen on models with smaller screens, so the file is most useful for reporting results.

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
- **MaskedLatch a (h): b $xx $ss c $yy n** &mdash; tests whether VIA2 (or the chip standing in for it; some are Apple ASIC reimplementations) latches the ASC interrupt in its IFR while it's masked in its IER, whether the flag stays once the ASC releases its line, and whether enabling the ASC afterwards delivers it. With the ASC masked and CPU interrupts off, fills the FIFO (on the original ASC, the full condition drives the line), reads $804 (which releases the line on the original ASC), then enables the ASC in the IER and turns CPU interrupts back on:
  - **a** is 1 if the test was performed
  - **h** is 1 if the FIFO IRQ test saw no full interrupt: the test then writes 768 samples instead, and polls for IFR bit 4 as the FIFO drains past half empty, so that the steps below apply to the half empty condition
  - **b** is 1 if IFR bit 4 was set after the FIFO filled up (or drained past half empty)
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
- **Threshold a (b): c d e, refill f g h** &mdash; measures the FIFO level at which the half empty condition happens, with VIA1 timer 2 (783.36 kHz on most models). Each of 16 trials writes 768 samples into an empty FIFO as fast as possible, waits for the half empty bit, writes 256 more samples and waits for the half empty bit again. Each trial starts at a slightly different point of the sample period.
  - **a** is 1 if the test was performed, **b** is the number of trials where a flag never showed up
  - **c d e** are the shortest, average and longest times in VIA ticks from the first write to the first half empty flag
  - **f g h** are the same for the time between the two half empty flags, which is exactly 256 sample periods: 256 &times; 35.2 = 9011 ticks at 22257 Hz
- **Samples to half: a b c** &mdash; the shortest, average and longest times from the first write to the half empty flag, in sample periods (calibrated with the refill time above). The FIFO loses one sample per period from the first write, so if the half empty condition happens when the level goes from N to N-1, this is 768 - (N - 1) periods minus up to one period, plus a small polling delay. Rounding the shortest value up gives 768 - (N - 1): 257 means the condition is on 512 to 511, 258 means 511 to 510.
- **Full: writes a b, level c d e** &mdash; 16 trials writing into an empty FIFO until the full bit sets: **a** and **b** are the fewest and most writes, **c d e** the lowest, average and highest FIFO level at that point (writes minus the samples played meanwhile, from the time it took). Each trial is off by up to one sample either way; the average is the FIFO level at which the full bit sets, minus a small polling delay (about 0.2 to 0.5).
- **HalfAck a: p b $ff c (t) d $xx e $yy, F29 f g, refill h $rr i (t2) $zz** &mdash; tests what releases the half empty interrupt, for variants where $804 holds levels rather than flags that a read clears (and that interrupt only when the FIFO drains past half empty). With the ASC masked in VIA2's IER and CPU interrupts off, writes 768 samples into an empty FIFO and polls the IFR:
  - **a** is 1 if the test was performed
  - **p** is 1 if VIA2 still flagged an ASC interrupt right after reading $804 and clearing the flag, before the fill (something else holding the line)
  - **b** is 1 if VIA2 flagged an ASC interrupt after the fill (the FIFO is above half but not full). A sample played at the moment the fill takes the FIFO through the half level can do that.
  - **$ff** is $804 read right after the fill, which also releases the line, so that the half empty condition below can make a new edge
  - **c** is 1 if VIA2 flagged an ASC interrupt as the FIFO drained past half empty, and **t** is when, in VIA ticks after the fill: 256 sample periods (about 9000 ticks) after the first write, minus the time the fill took. About 61440 means it didn't happen within about 80 ms. The rest is only done if c is 1.
  - **d** is 1 if the flag came back right after clearing it in VIA2, with $804 not read: the line is held (level triggered input, or a line held until $804 is read)
  - **$xx** is $804 read after that, and **e** is 1 if the flag came back after clearing it, after that read
  - **$yy** is $804 read again
  - **f** and **g** are 1 if the flag came back after writing 1 to F29 and after writing 0 to it, with the FIFO still below half (0 if there's no F29)
  - **h** is 1 if VIA2 flagged an ASC interrupt after writing 256 more samples, which brings the FIFO above half again, and **$rr** is $804 read right after that (it releases the line too)
  - **i** is 1 if VIA2 flagged an ASC interrupt as the FIFO drained past half empty again, and **t2** is when, in VIA ticks after the refill (also 256 sample periods after its first write)
  - **$zz** is $804 read at the end
- **Reg808 a: idle $xx $yy $ss, full b $xx $yy, half c (t) $xx $yy $ss** &mdash; tests whether $808, which isn't documented, reads like $804 without clearing it. With the ASC masked in VIA2's IER and CPU interrupts off, $804 is never read between the first and the last step:
  - **a** is 1 if the test was performed
  - **idle:** two reads of $808 with no flags in $804, then **$ss**, $804 read after them
  - **full:** **b** is 1 if VIA2 flagged an ASC interrupt after filling the FIFO up, then two reads of $808
  - **half:** **c** is 1 if VIA2 flagged an ASC interrupt again as the FIFO drained past half empty, after clearing the flag, and **t** is when, in VIA ticks (about 61440 means it didn't happen within about 80 ms). On the original ASC, which holds its line until $804 is read, that means reading $808 released the line. Then two reads of $808, and **$ss**, $804 read at the end: if it still has the full bits, reading $808 didn't clear them
- **ModeEntry a: 0>1 f $ss, 1>1 f $ss, 2>1 f $ss, 0>1+clear f $ss, 0+clear f $ss, 0>1 above half f $ss, 0+clear>1 f $ss** &mdash; tests whether entering FIFO mode, or clearing the FIFO, flags anything. The other tests always read $804 right after setting FIFO mode, which would hide it. With the ASC masked in VIA2's IER and CPU interrupts off, each case starts from an empty FIFO in the given mode, with $804 read and the VIA2 flag cleared, then waits a few sample periods after the change. **f** is 1 if VIA2 flagged an ASC interrupt, **$ss** is $804 read after that:
  - **0>1**, **1>1**, **2>1**: mode 0, 1 (written again) or 2 to FIFO mode
  - **0>1+clear**: mode 0 to FIFO mode, then a FIFO clear (bit 7 of $803 set and cleared)
  - **0+clear**: a FIFO clear in mode 0
  - **0>1 above half**: as 0>1, but with 768 samples written in FIFO mode before going to mode 0 (it shows whether the FIFO keeps them, and whether entering FIFO mode above half flags anything)
  - **0+clear>1**: a FIFO clear in mode 0, then mode 0 to FIFO mode, without reading $804 in between: whether a condition latched by the clear in mode 0 shows up when entering FIFO mode
- **ModeGate a (h): b, mode 0 c, mode 1 d $ss** &mdash; tests whether a FIFO condition left pending in $804 survives a switch to mode 0 and back, and whether the interrupt output follows the mode. With the ASC masked in VIA2's IER and CPU interrupts off, raises the full condition, switches to mode 0, clears the VIA2 flag, then switches back to FIFO mode, never reading $804:
  - **a** is 1 if the test was performed
  - **h** is 1 if the FIFO IRQ test saw no full interrupt: the test then writes 768 samples and waits for the half empty condition instead
  - **b** is 1 if VIA2 flagged an ASC interrupt after the condition
  - **c** is 1 if VIA2 flagged it again after switching to mode 0 and clearing the flag (a level triggered input with the line still asserted)
  - **d** is 1 if VIA2 flagged it again after switching back to FIFO mode. With an edge triggered input (IIci), that means the line was released in mode 0 and asserted again in FIFO mode, with the condition still latched
  - **$ss** is $804 read at the end: whether the flags survived the mode changes
- **Reg806 $ii: $00>$aa $EE>$bb $FF>$cc $55>$dd $AA>$ee, restored $rr** &mdash; tests whether $806 (the volume) reads back what was written. System 7.5.5 checks this on the $BB variant: if writing $00 and then $EE to $806 reads back $EE, it reports the sound hardware as 'whit' instead of 'asc ', which loads a sound output component that reads $808 and uses bit 1 of it to choose how it handles the FIFO. With CPU interrupts off:
  - **$ii** is $806 before the test
  - each **$vv>$xx** is a value written to $806 and what it read back right after
  - **$rr** is $806 read after writing $ii back
- **Levels X: $bb $ff (t), $vv@t, ...** &mdash; records $804 as a FIFO fills up and drains, to show which bits are levels and which are latched flags (a read clears those, so they show up once and then go away), and what each bit means on variants that differ from the original ASC. With interrupts off, $500 samples are written into an empty FIFO as fast as possible (more than it holds, as it drains meanwhile, so it ends up full), then $804 is polled until about 78 ms after the first write. **X** is the FIFO written: in stereo, **A** and **B** on their own and then **AB** for both; in mono, only **A**.
  - **$bb** is $804 right before the first write, **$ff** right after the last one, and **t** when that was, in VIA ticks after the first write
  - then each new value of $804, up to 10, with when it changed in VIA ticks after the first write. A sample period is about 35.2 ticks, so a FIFO filled up at time 0 is half empty at about 18000 and empty at about 36000 (counting from the end of the fill, if writing the samples took longer than playing them)

All the results are also saved to a text file named "ASCTester Results" next to the application.

Recent versions of Retro68's RetroConsole link in most of libstdc++'s iostreams and locale support, which makes the application about 1.2M, more than the 1M partition Retro68's default SIZE resource asks for: under System 7 it then fails to load its code and stops with an "unimplemented trap" system error. `make SIMPLE_CONSOLE=1` builds a version without RetroConsole (about 70K) that shows the results in a plain window instead; click to quit.

## Expected results gathered from working hardware

Several tests are counts that display a number other than 0 or 1. **The large numbers in parentheses other than 50000 will vary between runs.** The important thing is that it's a value greater than 0 and much less than 50000.

### Mac IIci

```
ASCTester test version 5
BoxFlag: 5   ASC Version: $00   System 7.1.0
AddrMapFlags: $0000773F
F09: 0 ($00)  F29: 0 ($00)
804Idle: $00  M0: 1 M1: 1 M2: 1 ($00)
Mono: 1 1 Stereo: 1 1
Mono FIFO Tests:
0 0 1 0 1 0 1 0 1 0 0 0 (1104 0)
Stereo FIFO Tests:
0 0 1 1 1 1 1 1 1 1 0 0 (1116 1116)
VIA2 (1 $0013) 1
Idle IRQ 0 0 0 (0), 0 0 0 (0), 0 0 0
FIFO IRQ 1 0 0 0
(2 2), (1 1), (0 0), (0 0), 1
3 IRQs (start time = 3952):
$0A at t =  3952, $0A at t =  3952, $05 at t =  3953
Cancel 1 0 1 0 0 $0F 0
Timer2: 1 (ACR $0C IER $A7)
StatusWrite 1 (0) repeat 0, clear $00 $00 0:
$01: 1 $01 $00 0, $02: 1 $02 $00 0, $04: 1 $04 $00 0, $08: 1 $08 $00 0,
$0F: 1 $0F $00 0, $10: 0 $00 $00 0, $80: 0 $00 $00 0, $00: 0 $00 $00 0
MaskedLatch 1 (0): 1 $11 $0A 1 $91 1
IRQLine 1: 0 1 0 0 (61449) $0F 0
Threshold 1 (0): 8983 8999 9016, refill 9010 9011 9012
Samples to half: 255.20 255.65 256.14
Full: writes 1142 1142, level 1023.76 1023.77 1023.79
HalfAck 1: 0 0 $00 1 (6970) 0 $05 0 $00, F29 0 0, refill 0 $00 1 (8337) $05
Reg808 1: idle $00 $00 $00, full 1 $00 $00, half 0 (61448) $00 $00 $0F
ModeEntry 1: 0>1 0 $00, 1>1 0 $00, 2>1 0 $00,
0>1+clear 0 $00, 0+clear 0 $00, 0>1 above half 0 $00,
0+clear>1 0 $00
ModeGate 1 (0): 1, mode 0 0, mode 1 0 $0A
Reg806 $60: $00>$00 $EE>$E0 $FF>$E0 $55>$40 $AA>$A0, restored $60
Levels A: $00 $02 (1167), $00@1180, $01@19188, $00@19201
Levels B: $00 $08 (1648), $00@1661, $04@19639, $00@19652
Levels AB: $00 $0A (2291), $00@2307, $05@20306, $00@20319
```

Note: The FIFO IRQ results sometimes vary slightly on this machine slightly. The existing tests might need to be tweaked to make it more repeatable.

### LC

```
ASCTester test version 5
BoxFlag: 13   ASC Version: $E8   System 7.1.0
AddrMapFlags: $0000773F
F09: 0 ($00)  F29: 0 ($00)
804Idle: $03  M0: 0 M1: 1 M2: 0 ($01)
Mono: 1 1 Stereo: 0 0
Mono FIFO Tests:
0 0 1 0 1 0 1 0 1 0 1 0 (1196 0)
VIA2 (1 $0013) 1
Idle IRQ 1 1 1 (50000), 0 0 0 (0), 0 0 0
FIFO IRQ 1 1 0 0
(0 0), (536 536), (50000 50000), (0 0), 0
50536 IRQs (start time = 1908):
$01 at t =  1910, $01 at t =  1910, $01 at t =  1910, $01 at t =  1910,
$01 at t =  1910, $01 at t =  1910, $01 at t =  1910, $01 at t =  1910,
$01 at t =  1910, $01 at t =  1910, $01 at t =  1910, $01 at t =  1910, ...
Cancel 0 0 0 0 0 $00 0
Timer2: 1 (ACR $0C IER $A6)
StatusWrite 1 (1) repeat 1, clear $03 $03 1:
$01: 1 $03 $03 1, $02: 1 $03 $03 1, $04: 1 $03 $03 1, $08: 1 $03 $03 1,
$0F: 1 $03 $03 1, $10: 1 $03 $03 1, $80: 1 $03 $03 1, $00: 1 $03 $03 1
MaskedLatch 1 (1): 1 $92 $01 1 $92 50000
IRQLine 1: 1 0 0 1 (17944) $01 1
Threshold 1 (0): 9061 9085 9104, refill 9009 9010 9011
Samples to half: 257.44 258.13 258.67
Full: writes 1271 1272, level 1021.06 1021.45 1022.11
HalfAck 1: 1 0 $00 1 (5295) 1 $01 1 $01, F29 0 0, refill 0 $00 1 (7736) $01
Reg808 1: idle $00 $00 $03, full 0 $00 $00, half 1 (17975) $00 $00 $01
ModeEntry 1: 0>1 1 $03, 1>1 1 $03, 2>1 1 $03,
0>1+clear 1 $03, 0+clear 1 $03, 0>1 above half 0 $00,
0+clear>1 1 $03
ModeGate 1 (1): 1, mode 0 1, mode 1 1 $01
Reg806 $60: $00>$00 $EE>$E0 $FF>$E0 $55>$40 $AA>$A0, restored $60
Levels A: $03 $02 (2357), $00@2384, $01@20365, $03@38318
```

### LC II

```
ASCTester test version 5
BoxFlag: 31   ASC Version: $E8   System 7.1.0
AddrMapFlags: $0000773F
F09: 0 ($00)  F29: 0 ($00)
804Idle: $03  M0: 0 M1: 1 M2: 0 ($01)
Mono: 1 1 Stereo: 0 0
Mono FIFO Tests:
0 0 1 0 1 0 1 0 1 0 1 0 (1149 0)
VIA2 (1 $0013) 1
Idle IRQ 1 1 1 (50000), 0 0 0 (0), 0 0 0
FIFO IRQ 1 1 0 0
(0 0), (591 591), (50000 50000), (0 0), 0
50591 IRQs (start time = 3766):
$01 at t =  3768, $01 at t =  3768, $01 at t =  3768, $01 at t =  3768,
$01 at t =  3768, $01 at t =  3768, $01 at t =  3768, $01 at t =  3768,
$01 at t =  3768, $01 at t =  3768, $01 at t =  3768, $01 at t =  3768, ...
Cancel 0 0 0 0 0 $00 0
Timer2: 1 (ACR $0C IER $A6)
StatusWrite 1 (1) repeat 1, clear $03 $03 1:
$01: 1 $03 $03 1, $02: 1 $03 $03 1, $04: 1 $03 $03 1, $08: 1 $03 $03 1,
$0F: 1 $03 $03 1, $10: 1 $03 $03 1, $80: 1 $03 $03 1, $00: 1 $03 $03 1
MaskedLatch 1 (1): 1 $10 $01 1 $90 50000
IRQLine 1: 1 0 0 1 (17962) $01 1
Threshold 1 (0): 9054 9078 9099, refill 9009 9010 9012
Samples to half: 257.25 257.93 258.52
Full: writes 1200 1201, level 1021.00 1021.42 1021.92
HalfAck 1: 1 0 $00 1 (4812) 1 $01 1 $01, F29 0 0, refill 0 $00 1 (8059) $01
Reg808 1: idle $00 $00 $03, full 0 $00 $00, half 1 (17994) $00 $00 $01
ModeEntry 1: 0>1 1 $03, 1>1 1 $03, 2>1 1 $03,
0>1+clear 1 $03, 0+clear 1 $03, 0>1 above half 0 $00,
0+clear>1 1 $03
ModeGate 1 (1): 1, mode 0 1, mode 1 1 $01
Reg806 $60: $00>$00 $EE>$E0 $FF>$E0 $55>$40 $AA>$A0, restored $60
Levels A: $03 $02 (1865), $00@1890, $01@19858, $03@37829
```

### LC III

```
ASCTester test version 5
BoxFlag: 21   ASC Version: $BC   System 7.1.0
AddrMapFlags: $0000773F
F09: 1 ($01)  F29: 1 ($01)
804Idle: $0E  M0: 0 M1: 1 M2: 0 ($01)
Mono: 1 0 Stereo: 0 1
Stereo FIFO Tests:
1 0 1 1 1 1 0 1 0 1 1 1 (0 1127)
VIA2 (1 $001F) 1
Idle IRQ 0 0 0 (0), 1 1 1 (50000), 1 1 1
FIFO IRQ 1 0 0 0
(0 0), (1105 1105), (50000 50000), (0 0), 0
51105 IRQs (start time = 1703):
$06 at t =  1705, $06 at t =  1705, $06 at t =  1705, $06 at t =  1705,
$06 at t =  1705, $06 at t =  1705, $06 at t =  1705, $06 at t =  1705,
$06 at t =  1705, $06 at t =  1705, $06 at t =  1705, $06 at t =  1705, ...
Cancel 0 0 0 0 0 $00 0
Timer2: 1 (ACR $0C IER $A6)
StatusWrite 1 (1) repeat 1, clear $0E $0E 1:
$01: 1 $0E $0E 1, $02: 1 $0E $0E 1, $04: 1 $0E $0E 1, $08: 1 $0E $0E 1,
$0F: 1 $0E $0E 1, $10: 1 $0E $0E 1, $80: 1 $0E $0E 1, $00: 1 $0E $0E 1
MaskedLatch 1 (1): 1 $92 $06 1 $92 50000
IRQLine 1: 1 0 0 1 (17996) $06 1
Threshold 1 (0): 9053 9067 9085, refill 9010 9010 9012
Samples to half: 257.22 257.61 258.13
Full: writes 1131 1132, level 1021.25 1021.68 1022.25
HalfAck 1: 1 0 $02 1 (6783) 1 $06 1 $06, F29 0 1, refill 0 $02 1 (8348) $06
Reg808 1: idle $00 $00 $0E, full 0 $00 $00, half 1 (18002) $00 $00 $06
ModeEntry 1: 0>1 1 $0E, 1>1 1 $0E, 2>1 1 $0E,
0>1+clear 1 $0E, 0+clear 1 $0E, 0>1 above half 0 $02,
0+clear>1 1 $0E
ModeGate 1 (1): 1, mode 0 1, mode 1 1 $06
Reg806 $E0: $00>$00 $EE>$E0 $FF>$E0 $55>$40 $AA>$A0, restored $E0
Levels A: $0E $0A (1168), $02@1181, $06@19170, $0E@37123
Levels B: $0E $0A (1651), $02@1664, $06@19652, $0E@37602
Levels AB: $0E $0A (2139), $02@2164, $06@20153, $0E@38103
```

### LC 475

```
ASCTester test version 5
BoxFlag: 83   ASC Version: $BB   System 7.5.3
AddrMapFlags: $0500183F
F09: 1 ($01)  F29: 1 ($01)
804Idle: $0E  M0: 1 M1: 1 M2: 0 ($01)
Mono: 1 0 Stereo: 0 1
Stereo FIFO Tests:
1 0 1 1 1 1 0 1 0 1 1 1 (0 1111)
VIA2 (1 $0000) 1
Idle IRQ 0 0 0 (0), 1 0 0 (1), 1 0 0
FIFO IRQ 1 0 0 0
(0 0), (1 1), (0 0), (0 0), 0
1 IRQs (start time = 4957):
$06 at t =  4958
Cancel 1 0 1 0 0 $0E 0
Timer2: 1 (ACR $0C IER $A6)
StatusWrite 1 (0) repeat 0, clear $0E $0E 0:
$01: 0 $0E $0E 0, $02: 0 $0E $0E 0, $04: 0 $0E $0E 0, $08: 0 $0E $0E 0,
$0F: 0 $0E $0E 0, $10: 0 $0E $0E 0, $80: 0 $0E $0E 0, $00: 0 $0E $0E 0
MaskedLatch 1 (1): 1 $92 $06 1 $92 1
IRQLine 1: 0 0 0 1 (18004) $06 0
Threshold 1 (0): 8990 9004 9019, refill 9010 9011 9012
Samples to half: 255.40 255.80 256.22
Full: writes 1112 1113, level 1022.97 1023.69 1023.94
HalfAck 1: 0 0 $02 1 (7783) 0 $06 0 $06, F29 0 1, refill 0 $02 1 (8556) $06
Reg808 1: idle $00 $00 $0E, full 0 $00 $00, half 1 (18007) $00 $00 $06
ModeEntry 1: 0>1 0 $0E, 1>1 0 $0E, 2>1 0 $0E,
0>1+clear 0 $0E, 0+clear 0 $0E, 0>1 above half 0 $02,
0+clear>1 0 $0E
ModeGate 1 (1): 1, mode 0 0, mode 1 0 $06
Reg806 $60: $00>$00 $EE>$E0 $FF>$E0 $55>$40 $AA>$A0, restored $60
Levels A: $0E $0A (1100), $02@1111, $06@19097, $0E@37117
Levels B: $0E $0A (1323), $02@1333, $06@19318, $0E@37343
Levels AB: $0E $0A (1584), $02@1615, $06@19600, $0E@37625
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
