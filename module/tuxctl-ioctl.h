// All necessary declarations for the Tux Controller driver must be in this file

#ifndef TUXCTL_H
#define TUXCTL_H

#define TUX_SET_LED _IOR('E', 0x10, unsigned long)
#define TUX_READ_LED _IOW('E', 0x11, unsigned long*)
#define TUX_BUTTONS _IOW('E', 0x12, unsigned long*)
#define TUX_INIT _IO('E', 0x13)
#define TUX_LED_REQUEST _IO('E', 0x14)
#define TUX_LED_ACK _IO('E', 0x15)
#define EIGHT_BIT_CODE_BIT_MASK 0xFF
#define LED_NUMBER_MASK 0x000F
#define LED_SWITCH 0x0F
#define LED_BORDER_SELECTOR 0x10000
#define DISPLAY_VALUE_MASK 0x10

#endif

