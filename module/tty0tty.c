/* ########################################################################

   tty0tty - linux null modem emulator (module)  for kernel > 3.8

   ########################################################################

   Copyright (c) : 2013  Luis Claudio Gambôa Lopes
 
    Based in Tiny TTY driver -  Copyright (C) 2002-2004 Greg Kroah-Hartman (greg@kroah.com)

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

   For e-mail suggestions :  lcgamboa@yahoo.com
   ######################################################################## */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/serial.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/version.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
#include <linux/sched/signal.h>
#endif
#include <asm/uaccess.h>

#define DRIVER_VERSION "v1.2"
#define DRIVER_AUTHOR "Luis Claudio Gamboa Lopes <lcgamboa@yahoo.com>"
#define DRIVER_DESC "tty0tty null modem driver"

#ifdef SCULL_DEBUG
#define DEBUG_PRINTK(...) do { printk(__VA_ARGS__); } while (0)
#else
#define DEBUG_PRINTK(...)
#endif

/* Module information */
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

short pairs = 4;		//Default number of pairs of devices
module_param(pairs, short, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
MODULE_PARM_DESC(pairs,
		 "Number of pairs of devices to be created, maximum of 128");

#if 0
#define TTY0TTY_MAJOR		240	/* experimental range */
#define TTY0TTY_MINOR		16
#else
#define TTY0TTY_MAJOR		0	/* dynamic allocation */
#define TTY0TTY_MINOR		0
#endif

/* fake UART values */
//out
#define MCR_DTR		0x01
#define MCR_RTS		0x02
#define MCR_LOOP	0x04
//in
#define MSR_CTS		0x10
#define MSR_CD		0x20
#define MSR_DSR		0x40
#define MSR_RI		0x80

static struct tty_port *tport;

struct tty0tty_serial {
	struct tty_struct *tty;	/* pointer to the tty for this device */
	int open_count;		/* number of times this port has been opened */
	struct semaphore sem;	/* locks this structure */

	/* for tiocmget and tiocmset functions */
	int msr;		/* MSR shadow */
	int mcr;		/* MCR shadow */

	/* for ioctl fun */
	struct serial_struct serial;
	wait_queue_head_t wait;
	struct async_icount icount;

	/* for timing control */
	u64 nanosecs_per_byte;
};

static struct tty0tty_serial **tty0tty_table;	/* initially all NULL */

static int tty0tty_open(struct tty_struct *tty, struct file *file)
{
	struct tty0tty_serial *tty0tty;
	int index;
	int msr = 0;
	int mcr = 0;

	DEBUG_PRINTK(KERN_DEBUG "%s - \n", __FUNCTION__);

	/* initialize the pointer in case something fails */
	tty->driver_data = NULL;

	/* get the serial object associated with this tty pointer */
	index = tty->index;
	tty0tty = tty0tty_table[index];
	if (tty0tty == NULL) {
		/* first time accessing this device, let's create it */
		tty0tty = kmalloc(sizeof(*tty0tty), GFP_KERNEL);
		if (!tty0tty)
			return -ENOMEM;

		sema_init(&tty0tty->sem, 1);
		tty0tty->open_count = 0;

		tty0tty_table[index] = tty0tty;

	}

	tport[index].tty = tty;
	tty->port = &tport[index];

	if ((index % 2) == 0) {
		if (tty0tty_table[index + 1] != NULL)
			if (tty0tty_table[index + 1]->open_count > 0)
				mcr = tty0tty_table[index + 1]->mcr;
	} else {
		if (tty0tty_table[index - 1] != NULL)
			if (tty0tty_table[index - 1]->open_count > 0)
				mcr = tty0tty_table[index - 1]->mcr;
	}

//null modem connection

	if ((mcr & MCR_RTS) == MCR_RTS) {
		msr |= MSR_CTS;
	}

	if ((mcr & MCR_DTR) == MCR_DTR) {
		msr |= MSR_DSR;
		msr |= MSR_CD;
	}

	tty0tty->msr = msr;
	tty0tty->mcr = 0;

	/* register the tty driver */

	down(&tty0tty->sem);

	/* save our structure within the tty structure */
	tty->driver_data = tty0tty;
	tty0tty->tty = tty;

	++tty0tty->open_count;

	up(&tty0tty->sem);
	return 0;
}

static void do_close(struct tty0tty_serial *tty0tty)
{
	unsigned int msr = 0;

	DEBUG_PRINTK(KERN_DEBUG "%s - \n", __FUNCTION__);

	if ((tty0tty->tty->index % 2) == 0) {
		if (tty0tty_table[tty0tty->tty->index + 1] != NULL)
			if (tty0tty_table[tty0tty->tty->index + 1]->open_count >
			    0)
				tty0tty_table[tty0tty->tty->index + 1]->msr =
				    msr;
	} else {
		if (tty0tty_table[tty0tty->tty->index - 1] != NULL)
			if (tty0tty_table[tty0tty->tty->index - 1]->open_count >
			    0)
				tty0tty_table[tty0tty->tty->index - 1]->msr =
				    msr;
	}

	down(&tty0tty->sem);
	if (!tty0tty->open_count) {
		/* port was never opened */
		goto exit;
	}

	--tty0tty->open_count;
exit:
	up(&tty0tty->sem);

	return;
}

static void tty0tty_close(struct tty_struct *tty, struct file *file)
{
	struct tty0tty_serial *tty0tty = tty->driver_data;

	DEBUG_PRINTK(KERN_DEBUG "%s - \n", __FUNCTION__);

	if (tty0tty)
		do_close(tty0tty);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,6,1)
static ssize_t tty0tty_write(struct tty_struct *tty, const unsigned char *buffer,
			 size_t count)
#else
static int tty0tty_write(struct tty_struct *tty, const unsigned char *buffer,
			 int count)
#endif
{
	struct tty0tty_serial *tty0tty = tty->driver_data;
	int retval = 0;
	struct tty_struct *ttyx = NULL;
	u64 elapsed, delay;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,10,0)
	ktime_t start_time = ktime_get_ns();
#else
	u64 start_time = ktime_get_ns();
#endif

	if (!tty0tty)
		return -ENODEV;

	down(&tty0tty->sem);

	if (!tty0tty->open_count)
		/* port was not opened */
		goto exit;

	if ((tty0tty->tty->index % 2) == 0) {
		if (tty0tty_table[tty0tty->tty->index + 1] != NULL)
			if (tty0tty_table[tty0tty->tty->index + 1]->open_count >
			    0)
				ttyx =
				    tty0tty_table[tty0tty->tty->index + 1]->tty;
	} else {
		if (tty0tty_table[tty0tty->tty->index - 1] != NULL)
			if (tty0tty_table[tty0tty->tty->index - 1]->open_count >
			    0)
				ttyx =
				    tty0tty_table[tty0tty->tty->index - 1]->tty;
	}

//        tty->low_latency=1;

	if (ttyx != NULL) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,8,0)
		tty_insert_flip_string(ttyx->port, buffer, count);
		tty_flip_buffer_push(ttyx->port);
