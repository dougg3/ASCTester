#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <Gestalt.h>
#include "asctester.h"

// How many IRQs we receive before we consider it "flooding"
#define IRQ_FLOOD_TEST_COUNT				50000

typedef void (*ASCTestFunc)(void);

// Results for a FIFO test, kept in a different struct because we can test mono and stereo separately
struct FIFOTestResults
{
	bool aFullTooSoon;						// After writing 0x100 samples to the ASC, bit 1 of reg 0x804 is already 1,
											// which indicates it's probably not actually a playback FIFO bit
	bool bFullTooSoon;						// After writing 0x100 samples to the ASC, bit 3 of reg 0x804 is already 1,
											// which indicates it's probably not actually a playback FIFO bit
	bool aReachesFull;						// When we flood the ASC with writes, bit 1 of reg 0x804 eventually becomes 1
	bool bReachesFull;						// When we flood the ASC with writes, bit 3 of reg 0x804 eventually becomes 1
	bool aHalfEmptyIsOffWhenFull;			// (Only if aReachesFull) Bit 0 of reg 0x804 is 0 when we first notice bit 1 is 1
	bool bHalfEmptyIsOffWhenFull;			// (Only if bReachesFull) Bit 2 of reg 0x804 is 0 when we first notice bit 3 is 1
	bool aHalfEmptyTurnsOn;					// (Only if aReachesFull) Bit 0 of reg 0x804 eventually becomes 1 again
	bool bHalfEmptyTurnsOn;					// (Only if bReachesFull) Bit 2 of reg 0x804 eventually becomes 1 again
	bool aEmptyIsOffWhenHalfEmpty;			// (Only if aHalfEmptyTurnsOn) Bit 1 of reg 0x804 is 0 when we first notice bit 0
											// is 1 as the FIFO is draining
	bool bEmptyIsOffWhenHalfEmpty;			// (Only if bHalfEmptyTurnsOn) Bit 3 of reg 0x804 is 0 when we first notice bit 2
											// is 1 as the FIFO is draining
	bool aReachesEmpty;						// (Only if aReachesFull) we eventually notice bit 1 of reg 0x804 become 1 again
											// to indicate that FIFO A is completely empty
	bool bReachesEmpty;						// (Only if bReachesFull) we eventually notice bit 3 of reg 0x804 become 1 again
											// to indicate that FIFO B is completely empty
	uint32_t aFullCount;					// Number of samples written to FIFO A before it's marked as full
	uint32_t bFullCount;					// Number of samples written to FIFO B before it's marked as full
};

// Test results
struct TestResults
{
	uint8_t ascVersion;						// ASC revision identifier byte
	bool isSonoraVersion;					// High nibble of ASC revision is 0xB
	uint8_t boxFlag;						// Machine identifier byte
	long sysVersion;						// The system version
	bool regF09Exists;						// Whether reg 0xF09 appears to exist
	bool regF29Exists;						// Whether reg 0xF29 appears to exist
	uint8_t regF09InitialValue;				// Value of reg 0xF09 we first observe (if it exists)
	uint8_t regF29InitialValue;				// Value of reg 0xF29 we first observe (if it exists)
	uint8_t reg804IdleValue;				// Value of reg 0x804 when ASC is idle
	uint8_t reg801InitialValue;				// Value of reg 0x801 we first observe
	bool acceptsMode0;						// Allows writing 0 to reg 0x801
	bool acceptsMode1;						// Allows writing 1 to reg 0x801
	bool acceptsMode2;						// Allows writing 2 to reg 0x801
	bool acceptsConfigMono;					// Allows writing 0 to bit 1 of reg 0x802
	bool acceptsConfigStereo;				// Allows writing 1 to bit 1 of reg 0x802
	bool shouldTestMono;					// Whether we should actually test mono
	bool shouldTestStereo;					// Whether we should actually test stereo
	struct FIFOTestResults monoFIFO;		// FIFO test results in mono mode
	struct FIFOTestResults stereoFIFO;		// FIFO test results in stereo mode
	uint16_t via2AddressDecodeMask;			// Mask of bits that appear to be decoded inside the first 0x200 bytes of VIA2.
											// If it's 0, it likely means it's a real VIA with a different register every
											// 0x200 bytes in the VIA2 address space.
	bool via2MirroringOK;					// Whether the address mirroring of the VIA2 registers works correctly
	bool via2ReadbackConsistent;			// Whether we read back 2 identical copies of the beginning of VIA2 space during our test
	volatile uint32_t tmpIRQCount;			// Temporary counter used during IRQ tests
	bool idleIRQWithoutF29;					// An IRQ fires immediately when you enable IRQs without register F29 enabled
	bool idleIRQWithF29;					// An IRQ fires immediately when you enable IRQs with register F29 enabled
	bool refiresIdleIRQWithF29;				// An IRQ fires immediately if you disable and re-enable F29 again
	bool floodsIRQWithoutF29;				// Floods IRQ when idle without register F29 enabled
	bool floodsIRQWithF29;					// Floods IRQ when idle with register F29 enabled (if available)
	bool refiresIdleIRQFloodWithF29;		// Floods IRQ when idle if you disable and re-enable F29 again
	bool irqFloodWithoutF29TakesOverCPU;	// (Only if floodsIRQWithoutF29) the IRQ flood takes over the CPU
	bool irqFloodWithF29TakesOverCPU;		// (Only if floodsIRQWithF29) the IRQ flood takes over the CPU
	bool irqFloodRefireWithF29TakesOverCPU;	// (Only if refiresIdleIRQFloodWithF29) the IRQ flood takes over the CPU again
	uint32_t irqCountTest;					// Temporary variable
	bool testedFIFOIRQs;					// True if we actually tested FIFO IRQs. False if we didn't find
											// a working FIFO during our polling tests.
	bool fifoIRQTestedWasA;					// True if FIFO A was tested for IRQs; false if FIFO B was tested
	bool gotIRQOnFIFOHalfEmptyTooSoon;		// True if the half empty IRQ was too soon to be real (immediate IRQ)
	bool gotIRQOnFIFOEmptyTooSoon;			// True if the empty IRQ was too soon to be real
	volatile uint32_t fullIRQCount;			// Count of "FIFO full" IRQs we observed
	volatile uint32_t halfEmptyIRQCount;	// Count of "FIFO half empty" IRQs we observed
	volatile uint32_t emptyIRQCount;		// Count of "FIFO empty" IRQs we observed
	volatile uint32_t otherIRQCount;		// Count of other IRQs we observed
	uint32_t fullIRQMaxDiff;				// Maximum difference in fullIRQCount we see while waiting 4 seconds
	uint32_t halfEmptyIRQMaxDiff;			// Maximum difference in halfEmptyIRQCount we see while waiting 4 seconds
	uint32_t emptyIRQMaxDiff;				// Maximum difference in emptyIRQCount we see while waiting 4 seconds
	uint32_t otherIRQMaxDiff;				// Maximum difference in otherIRQCount we see while waiting 4 seconds
};

