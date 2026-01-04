#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define ASCBase				0xCC0
#define VIA2Base			0xCEC
#define VIA2DT				0xD70
#define ApplScratch			0xA78
#define BoxFlag				0xCB3
#define Ticks				0x16A

// Register offsets thanks to:
// https://github.com/mamedev/mame/blob/master/src/devices/sound/asc.cpp
typedef struct ASC
{
	uint8_t fifoA[0x400];
	uint8_t fifoB[0x400];
	uint8_t version;			// 0x800
	uint8_t mode;				// 0x801
	uint8_t control;			// 0x802
	uint8_t fifoMode;			// 0x803
	uint8_t fifoIRQStatus;		// 0x804
	uint8_t wavetableControl;	// 0x805
	uint8_t volume;				// 0x806
	uint8_t clockRate;			// 0x807
	uint8_t padding[0x701];		// 0x808-0xF08
	uint8_t irqA;				// 0xF09
	uint8_t padding2[0x1F];		// 0xF0A-0xF28
	uint8_t irqB;				// 0xF29
} ASC;

// Struct members with "new" in the name are used in V8, Sonora, etc.
// Struct members with "old" in the name are used on II, IIx, SE/30.
// Struct members with "both" in the name theoretically work on both.
typedef struct VIA
{
	uint8_t bufferBBoth;		// 0x00
	uint8_t padding[2];			// 0x01-0x02
	uint8_t irqFlagsNew;		// 0x03
	uint8_t padding2[0xF];		// 0x04-0x12
	uint8_t irqEnableNew;		// 0x13
	uint8_t padding3[0x19EC];	// 0x14-0x19FF
	uint8_t irqFlagsOld;		// 0x1A00
	uint8_t padding4[2];		// 0x1A01-0x1A02
	uint8_t irqFlagsBoth;		// 0x1A03
	uint8_t padding5[0x1FC];	// 0x1A04-0x1BFF
	uint8_t irqEnableOld;		// 0x1C00
	uint8_t padding6[0x12];		// 0x1C01-0x1C12
	uint8_t irqEnableBoth;		// 0x1C13
} VIA;

typedef void (*VIA2Handler)(void);

typedef struct TestResults
{
	uint8_t boxFlag;
	uint8_t ascVersion;
	uint16_t via2RepeatOffset;
	uint8_t initialASCMode;
	uint8_t initialASCControl;
	bool ascRejects801Write00;
	bool ascRejects801Write01;
	uint8_t initialFifoStatus;
	uint8_t idleFifoStatus;
	uint8_t via2InitialIER;
	uint8_t via2IERAfterEnable;
	uint16_t fifoABytesToFull;
	uint16_t fifoBBytesToFull;
	uint32_t iterationsToFifoAHalfEmpty;
	uint32_t iterationsToFifoBHalfEmpty;
	uint32_t iterationsToFifoAEmpty;
	uint32_t iterationsToFifoBEmpty;
	uint32_t ascIrqCountBeforeFinalWait;
	uint32_t ascIrqCount;
	uint32_t ascIrqTicks[20];
	uint32_t startWritingTicks;
	uint32_t fifoAFullTicks;
	uint32_t fifoBFullTicks;
	uint32_t fifoAHalfEmptyTicks;
	uint32_t fifoBHalfEmptyTicks;
	uint32_t fifoAEmptyTicks;
	uint32_t fifoBEmptyTicks;
	uint32_t finalWaitBeginTicks;
	uint32_t finalWaitEndTicks;
} TestResults;

// The base address of the ASC, grabbed from Mac's globals
static inline volatile ASC *asc(void)
{
	return *(ASC **)ASCBase;
}

// The base address of VIA2, grabbed from Mac's globals
static inline volatile VIA *via2(void)
{
	return *(VIA **)VIA2Base;
}

// The VIA2 dispatch table
static inline volatile VIA2Handler *via2Handlers(void)
{
	return (VIA2Handler *)VIA2DT;
}

// The number of ticks that have elapsed since startup
static inline uint32_t ticks(void)
{
	return *(uint32_t *)Ticks;
}

// Pointer to our IRQ count variable (stored in global data so IRQ handler can access it)
static uint32_t *irqCountScratchData(void)
{
	return (uint32_t *)ApplScratch;
}

// Pointer to our "last IRQ time in ticks" variable (also stored in global data)
static uint32_t *irqLastTimeScratchData(void)
{
	return (uint32_t *)(ApplScratch + 4);
}

