#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define ASCBase				0xCC0
#define VIA2Base			0xCEC
#define VIA2DT				0xD70
#define ApplScratch			0xA78

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
	uint8_t padding2[0x19];		// 0xF10-0xF28
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
	uint8_t ascVersion;
	uint16_t via2RepeatOffset;
	uint8_t initialASCMode;
	uint8_t initialASCControl;
	bool ascRejects801Write00;
	bool ascRejects801Write01;
	uint8_t initialFifoStatus;
	uint8_t idleFifoStatus;
	uint16_t fifoABytesToFull;
	uint16_t fifoBBytesToFull;
	uint32_t iterationsToFifoAHalfEmpty;
	uint32_t iterationsToFifoBHalfEmpty;
	uint32_t iterationsToFifoAEmpty;
	uint32_t iterationsToFifoBEmpty;
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

static uint32_t *scratchData(void)
{
	return (uint32_t *)ApplScratch;
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
static void ASCIRQHandler(void)
{
	(*scratchData())++;
}

static TestResults results;
static union
{
	uint8_t bytes[0x200];
	uint32_t words[0x80];
} buf;

int main(void)
{
	// Disable IRQs, remember old state
	uint16_t oldSR = DisableIRQ();
	//VIA2Handler oldASCIRQHandler = via2Handlers()[4];

	//*scratchData() = 0;

	// Install our custom IRQ handler
	//via2Handlers()[4] = ASCIRQHandler;
	results.ascVersion = asc()->version;
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

	results.initialASCControl = asc()->control;

	// Read once to clear, in case there was an IRQ
	results.initialFifoStatus = asc()->fifoIRQStatus;
	// Now read the actual status
	results.idleFifoStatus = asc()->fifoIRQStatus;

	// Write sound data until FIFO full
	asc()->mode = 1;
	asc()->control = results.initialASCControl | 0x02; // Stereo
	for (int i = 0; i < 0x200; i++)
	{
		uint8_t nextSample = (i & 0xFF);
		asc()->fifoA[0] = nextSample;
		asc()->fifoB[0] = nextSample;
	}
	asc()->fifoIRQStatus; // Make sure status bits are clear after reconfig
	int totalWritten = 0x200;

	while (totalWritten < 0x1000)
	{
		const uint8_t irqStat = asc()->fifoIRQStatus;
		if ((irqStat & 0x2) && (results.fifoABytesToFull == 0))
		{
			results.fifoABytesToFull = totalWritten;
		}
		if ((irqStat & 0x8) && (results.fifoBBytesToFull == 0))
		{
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
	}

	for (int i = 1; i < 1000000; i++)
	{
		const uint8_t irqStat = asc()->fifoIRQStatus;
		if ((irqStat & 0x1) && (results.iterationsToFifoAHalfEmpty == 0))
		{
			results.iterationsToFifoAHalfEmpty = i;
		}
		if ((irqStat & 0x4) && (results.iterationsToFifoBHalfEmpty == 0))
		{
			results.iterationsToFifoBHalfEmpty = i;
		}
		if ((irqStat & 0x2) && (results.iterationsToFifoAHalfEmpty != 0) &&
			(results.iterationsToFifoAEmpty == 0))
		{
			results.iterationsToFifoAEmpty = i;
		}
		if ((irqStat & 0x8) && (results.iterationsToFifoBHalfEmpty != 0) &&
			(results.iterationsToFifoBEmpty == 0))
		{
			results.iterationsToFifoBEmpty = i;
		}
	}

	// Clean up, to avoid confusing the Sound Manager
	asc()->mode = results.initialASCMode;
	asc()->control = results.initialASCControl;
	asc()->fifoIRQStatus;
	// All done, restore old IRQ handler and IRQ state
	//via2Handlers()[4] = oldASCIRQHandler;
	RestoreIRQ(oldSR);

	printf("ASC Version: $%02X\n", results.ascVersion);
	if (results.via2RepeatOffset)
	{
		printf("VIA2 repeats every $%02X bytes\n", results.via2RepeatOffset);
	}
	else
	{
		printf("No VIA2 repetition observed in first $200 bytes\n");
	}

	// Reg $801 tests
	printf("ASC reg $801 is initially $%02X\n", results.initialASCMode);
	if (results.ascRejects801Write00)
	{
		printf("ASC rejects $00 write to reg $801\n");
	}
	if (results.ascRejects801Write01)
	{
		printf("ASC rejects $01 write to reg $801\n");
	}

	// Reg $802 tests
	printf("ASC reg $802 is initially $%02X\n", results.initialASCControl);

	// Reg $804 tests
	printf("Reg $804 is $%02X initially\n", results.initialFifoStatus);
	printf("Reg $804 is $%02X at idle\n", results.idleFifoStatus);
	if (results.fifoABytesToFull)
	{
		printf("Reg $804 showed FIFO A full after %u bytes\n", results.fifoABytesToFull);
	}
	else
	{
		printf("Reg $804 never showed FIFO A full\n");
	}
	if (results.fifoBBytesToFull)
	{
		printf("Reg $804 showed FIFO B full after %u bytes\n", results.fifoBBytesToFull);
	}
	else
	{
		printf("Reg $804 never showed FIFO B full\n");
	}
	if (results.iterationsToFifoAHalfEmpty)
	{
		printf("Reg $804 showed FIFO A half empty %u iterations after full\n", results.iterationsToFifoAHalfEmpty);
	}
	else
	{
		printf("Reg $804 never showed FIFO A half empty\n");
	}
	if (results.iterationsToFifoBHalfEmpty)
	{
		printf("Reg $804 showed FIFO B half empty %u iterations after full\n", results.iterationsToFifoBHalfEmpty);
	}
	else
	{
		printf("Reg $804 never showed FIFO B half empty\n");
	}
	if (results.iterationsToFifoAEmpty)
	{
		printf("Reg $804 showed FIFO A empty %u iterations after full\n", results.iterationsToFifoAEmpty);
	}
	else
	{
		printf("Reg $804 never showed FIFO A empty\n");
	}
	if (results.iterationsToFifoBEmpty)
	{
		printf("Reg $804 showed FIFO B empty %u iterations after full\n", results.iterationsToFifoBEmpty);
	}
	else
	{
		printf("Reg $804 never showed FIFO B empty\n");
	}

	getchar();

	return 0;
}