static void Test_MachineInfo(void);
static void Test_RegF09F29Exists(void);
static void Test_Reg804Idle(void);
static void Test_ModeRegisterConfigurable(void);
static void Test_MonoStereoConfigurable(void);
static void Test_FIFOFullHalfFullEmpty_Mono(void);
static void Test_FIFOFullHalfFullEmpty_Stereo(void);

static void Test_VIA2Repeat(void);
static void Test_VIA2Mirror(void);

static void Test_IdleIRQWithoutF29(void);
static void Test_IdleIRQWithF29(void);
static void Test_FIFOIRQ(void);

// List of all tests
static ASCTestFunc tests[] =
{
	Test_MachineInfo,
	Test_RegF09F29Exists,
	Test_Reg804Idle,
	Test_ModeRegisterConfigurable,
	Test_MonoStereoConfigurable,
	Test_FIFOFullHalfFullEmpty_Mono,
	Test_FIFOFullHalfFullEmpty_Stereo,
	Test_VIA2Repeat,
	Test_VIA2Mirror,
	Test_IdleIRQWithoutF29,
	Test_IdleIRQWithF29,
	Test_FIFOIRQ,
};

static struct TestResults results;

// Temporary buffer for storing stuff
union TempBuffer
{
	uint8_t bytes[0x200];
	uint32_t words[0x80];
};

static union TempBuffer buf;
static union TempBuffer buf2;

// Just grabs the ASC version and BoxFlag
static void Test_MachineInfo(void)
{
	results.ascVersion = ascReadReg(0x800);
	results.isSonoraVersion = (results.ascVersion & 0xB0) == 0xB0;
	results.boxFlag = *(uint8_t *)BoxFlag;

	long response;
	OSErr err = Gestalt(gestaltSystemVersion, &response);
	if (err == noErr)
	{
		results.sysVersion = response;
	}
}

// Tests to see if registers $F09 and $F29 seem to exist
static void Test_RegF09F29Exists(void)
{
	results.regF09Exists = true;
	results.regF29Exists = true;

	const uint16_t irqState = DisableIRQ();
	const uint8_t originalF09 = ascReadReg(0xF09);
	const uint8_t originalF29 = ascReadReg(0xF29);

	// Make sure they can each take both 1 and 0 as values
	ascWriteReg(0xF09, 0x01);
	if (ascReadReg(0xF09) != 0x01)
	{
		results.regF09Exists = false;
	}
	else
	{
		ascWriteReg(0xF09, 0x00);
		if (ascReadReg(0xF09) != 0x00)
		{
			results.regF09Exists = false;
		}
	}

	ascWriteReg(0xF29, 0x01);
	if (ascReadReg(0xF29) != 0x01)
	{
		results.regF29Exists = false;
	}
	else
	{
		ascWriteReg(0xF29, 0x00);
		if (ascReadReg(0xF29) != 0x00)
		{
			results.regF29Exists = false;
		}
	}

	ascWriteReg(0xF09, originalF09);
	ascWriteReg(0xF29, originalF29);
	RestoreIRQ(irqState);

	results.regF09InitialValue = originalF09;
	results.regF29InitialValue = originalF29;
}

