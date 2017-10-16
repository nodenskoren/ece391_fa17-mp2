/* tuxctl-ioctl.c
 *
 * Driver (skeleton) for the mp2 tuxcontrollers for ECE391 at UIUC.
 *
 * Mark Murphy 2006
 * Andrew Ofisher 2007
 * Steve Lumetta 12-13 Sep 2009
 * Puskar Naha 2013
 */

#include <asm/current.h>
#include <asm/uaccess.h>

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/file.h>
#include <linux/miscdevice.h>
#include <linux/kdev_t.h>
#include <linux/tty.h>
#include <linux/spinlock.h>

#include "tuxctl-ld.h"
#include "tuxctl-ioctl.h"
#include "mtcp.h"

#define debug(str, ...) \
	printk(KERN_DEBUG "%s: " str, __FUNCTION__, ## __VA_ARGS__)
	
/* conditional code to check if the program is ready to acknowledge new input from tux controller */
static unsigned int ack_flag;

/* spin lock to protect critical section */
struct bioc_lock {
	spinlock_t button_lock;
	unsigned long status; /* stores the command code of the button pressed */
} bioc_lock;

unsigned long led_value;
static const unsigned char seven_led_information[16] = {0xE7, 0x06, 0xCB, 0x8F,
														0x2E, 0xAD, 0xED, 0x86,
														0xEF, 0xAF, 0xEE, 0x6D,
														0xE1, 0x4F, 0xE9, 0xE8};

void tuxctl_BIOC_handler(unsigned int b, unsigned int c);												
/************************ Protocol Implementation *************************/

/* tuxctl_handle_packet()
 * IMPORTANT : Read the header for tuxctl_ldisc_data_callback() in 
 * tuxctl-ld.c. It calls this function, so all warnings there apply 
 * here as well.
 */
void tuxctl_handle_packet (struct tty_struct* tty, unsigned char* packet)
{
    unsigned a, b, c;

    a = packet[0]; /* Avoid printk() sign extending the 8-bit */
    b = packet[1]; /* values when printing them. */
    c = packet[2];

    /*printk("packet : %x %x %x\n", a, b, c); */
	switch(a) {
		
		/* (re)initialize the tux controller and LED  */
		case MTCP_RESET:
			tuxctl_ioctl(tty, NULL, TUX_INIT, 0);
			if(ack_flag == 1) {
				tuxctl_ioctl(tty, NULL, TUX_SET_LED, led_value);
			}
			break;
		
		/* Set the conditional flag to 1 when the program is ready to acknowledge a new input */
		case MTCP_ACK:
			ack_flag = 1;
			break;
		
		/* Set the status field to the instruction code of the button pressed */
		case MTCP_BIOC_EVENT:
			tuxctl_BIOC_handler(b, c);
			break;
			
		default:
			break;
	}
}

/*
 *tuxctl_BIOC_handler
 *	DESCRIPTION: the function produces the instruction code of the button pressed.
 *	INPUT: b and c corresponding to the 2nd and 3rd bytes of the input packet
 *	OUPUT: None
 *	Return Value: None.
 *	Side Effects: It sets the status of button_lock to the instruction code corresponding to the button pressed.
 */
void tuxctl_BIOC_handler(unsigned int b, unsigned int c) {
	unsigned long flags;
	spin_lock_irqsave(&(bioc_lock.button_lock), flags);
	bioc_lock.status = ~((~b & 0x0F) 
					| (((~c & 0x0F) << 4) & 0x9F) 
					| ((~c & 0x02) << 5)
					| ((~c & 0x04) << 3)); 
	spin_unlock_irqrestore(&(bioc_lock.button_lock), flags);
	return;		
}

/******** IMPORTANT NOTE: READ THIS BEFORE IMPLEMENTING THE IOCTLS ************
 *                                                                            *
 * The ioctls should not spend any time waiting for responses to the commands *
 * they send to the controller. The data is sent over the serial line at      *
 * 9600 BAUD. At this rate, a byte takes approximately 1 millisecond to       *
 * transmit; this means that there will be about 9 milliseconds between       *
 * the time you request that the low-level serial driver send the             *
 * 6-byte SET_LEDS packet and the time the 3-byte ACK packet finishes         *
 * arriving. This is far too long a time for a system call to take. The       *
 * ioctls should return immediately with success if their parameters are      *
 * valid.                                                                     *
 *                                                                            *
 ******************************************************************************/
int 
tuxctl_ioctl (struct tty_struct* tty, struct file* file, 
	      unsigned cmd, unsigned long arg)
{
	unsigned char mtcp_write[6];
	unsigned long flags;
	unsigned long bit_mask;
	unsigned long bit_mask_2;
	unsigned long bit_mask_3;	
	int i;
	int ret;
	
    switch (cmd) {
		/* Initialize all the variables needed to start taking tux controller actions */
		case TUX_INIT:
			ack_flag = 0;
			led_value = 0;
			bioc_lock.status = INSTRUCTION_CODE_BIT_MASK;
			bioc_lock.button_lock = SPIN_LOCK_UNLOCKED;
			mtcp_write[0] = MTCP_BIOC_ON;
			mtcp_write[1] = MTCP_LED_USR;
			tuxctl_ldisc_put(tty, mtcp_write, 2);
			break;	
		
		/* Copy the code for button to arg */
		case TUX_BUTTONS:
			/* Critical section to prevent multiple buttons pressed at once */
			spin_lock_irqsave(&(bioc_lock.button_lock), flags);
			ret = copy_to_user((void *)arg, (void *)&(bioc_lock.status), sizeof(uint32_t));				
			if (ret > 0){
				spin_unlock_irqrestore(&(bioc_lock.button_lock), flags);				
				return -EFAULT;
			}
			else {
				spin_unlock_irqrestore(&(bioc_lock.button_lock), flags);
				break;
			}
		
		case TUX_SET_LED:
			/* Prevention of LED set spam */
			if (ack_flag == 0) {
				return 0;
			}
			/* Prevention of LED set spam */
			ack_flag = 0;
			led_value = arg;
			mtcp_write[0] = MTCP_LED_SET;
			/* Switch to determine which LED should be on */
			mtcp_write[1] = LED_SWITCH;
			/* A few bitmasks filtering the display of each digit */ 
			bit_mask = LED_NUMBER_MASK;
			bit_mask_2 = DISPLAY_VALUE_MASK;
			bit_mask_3 = LED_BORDER_SELECTOR;
			
			/* Setting up LED digits 1 to 4 */
			for (i = 0; i < 4; i++) {
				if (arg & (bit_mask_3 << i)) {
					mtcp_write[(i + 2)] = (seven_led_information[((arg & (bit_mask << (4 * i))) >> (4 * i))] | ((arg >> (20 + i)) & bit_mask_2));
				}
				else {
					mtcp_write[(i + 2)] = 0;
				}
			}
			
			tuxctl_ldisc_put(tty, mtcp_write, 6);
			break;
				
		case TUX_LED_ACK:
			break;
		case TUX_LED_REQUEST:
			break;
		case TUX_READ_LED:
			break;
		default:
			return -EINVAL;
    }
	return 0;
}

