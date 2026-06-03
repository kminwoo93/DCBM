/**
 * @file mux.h
 * @brief Driver interface for the ADG708 8:1 analog multiplexer controlled by the PIC16F15256.
 *
 * The multiplexer is connected to port B with the following mapping:
 * - RB2 : Address bit A0 (least significant bit)
 * - RB3 : Address bit A1
 * - RB4 : Address bit A2 (most significant bit)
 * - RB5 : Enable input (active high)
 */
#ifndef MUX_H
#define MUX_H

#include <xc.h>
#include <stdint.h>

/**
 * @brief Initialize the GPIO pins that control the multiplexer.
 *
 * The function configures the address and enable pins as digital outputs and
 * ensures that the enable line is driven low before any channel selection
 * takes place.
 */
void mux_init(void);

/**
 * @brief Select one of the eight inputs on the multiplexer.
 *
 * @param channel Channel number in the range 1-8. Values outside of this
 * range are ignored in order to maintain the previously selected channel.
 */
void mux_select(uint8_t channel);

#endif /* MUX_H */