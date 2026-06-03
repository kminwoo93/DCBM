/* Board-level configuration header */
 
#ifndef BOARD_H
#define BOARD_H

#include <xc.h> /* Include processor definitions for XC8. */

/*
 * Define the device system clock in hertz. Centralising the oscillator value
 * here makes it easy for every module to pick up the same frequency without
 * duplicating literals across the project.
 */
#define BOARD_SYSTEM_CLOCK_HZ (32000000UL)
/*
 * XC8's built-in delay macros expect _XTAL_FREQ to match the oscillator. Map
 * the macro onto the board-level constant while allowing external definitions
 * (for example, command-line overrides) without triggering redefinition
 * warnings.
 */
#ifndef _XTAL_FREQ
#define _XTAL_FREQ BOARD_SYSTEM_CLOCK_HZ
#endif
#endif /* BOARD_H */