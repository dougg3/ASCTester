#ifndef ASCTESTER_H
#define ASCTESTER_H

#define ASCBase				0xCC0
#define VIA2Base			0xCEC
#define VIA2DT				0xD70
#define ApplScratch			0xA78
#define BoxFlag				0xCB3
#define Ticks				0x16A
#define AddrMapFlags		0xDD0

typedef void (*VIA2Handler)(void);

// Reads an ASC register
static inline uint8_t ascReadReg(uint16_t offset)
{
	return *((*(volatile uint8_t **)ASCBase) + offset);
}

// Writes an ASC register
static inline void ascWriteReg(uint16_t offset, uint8_t value)
{
	*((*(volatile uint8_t **)ASCBase) + offset) = value;
}

// Reads a VIA2 register
static inline uint8_t via2ReadReg(uint16_t offset)
{
	return *((*(volatile uint8_t **)VIA2Base) + offset);
}

// Writes a VIA2 register
static inline void via2WriteReg(uint16_t offset, uint8_t value)
{
	*((*(volatile uint8_t **)VIA2Base) + offset) = value;
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
