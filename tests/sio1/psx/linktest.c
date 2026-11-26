/*
 * Link cable test program for the PlayStation.
 *
 * Runs on the bare machine (no BIOS calls, no interrupts), so the same
 * executable can be started on two real consoles joined by a link cable or
 * in two linked emulators, and on one console with a loopback plug.
 *
 * The role is read from 0x80100200, the result is left at 0x80100000:
 *   role 1 (A): sends bytes and checks that each comes back inverted
 *   role 2 (B): sends back every byte it receives, inverted
 *   role 3 (loopback): as A, but expects its own bytes
 *   role 4: does not use the port, only times a delay loop 64 times (to
 *           tell what an emulator's savestates do to timing by themselves)
 *   role 5 (alone): for a console with nothing in its port. Leaves what SR
 *           reads before the port is set up, then sends one byte, which
 *           has to leave although nobody takes it
 * A and B find each other with a byte that A repeats until B answers it:
 * with no cable DSR may well read as on, so it tells nothing.
 * The first half of the bytes is received by polling SR, the second half
 * by waiting for the SIO1 bit in I_STAT (the interrupt request, with the
 * cpu's interrupts off). For the first 64 bytes of each half the time from
 * writing the byte to having its answer is recorded, in units of 8 cycles.
 */

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

#define REG16(a)	(*(volatile u16 *)(a))
#define REG32(a)	(*(volatile u32 *)(a))
#define REG8(a)		(*(volatile u8 *)(a))

#define SIO1_DR		REG8(0x1f801050)
#define SIO1_SR		REG16(0x1f801054)
#define SIO1_MR		REG16(0x1f801058)
#define SIO1_CR		REG16(0x1f80105a)
#define SIO1_BR		REG16(0x1f80105e)
#define I_STAT		REG32(0x1f801070)
#define I_MASK		REG32(0x1f801074)
#define T2_COUNT	REG16(0x1f801120)
#define T2_MODE		REG16(0x1f801124)

#define SR_TXRDY	0x0001
#define SR_RXRDY	0x0002
#define SR_ERRORS	0x0038
#define SR_TXEMPTY	0x0004
#define CR_ON		0x0027	/* TXEN, DTR, RXEN, RTS */
#define CR_ERRRST	0x0010
#define CR_INTRST	0x0040
#define CR_RXIEN	0x0800

#ifndef NBYTES
#define NBYTES		1024
#endif
#define TRIES		4000000
#define SYNC		0x16
#define SYNC_TRIES	40000	/* many times a byte's way there and back */
#define FIND_TRIES	(16 * TRIES)	/* for the other console to turn up */

struct result {
	u32 magic;
	u32 role;
	u32 status;	/* 1 = finished, 0xe0.. = gave up at that step */
	u32 count;	/* bytes handled */
	u32 sum;	/* over the bytes received */
	u32 wrong;	/* bytes that were not what was expected */
	u32 errors;	/* SR error bits seen */
	u32 sr0;	/* role 5: SR bits 0-8 as found */
	u16 rt_poll[64];
	u16 rt_irq[64];
};

static struct result * const res = (struct result *)0x80100000;

static u32 pattern(u32 i)
{
	return (i * 37 + 11) & 0xff;
}

static int wait_sr_for(u32 bit, u32 tries)
{
	u32 n;

	for (n = 0; n < tries; n++)
		if (SIO1_SR & bit)
			return 0;
	return -1;
}

static int wait_sr(u32 bit)
{
	return wait_sr_for(bit, TRIES);
}

static int wait_irq(void)
{
	u32 n;

	for (n = 0; n < TRIES; n++)
		if (I_STAT & 0x100)
			return 0;
	return -1;
}

static void fail(u32 step)
{
	res->status = 0xe0 + step;
	for (;;)
		;
}

/* A: until the other end is there, these go nowhere. One at a time: the
 * next is not written before the last has left (the other end may hold it
 * back with its RTS for a while) and had its time to be answered. */
