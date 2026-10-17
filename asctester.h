#ifndef ASCTESTER_H
#define ASCTESTER_H

#define ASCBase				0xCC0
#define VIA2Base			0xCEC
#define VIA2DT				0xD70
#define ApplScratch			0xA78
#define BoxFlag				0xCB3
#define Ticks				0x16A
#define AddrMapFlags		0xDD0
#define VIA1Base			0x1D4

typedef void (*VIA2Handler)(void);

// Reads an ASC register
static inline uint8_t ascReadReg(uint16_t offset)
{
	return *((volatile uint8_t *)((*(uint8_t **)ASCBase) + offset));
}

// Writes an ASC register
static inline void ascWriteReg(uint16_t offset, uint8_t value)
{
	*((volatile uint8_t *)((*(uint8_t **)ASCBase) + offset)) = value;
}

static inline void __attribute__ ((always_inline)) ascWriteStereoSample(uint8_t sample)
{
	// Keep the final move instructions for each channel directly together.
	// The idea is to try to avoid the situation on slower original ASC machines
	// where the "FIFO full" IRQ fires before both samples have been written.
	volatile uint8_t *asc = (volatile uint8_t *)*(uint8_t **)ASCBase;
	__asm__ volatile (
		"move.b %0, (%1)\n\t"
		"move.b %0, 0x400(%1)\n\t"
		:
		: "d"(sample), "a"(asc)
		: "memory"
	);
}

// Reads a VIA2 register
static inline uint8_t via2ReadReg(uint16_t offset)
{
	return *((volatile uint8_t *)((*(uint8_t **)VIA2Base) + offset));
}

// Writes a VIA2 register
static inline void via2WriteReg(uint16_t offset, uint8_t value)
{
	*((volatile uint8_t *)((*(uint8_t **)VIA2Base) + offset)) = value;
}

// Reads a VIA1 register
static inline uint8_t via1ReadReg(uint16_t offset)
{
	return *((volatile uint8_t *)((*(uint8_t **)VIA1Base) + offset));
}

// Writes a VIA1 register
static inline void via1WriteReg(uint16_t offset, uint8_t value)
{
	*((volatile uint8_t *)((*(uint8_t **)VIA1Base) + offset)) = value;
}

// Loads VIA1 timer 2 with a count and starts it, which also clears its flag
static inline void timerLoad(uint16_t count)
{
	via1WriteReg(0x1000, count & 0xFF);
	via1WriteReg(0x1200, count >> 8);
}

// Reads the VIA1 timer 2 counter. This clears its flag.
static inline uint16_t timerCount(void)
{
	uint8_t hi, lo;

	do
	{
		hi = via1ReadReg(0x1200);
		lo = via1ReadReg(0x1000);
	} while (hi != via1ReadReg(0x1200));

	return (hi << 8) | lo;
}

// Starts VIA1 timer 2 counting down from $FFFF, used as a stopwatch
static inline void timerStart(void)
{
	timerLoad(0xFFFF);
}

// Returns the number of VIA ticks since timerStart (valid for less than $FFFF ticks)
static inline uint16_t timerElapsed(void)
{
	return 0xFFFF - timerCount();
}

// The VIA2 dispatch table
static inline volatile VIA2Handler *via2Handlers(void)
{
	return (VIA2Handler *)VIA2DT;
}

// Gets the number of 60 Hz ticks that have elapsed
static inline uint32_t ticks(void)
{
	return *(volatile uint32_t *)Ticks;
}

static inline uint32_t addrMapFlags(void)
{
	return *(uint32_t *)AddrMapFlags;
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

#endif