// Tests what register $804 is at idle
static void Test_Reg804Idle(void)
{
	const uint16_t irqState = DisableIRQ();
	const uint8_t originalMode = ascReadReg(0x801);

	ascWriteReg(0x801, 1);
	// Read it once, then read again to see the "idle" status (some variants clear the bits after reading)
	(void)ascReadReg(0x804);
	results.reg804IdleValue = ascReadReg(0x804);

	ascWriteReg(0x801, originalMode);
	RestoreIRQ(irqState);
}

// Tests to see if register $801 allows writing different values
static void Test_ModeRegisterConfigurable(void)
{
	const uint16_t irqState = DisableIRQ();
	const uint8_t originalMode = ascReadReg(0x801);

	// Try setting all 3 possible modes and seeing if the chip allows them
	ascWriteReg(0x801, 0);
	results.acceptsMode0 = (ascReadReg(0x801) == 0);
	ascWriteReg(0x801, 1);
	results.acceptsMode1 = (ascReadReg(0x801) == 1);
	ascWriteReg(0x801, 2);
	results.acceptsMode2 = (ascReadReg(0x801) == 2);

	ascWriteReg(0x801, originalMode);
	RestoreIRQ(irqState);

	results.reg801InitialValue = originalMode;
}

// Tests to see if the mono/stereo bit is writable in $802
static void Test_MonoStereoConfigurable(void)
{
	const uint16_t irqState = DisableIRQ();
	const uint8_t originalControl = ascReadReg(0x802);

	// Check if we have control of the mono/stereo bit
	ascWriteReg(0x802, ascReadReg(0x802) & ~0x02);
	results.acceptsConfigMono = !(ascReadReg(0x802) & (1 << 1));
	ascWriteReg(0x802, ascReadReg(0x802) | 0x02);
	results.acceptsConfigStereo = ascReadReg(0x802) & (1 << 1);

	ascWriteReg(0x802, originalControl);
	RestoreIRQ(irqState);

	// What we should actually test doesn't always match this bit.
	// Case in point: Sonora leaves the mono/stereo bit as 0, but it's really stereo.
	results.shouldTestMono = results.acceptsConfigMono && !results.isSonoraVersion;
	results.shouldTestStereo = results.acceptsConfigStereo || results.isSonoraVersion;
}

