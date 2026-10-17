#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdarg.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <Gestalt.h>
#include <Files.h>
#ifdef SIMPLE_CONSOLE
#include <Quickdraw.h>
#include <Fonts.h>
#include <Windows.h>
#include <Events.h>

QDGlobals qd;
#endif
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

// Values written to $804 by the status write test
static const uint8_t statusWriteValues[] = { 0x01, 0x02, 0x04, 0x08, 0x0F, 0x10, 0x80, 0x00 };
#define STATUS_WRITE_COUNT					(sizeof(statusWriteValues) / sizeof(statusWriteValues[0]))

// Results for one value written to $804
struct StatusWriteResult
{
	bool irqAfterWrite;						// An ASC IRQ was flagged in VIA2 after the write
	uint8_t readback;						// First read of $804 after the write
	uint8_t readbackAgain;					// Second read of $804 after the write
	bool irqAfterRead;						// The VIA2 flag came back after clearing it, after reading $804
};

// Number of trials and sample counts for the half empty threshold test
#define THRESHOLD_TRIALS					16
#define THRESHOLD_FILL						0x300
#define THRESHOLD_REFILL					0x100

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
	uint32_t idleIRQWithF29Count;			// Total count of IRQs observed at idle without F29 enabled
	uint32_t idleIRQWithoutF29Count;		// Total count of IRQs observed at idle with F29 enabled (if available)
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
	volatile bool fifoIRQFiredAfterToggleWhenFull;	// True if an IRQ fired after we toggled the IRQ off and back on,
													// even though FIFO was full and thus no conditions should
													// have been met to cause an IRQ to fire at that time.
													// If F29 exists, we use that for the toggle. Otherwise, VIA2.
	volatile uint32_t fifoIRQTime[12];		// The time of the first 12 detected FIFO IRQs
	volatile uint8_t fifoIRQValues[12];		// The status value read from reg 0x804 for the first 12 detected FIFO IRQs
	volatile uint32_t fifoIRQCount;			// The number of FIFO IRQs detected
	volatile uint32_t fifoIRQEnableTime;	// The time when we enabled FIFO IRQs
	bool testedCancelIRQ;					// True if we ran the cancel IRQ test (we don't do this if it floods IRQs)
	bool cancelIRQInitial;					// True if we saw an IRQ flagged in VIA2 in cancel test before writing any samples
	bool cancelIRQAfterFill;				// True if we saw an IRQ flagged in VIA2 after filling samples, before enabling IRQs
	bool cancelIRQAfterAck;					// True if an IRQ was still flagged in VIA2 after we acknowledged it, before enabling IRQs
	bool cancelIRQAfterWait;				// True if an IRQ was still flagged in VIA2 after we enabled IRQs and waited 10 seconds
	uint8_t cancelIRQFinalStatus;			// Final status read from reg 0x804 after the cancel test
	volatile uint32_t cancelIRQCount;		// Count of IRQs we observed during the cancel test
	bool timerAvailable;					// VIA1 timer 2 is free to use as a stopwatch
	uint8_t via1ACR;						// VIA1 ACR when checking timer 2
	uint8_t via1IER;						// VIA1 IER when checking timer 2
	bool testedStatusWrite;					// True if we ran the status write test
	bool statusWriteIRQBefore;				// An ASC IRQ was flagged in VIA2 right after clearing it, before any write
	uint8_t fifoClearStatus;				// $804 read right after a FIFO clear (bit 7 of $803)
	uint8_t fifoClearStatusAgain;			// $804 read again after that
	bool fifoClearIRQ;						// An ASC IRQ was flagged in VIA2 after the FIFO clear
	struct StatusWriteResult statusWrite[STATUS_WRITE_COUNT];
	bool statusWriteRepeatIRQ;				// A second write of $01, with the first one not read back yet, flagged an IRQ again
	bool testedMaskedLatch;					// True if we ran the masked latch test
	bool maskedFlagAfterFill;				// VIA2 IFR bit 4 was set after the FIFO filled up with the ASC masked in IER
	uint8_t maskedIFRAfterFill;				// VIA2 IFR at that point
	uint8_t maskedStatus;					// $804 read after that, which releases the original ASC's IRQ line
	bool maskedFlagAfterRead;				// VIA2 IFR bit 4 was still set after that read
	uint8_t maskedIFRAfterEnable;			// VIA2 IFR after enabling the ASC in IER, CPU interrupts still off
	volatile uint32_t maskedIRQCount;		// IRQs taken after turning CPU interrupts back on
	bool testedLineType;					// True if we ran the IRQ line type test
	bool lineIdleIRQ;						// An ASC IRQ was flagged in VIA2 right after clearing it, before filling the FIFO
	bool lineFullIRQ;						// An ASC IRQ was flagged in VIA2 after filling the FIFO
	bool lineIRQAfterClear;					// The VIA2 flag came back right after clearing it, with $804 not read yet
	bool lineSecondIRQ;						// The VIA2 flag came back later while the FIFO drained, with $804 not read yet
	uint16_t lineSecondTime;				// When the second flag appeared, in VIA ticks after clearing the first one
	uint8_t lineStatus;						// $804 read after waiting for the second flag
	bool lineIRQAfterRead;					// The VIA2 flag came back after clearing it, after reading $804
	bool testedThreshold;					// True if we ran the half empty threshold test
	uint8_t thresholdFailures;				// Number of trials where a flag never showed up in time
	uint16_t thresholdFirstMin;				// Shortest time from the first write into an empty FIFO to the half empty flag
	uint16_t thresholdFirstMax;				// Longest time from the first write into an empty FIFO to the half empty flag
	uint16_t thresholdRefillMin;			// Shortest time between the two half empty flags around a refill
	uint16_t thresholdRefillMax;			// Longest time between the two half empty flags around a refill
	uint32_t thresholdRefillSum;			// Sum of the times between the two half empty flags
	uint16_t thresholdFirstAvg;				// Average time from the first write into an empty FIFO to the half empty flag
	uint16_t thresholdRefillAvg;			// Average time between the two half empty flags (THRESHOLD_REFILL sample periods)
	uint16_t fullWritesMin;					// Fewest writes into an empty FIFO before the full flag
	uint16_t fullWritesMax;					// Most writes into an empty FIFO before the full flag
	int32_t fullLevelMin;					// Lowest estimated FIFO level at the full flag, in 1/100 samples
	int32_t fullLevelMax;					// Highest estimated FIFO level at the full flag, in 1/100 samples
	int32_t fullLevelAvg;					// Average estimated FIFO level at the full flag, in 1/100 samples
};