#else
		tty_insert_flip_string(ttyx, buffer, count);
		tty_flip_buffer_push(ttyx);
#endif
		retval = count;
	}

	/* Compute time spent pushing this buffer */
	elapsed = ktime_get_ns() - start_time;

	/* Compute how much to wait to make sure this buffer is, from
	 * userland's point of view, pushed as slow as it would be on
	 * a real serial port */
	delay = ((count * tty0tty->nanosecs_per_byte) - elapsed) / 1000;

	if (delay > 1)
		usleep_range(delay-1, delay);

exit:
	up(&tty0tty->sem);
	return retval;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 14, 0)
static unsigned int tty0tty_write_room(struct tty_struct *tty)
#else
static int tty0tty_write_room(struct tty_struct *tty)
#endif
{
	struct tty0tty_serial *tty0tty = tty->driver_data;
	int room = 0;

	if (!tty0tty)
		return -ENODEV;

	down(&tty0tty->sem);

	if (!tty0tty->open_count) {
		/* port was not opened */
		goto exit;
	}

	/* calculate how much room is left in the device */
	room = 255;

exit:
	up(&tty0tty->sem);
	return room;
}

#define RELEVANT_IFLAG(iflag) ((iflag) & (IGNBRK|BRKINT|IGNPAR|PARMRK|INPCK))

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,1,0)
static void tty0tty_set_termios(struct tty_struct *tty,
				const struct ktermios *old_termios)
#else
static void tty0tty_set_termios(struct tty_struct *tty,
				struct ktermios *old_termios)