// Disables interrupts and returns the old SR so they can be restored to what they were
static inline uint16_t DisableIRQ(void)
{
	uint16_t sr;

	__asm__ volatile (
		"move.w %%sr,%0\n\t"     /* read current SR */
		"ori.w  #0x0700,%%sr"    /* set IPL = 7 (disable interrupts) */
		: "=d"(sr)
		:
		: "cc"
	);

	return sr;
}

// Restores the IRQ state to what it was before
static inline void RestoreIRQ(uint16_t sr)
{
	__asm__ volatile (
		"move.w %0,%%sr"
		:
		: "d"(sr)
		: "cc"
	);
}

// Simple ASC IRQ handler that increments a counter.
// Uses ApplScratch for simplicity. Should be safe as long
// as we stick to simple stuff during ASC tests.
// The Mac ROM saves A0-A3 and D0-D3 before executing this,
// so we shouldn't need to bother saving anything as long
// as it stays simple.
static void ASCIRQHandler(void)
{
	// Acknowledge the IRQ
	via2()->irqFlagsBoth = 0x90;

	// Save the tick counter when it occurred, and increment our counter
	*irqLastTimeScratchData() = ticks();
	(*irqCountScratchData())++;
}

static TestResults results;
static union
{
	uint8_t bytes[0x200];
	uint32_t words[0x80];
} buf;

// Checks to see if an IRQ has happened since the last time we checked, and if so, saves
// the Ticks variable in an array so we can see when it happened
static void CheckIRQTimes(void)
{
	static uint32_t lastIRQCount = 0;
	const uint32_t newIRQCount = *irqCountScratchData();
	const uint32_t newIRQTicks = *irqLastTimeScratchData();
	if (newIRQCount != lastIRQCount)
	{
		for (uint32_t i = lastIRQCount; i < newIRQCount && i < 20; i++)
		{
			results.ascIrqTicks[i] = newIRQTicks;
		}
		lastIRQCount = newIRQCount;
	}
}