static void find_peer(u32 loopback)
{
	u32 n;

	for (n = 0; n < FIND_TRIES / SYNC_TRIES; n++) {
		SIO1_DR = SYNC;
		if (wait_sr_for(SR_TXEMPTY, FIND_TRIES))
			fail(2);
		if (wait_sr_for(SR_RXRDY, SYNC_TRIES))
			continue;
		if (SIO1_DR == (loopback ? SYNC : SYNC ^ 0xff))
			return;
	}
	fail(1);
}

static void sender(u32 loopback)
{
	u32 i, b, expect, sr;
	u16 t0;

	find_peer(loopback);
	for (i = 0; i < NBYTES; i++) {
		int irq = i >= NBYTES / 2;

		if (wait_sr(SR_TXRDY))
			fail(2);
		if (irq) {
			SIO1_CR = CR_ON | CR_RXIEN | CR_ERRRST;
			I_STAT = ~0x100u;
		}
		t0 = T2_COUNT;
		SIO1_DR = pattern(i);
		if (irq) {
			if (wait_irq())
				fail(4);
		}
		else if (wait_sr(SR_RXRDY))
			fail(3);
		sr = SIO1_SR;
		b = SIO1_DR;
		if (i < 64)
			res->rt_poll[i] = T2_COUNT - t0;
		else if (irq && i - NBYTES / 2 < 64)
			res->rt_irq[i - NBYTES / 2] = T2_COUNT - t0;

		expect = loopback ? pattern(i) : pattern(i) ^ 0xff;
		if (b != expect)
			res->wrong++;
		if (sr & SR_ERRORS) {
			res->errors++;
			SIO1_CR = CR_ON | CR_ERRRST;
		}
		res->sum = res->sum * 31 + b;
		res->count = i + 1;
	}
}

static void echo(void)
{
	u32 i, b;

	/* B: A's first byte that gets here */
	if (wait_sr_for(SR_RXRDY, FIND_TRIES))
		fail(1);
	if (SIO1_DR != SYNC)
		fail(1);
	if (wait_sr(SR_TXRDY))
		fail(6);
	SIO1_DR = SYNC ^ 0xff;

	for (i = 0; i < NBYTES; i++) {
		if (wait_sr(SR_RXRDY))
			fail(5);
		if (SIO1_SR & SR_ERRORS) {
			res->errors++;
			SIO1_CR = CR_ON | CR_ERRRST;
		}
		b = SIO1_DR;
		if (wait_sr(SR_TXRDY))
			fail(6);
		SIO1_DR = b ^ 0xff;
		if (b != pattern(i))
			res->wrong++;
		res->sum = res->sum * 31 + b;
		res->count = i + 1;
	}
}

static void timer_only(void)
{
	volatile u32 n;
	u32 i;
	u16 t0;

	for (i = 0; i < 64; i++) {
		t0 = T2_COUNT;
		for (n = 0; n < 20000; n++)
			;
		res->rt_poll[i] = T2_COUNT - t0;
		res->count = i + 1;
	}
}

static void alone(void)
{
	if (wait_sr(SR_TXRDY))
		fail(2);
	SIO1_DR = 0x55;
	/* sent, to nobody */
	if (wait_sr(SR_TXEMPTY))
		fail(7);
	res->count = 1;
}

void main(void)
{
	u32 role = REG32(0x80100200);
	u32 i;

	for (i = 0; i < sizeof(*res) / 4; i++)
		((volatile u32 *)res)[i] = 0;
	res->magic = 0x314b4e4c; /* "LNK1" */
	res->role = role;

	I_MASK = 0;
	T2_MODE = 0x0200; /* system clock / 8, free running */
	if (role == 4) {
		timer_only();
		res->status = 1;
		return;
	}
	res->sr0 = SIO1_SR & 0x01ff;
	SIO1_CR = CR_INTRST;
	SIO1_MR = 0x004e; /* 8 bits, no parity, 1 stop bit, x16 */
	SIO1_BR = 0x0012; /* "115200" */
	SIO1_CR = CR_ON;

	if (role == 5)
		alone();
	else if (role == 2)
		echo();
	else
		sender(role == 3);
	res->status = 1;
}