#endif
{
	unsigned int cflag;
	unsigned int iflag;
	unsigned int bits_per_byte;
	unsigned int baud_rate;
	struct tty0tty_serial *tty0tty = tty->driver_data;

	DEBUG_PRINTK(KERN_DEBUG "%s - \n", __FUNCTION__);

	if (!tty0tty)
		return;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,7,0)
	cflag = tty->termios.c_cflag;
	iflag = tty->termios.c_iflag;
#else
	cflag = tty->termios->c_cflag;
	iflag = tty->termios->c_iflag;
#endif

	/* check that they really want us to change something */
	if (old_termios) {
		if ((cflag == old_termios->c_cflag) &&
		    (RELEVANT_IFLAG(iflag) ==
		     RELEVANT_IFLAG(old_termios->c_iflag))) {
			DEBUG_PRINTK(KERN_DEBUG " - nothing to change...\n");
			return;
		}
	}

	/* get the byte size */
	switch (cflag & CSIZE) {
	case CS5:
		bits_per_byte = 5;
		break;
	case CS6:
		bits_per_byte = 6;
		break;
	case CS7:
		bits_per_byte = 7;
		break;
	default:
	case CS8:
		bits_per_byte = 8;
		break;
	}

	DEBUG_PRINTK(KERN_DEBUG " - data bits = %d\n", bits_per_byte);

	/* Add start bit */
	bits_per_byte++;

	/* determine the parity */
	if (cflag & PARENB) {
		if (cflag & PARODD)
			DEBUG_PRINTK(KERN_DEBUG " - parity = odd\n");
		else
			DEBUG_PRINTK(KERN_DEBUG " - parity = even\n");
		/* Add parity bit */
		bits_per_byte++;
	} else
		DEBUG_PRINTK(KERN_DEBUG " - parity = none\n");

	/* figure out the stop bits requested */
	if (cflag & CSTOPB) {
		DEBUG_PRINTK(KERN_DEBUG " - stop bits = 2\n");
		bits_per_byte+=2;
	} else {
		DEBUG_PRINTK(KERN_DEBUG " - stop bits = 1\n");
		bits_per_byte+=1;
	}

	DEBUG_PRINTK(KERN_DEBUG " - bits per byte = %d\n", bits_per_byte);

	/* figure out the hardware flow control settings */
	if (cflag & CRTSCTS)
		DEBUG_PRINTK(KERN_DEBUG " - RTS/CTS is enabled\n");
	else
		DEBUG_PRINTK(KERN_DEBUG " - RTS/CTS is disabled\n");

	/* determine software flow control */
	/* if we are implementing XON/XOFF, set the start and 
	 * stop character in the device */
	if (I_IXOFF(tty) || I_IXON(tty)) {
#ifdef SCULL_DEBUG
		unsigned char stop_char = STOP_CHAR(tty);
		unsigned char start_char = START_CHAR(tty);
#endif

		/* if we are implementing INBOUND XON/XOFF */
		if (I_IXOFF(tty))
			DEBUG_PRINTK(KERN_DEBUG " - INBOUND XON/XOFF is enabled, "
			       "XON = %2x, XOFF = %2x\n", start_char,
			       stop_char);
		else
			DEBUG_PRINTK(KERN_DEBUG " - INBOUND XON/XOFF is disabled\n");

		/* if we are implementing OUTBOUND XON/XOFF */
		if (I_IXON(tty))
			DEBUG_PRINTK(KERN_DEBUG " - OUTBOUND XON/XOFF is enabled, "
			       "XON = %2x, XOFF = %2x\n", start_char,
			       stop_char);
		else
			DEBUG_PRINTK(KERN_DEBUG " - OUTBOUND XON/XOFF is disabled\n");
	}

	/* get the baud rate wanted */
	baud_rate = tty_get_baud_rate(tty);
	DEBUG_PRINTK(KERN_DEBUG " - baud rate = %d\n", baud_rate);

	/* get the time a real serial port would require to push a byte */
	tty0tty->nanosecs_per_byte = 1000000000ULL / (baud_rate / bits_per_byte);
	DEBUG_PRINTK(KERN_DEBUG " - time per byte = %lluns\n", tty0tty->nanosecs_per_byte);
}