// Extensively tests the FIFO in mono or stereo mode, checks to see if the
// FIFO status bits react as expected. No IRQs involved yet.
static void Test_FIFOFullHalfFullEmpty(bool mono, FIFOTestResults *f)
{
	uint16_t irqState = DisableIRQ();
	const uint8_t originalMode = ascReadReg(0x801);
	const uint8_t originalControl = ascReadReg(0x802);
	const bool irqOriginallyEnabledInVIA2 = via2ReadReg(0x1C13) & 0x10;
	const uint8_t originalF29Value = results.regF29Exists ? ascReadReg(0xF29) : 0;

	// Put in FIFO mode, mono or stereo
	ascWriteReg(0x801, 1);
	if (mono)
	{
		ascWriteReg(0x802, ascReadReg(0x802) & ~0x02);
	}
	else
	{
		ascWriteReg(0x802, ascReadReg(0x802) | 0x02);
	}
	// Clear the FIFO if needed
	ascWriteReg(0x803, 0x80);
	ascWriteReg(0x803, 0);
	// Make sure the ASC IRQ is disabled in VIA2 and F29
	via2WriteReg(0x1C13, 0x10);
	if (results.regF29Exists)
	{
		ascWriteReg(0xF29, 1);
	}

	// Now we can re-enable interrupts
	RestoreIRQ(irqState);

	// Clear any old status bits just in case
	(void)ascReadReg(0x804);

	// Prime it with 0x100 samples to begin
	for (int i = 0; i < 0x100; i++)
	{
		const uint8_t nextSample = (i & 0xFF);
		ascWriteReg(0x0, nextSample);
		if (!mono)
		{
			ascWriteReg(0x400, nextSample);
		}
	}

	// Check the state of the bits now; make sure they don't indicate the FIFO is full/empty.
	// If they do, then the bit probably is not related to the playback FIFO.
	const uint8_t statusAfterFirstWrite = ascReadReg(0x804);
	f->aFullTooSoon = statusAfterFirstWrite & 0x02;
	f->bFullTooSoon = statusAfterFirstWrite & 0x08;

	// As long as we found something that could potentially be a "fifo full" flag,
	// write up to 0x1000 additional samples (maybe fewer if we figure out what we need to know)
	if (!f->aFullTooSoon || !f->bFullTooSoon)
	{
		for (int i = 0; i < 0x1000; i++)
		{
			const uint8_t nextSample = (i & 0xFF);
			ascWriteReg(0x0, nextSample);
			if (!mono)
			{
				ascWriteReg(0x400, nextSample);
			}
			const uint8_t irqState = ascReadReg(0x804);
			if ((irqState & 0x02) && !f->aReachesFull)
			{
				f->aReachesFull = true;
				f->aFullCount = i + 0x101;
				if (!(irqState & 0x01))
				{
					f->aHalfEmptyIsOffWhenFull = true;
				}

			}
			if ((irqState & 0x08) && !f->bReachesFull)
			{
				f->bReachesFull = true;
				f->bFullCount = i + 0x101;
				if (!(irqState & 0x04))
				{
					f->bHalfEmptyIsOffWhenFull = true;
				}
			}

			// If we have nothing left to check flags on, we're good.
			if ((f->aFullTooSoon || f->aReachesFull) &&
				(f->bFullTooSoon || f->bReachesFull))
			{
				break;
			}
		}
	}

	// Now wait a maximum of 1 second for it to reach half empty.
	// Only bother with this test if we noticed something that could be a half empty flag
	if (f->aHalfEmptyIsOffWhenFull ||
		f->bHalfEmptyIsOffWhenFull)
	{
		const uint32_t startTicks = ticks();
		while (ticks() - startTicks < 60*1)
		{
			const uint8_t irqState = ascReadReg(0x804);
			if (irqState & 0x01)
			{
				f->aHalfEmptyTurnsOn = true;
				if (!(irqState & 0x02))
				{
					f->aEmptyIsOffWhenHalfEmpty = true;
				}
			}
			if (irqState & 0x04)
			{
				f->bHalfEmptyTurnsOn = true;
				if (!(irqState & 0x08))
				{
					f->bEmptyIsOffWhenHalfEmpty = true;
				}
			}

			// If we have nothing left to check flags on, we're good
			if ((!f->aHalfEmptyIsOffWhenFull || f->aEmptyIsOffWhenHalfEmpty) &&
				(!f->bHalfEmptyIsOffWhenFull || f->bEmptyIsOffWhenHalfEmpty))
			{
				break;
			}
		}
	}

	// If anything reached half empty, wait another second to see if the empty/full flag will turn on when it empties
	if (f->aEmptyIsOffWhenHalfEmpty ||
		f->bEmptyIsOffWhenHalfEmpty)
	{
		const uint32_t startTicks = ticks();
		while (ticks() - startTicks < 60*1)
		{
			const uint8_t irqState = ascReadReg(0x804);
			if (irqState & 0x02)
			{
				f->aReachesEmpty = true;
			}
			if (irqState & 0x08)
			{
				f->bReachesEmpty = true;
			}

			// If we have nothing left to check flags on, we're good
			if ((!f->aEmptyIsOffWhenHalfEmpty || f->aReachesEmpty) &&
				(!f->bEmptyIsOffWhenHalfEmpty || f->bReachesEmpty))
			{
				break;
			}
		}
	}

	// Remove misleading results
	if (f->aFullTooSoon)
	{
		f->aFullCount = 0;
	}
	if (f->bFullTooSoon)
	{
		f->bFullCount = 0;
	}

	irqState = DisableIRQ();
	if (results.regF29Exists)
	{
		ascWriteReg(0xF29, originalF29Value);
	}
	via2WriteReg(0x1C13, irqOriginallyEnabledInVIA2 ? 0x90 : 0x10);
	ascWriteReg(0x802, originalControl);
	ascWriteReg(0x801, originalMode);
	RestoreIRQ(irqState);
}

// Tests the FIFO in mono mode (if allowed)
static void Test_FIFOFullHalfFullEmpty_Mono(void)
{
	// Only do this test if we have deemed that we should test mono mode
	if (!results.shouldTestMono)
	{
		return;
	}

	Test_FIFOFullHalfFullEmpty(true, &results.monoFIFO);
}

// Tests the FIFO in stereo mode (if allowed)
static void Test_FIFOFullHalfFullEmpty_Stereo(void)
{
	// Only do this test if we saw that it accepted stereo mode
	// Exception: Sonora doesn't accept stereo mode but is really always stereo
	if (!results.acceptsConfigStereo && ((results.ascVersion & 0xF0) != 0xB0))
	{
		return;
	}

	Test_FIFOFullHalfFullEmpty(false, &results.stereoFIFO);
}