int main(void)
{
	// Disable IRQs, remember old state of IRQs and the original ASC IRQ handler
	uint16_t oldSR = DisableIRQ();
	VIA2Handler oldASCIRQHandler = via2Handlers()[4];

	// Clear out the scratch data that our new IRQ handler will use
	(*irqCountScratchData()) = 0;
	(*irqLastTimeScratchData()) = 0;

	// Install our custom IRQ handler
	via2Handlers()[4] = ASCIRQHandler;
	results.boxFlag = *(uint8_t *)BoxFlag;
	results.ascVersion = asc()->version;

	// Attempt to calculate how often VIA2 space repeats
	uint8_t *readLoc = *(uint8_t **)VIA2Base;
	for (int i = 0; i < 0x200; i++)
	{
		buf.bytes[i] = *readLoc++;
	}

	for (int i = 1; i < 0x80; i++)
	{
		if (buf.words[i] == buf.words[0])
		{
			results.via2RepeatOffset = i * 4;
			break;
		}
	}

	// Double-check if via2RepeatOffset is 4.
	// It could mean the entire reading is the same, in which case it's all ONE register.
	if (results.via2RepeatOffset == 4)
	{
		bool foundNonMatch = false;
		for (int i = 1; i < 0x200; i++)
		{
			// If we found a single register that DOESN'T match the
			// very first register, then we are good to go and it's
			// truly repeating register space, not just 0x200 repetitions
			// of the same register.
			if (buf.bytes[i] != buf.bytes[0])
			{
				foundNonMatch = true;
				break;
			}
		}

		// If every byte was identical, it means the entire address space is all the same value,
		// so we may be looking at an old-style real VIA with huge offsets.
		if (!foundNonMatch)
		{
			results.via2RepeatOffset = 0xFFFF;
		}
	}

	// Check the mode register
	results.initialASCMode = asc()->mode;
	if (results.initialASCMode != 0)
	{
		asc()->mode = 0;
		if (asc()->mode != 0)
		{
			results.ascRejects801Write00 = true;
		}
	}
	else
	{
		asc()->mode = 1;
		if (asc()->mode != 1)
		{
			results.ascRejects801Write01 = true;
		}
		asc()->mode = 0;
	}

	// Check the control register
	results.initialASCControl = asc()->control;

	// Read once to clear, in case there was an IRQ
	results.initialFifoStatus = asc()->fifoIRQStatus;
	// Now read the actual status
	results.idleFifoStatus = asc()->fifoIRQStatus;

	// Determine if the IRQ is already enabled in VIA2
	results.via2InitialIER = via2()->irqEnableBoth;
	// If it wasn't already enabled, try to turn it on
	if (!(results.via2InitialIER & 0x10))
	{
		via2()->irqEnableBoth = 0x90;
	}
	results.via2IERAfterEnable = via2()->irqEnableBoth;

	// Write sound data until FIFO full. Start by priming with 0x200 samples
	asc()->mode = 1;
	asc()->control = results.initialASCControl | 0x02; // Stereo
	asc()->fifoIRQStatus; // Make sure status bits are clear after reconfig
	// Clear any pending IRQ before we do anything
	via2()->irqFlagsBoth = 0x90;

	// If this is a Sonora-based VIA, enable IRQs in the ASC now
	if ((results.ascVersion & 0xF0) == 0xB0)
	{
		asc()->irqB = 0;
	}

	// Now enable IRQs and start doing stuff
	RestoreIRQ(oldSR);
	results.startWritingTicks = ticks();
	CheckIRQTimes();
	for (int i = 0; i < 0x200; i++)
	{
		uint8_t nextSample = (i & 0xFF);
		asc()->fifoA[0] = nextSample;
		asc()->fifoB[0] = nextSample;
		CheckIRQTimes();
	}
	int totalWritten = 0x200;

	// Continue writing until the FIFOs are full
	while (totalWritten < 0x1000)
	{
		const uint8_t irqStat = asc()->fifoIRQStatus;
		if ((irqStat & 0x2) && (results.fifoABytesToFull == 0))
		{
			results.fifoAFullTicks = ticks();
			results.fifoABytesToFull = totalWritten;
		}
		if ((irqStat & 0x8) && (results.fifoBBytesToFull == 0))
		{
			results.fifoBFullTicks = ticks();
			results.fifoBBytesToFull = totalWritten;
		}

		if ((results.fifoABytesToFull != 0) &&
			(results.fifoBBytesToFull != 0))
		{
			break;
		}

		uint8_t nextSample = totalWritten & 0xFF;
		asc()->fifoA[0] = nextSample;
		asc()->fifoB[0] = nextSample;
		totalWritten++;
		CheckIRQTimes();
	}

	for (int i = 1; i < 1000000; i++)
	{
		const uint8_t irqStat = asc()->fifoIRQStatus;
		if ((irqStat & 0x1) && (results.iterationsToFifoAHalfEmpty == 0))
		{
			results.fifoAHalfEmptyTicks = ticks();
			results.iterationsToFifoAHalfEmpty = i;
		}
		if ((irqStat & 0x4) && (results.iterationsToFifoBHalfEmpty == 0))
		{
			results.fifoBHalfEmptyTicks = ticks();
			results.iterationsToFifoBHalfEmpty = i;
		}
		if ((irqStat & 0x2) && (results.iterationsToFifoAHalfEmpty != 0) &&
			(results.iterationsToFifoAEmpty == 0))
		{
			results.fifoAEmptyTicks = ticks();
			results.iterationsToFifoAEmpty = i;
		}
		if ((irqStat & 0x8) && (results.iterationsToFifoBHalfEmpty != 0) &&
			(results.iterationsToFifoBEmpty == 0))
		{
			results.fifoBEmptyTicks = ticks();
			results.iterationsToFifoBEmpty = i;
		}

		if ((results.iterationsToFifoAHalfEmpty != 0) &&
			(results.iterationsToFifoBHalfEmpty != 0) &&
			(results.iterationsToFifoAEmpty != 0) &&
			(results.iterationsToFifoBEmpty != 0))
		{
			break;
		}

		CheckIRQTimes();
	}

	oldSR = DisableIRQ();
	results.ascIrqCountBeforeFinalWait = *irqCountScratchData();
	RestoreIRQ(oldSR);

	// Now, let's wait for a while. See if any more IRQs come in.
	// Just poll the IRQ status register while we do this.
	results.finalWaitBeginTicks = ticks();
	for (int i = 0; i < 2000000; i++)
	{
		CheckIRQTimes();
	}
	results.finalWaitEndTicks = ticks();

	// Clean up, to avoid confusing the Sound Manager
	oldSR = DisableIRQ();
	// If this is a Sonora-based VIA, disable IRQs in the ASC now
	if ((results.ascVersion & 0xF0) == 0xB0)
	{
		asc()->irqB = 1;
	}
	asc()->mode = results.initialASCMode;
	asc()->control = results.initialASCControl;
	asc()->fifoIRQStatus;
	// Disable the IRQ if it was originally disabled
	if (!(results.via2InitialIER & 0x10))
	{
		via2()->irqEnableBoth = 0x10;
	}
	// Clear it as well
	via2()->irqFlagsBoth = 0x90;
	// Restore old IRQ handler and IRQ state
	via2Handlers()[4] = oldASCIRQHandler;
	// Save the IRQ count we accumulated
	results.ascIrqCount = *irqCountScratchData();
	RestoreIRQ(oldSR);

	// All done, now print results
	printf("BoxFlag: %02X\n", results.boxFlag);
	printf("ASC Version: $%02X\n", results.ascVersion);
	if (results.via2RepeatOffset == 0xFFFF)
	{
		printf("VIA2 reads the same from offset $0 to $200; probably a real VIA\n");
	}
	else if (results.via2RepeatOffset != 0)
	{
		printf("VIA2 repeats every $%02X bytes\n", results.via2RepeatOffset);
	}
	else
	{
		printf("No VIA2 repetition observed in first $200 bytes\n");
	}

	// Reg $801 tests
	printf("Reg $801 is initially $%02X\n", results.initialASCMode);
	if (results.ascRejects801Write00)
	{
		printf("ASC rejects $00 write to reg $801\n");
	}
	if (results.ascRejects801Write01)
	{
		printf("ASC rejects $01 write to reg $801\n");
	}

	// Reg $802 tests
	printf("Reg $802 is initially $%02X\n", results.initialASCControl);

	// VIA2 IER tests
	printf("VIA IER is initially $%02X\n", results.via2InitialIER);
	printf("VIA IER is $%02X after enabling\n", results.via2IERAfterEnable);

	// Reg $804 tests
	printf("Reg $804 is $%02X initially\n", results.initialFifoStatus);
	printf("Reg $804 is $%02X at idle\n", results.idleFifoStatus);
	if (results.fifoABytesToFull == 0x200)
	{
		if ((results.ascVersion & 0xF0) == 0xB0)
		{
			printf("Reg $804 showed FIFO A full immediately. This is normal for this ASC; it's for recording.\n");
		}
		else
		{
			printf("Reg $804 showed FIFO A full immediately. This bit is likely not related to playback.\n");
		}
	}
	else if (results.fifoABytesToFull)
	{
		printf("Reg $804 showed FIFO A full after %u bytes (ticks = %u)\n", results.fifoABytesToFull, results.fifoAFullTicks);
	}
	else
	{
		printf("Reg $804 never showed FIFO A full\n");
	}
	if (results.fifoBBytesToFull)
	{
		printf("Reg $804 showed FIFO B full after %u bytes (ticks = %u)\n", results.fifoBBytesToFull, results.fifoBFullTicks);
	}
	else
	{
		printf("Reg $804 never showed FIFO B full\n");
	}
	if (results.iterationsToFifoAHalfEmpty)
	{
		printf("Reg $804 showed FIFO A half empty %u iterations after full (ticks = %u)\n", results.iterationsToFifoAHalfEmpty, results.fifoAHalfEmptyTicks);
	}
	else
	{
		printf("Reg $804 never showed FIFO A half empty\n");
	}
	if (results.iterationsToFifoBHalfEmpty)
	{
		printf("Reg $804 showed FIFO B half empty %u iterations after full (ticks = %u)\n", results.iterationsToFifoBHalfEmpty, results.fifoBHalfEmptyTicks);
	}
	else
	{
		printf("Reg $804 never showed FIFO B half empty\n");
	}
	if (results.iterationsToFifoAEmpty)
	{
		printf("Reg $804 showed FIFO A empty %u iterations after full (ticks = %u)\n", results.iterationsToFifoAEmpty, results.fifoAEmptyTicks);
	}
	else
	{
		printf("Reg $804 never showed FIFO A empty\n");
	}
	if (results.iterationsToFifoBEmpty)
	{
		printf("Reg $804 showed FIFO B empty %u iterations after full (ticks = %u)\n", results.iterationsToFifoBEmpty, results.fifoBEmptyTicks);
	}
	else
	{
		printf("Reg $804 never showed FIFO B empty\n");
	}

	printf("We began writing sound data at ticks = %u\n", results.startWritingTicks);
	printf("We waited around doing nothing from ticks = %u to %u\n", results.finalWaitBeginTicks, results.finalWaitEndTicks);
	printf("A total of %d ASC IRQs were observed\n", results.ascIrqCount);
	printf("%d of these occurred after we were finished observing the flags.\n", results.ascIrqCount - results.ascIrqCountBeforeFinalWait);
	printf("IRQ times:\n");
	for (uint32_t i = 0; i < results.ascIrqCount && i < 20; i++)
	{
		printf("#%2u: %u\n", i + 1, results.ascIrqTicks[i]);
	}
	if (results.ascIrqCount > 20)
	{
		printf("<results truncated>\n");
	}

	getchar();

	return 0;
}