//static int tty0tty_tiocmget(struct tty_struct *tty, struct file *file)
static int tty0tty_tiocmget(struct tty_struct *tty)
{
	struct tty0tty_serial *tty0tty = tty->driver_data;

	unsigned int result = 0;
	unsigned int msr = tty0tty->msr;
	unsigned int mcr = tty0tty->mcr;

	result = ((mcr & MCR_DTR) ? TIOCM_DTR : 0) |	/* DTR is set */
	    ((mcr & MCR_RTS) ? TIOCM_RTS : 0) |	/* RTS is set */
	    ((mcr & MCR_LOOP) ? TIOCM_LOOP : 0) |	/* LOOP is set */
	    ((msr & MSR_CTS) ? TIOCM_CTS : 0) |	/* CTS is set */
	    ((msr & MSR_CD) ? TIOCM_CAR : 0) |	/* Carrier detect is set */
	    ((msr & MSR_RI) ? TIOCM_RI : 0) |	/* Ring Indicator is set */
	    ((msr & MSR_DSR) ? TIOCM_DSR : 0);	/* DSR is set */

	return result;
}

//static int tty0tty_tiocmset(struct tty_struct *tty, struct file *file,
static int tty0tty_tiocmset(struct tty_struct *tty,
			    unsigned int set, unsigned int clear)
{
	struct tty0tty_serial *tty0tty = tty->driver_data;
	unsigned int mcr = tty0tty->mcr;
	unsigned int msr = 0;

	DEBUG_PRINTK(KERN_DEBUG "%s - \n", __FUNCTION__);

	if ((tty0tty->tty->index % 2) == 0) {
		if (tty0tty_table[tty0tty->tty->index + 1] != NULL)
			if (tty0tty_table[tty0tty->tty->index + 1]->open_count >
			    0)
				msr =
				    tty0tty_table[tty0tty->tty->index + 1]->msr;
	} else {
		if (tty0tty_table[tty0tty->tty->index - 1] != NULL)
			if (tty0tty_table[tty0tty->tty->index - 1]->open_count >
			    0)
				msr =
				    tty0tty_table[tty0tty->tty->index - 1]->msr;
	}

//null modem connection

	if (set & TIOCM_RTS) {
		mcr |= MCR_RTS;
		msr |= MSR_CTS;
	}

	if (set & TIOCM_DTR) {
		mcr |= MCR_DTR;
		msr |= MSR_DSR;
		msr |= MSR_CD;
	}

	if (clear & TIOCM_RTS) {
		mcr &= ~MCR_RTS;
		msr &= ~MSR_CTS;
	}

	if (clear & TIOCM_DTR) {
		mcr &= ~MCR_DTR;
		msr &= ~MSR_DSR;
		msr &= ~MSR_CD;
	}

	/* set the new MCR value in the device */
	tty0tty->mcr = mcr;

	if ((tty0tty->tty->index % 2) == 0) {
		if (tty0tty_table[tty0tty->tty->index + 1] != NULL)
			if (tty0tty_table[tty0tty->tty->index + 1]->open_count >
			    0)
				tty0tty_table[tty0tty->tty->index + 1]->msr =
				    msr;
	} else {
		if (tty0tty_table[tty0tty->tty->index - 1] != NULL)
			if (tty0tty_table[tty0tty->tty->index - 1]->open_count >
			    0)
				tty0tty_table[tty0tty->tty->index - 1]->msr =
				    msr;
	}
	return 0;
}

static int tty0tty_ioctl_tiocgserial(struct tty_struct *tty,
				     unsigned int cmd, unsigned long arg)
{
	struct tty0tty_serial *tty0tty = tty->driver_data;

	DEBUG_PRINTK(KERN_DEBUG "%s - \n", __FUNCTION__);

	if (cmd == TIOCGSERIAL) {
		struct serial_struct tmp;

		if (!arg)
			return -EFAULT;

		memset(&tmp, 0, sizeof(tmp));

		tmp.type = tty0tty->serial.type;
		tmp.line = tty0tty->serial.line;
		tmp.port = tty0tty->serial.port;
		tmp.irq = tty0tty->serial.irq;
		tmp.flags = ASYNC_SKIP_TEST | ASYNC_AUTO_IRQ;
		tmp.xmit_fifo_size = tty0tty->serial.xmit_fifo_size;
		tmp.baud_base = tty0tty->serial.baud_base;
		tmp.close_delay = 5 * HZ;
		tmp.closing_wait = 30 * HZ;
		tmp.custom_divisor = tty0tty->serial.custom_divisor;
		tmp.hub6 = tty0tty->serial.hub6;
		tmp.io_type = tty0tty->serial.io_type;

		if (copy_to_user
		    ((void __user *)arg, &tmp, sizeof(struct serial_struct)))
			return -EFAULT;
		return 0;
	}
	return -ENOIOCTLCMD;
}