static void DisableASCVBLTask(void);
static void RestoreASCVBLTask(void);

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
static void Test_FIFOIRQ_WhileFull(void);

static void Test_CancelIRQ(void);

static void Test_TimerAvailable(void);
static void Test_StatusWrite(void);
static void Test_MaskedLatch(void);
static void Test_IRQLineType(void);
static void Test_HalfEmptyThreshold(void);

// List of all tests
static ASCTestFunc tests[] =
{
	DisableASCVBLTask,
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
	Test_FIFOIRQ_WhileFull,
	Test_CancelIRQ,
	Test_TimerAvailable,
	Test_StatusWrite,
	Test_MaskedLatch,
	Test_IRQLineType,
	Test_HalfEmptyThreshold,
	RestoreASCVBLTask,
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

static VBLTask *ascVBLTask;
static ProcPtr originalASCVBLFunc;

// A simple VBL task function that does nothing and schedules
// itself to run again in 30 ticks
#pragma parameter DummyASCVBLTask(__A0)
static void DummyASCVBLTask(VBLTask *task)
{
	task->vblCount = 30;
}

// The Quadra 700 and 900 have a VBL task that interferes with ASCTester's
// IRQ tests. Locate it and disable it if it exists.
static void DisableASCVBLTask(void)
{
	const uint16_t irqState = DisableIRQ();
	QHdr vblQueue = LMGetVBLQueue();
	if (vblQueue.qHead)
	{
		VBLTask *task = (VBLTask *)(vblQueue.qHead);
		while (task)
		{
			// Confirmed to be like this in 7.1, 7.6, 8.0, and 8.1
			static const uint8_t ascVBLStart[10] = {
				0x31, 0x7C, 0x00, 0x1E, 0x00, 0x0A,
				0x22, 0x78, 0x0C, 0xC0
			};
			if (!memcmp((uint8_t *)task->vblAddr, ascVBLStart, sizeof(ascVBLStart)))
			{
				ascVBLTask = task;
				originalASCVBLFunc = task->vblAddr;
				task->vblAddr = (ProcPtr)DummyASCVBLTask;

				// We found it, bail now
				RestoreIRQ(irqState);
				return;
			}
			task = (VBLTask *)task->qLink;
		}
	}

	// Didn't find one
	ascVBLTask = NULL;
	RestoreIRQ(irqState);
}

// Restore the VBL task if we disabled it.
static void RestoreASCVBLTask(void)
{
	const uint16_t irqState = DisableIRQ();
	if (ascVBLTask)
	{
		ascVBLTask->vblAddr = originalASCVBLFunc;
	}
	RestoreIRQ(irqState);
}

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
	const uint8_t originalF09Value = results.regF09Exists ? ascReadReg(0xF09) : 0;
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
	// Make sure the ASC IRQ is disabled in VIA2 and F09/F29
	via2WriteReg(0x1C13, 0x10);
	if (results.regF09Exists)
	{
		ascWriteReg(0xF09, 1);
	}
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
		if (mono)
		{
			ascWriteReg(0x0, nextSample);
		}
		else
		{
			ascWriteStereoSample(nextSample);
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
			if (mono)
			{
				ascWriteReg(0x0, nextSample);
			}
			else
			{
				ascWriteStereoSample(nextSample);
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
	if (results.regF09Exists)
	{
		ascWriteReg(0xF09, originalF09Value);
	}
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
static void Test_IdleIRQ(bool hasF09, bool hasF29, bool enableF29)
{
	uint16_t irqState = DisableIRQ();
	const bool irqOriginallyEnabledInVIA2 = via2ReadReg(0x1C13) & 0x10;
	const uint8_t originalF09Value = hasF09 ? ascReadReg(0xF09) : 0;
	const uint8_t originalF29Value = hasF29 ? ascReadReg(0xF29) : 0;
	VIA2Handler originalASCIRQHandler = via2Handlers()[4];

	*(TestResults **)ApplScratch = &results;
	results.tmpIRQCount = 0;
	via2Handlers()[4] = Test_IdleIRQHandler;
	via2WriteReg(0x1C13, 0x90);
	via2WriteReg(0x1A03, 0x90); // Acknowledge anything already waiting
	if (hasF09)
	{
		// Leave F09 disabled; on newer variants it's related to recording instead of playback.
		ascWriteReg(0xF09, 1);
	}
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

	if (enableF29)
	{
		results.idleIRQWithF29Count = results.tmpIRQCount;
	}
	else
	{
		results.idleIRQWithoutF29Count = results.tmpIRQCount;
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
	if (hasF09)
	{
		ascWriteReg(0xF09, originalF09Value);
	}
	if (hasF29)
	{
		ascWriteReg(0xF29, originalF29Value);
	}
	if (irqOriginallyEnabledInVIA2)
	{
		via2WriteReg(0x1C13, 0x90);
	}
	else
	{
		via2WriteReg(0x1C13, 0x10);
	}
	RestoreIRQ(irqState);
}

// Tests to see if IRQs flood at idle without reg $F29
static void Test_IdleIRQWithoutF29(void)
{
	Test_IdleIRQ(results.regF09Exists, results.regF29Exists, false);
}

// Tests to see if IRQs flood at idle with reg $F29 = 0 (if it exists)
static void Test_IdleIRQWithF29(void)
{
	if (results.regF29Exists)
	{
		Test_IdleIRQ(results.regF09Exists, results.regF29Exists, true);
	}
}

// IRQ handler used for testing the FIFO IRQ
static void Test_FIFOIRQHandler(void)
{
	// Acknowledge the IRQ
	via2WriteReg(0x1A03, 0x90);

	// Read the status reg
	uint8_t status = ascReadReg(0x804);

	// Track at what time it happened. Not super high precision, but whatever.
	const uint32_t time = ticks();

	TestResults *r = resultsFromIRQ();

	// Save raw info about the first 12 IRQs detected
	uint32_t count = r->fifoIRQCount;
	if (count < 12)
	{
		r->fifoIRQValues[count] = status;
		r->fifoIRQTime[count] = time;
	}
	r->fifoIRQCount = count + 1;

	// Shift it over if we're looking at FIFO B
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
	const uint8_t originalF09Value = results.regF09Exists ? ascReadReg(0xF09) : 0;
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
		if (mono)
		{
			ascWriteReg(0x0, nextSample);
		}
		else
		{
			ascWriteStereoSample(nextSample);
		}
	}

	// Turn on IRQs after it's more than half full
	via2WriteReg(0x1C13, 0x90);
	if (results.regF09Exists)
	{
		// Leave F09 disabled; on newer variants it's related to recording instead of playback.
		ascWriteReg(0xF09, 1);
	}
	if (enableF29)
	{
		ascWriteReg(0xF29, 0);
	}
	RestoreIRQ(irqState);
	results.fifoIRQEnableTime = ticks();

	// Keep filling the FIFO for a while, let's see if we ever get an IRQ
	for (int i = 0; i < 0x1000; i++)
	{
		const uint8_t nextSample = (i & 0xFF);
		if (mono)
		{
			ascWriteReg(0x0, nextSample);
		}
		else
		{
			ascWriteStereoSample(nextSample);
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
	if (results.regF09Exists)
	{
		ascWriteReg(0xF09, originalF09Value);
	}
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

static void Test_FIFOIRQ_WhileFullHandler(void)
{
	// Acknowledge the IRQ
	via2WriteReg(0x1A03, 0x90);

	// Read the status reg (in case it's needed to clear an IRQ)
	ascReadReg(0x804);

	TestResults *r = resultsFromIRQ();

	// Simply flag that we saw an IRQ after toggling, and then stop further IRQs
	r->fifoIRQFiredAfterToggleWhenFull = true;
	via2WriteReg(0x1C13, 0x10);
}

// Tests to see if an IRQ fires immediately if we quickly enable and disable the IRQ while it's full.
// Determines if enabling the IRQ causes it to fire immediately if no IRQ condition is active.
// Uses F29 if it exists; otherwise uses VIA2
static void Test_FIFOIRQ_WhileFull(void)
{
	if (!results.testedFIFOIRQs)
	{
		return;
	}

	// Only use mono if stereo isn't supported by this variant
	const bool mono = !results.shouldTestStereo;
	const FIFOTestResults *f = mono ? &results.monoFIFO : &results.stereoFIFO;

	uint16_t irqState = DisableIRQ();
	const uint8_t originalMode = ascReadReg(0x801);
	const uint8_t originalControl = ascReadReg(0x802);
	const bool irqOriginallyEnabledInVIA2 = via2ReadReg(0x1C13) & 0x10;
	const uint8_t originalF09Value = results.regF09Exists ? ascReadReg(0xF09) : 0;
	const uint8_t originalF29Value = results.regF29Exists ? ascReadReg(0xF29) : 0;
	VIA2Handler originalASCIRQHandler = via2Handlers()[4];
	*(TestResults **)ApplScratch = &results;
	via2Handlers()[4] = Test_FIFOIRQ_WhileFullHandler;

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

	// Keep filling the FIFO until it is full
	for (int i = 0; i < 0x1000; i++)
	{
		const uint8_t nextSample = (i & 0xFF);
		if (mono)
		{
			ascWriteReg(0x0, nextSample);
		}
		else
		{
			ascWriteStereoSample(nextSample);
		}

		uint8_t status = ascReadReg(0x804);
		if (!results.fifoIRQTestedWasA)
		{
			status >>= 2;
		}
		status &= 0x03;

		// We filled up!
		if (status == 0x02)
		{
			break;
		}
	}

	// Turn on IRQs after it's full
	via2WriteReg(0x1C13, 0x90);
	if (results.regF09Exists)
	{
		// Leave F09 disabled; on newer variants it's related to recording instead of playback.
		ascWriteReg(0xF09, 1);
	}
	if (results.regF29Exists)
	{
		ascWriteReg(0xF29, 0);
	}
	RestoreIRQ(irqState);

	// Toggle the IRQ off and back on one more time
	if (results.regF29Exists)
	{
		ascWriteReg(0xF29, 1);
		__asm__ volatile ( "nop;" );
		ascWriteReg(0xF29, 0);
	}
	else
	{
		via2WriteReg(0x1C13, 0x10);
		__asm__ volatile ( "nop;" );
		via2WriteReg(0x1C13, 0x90);
	}

	// Wait a very brief moment to see if anything happens
	__asm__ volatile ( "nop; nop; nop; nop; nop; nop;" );

	// The IRQ handler will set fifoIRQFiredAfterToggleWhenFull if an IRQ occurs

	irqState = DisableIRQ();
	via2Handlers()[4] = originalASCIRQHandler;
	if (results.regF09Exists)
	{
		ascWriteReg(0xF09, originalF09Value);
	}
	if (results.regF29Exists)
	{
		ascWriteReg(0xF29, originalF29Value);
	}
	via2WriteReg(0x1C13, irqOriginallyEnabledInVIA2 ? 0x90 : 0x10);
	ascWriteReg(0x802, originalControl);
	ascWriteReg(0x801, originalMode);
	(void)ascReadReg(0x804);
	RestoreIRQ(irqState);

	// Wait a second for the FIFO to drain
	const uint32_t startTicks = ticks();
	while (ticks() - startTicks < 60*1)
	{
	}
}

static void Test_CancelIRQHandler(void)
{
	// Acknowledge the IRQ
	via2WriteReg(0x1A03, 0x90);

	TestResults *r = resultsFromIRQ();
	r->cancelIRQCount++;

	// Prevent IRQ floods
	if (r->cancelIRQCount >= IRQ_FLOOD_TEST_COUNT)
	{
		via2WriteReg(0x1C13, 0x10);
	}
}

// Tests to see if we can cancel an IRQ by clearing it in VIA2
static void Test_CancelIRQ(void)
{
	// Don't bother with this test if the IRQ floods when the condition is active
	// (or if we didn't test the FIFO IRQs at all)
	if (results.fifoIRQCount >= IRQ_FLOOD_TEST_COUNT ||
		!results.testedFIFOIRQs)
	{
		return;
	}
	results.testedCancelIRQ = true;

	// Only use mono if stereo isn't supported by this variant
	const bool mono = !results.shouldTestStereo;
	const bool enableF29 = results.regF29Exists;

	uint16_t irqState = DisableIRQ();
	const uint8_t originalMode = ascReadReg(0x801);
	const uint8_t originalControl = ascReadReg(0x802);
	const bool irqOriginallyEnabledInVIA2 = via2ReadReg(0x1C13) & 0x10;
	const uint8_t originalF09Value = results.regF09Exists ? ascReadReg(0xF09) : 0;
	const uint8_t originalF29Value = enableF29 ? ascReadReg(0xF29) : 0;
	VIA2Handler originalASCIRQHandler = via2Handlers()[4];
	*(TestResults **)ApplScratch = &results;
	via2Handlers()[4] = Test_CancelIRQHandler;

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

	// Enable F29 interrupts if F29 exists
	if (results.regF09Exists)
	{
		// Leave F09 disabled; on newer variants it's related to recording instead of playback.
		ascWriteReg(0xF09, 1);
	}
	if (enableF29)
	{
		ascWriteReg(0xF29, 0);
	}
	// Make sure ASC IRQs are off in VIA2
	via2WriteReg(0x1C13, 0x10);
	via2WriteReg(0x1A03, 0x90);
	const uint8_t origifr = via2ReadReg(0x1A03);

	// Now enable actual IRQs so that ticks work
	RestoreIRQ(irqState);

	// Keep filling the FIFO until it is full
	for (int i = 0; i < 0x1000; i++)
	{
		const uint8_t nextSample = (i & 0xFF);
		ascWriteStereoSample(nextSample);
	}

	// Wait 1 second
	uint32_t startTicks = ticks();
	while (ticks() - startTicks < 60*1)
	{
	}

	// See if an IRQ is flagged in VIA2
	const uint8_t ifrAfterFill = via2ReadReg(0x1A03);
	// And acknowledge it
	via2WriteReg(0x1A03, 0x90);
	const uint8_t ifrAfterAck = via2ReadReg(0x1A03);

	// Wait 1 second
	startTicks = ticks();
	while (ticks() - startTicks < 60*1)
	{
	}

	// Now allow interrupts from VIA2. Let's see if the IRQ fires
	via2WriteReg(0x1C13, 0x90);

	// Wait 1 second
	startTicks = ticks();
	while (ticks() - startTicks < 60*1)
	{
	}

	// Sample the IRQ afterward
	const uint8_t ifrAfterWait = via2ReadReg(0x1A03);

	// Wait 1 second
	startTicks = ticks();
	while (ticks() - startTicks < 60*1)
	{
	}

	// Then read the status register after all that
	const uint8_t finalStatus = ascReadReg(0x804);

	// Now restore things the way they were
	irqState = DisableIRQ();
	via2Handlers()[4] = originalASCIRQHandler;
	if (results.regF09Exists)
	{
		ascWriteReg(0xF09, originalF09Value);
	}
	if (enableF29)
	{
		ascWriteReg(0xF29, originalF29Value);
	}
	via2WriteReg(0x1C13, irqOriginallyEnabledInVIA2 ? 0x90 : 0x10);
	ascWriteReg(0x802, originalControl);
	ascWriteReg(0x801, originalMode);
	(void)ascReadReg(0x804);
	RestoreIRQ(irqState);

	// Save info we observed
	results.cancelIRQInitial = origifr & 0x10;
	results.cancelIRQAfterFill = ifrAfterFill & 0x10;
	results.cancelIRQAfterAck = ifrAfterAck & 0x10;
	results.cancelIRQAfterWait = ifrAfterWait & 0x10;
	results.cancelIRQFinalStatus = finalStatus;
}

// ASC and VIA2 state saved by PrepareIRQLineTest
struct SavedASCState
{
	uint8_t mode;
	uint8_t control;
	bool irqEnabledInVIA2;
	uint8_t f09;
	uint8_t f29;
};

// Writes one sample to FIFO A, or to both FIFOs in stereo
static void writeSample(bool mono, uint8_t sample)
{
	if (mono)
	{
		ascWriteReg(0x0, sample);
	}
	else
	{
		ascWriteStereoSample(sample);
	}
}

// Returns the half empty (bit 0) and full/empty (bit 1) status of the FIFO the IRQ tests use
static uint8_t fifoStatus(uint8_t status)
{
	if (!results.fifoIRQTestedWasA)
	{
		status >>= 2;
	}
	return status & 0x03;
}

// Clears the ASC flag in VIA2's IFR
static void clearVIA2IRQ(void)
{
	via2WriteReg(0x1A03, 0x90);
}

// Returns whether the ASC is flagged in VIA2's IFR, after giving VIA2 a few cycles to see the line
static bool via2IRQFlagged(void)
{
	for (int i = 0; i < 4; i++)
	{
		(void)via1ReadReg(0x1E00);
	}
	return via2ReadReg(0x1A03) & 0x10;
}

// Puts the ASC in FIFO mode with its IRQ masked in VIA2, so that the tests can poll VIA2's IFR.
// Enables F29 if it exists, like the other FIFO IRQ tests.
static void PrepareIRQLineTest(bool mono, SavedASCState *s)
{
	const uint16_t irqState = DisableIRQ();
	s->mode = ascReadReg(0x801);
	s->control = ascReadReg(0x802);
	s->irqEnabledInVIA2 = via2ReadReg(0x1C13) & 0x10;
	s->f09 = results.regF09Exists ? ascReadReg(0xF09) : 0;
	s->f29 = results.regF29Exists ? ascReadReg(0xF29) : 0;

	via2WriteReg(0x1C13, 0x10);
	ascWriteReg(0x801, 1);
	if (mono)
	{
		ascWriteReg(0x802, ascReadReg(0x802) & ~0x02);
	}
	else
	{
		ascWriteReg(0x802, ascReadReg(0x802) | 0x02);
	}
	if (results.regF09Exists)
	{
		// Leave F09 disabled; on newer variants it's related to recording instead of playback.
		ascWriteReg(0xF09, 1);
	}
	if (results.regF29Exists)
	{
		ascWriteReg(0xF29, 0);
	}
	RestoreIRQ(irqState);
}

// Undoes PrepareIRQLineTest
static void FinishIRQLineTest(const SavedASCState *s)
{
	const uint16_t irqState = DisableIRQ();
	if (results.regF09Exists)
	{
		ascWriteReg(0xF09, s->f09);
	}
	if (results.regF29Exists)
	{
		ascWriteReg(0xF29, s->f29);
	}
	ascWriteReg(0x802, s->control);
	ascWriteReg(0x801, s->mode);
	(void)ascReadReg(0x804);
	clearVIA2IRQ();
	via2WriteReg(0x1C13, s->irqEnabledInVIA2 ? 0x90 : 0x10);
	RestoreIRQ(irqState);
}

// Clears the FIFO and waits long enough for it to be empty in any case
static void EmptyFIFO(void)
{
	ascWriteReg(0x803, 0x80);
	ascWriteReg(0x803, 0);

	const uint32_t startTicks = ticks();
	while (ticks() - startTicks < 4)
	{
	}
}

// Checks that VIA1 timer 2 counts clock pulses, so it can be used as a stopwatch.
// Mac OS has its interrupt enabled, so the tests only borrow it with interrupts off.
static void Test_TimerAvailable(void)
{
	results.via1ACR = via1ReadReg(0x1600);
	results.via1IER = via1ReadReg(0x1C00);
	results.timerAvailable = !(results.via1ACR & 0x20);
}

// What the Time Manager had in VIA1 timer 2 when a test borrowed it
static bool borrowedTimerPending;
static uint16_t borrowedTimerCount;

// Masks the VIA1 timer 2 interrupt and saves the timer state. Call with interrupts disabled.
static void BorrowTimer(void)
{
	via1WriteReg(0x1C00, 0x20);
	borrowedTimerPending = via1ReadReg(0x1A00) & 0x20;
	borrowedTimerCount = timerCount();
}

// Reloads VIA1 timer 2 with what the Time Manager was waiting for, so it still gets its
// interrupt (late), and restores its interrupt enable. Call with interrupts disabled.
static void ReturnTimer(void)
{
	if (borrowedTimerPending || borrowedTimerCount == 0)
	{
		timerLoad(1);
	}
	else
	{
		timerLoad(borrowedTimerCount);
	}
	if (results.via1IER & 0x20)
	{
		via1WriteReg(0x1C00, 0xA0);
	}
}

// Tests whether writing to $804 raises the IRQ (the Mac II MMU mode switch code writes
// the flags back to get a new interrupt), and what $804 reads back afterwards.
// Done with an empty FIFO, where the original ASC has no IRQ condition of its own.
static void Test_StatusWrite(void)
{
	if (!results.testedFIFOIRQs)
	{
		return;
	}
	results.testedStatusWrite = true;

	const bool mono = !results.shouldTestStereo;
	SavedASCState saved;
	PrepareIRQLineTest(mono, &saved);
	EmptyFIFO();

	const uint16_t irqState = DisableIRQ();
	(void)ascReadReg(0x804);
	clearVIA2IRQ();
	results.statusWriteIRQBefore = via2IRQFlagged();

	// What a FIFO clear leaves in $804
	ascWriteReg(0x803, 0x80);
	ascWriteReg(0x803, 0);
	results.fifoClearIRQ = via2IRQFlagged();
	results.fifoClearStatus = ascReadReg(0x804);
	results.fifoClearStatusAgain = ascReadReg(0x804);

	for (size_t i = 0; i < STATUS_WRITE_COUNT; i++)
	{
		StatusWriteResult *w = &results.statusWrite[i];

		(void)ascReadReg(0x804);
		clearVIA2IRQ();
		ascWriteReg(0x804, statusWriteValues[i]);
		w->irqAfterWrite = via2IRQFlagged();
		w->readback = ascReadReg(0x804);
		w->readbackAgain = ascReadReg(0x804);
		clearVIA2IRQ();
		w->irqAfterRead = via2IRQFlagged();
	}

	// Write the same flag twice without reading $804 in between
	(void)ascReadReg(0x804);
	ascWriteReg(0x804, 0x01);
	clearVIA2IRQ();
	ascWriteReg(0x804, 0x01);
	results.statusWriteRepeatIRQ = via2IRQFlagged();

	RestoreIRQ(irqState);
	FinishIRQLineTest(&saved);
}

static void Test_MaskedLatchHandler(void)
{
	// Acknowledge the IRQ, but leave $804 alone so the handler only sees what VIA2 latched
	via2WriteReg(0x1A03, 0x90);

	TestResults *r = resultsFromIRQ();
	r->maskedIRQCount++;

	// Prevent IRQ floods
	if (r->maskedIRQCount >= IRQ_FLOOD_TEST_COUNT)
	{
		via2WriteReg(0x1C13, 0x10);
	}
}

// Tests whether VIA2 (or the chip standing in for it) latches the ASC IRQ in its IFR while
// the ASC is masked in its IER, whether the flag stays once the ASC releases its line, and
// whether enabling the ASC afterwards delivers that interrupt. Some of these are Apple ASIC
// reimplementations of the VIA and may not behave like a 6522.
// The FIFO filling up raises the original ASC's line and reading $804 releases it. Variants
// with a level IRQ (V8...) release it only when the FIFO is above half empty instead.
static void Test_MaskedLatch(void)
{
	if (!results.testedFIFOIRQs)
	{
		return;
	}
	results.testedMaskedLatch = true;

	const bool mono = !results.shouldTestStereo;
	SavedASCState saved;
	PrepareIRQLineTest(mono, &saved);
	EmptyFIFO();

	uint16_t irqState = DisableIRQ();
	VIA2Handler originalASCIRQHandler = via2Handlers()[4];
	*(TestResults **)ApplScratch = &results;
	via2Handlers()[4] = Test_MaskedLatchHandler;
	(void)ascReadReg(0x804);
	clearVIA2IRQ();

	for (int i = 0; i < 0x500; i++)
	{
		writeSample(mono, i & 0xFF);
	}
	results.maskedFlagAfterFill = via2IRQFlagged();
	results.maskedIFRAfterFill = via2ReadReg(0x1A03);
	results.maskedStatus = ascReadReg(0x804);
	results.maskedFlagAfterRead = via2IRQFlagged();

	via2WriteReg(0x1C13, 0x90);
	results.maskedIFRAfterEnable = via2ReadReg(0x1A03);
	RestoreIRQ(irqState);

	// Give a pending interrupt time to be taken. The FIFO drains past half empty
	// after about 23 ms, so stay well below that.
	for (int i = 0; i < 200; i++)
	{
		(void)via1ReadReg(0x1E00);
	}

	irqState = DisableIRQ();
	via2WriteReg(0x1C13, 0x10);
	via2Handlers()[4] = originalASCIRQHandler;
	RestoreIRQ(irqState);
	FinishIRQLineTest(&saved);
}

// Tests how VIA2 sees the ASC IRQ line. Fills the FIFO without reading $804, so the
// full and then the half empty conditions happen with their flags still pending:
// - a level triggered input flags the IRQ again as soon as it's cleared
// - an edge triggered input sees the half empty condition only if the ASC pulses its
//   line for each condition, rather than holding it until $804 is read
static void Test_IRQLineType(void)
{
	if (!results.testedFIFOIRQs)
	{
		return;
	}
	results.testedLineType = true;

	const bool mono = !results.shouldTestStereo;
	const bool timerAvailable = results.timerAvailable;
	SavedASCState saved;
	PrepareIRQLineTest(mono, &saved);
	EmptyFIFO();

	const uint16_t irqState = DisableIRQ();
	(void)ascReadReg(0x804);
	clearVIA2IRQ();
	results.lineIdleIRQ = via2IRQFlagged();

	for (int i = 0; i < 0x500; i++)
	{
		writeSample(mono, i & 0xFF);
	}
	results.lineFullIRQ = via2IRQFlagged();
	clearVIA2IRQ();
	results.lineIRQAfterClear = via2IRQFlagged();

	// Wait up to about 80 ms for the FIFO to drain past half empty
	if (timerAvailable)
	{
		BorrowTimer();
		timerStart();
	}
	for (uint32_t i = 0; i < 200000; i++)
	{
		if (via2ReadReg(0x1A03) & 0x10)
		{
			results.lineSecondIRQ = true;
			break;
		}
		if (timerAvailable && timerElapsed() >= 0xF000)
		{
			break;
		}
	}
	if (timerAvailable)
	{
		results.lineSecondTime = timerElapsed();
		ReturnTimer();
	}

	results.lineStatus = ascReadReg(0x804);
	clearVIA2IRQ();
	results.lineIRQAfterRead = via2IRQFlagged();

	RestoreIRQ(irqState);
	FinishIRQLineTest(&saved);
}

// Waits a little longer for each trial, so that the trials start at different points
// of the ASC's sample period (about 1.3 us per VIA access, 45 us per sample)
static void PhaseDelay(int trial)
{
	for (int i = 0; i < trial * 3; i++)
	{
		(void)via1ReadReg(0x1E00);
	}
}

// Polls $804 until the half empty bit is clear and then set again. Returns the
// VIA tick count at that point, or $FFFF if it doesn't happen.
static uint16_t WaitHalfEmpty(void)
{
	bool sawAboveHalf = false;

	for (uint32_t i = 0; i < 200000; i++)
	{
		const uint8_t status = fifoStatus(ascReadReg(0x804));
		if (!(status & 0x01))
		{
			sawAboveHalf = true;
		}
		else if (sawAboveHalf)
		{
			return timerElapsed();
		}
	}
	return 0xFFFF;
}

// Measures where the half empty condition happens, using VIA1 timer 2 as a stopwatch:
// - THRESHOLD_FILL samples written into an empty FIFO, time from the first write to the
//   half empty flag. The FIFO drains one sample per sample period from the first write,
//   so this is (THRESHOLD_FILL - level after the half empty condition) sample periods,
//   minus up to one period for where the first write fell within a sample period.
// - THRESHOLD_REFILL samples written right after that, time to the next half empty flag.
//   That is exactly THRESHOLD_REFILL sample periods, which calibrates the period.
// - Writes into an empty FIFO until the full flag, with the time it took, to estimate the
//   FIFO level when the full flag sets.
static void Test_HalfEmptyThreshold(void)
{
	if (!results.testedFIFOIRQs || !results.timerAvailable)
	{
		return;
	}
	results.testedThreshold = true;

	const bool mono = !results.shouldTestStereo;
	SavedASCState saved;
	PrepareIRQLineTest(mono, &saved);

	results.thresholdFirstMin = 0xFFFF;
	results.thresholdRefillMin = 0xFFFF;
	results.fullWritesMin = 0xFFFF;
	uint32_t firstSum = 0;
	uint32_t refillTrials = 0;

	for (int trial = 0; trial < THRESHOLD_TRIALS; trial++)
	{
		EmptyFIFO();

		const uint16_t irqState = DisableIRQ();
		BorrowTimer();
		(void)ascReadReg(0x804);
		PhaseDelay(trial);
		timerStart();
		for (int i = 0; i < THRESHOLD_FILL; i++)
		{
			writeSample(mono, i & 0xFF);
		}
		const uint16_t first = WaitHalfEmpty();
		for (int i = 0; i < THRESHOLD_REFILL; i++)
		{
			writeSample(mono, i & 0xFF);
		}
		const uint16_t second = (first == 0xFFFF) ? 0xFFFF : WaitHalfEmpty();
		ReturnTimer();
		RestoreIRQ(irqState);

		if (second == 0xFFFF)
		{
			results.thresholdFailures++;
			continue;
		}

		const uint16_t refill = second - first;
		if (first < results.thresholdFirstMin)
		{
			results.thresholdFirstMin = first;
		}
		if (first > results.thresholdFirstMax)
		{
			results.thresholdFirstMax = first;
		}
		if (refill < results.thresholdRefillMin)
		{
			results.thresholdRefillMin = refill;
		}
		if (refill > results.thresholdRefillMax)
		{
			results.thresholdRefillMax = refill;
		}
		firstSum += first;
		results.thresholdRefillSum += refill;
		refillTrials++;
	}

	if (refillTrials == 0)
	{
		FinishIRQLineTest(&saved);
		return;
	}
	results.thresholdFirstAvg = firstSum / refillTrials;
	results.thresholdRefillAvg = results.thresholdRefillSum / refillTrials;

	// VIA ticks per THRESHOLD_REFILL sample periods
	const int32_t refillTicks = results.thresholdRefillAvg;

	results.fullLevelMin = INT32_MAX;
	results.fullLevelMax = INT32_MIN;
	int32_t levelSum = 0;
	uint32_t fullTrials = 0;

	for (int trial = 0; trial < THRESHOLD_TRIALS; trial++)
	{
		EmptyFIFO();

		const uint16_t irqState = DisableIRQ();
		BorrowTimer();
		(void)ascReadReg(0x804);
		PhaseDelay(trial);
		timerStart();
		uint16_t writes;
		bool full = false;
		for (writes = 1; writes <= 0x800; writes++)
		{
			writeSample(mono, writes & 0xFF);
			if (fifoStatus(ascReadReg(0x804)) & 0x02)
			{
				full = true;
				break;
			}
		}
		const uint16_t elapsed = timerElapsed();
		ReturnTimer();
		RestoreIRQ(irqState);

		if (!full)
		{
			results.thresholdFailures++;
			continue;
		}

		const int32_t level = writes * 100 -
			(int32_t)elapsed * THRESHOLD_REFILL * 100 / refillTicks;
		if (writes < results.fullWritesMin)
		{
			results.fullWritesMin = writes;
		}
		if (writes > results.fullWritesMax)
		{
			results.fullWritesMax = writes;
		}
		if (level < results.fullLevelMin)
		{
			results.fullLevelMin = level;
		}
		if (level > results.fullLevelMax)
		{
			results.fullLevelMax = level;
		}
		levelSum += level;
		fullTrials++;
	}
	if (fullTrials)
	{
		results.fullLevelAvg = levelSum / (int32_t)fullTrials;
	}

	FinishIRQLineTest(&saved);
}

// Runs all the tests
void DoTests(void)
{
	for (size_t i = 0; i < sizeof(tests)/sizeof(tests[0]); i++)
	{
		tests[i]();
	}
}

// Everything printed, kept to save it to a file at the end
static char outText[4096];
static size_t outLength;

// Prints to the console and keeps a copy for the results file
static void out(const char *format, ...)
{
	char line[256];
	va_list args;

	va_start(args, format);
	vsnprintf(line, sizeof(line), format, args);
	va_end(args);

#ifndef SIMPLE_CONSOLE
	fputs(line, stdout);
#endif
	for (const char *c = line; *c && outLength < sizeof(outText); c++)
	{
		outText[outLength++] = (*c == '\n') ? '\r' : *c;
	}
}

#ifdef SIMPLE_CONSOLE
// Shows everything printed in a window, for toolchains where RetroConsole doesn't work.
// Waits for a click.
static void ShowResults(void)
{
	InitGraf(&qd.thePort);
	InitFonts();
	InitWindows();
	InitCursor();

	Rect bounds = qd.screenBits.bounds;
	InsetRect(&bounds, 8, 8);
	bounds.top += 32;
	WindowPtr window = NewWindow(NULL, &bounds, "\pASCTester", true, documentProc, (WindowPtr)-1, false, 0);
	SetPort(window);
	TextFont(kFontIDMonaco);
	TextSize(9);

	short y = 12;
	size_t start = 0;
	for (size_t i = 0; i <= outLength; i++)
	{
		if (i == outLength || outText[i] == '\r')
		{
			MoveTo(4, y);
			DrawText(outText, start, i - start);
			y += 11;
			start = i + 1;
		}
	}

	while (!Button())
	{
	}
}
#endif

// Saves everything printed to "ASCTester Results" next to the application
static void SaveResults(void)
{
	static const unsigned char name[] = "\pASCTester Results";
	short refNum;

	(void)FSDelete(name, 0);
	if (Create(name, 0, 'ttxt', 'TEXT') != noErr || FSOpen(name, 0, &refNum) != noErr)
	{
		return;
	}
	long count = outLength;
	(void)FSWrite(refNum, &count, outText);
	(void)FSClose(refNum);
	(void)FlushVol(NULL, 0);
}

// Prints info about a FIFO test (stereo or mono)
void PrintFIFOTests(const char *title, struct FIFOTestResults const *f)
{
	out("%s:\n", title);
	out("%d %d %d %d %d %d %d %d %d %d %d %d (%u %u)\n",
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
	// Only do tests if we can
	const uint32_t flags = addrMapFlags();
	const bool via2Exists = flags & (1U << 11);
	const bool ascExists = flags & (1U << 12);
	const bool rbvExists = flags & (1U << 13);

	if (ascExists && (via2Exists || rbvExists))
	{
		DoTests();
	}

	out("ASCTester test version 4\n");

	if (ascExists && (via2Exists || rbvExists))
	{
		out("BoxFlag: %d   ASC Version: $%02X   System %d.%d.%d\n", results.boxFlag, results.ascVersion,
				(results.sysVersion >> 8) & 0xFF, (results.sysVersion >> 4) & 0x0F,
				results.sysVersion & 0x0F);
		out("AddrMapFlags: $%08X\n", flags);
		out("F09: %d ($%02X)  F29: %d ($%02X)\n",
				results.regF09Exists, results.regF09InitialValue,
				results.regF29Exists, results.regF29InitialValue);
		out("804Idle: $%02X  M0: %d M1: %d M2: %d ($%02X)\n",
				results.reg804IdleValue, results.acceptsMode0, results.acceptsMode1, results.acceptsMode2, results.reg801InitialValue);
		out("Mono: %d %d Stereo: %d %d\n", results.acceptsConfigMono, results.shouldTestMono,
				results.acceptsConfigStereo, results.shouldTestStereo);

		if (results.shouldTestMono)
		{
			PrintFIFOTests("Mono FIFO Tests", &results.monoFIFO);
		}
		if (results.shouldTestStereo)
		{
			PrintFIFOTests("Stereo FIFO Tests", &results.stereoFIFO);
		}
		out("VIA2 (%d $%04X) %d\n", results.via2ReadbackConsistent, results.via2AddressDecodeMask, results.via2MirroringOK);
		out("Idle IRQ %d %d %d (%u), %d %d %d (%u), %d %d %d\n",
				results.idleIRQWithoutF29, results.floodsIRQWithoutF29, results.irqFloodWithoutF29TakesOverCPU,
				results.idleIRQWithoutF29Count,
				results.idleIRQWithF29, results.floodsIRQWithF29, results.irqFloodWithF29TakesOverCPU,
				results.idleIRQWithF29Count,
				results.refiresIdleIRQWithF29, results.refiresIdleIRQFloodWithF29, results.irqFloodRefireWithF29TakesOverCPU);
		out("FIFO IRQ %d %d %d %d\n", results.testedFIFOIRQs, results.fifoIRQTestedWasA,
				results.gotIRQOnFIFOHalfEmptyTooSoon, results.gotIRQOnFIFOEmptyTooSoon);
		out("(%u %u), (%u %u), (%u %u), (%u %u), %d\n",
				results.fullIRQCount, results.fullIRQMaxDiff,
				results.halfEmptyIRQCount, results.halfEmptyIRQMaxDiff,
				results.emptyIRQCount, results.emptyIRQMaxDiff,
				results.otherIRQCount, results.otherIRQMaxDiff,
				results.fifoIRQFiredAfterToggleWhenFull);
		if (ascVBLTask)
		{
			out("ASC VBL Task was located and temporarily disabled during this test.\n");
		}

		out("%d IRQs (start time = %u):\n", results.fifoIRQCount, results.fifoIRQEnableTime);
		for (int i = 0; i < results.fifoIRQCount && i < 12; i++)
		{
			out("%s$%02X at t = %5u",
					i > 0 ? (i & 3 ? ", " : ",\n") : "",
					results.fifoIRQValues[i],
					results.fifoIRQTime[i] & 0xFFFF);
		}
		out("%s\n", results.fifoIRQCount > 12 ? ", ..." : "");
		out("Cancel %d %d %d %d %d $%02X %u\n", results.testedCancelIRQ, results.cancelIRQInitial,
				results.cancelIRQAfterFill, results.cancelIRQAfterAck, results.cancelIRQAfterWait,
				results.cancelIRQFinalStatus, results.cancelIRQCount);
		out("Timer2: %d (ACR $%02X IER $%02X)\n", results.timerAvailable, results.via1ACR, results.via1IER);
		out("StatusWrite %d (%d) repeat %d, clear $%02X $%02X %d:\n", results.testedStatusWrite,
				results.statusWriteIRQBefore, results.statusWriteRepeatIRQ,
				results.fifoClearStatus, results.fifoClearStatusAgain, results.fifoClearIRQ);
		for (size_t i = 0; i < STATUS_WRITE_COUNT; i++)
		{
			const StatusWriteResult *w = &results.statusWrite[i];
			out("%s$%02X: %d $%02X $%02X %d",
					i > 0 ? (i & 3 ? ", " : ",\n") : "",
					statusWriteValues[i], w->irqAfterWrite, w->readback,
					w->readbackAgain, w->irqAfterRead);
		}
		out("\n");
		out("MaskedLatch %d: %d $%02X $%02X %d $%02X %u\n", results.testedMaskedLatch,
				results.maskedFlagAfterFill, results.maskedIFRAfterFill, results.maskedStatus,
				results.maskedFlagAfterRead, results.maskedIFRAfterEnable, results.maskedIRQCount);
		out("IRQLine %d: %d %d %d %d (%u) $%02X %d\n", results.testedLineType,
				results.lineIdleIRQ, results.lineFullIRQ, results.lineIRQAfterClear,
				results.lineSecondIRQ, results.lineSecondTime, results.lineStatus,
				results.lineIRQAfterRead);
		out("Threshold %d (%u): %u %u %u, refill %u %u %u\n", results.testedThreshold,
				results.thresholdFailures,
				results.thresholdFirstMin, results.thresholdFirstAvg, results.thresholdFirstMax,
				results.thresholdRefillMin, results.thresholdRefillAvg, results.thresholdRefillMax);
		if (results.thresholdRefillAvg)
		{
			const int32_t r = results.thresholdRefillAvg;
			const long toHalfMin = (long)results.thresholdFirstMin * THRESHOLD_REFILL * 100 / r;
			const long toHalfAvg = (long)results.thresholdFirstAvg * THRESHOLD_REFILL * 100 / r;
			const long toHalfMax = (long)results.thresholdFirstMax * THRESHOLD_REFILL * 100 / r;
			out("Samples to half: %ld.%02ld %ld.%02ld %ld.%02ld\n",
					toHalfMin / 100, toHalfMin % 100, toHalfAvg / 100, toHalfAvg % 100,
					toHalfMax / 100, toHalfMax % 100);
			out("Full: writes %u %u, level %ld.%02ld %ld.%02ld %ld.%02ld\n",
					results.fullWritesMin, results.fullWritesMax,
					(long)results.fullLevelMin / 100, (long)results.fullLevelMin % 100,
					(long)results.fullLevelAvg / 100, (long)results.fullLevelAvg % 100,
					(long)results.fullLevelMax / 100, (long)results.fullLevelMax % 100);
		}
	}
	else
	{
		out("BoxFlag: %d cannot be tested. AddrMapFlags: $%08X\n", *(uint8_t *)BoxFlag, flags);
		if (!ascExists)
		{
			out("- ASC address map flag isn't valid\n");
		}
		if (!via2Exists && !rbvExists)
		{
			out("- VIA2 and RBV address map flags aren't valid\n");
		}
	}

	SaveResults();
#ifdef SIMPLE_CONSOLE
	ShowResults();
#else
	getchar();
#endif
}