// Tests how often VIA2's address space repeats
static void Test_VIA2Repeat(void)
{
	const uint16_t irqState = DisableIRQ();

	// Read the first 0x200 bytes of VIA2.
	// Do it multiple times until we get two identical readbacks.
	// If we try 1000 times and can't get a consistent readback, then move on.
	bool consistentReadback = false;
	for (int i = 0; !consistentReadback && i < 1000; i++)
	{
		uint32_t *readLoc = *(uint32_t **)VIA2Base;
		TempBuffer *destBuf = (i & 1) ? &buf : &buf2;

		for (int j = 0; j < 0x80; j++)
		{
			destBuf->words[j] = *readLoc++;
		}

		if (i > 0)
		{
			if (!memcmp(buf.bytes, buf2.bytes, sizeof(TempBuffer)))
			{
				consistentReadback = true;
			}
		}
	}
	results.via2ReadbackConsistent = consistentReadback;

	RestoreIRQ(irqState);

	// Check A0-A8 lines against readback of data to glean whether they are used or not
	// (Those are the address lines that would be used inside 0x200 bytes of space)
	uint16_t decodeMask = 0;
	for (int bit = 0; bit <= 8; bit++)
	{
		bool matters = false;
		const int mask = (1 << bit);
		for (int i = 0; i < 0x200; i++)
		{
			int j = i ^ mask;
			if ((j < 0x200) && (buf.bytes[i] != buf.bytes[j]))
			{
				matters = true;
				break;
			}
		}
		if (matters)
		{
			decodeMask |= mask;
		}
	}

	results.via2AddressDecodeMask = decodeMask;
}

// Tests whether the VIA2 mirroring works (whether you can use $1C13 instead of
// $1C00 or $13 depending on the variant)
static void Test_VIA2Mirror(void)
{
	const uint16_t irqState = DisableIRQ();
	const bool irqOriginallyEnabledInVIA2 = via2ReadReg(0x1C13) & 0x10;
	bool ok = true;

	const uint16_t actualOffset = (results.via2AddressDecodeMask == 0) ?
		0x1C00 : 0x13;

	// Confirm that writes to 0x1C13 apply to 0x1C00 or 0x13, depending on this machine's VIA2 setup
	// Sanity check: are they the same?
	if (via2ReadReg(actualOffset) != via2ReadReg(0x1C13))
	{
		ok = false;
	}

	// Enable ASC IRQ in 1C13, see if change goes through to 1C00 or 13
	via2WriteReg(0x1C13, 0x90);
	if (!(via2ReadReg(actualOffset) & 0x10))
	{
		ok = false;
	}

	// Disable ASC IRQ in 1C13, see if change goes through to 1C00 or 13
	via2WriteReg(0x1C13, 0x10);
	if (via2ReadReg(actualOffset) & 0x10)
	{
		ok = false;
	}

	// Enable ASC IRQ in 1C00 or 13, see if change goes through to 1C13
	via2WriteReg(actualOffset, 0x90);
	if (!(via2ReadReg(0x1C13) & 0x10))
	{
		ok = false;
	}

	// Disable ASC IRQ in 1C00 or 13, see if change goes through to 1C13
	via2WriteReg(actualOffset, 0x10);
	if (via2ReadReg(0x1C13) & 0x10)
	{
		ok = false;
	}

	via2WriteReg(0x1C13, irqOriginallyEnabledInVIA2 ? 0x90 : 0x10);
	// Clear any active IRQs just in case
	via2WriteReg(0x1A03, 0x90);

	results.via2MirroringOK = ok;

	RestoreIRQ(irqState);
}

// Gets pointer to test results struct from IRQ context
static TestResults *resultsFromIRQ(void)
{
	return *(TestResults **)ApplScratch;
}

// IRQ handler used for idle testing
static void Test_IdleIRQHandler(void)
{
	// Read the reg in case we need to clear an IRQ
	(void)ascReadReg(0x804);

	// Acknowledge the IRQ
	via2WriteReg(0x1A03, 0x90);

	TestResults *r = resultsFromIRQ();
	r->tmpIRQCount++;

	// Safety: if we get too many idle IRQs, disable it
	if (r->tmpIRQCount >= IRQ_FLOOD_TEST_COUNT)
	{
		via2WriteReg(0x1C13, 0x10);
	}
}