static int tty0tty_ioctl_tiocmiwait(struct tty_struct *tty,
				    unsigned int cmd, unsigned long arg)
{
	struct tty0tty_serial *tty0tty = tty->driver_data;

	DEBUG_PRINTK(KERN_DEBUG "%s - \n", __FUNCTION__);

	if (cmd == TIOCMIWAIT) {
		DECLARE_WAITQUEUE(wait, current);
		struct async_icount cnow;
		struct async_icount cprev;

		cprev = tty0tty->icount;
		while (1) {
			add_wait_queue(&tty0tty->wait, &wait);
			set_current_state(TASK_INTERRUPTIBLE);
			schedule();
			remove_wait_queue(&tty0tty->wait, &wait);

			/* see if a signal woke us up */
			if (signal_pending(current))
				return -ERESTARTSYS;

			cnow = tty0tty->icount;
			if (cnow.rng == cprev.rng && cnow.dsr == cprev.dsr &&
			    cnow.dcd == cprev.dcd && cnow.cts == cprev.cts)
				return -EIO;	/* no change => error */
			if (((arg & TIOCM_RNG) && (cnow.rng != cprev.rng)) ||
			    ((arg & TIOCM_DSR) && (cnow.dsr != cprev.dsr)) ||
			    ((arg & TIOCM_CD) && (cnow.dcd != cprev.dcd)) ||
			    ((arg & TIOCM_CTS) && (cnow.cts != cprev.cts))) {
				return 0;
			}
			cprev = cnow;
		}

	}
	return -ENOIOCTLCMD;
}

static int tty0tty_ioctl_tiocgicount(struct tty_struct *tty,
				     unsigned int cmd, unsigned long arg)
{
	struct tty0tty_serial *tty0tty = tty->driver_data;

	DEBUG_PRINTK(KERN_DEBUG "%s - \n", __FUNCTION__);

	if (cmd == TIOCGICOUNT) {
		struct async_icount cnow = tty0tty->icount;
		struct serial_icounter_struct icount;

		icount.cts = cnow.cts;
		icount.dsr = cnow.dsr;
		icount.rng = cnow.rng;
		icount.dcd = cnow.dcd;
		icount.rx = cnow.rx;
		icount.tx = cnow.tx;
		icount.frame = cnow.frame;
		icount.overrun = cnow.overrun;
		icount.parity = cnow.parity;
		icount.brk = cnow.brk;
		icount.buf_overrun = cnow.buf_overrun;

		if (copy_to_user((void __user *)arg, &icount, sizeof(icount)))
			return -EFAULT;
		return 0;
	}
	return -ENOIOCTLCMD;
}

static int tty0tty_ioctl(struct tty_struct *tty,
			 unsigned int cmd, unsigned long arg)
{
	DEBUG_PRINTK(KERN_DEBUG "%s - %04X \n", __FUNCTION__, cmd);

	switch (cmd) {
	case TIOCGSERIAL:
		return tty0tty_ioctl_tiocgserial(tty, cmd, arg);
	case TIOCMIWAIT:
		return tty0tty_ioctl_tiocmiwait(tty, cmd, arg);
	case TIOCGICOUNT:
		return tty0tty_ioctl_tiocgicount(tty, cmd, arg);
	}

	return -ENOIOCTLCMD;
}

static int tty0tty_get_serial(struct tty_struct *tty, struct serial_struct *p)
{
    	DEBUG_PRINTK(KERN_DEBUG "%s - \n", __FUNCTION__);
	struct tty0tty_serial *tty0tty = tty->driver_data;

	if (!tty0tty)
		return -ENODEV;

	p->type = tty0tty->serial.type;
	p->line = tty0tty->serial.line;
	p->port = tty0tty->serial.port;
	p->irq = tty0tty->serial.irq;
	p->flags = ASYNC_SKIP_TEST | ASYNC_AUTO_IRQ;
	p->xmit_fifo_size = tty0tty->serial.xmit_fifo_size;
	p->baud_base = tty0tty->serial.baud_base;
	p->close_delay = 5 * HZ;
	p->closing_wait = 30 * HZ;
	p->custom_divisor = tty0tty->serial.custom_divisor;
	p->hub6 = tty0tty->serial.hub6;
	p->io_type = tty0tty->serial.io_type;

	return 0;
}

