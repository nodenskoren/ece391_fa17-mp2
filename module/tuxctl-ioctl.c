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
	
static unsigned int ack_flag;
struct bioc_lock {
	spinlock_t button_lock;
	unsigned long status;
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
		
		case MTCP_RESET:
		
			tuxctl_ioctl(tty, NULL, TUX_INIT, 0);
			if(ack_flag == 1) {
				tuxctl_ioctl(tty, NULL, TUX_SET_LED, led_value);
			}
			return;
		
		case MTCP_ACK:
		
			ack_flag = 1;
			return;
			
		case MTCP_BIOC_EVENT:
		
			tuxctl_BIOC_handler(b, c);
			return;
			
		default:
			return;
	}
}

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
		
		case TUX_INIT:
		
			ack_flag = 0;
			led_value = 0;
			bioc_lock.status = 0xFF;
			bioc_lock.button_lock = SPIN_LOCK_UNLOCKED;
			mtcp_write[0] = MTCP_BIOC_ON;
			mtcp_write[1] = MTCP_LED_USR;
			tuxctl_ldisc_put(tty, mtcp_write, 2);
			return 0;	
			
		case TUX_BUTTONS:
		
			spin_lock_irqsave(&(bioc_lock.button_lock), flags);
			ret = copy_to_user((void *)arg, (void *)&(bioc_lock.status), sizeof(uint32_t));				

			if (ret > 0){
				spin_unlock_irqrestore(&(bioc_lock.button_lock), flags);				
				return -EFAULT;
			}	
			
			else {
				spin_unlock_irqrestore(&(bioc_lock.button_lock), flags);
				return 0;
			}
		
		case TUX_SET_LED:
		
			if (ack_flag == 0) {
				return 0;
			}
			
			led_value = arg;
			ack_flag = 0;
			mtcp_write[0] = MTCP_LED_USR;
			tuxctl_ldisc_put(tty, &mtcp_write[0], 1);
			mtcp_write[0] = MTCP_LED_SET;
			mtcp_write[1] = 0x0F;
			bit_mask = 0x1000;
			bit_mask_2 = 0x000F;
			bit_mask_3 = 0x10;
			
			for (i = 2; i < 6; i++) {
				if ((arg >> 4) & bit_mask)
					mtcp_write[i] = seven_led_information[(arg & bit_mask_2) >> (4 * (i-2))] | ((arg >> (18 + i)) & bit_mask_3);
				else
					mtcp_write[i] = 0;
				
				bit_mask = bit_mask << 1;
				bit_mask_2 = bit_mask_2 << 4;
			}
			
			tuxctl_ldisc_put(tty, mtcp_write, 6);
			return 0;
				
		case TUX_LED_ACK:
			return 0;
		case TUX_LED_REQUEST:
			return 0;
		case TUX_READ_LED:
			return 0;
		default:
			return -EINVAL;
    }
}