// Tests to see if the ASC floods IRQs while idle
static void Test_IdleIRQ(bool hasF29, bool enableF29)
{
	uint16_t irqState = DisableIRQ();
	const bool irqOriginallyEnabledInVIA2 = via2ReadReg(0x1C13) & 0x10;
	const uint8_t originalF29Value = hasF29 ? ascReadReg(0xF29) : 0;
	VIA2Handler originalASCIRQHandler = via2Handlers()[4];

	*(TestResults **)ApplScratch = &results;
	results.tmpIRQCount = 0;
	via2Handlers()[4] = Test_IdleIRQHandler;
	via2WriteReg(0x1C13, 0x90);
	via2WriteReg(0x1A03, 0x90); // Acknowledge anything already waiting
	if (hasF29)
	{
		ascWriteReg(0xF29, enableF29 ? 0 : 1);
	}

	RestoreIRQ(irqState);
	// Immediately read the IRQ count to see how far we get
	results.irqCountTest = results.tmpIRQCount;

	// Wait for 2 seconds
	const uint32_t startTicks = ticks();
	while (ticks() - startTicks < 60*2)
	{
	}

	irqState = DisableIRQ();

	// See if we got any IRQs
	if (results.tmpIRQCount > 0)
	{
		if (enableF29)
		{
			results.idleIRQWithF29 = true;
		}
		else
		{
			results.idleIRQWithoutF29 = true;
		}
	}

	// Look to see if we had to disable the IRQ due to flooding
	if (results.tmpIRQCount >= IRQ_FLOOD_TEST_COUNT)
	{
		if (enableF29)
		{
			results.floodsIRQWithF29 = true;
			if (results.irqCountTest >= IRQ_FLOOD_TEST_COUNT)
			{
				results.irqFloodWithF29TakesOverCPU = true;
			}
		}
		else
		{
			results.floodsIRQWithoutF29 = true;
			if (results.irqCountTest >= IRQ_FLOOD_TEST_COUNT)
			{
				results.irqFloodWithoutF29TakesOverCPU = true;
			}
		}
	}

	// If we are in the F29 test, try toggling it to 1 and 0 again to see if it re-fires
	if (hasF29 && enableF29)
	{
		results.tmpIRQCount = 0;
		via2WriteReg(0x1C13, 0x90); // If it flooded the first time, we need to re-enable it
		ascWriteReg(0xF29, 1);
		ascWriteReg(0xF29, 0);
		RestoreIRQ(irqState);
		// Immediately read the IRQ count to see how far we get
		results.irqCountTest = results.tmpIRQCount;

		// Wait for 2 seconds and then clear it again
		const uint32_t startTicks = ticks();
		while (ticks() - startTicks < 60*2)
		{
		}

		irqState = DisableIRQ();

		// See if re-enabling the IRQ re-fired it and if it flooded again
		if (results.tmpIRQCount > 0)
		{
			results.refiresIdleIRQWithF29 = true;
		}
		if (results.tmpIRQCount >= IRQ_FLOOD_TEST_COUNT)
		{
			results.refiresIdleIRQFloodWithF29 = true;
			if (results.irqCountTest >= IRQ_FLOOD_TEST_COUNT)
			{
				results.irqFloodRefireWithF29TakesOverCPU = true;
			}
		}
	}

	via2Handlers()[4] = originalASCIRQHandler;
	if (hasF29)
	{
		ascWriteReg(0xF29, originalF29Value);
	}
	if (irqOriginallyEnabledInVIA2)
	{
		ascWriteReg(0x1C13, 0x90);
	}
	else
	{
		ascWriteReg(0x1C13, 0x10);
	}
	RestoreIRQ(irqState);
}

// Tests to see if IRQs flood at idle without reg $F29
static void Test_IdleIRQWithoutF29(void)
{
	Test_IdleIRQ(results.regF29Exists, false);
}

// Tests to see if IRQs flood at idle with reg $F29 = 0 (if it exists)
static void Test_IdleIRQWithF29(void)
{
	if (results.regF29Exists)
	{
		Test_IdleIRQ(results.regF29Exists, true);
	}
}

// IRQ handler used for testing the FIFO IRQ
static void Test_FIFOIRQHandler(void)
{
	// Acknowledge the IRQ
	via2WriteReg(0x1A03, 0x90);

	// Read the status reg
	uint8_t status = ascReadReg(0x804);

	// Shift it over if we're looking at FIFO B
	TestResults *r = resultsFromIRQ();
	if (!r->fifoIRQTestedWasA)
	{
		status >>= 2;
	}
	// Mask so we only look at the bits we care about
	status &= 0x03;

	bool irqFlood = false;

	if (status == 0x02)
	{
		if (++r->fullIRQCount >= IRQ_FLOOD_TEST_COUNT)
		{
			irqFlood = true;
		}
	}
	else if (status == 0x01)
	{
		if (++r->halfEmptyIRQCount >= IRQ_FLOOD_TEST_COUNT)
		{
			irqFlood = true;
		}
	}
	else if (status == 0x03)
	{
		if (++r->emptyIRQCount >= IRQ_FLOOD_TEST_COUNT)
		{
			irqFlood = true;
		}
	}
	else
	{
		if (++r->otherIRQCount >= IRQ_FLOOD_TEST_COUNT)
		{
			irqFlood = true;
		}
	}

	// Safety: if we get too many IRQs, disable it
	if (irqFlood)
	{
		via2WriteReg(0x1C13, 0x10);
	}
}