static struct tty_operations serial_ops = {
	.open = tty0tty_open,
	.close = tty0tty_close,
	.write = tty0tty_write,
	.write_room = tty0tty_write_room,
	.set_termios = tty0tty_set_termios,
	.tiocmget = tty0tty_tiocmget,
	.tiocmset = tty0tty_tiocmset,
	.ioctl = tty0tty_ioctl,
	.get_serial = tty0tty_get_serial,
};

static struct tty_driver *tty0tty_tty_driver;

static int __init tty0tty_init(void)
{
	int retval;
	int i;
	if (pairs > 128)
		pairs = 128;
	if (pairs < 1)
		pairs = 1;
	tport = kmalloc(2 * pairs * sizeof(struct tty_port), GFP_KERNEL);
	tty0tty_table =
	    kmalloc(2 * pairs * sizeof(struct tty0tty_serial *), GFP_KERNEL);

	for (i = 0; i < 2 * pairs; i++) {
		tty0tty_table[i] = NULL;
	}

	DEBUG_PRINTK(KERN_DEBUG "%s - \n", __FUNCTION__);

	/* allocate the tty driver */
	tty0tty_tty_driver = tty_alloc_driver(2 * pairs, 0);
	if (!tty0tty_tty_driver)
		return -ENOMEM;

	/* initialize the tty driver */
	tty0tty_tty_driver->owner = THIS_MODULE;
	tty0tty_tty_driver->driver_name = "tty0tty";
	tty0tty_tty_driver->name = "tnt";
	/* no more devfs subsystem */
	tty0tty_tty_driver->major = TTY0TTY_MAJOR;
	tty0tty_tty_driver->minor_start = TTY0TTY_MINOR;
	tty0tty_tty_driver->type = TTY_DRIVER_TYPE_SERIAL;
	tty0tty_tty_driver->subtype = SERIAL_TYPE_NORMAL;
	tty0tty_tty_driver->flags =
	    TTY_DRIVER_RESET_TERMIOS | TTY_DRIVER_REAL_RAW;
	/* no more devfs subsystem */
	tty0tty_tty_driver->init_termios = tty_std_termios;
	tty0tty_tty_driver->init_termios.c_iflag = 0;
	tty0tty_tty_driver->init_termios.c_oflag = 0;
	tty0tty_tty_driver->init_termios.c_cflag = B38400 | CS8 | CREAD;
	tty0tty_tty_driver->init_termios.c_lflag = 0;
	tty0tty_tty_driver->init_termios.c_ispeed = 38400;
	tty0tty_tty_driver->init_termios.c_ospeed = 38400;

	tty_set_operations(tty0tty_tty_driver, &serial_ops);

	for (i = 0; i < 2 * pairs; i++) {
		tty_port_init(&tport[i]);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,7,0)
		tty_port_link_device(&tport[i], tty0tty_tty_driver, i);
#endif
	}

	retval = tty_register_driver(tty0tty_tty_driver);
	if (retval) {
		printk(KERN_ERR "failed to register tty0tty tty driver");
		tty_driver_kref_put(tty0tty_tty_driver);
		return retval;
	}

	printk(KERN_INFO DRIVER_DESC " " DRIVER_VERSION "\n");
	return retval;
}

static void __exit tty0tty_exit(void)
{
	struct tty0tty_serial *tty0tty;
	int i;

	DEBUG_PRINTK(KERN_DEBUG "%s - \n", __FUNCTION__);

	for (i = 0; i < 2 * pairs; ++i) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,7,0)
		tty_port_destroy(&tport[i]);
#endif
		tty_unregister_device(tty0tty_tty_driver, i);
	}
	tty_unregister_driver(tty0tty_tty_driver);

	/* shut down all of the timers and free the memory */
	for (i = 0; i < 2 * pairs; ++i) {
		tty0tty = tty0tty_table[i];
		if (tty0tty) {
			/* close the port */
			while (tty0tty->open_count)
				do_close(tty0tty);

			/* shut down our timer and free the memory */
			kfree(tty0tty);
			tty0tty_table[i] = NULL;
		}
	}
	kfree(tport);
	kfree(tty0tty_table);
}

module_init(tty0tty_init);
module_exit(tty0tty_exit);