// Tests the FIFO again, this time seeing which IRQs activate
static void Test_FIFOIRQ(void)
{
	// Only use mono if stereo isn't supported by this variant
	const bool mono = !results.shouldTestStereo;
	const bool enableF29 = results.regF29Exists;
	const FIFOTestResults *f = mono ? &results.monoFIFO : &results.stereoFIFO;

	// Based on our earlier tests, see if we can actually run this test.
	// Prefer checking FIFO B bits because it's what Sonora uses, and it makes
	// sense to check them in stereo mode too
	if (!f->bFullTooSoon && f->bReachesFull && f->bHalfEmptyIsOffWhenFull &&
		f->bHalfEmptyTurnsOn && f->bEmptyIsOffWhenHalfEmpty)
	{
		results.fifoIRQTestedWasA = false;
		results.testedFIFOIRQs = true;
	}
	else if (!f->aFullTooSoon && f->aReachesFull && f->aHalfEmptyIsOffWhenFull &&
		f->aHalfEmptyTurnsOn && f->aEmptyIsOffWhenHalfEmpty)
	{
		results.fifoIRQTestedWasA = true;
		results.testedFIFOIRQs = true;
	}
	else
	{
		// We didn't observe a working FIFO without the IRQs, so don't bother trying to
		// test with IRQs
		return;
	}

	uint16_t irqState = DisableIRQ();
	const uint8_t originalMode = ascReadReg(0x801);
	const uint8_t originalControl = ascReadReg(0x802);
	const bool irqOriginallyEnabledInVIA2 = via2ReadReg(0x1C13) & 0x10;
	const uint8_t originalF29Value = enableF29 ? ascReadReg(0xF29) : 0;
	VIA2Handler originalASCIRQHandler = via2Handlers()[4];
	*(TestResults **)ApplScratch = &results;
	via2Handlers()[4] = Test_FIFOIRQHandler;

	// Put in FIFO mode, mono or stereo
	ascWriteReg(0x801, 1);
	if (mono)
	{
		ascWriteReg(0x802, ascReadReg(0x802) & ~0x02);
	}
	else
	{
		ascWriteReg(0x802, ascReadReg(0x802) | 0x02);
	}

	// Clear any old status bits just in case
	(void)ascReadReg(0x804);

	// Keep filling the FIFO until it is more than half full
	for (int i = 0; i < 0x300; i++)
	{
		const uint8_t nextSample = (i & 0xFF);
		ascWriteReg(0x0, nextSample);
		if (!mono)
		{
			ascWriteReg(0x400, nextSample);
		}
	}

	// Turn on IRQs after it's more than half full
	via2WriteReg(0x1C13, 0x90);
	if (enableF29)
	{
		ascWriteReg(0xF29, 0);
	}
	RestoreIRQ(irqState);

	// Keep filling the FIFO for a while, let's see if we ever get an IRQ
	for (int i = 0; i < 0x1000; i++)
	{
		const uint8_t nextSample = (i & 0xFF);
		ascWriteReg(0x0, nextSample);
		if (!mono)
		{
			ascWriteReg(0x400, nextSample);
		}

		// The handler will tell us if the FIFO is full, unless the ASC doesn't
		// interrupt on FIFO full. We already know the FIFO full bit works because
		// we tested it earlier.
		if (results.fullIRQCount > 0)
		{
			break;
		}
	}

	// Make sure we haven't received a half empty or empty IRQ yet. It hasn't had enough time to empty out.
	if (results.halfEmptyIRQCount > 0)
	{
		results.gotIRQOnFIFOHalfEmptyTooSoon = true;
	}
	if (results.emptyIRQCount > 0)
	{
		results.gotIRQOnFIFOEmptyTooSoon = true;
	}

	// Now stop and wait 4 seconds for the FIFO to empty out, see what types of IRQs we get
	uint32_t maxDiffFull = 0;
	uint32_t maxDiffHalf = 0;
	uint32_t maxDiffEmpty = 0;
	uint32_t maxDiffOther = 0;
	uint32_t lastFull = 0;
	uint32_t lastHalf = 0;
	uint32_t lastEmpty = 0;
	uint32_t lastOther = 0;
	const uint32_t startTicks = ticks();
	while (ticks() - startTicks < 60*4)
	{
		// Sample the four counters that the IRQ will increment
		const uint32_t newFull = results.fullIRQCount;
		const uint32_t newHalf = results.halfEmptyIRQCount;
		const uint32_t newEmpty = results.emptyIRQCount;
		const uint32_t newOther = results.otherIRQCount;

		// Calculate the maximum differences we observe while waiting
		uint32_t diff = newFull - lastFull;
		if (diff > maxDiffFull)
		{
			maxDiffFull = diff;
		}
		diff = newHalf - lastHalf;
		if (diff > maxDiffHalf)
		{
			maxDiffHalf = diff;
		}
		diff = newEmpty - lastEmpty;
		if (diff > maxDiffEmpty)
		{
			maxDiffEmpty = diff;
		}
		diff = newOther - lastOther;
		if (diff > maxDiffOther)
		{
			maxDiffOther = diff;
		}

		lastFull = newFull;
		lastHalf = newHalf;
		lastEmpty = newEmpty;
		lastOther = newOther;
	}

	irqState = DisableIRQ();
	via2Handlers()[4] = originalASCIRQHandler;
	if (enableF29)
	{
		ascWriteReg(0xF29, originalF29Value);
	}
	via2WriteReg(0x1C13, irqOriginallyEnabledInVIA2 ? 0x90 : 0x10);
	ascWriteReg(0x802, originalControl);
	ascWriteReg(0x801, originalMode);
	(void)ascReadReg(0x804);
	RestoreIRQ(irqState);

	// Save max differences we observed
	results.fullIRQMaxDiff = maxDiffFull;
	results.halfEmptyIRQMaxDiff = maxDiffHalf;
	results.emptyIRQMaxDiff = maxDiffEmpty;
	results.otherIRQMaxDiff = maxDiffOther;
}

// Runs all the tests
void DoTests(void)
{
	for (size_t i = 0; i < sizeof(tests)/sizeof(tests[0]); i++)
	{
		tests[i]();
	}
}

// Prints info about a FIFO test (stereo or mono)
void PrintFIFOTests(const char *title, struct FIFOTestResults const *f)
{
	printf("%s:\n", title);
	printf("%d %d %d %d %d %d %d %d %d %d %d %d (%u %u)\n",
			f->aFullTooSoon, f->bFullTooSoon,
			f->aReachesFull, f->bReachesFull,
			f->aHalfEmptyIsOffWhenFull, f->bHalfEmptyIsOffWhenFull,
			f->aHalfEmptyTurnsOn, f->bHalfEmptyTurnsOn,
			f->aEmptyIsOffWhenHalfEmpty, f->bEmptyIsOffWhenHalfEmpty,
			f->aReachesEmpty, f->bReachesEmpty,
			f->aFullCount, f->bFullCount);
}

int main(void)
{
	DoTests();

	printf("ASCTester test version 2\n");
	printf("BoxFlag: %d   ASC Version: $%02X   System %d.%d.%d\n", results.boxFlag, results.ascVersion,
			(results.sysVersion >> 8) & 0xFF, (results.sysVersion >> 4) & 0x0F,
			results.sysVersion & 0x0F);
	printf("F09: %d ($%02X)  F29: %d ($%02X)\n",
			results.regF09Exists, results.regF09InitialValue,
			results.regF29Exists, results.regF29InitialValue);
	printf("804Idle: $%02X  M0: %d M1: %d M2: %d ($%02X)\n",
			results.reg804IdleValue, results.acceptsMode0, results.acceptsMode1, results.acceptsMode2, results.reg801InitialValue);
	printf("Mono: %d %d Stereo: %d %d\n", results.acceptsConfigMono, results.shouldTestMono,
			results.acceptsConfigStereo, results.shouldTestStereo);

	if (results.shouldTestMono)
	{
		PrintFIFOTests("Mono FIFO Tests", &results.monoFIFO);
	}
	if (results.shouldTestStereo)
	{
		PrintFIFOTests("Stereo FIFO Tests", &results.stereoFIFO);
	}
	printf("VIA2 (%d $%04X) %d\n", results.via2ReadbackConsistent, results.via2AddressDecodeMask, results.via2MirroringOK);
	printf("Idle IRQ %d %d %d, %d %d %d, %d %d %d\n",
			results.idleIRQWithoutF29, results.floodsIRQWithoutF29, results.irqFloodWithoutF29TakesOverCPU,
			results.idleIRQWithF29, results.floodsIRQWithF29, results.irqFloodWithF29TakesOverCPU,
			results.refiresIdleIRQWithF29, results.refiresIdleIRQFloodWithF29, results.irqFloodRefireWithF29TakesOverCPU);
	printf("FIFO IRQ %d %d %d %d\n", results.testedFIFOIRQs, results.fifoIRQTestedWasA,
			results.gotIRQOnFIFOHalfEmptyTooSoon, results.gotIRQOnFIFOEmptyTooSoon);
	printf("(%u %u), (%u %u), (%u %u), (%u %u)\n",
			results.fullIRQCount, results.fullIRQMaxDiff,
			results.halfEmptyIRQCount, results.halfEmptyIRQMaxDiff,
			results.emptyIRQCount, results.emptyIRQMaxDiff,
			results.otherIRQCount, results.otherIRQMaxDiff);

	getchar();
}
