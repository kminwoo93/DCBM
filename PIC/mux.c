/*
 * File:   mux.c
 * Author: MinwooKim
 *
 * @brief Implementation of the ADG708 multiplexer control driver for the PIC16F15256.
 * Created on October 24, 2025, 11:22 AM
 */

#include "board.h"
#include "mux.h"

/* -------------------------------------------------------------------------- */
/*                              Helper macros                                 */
/* -------------------------------------------------------------------------- */

#define MUX_A0_LAT              LATBbits.LATB2
#define MUX_A1_LAT              LATBbits.LATB3
#define MUX_A2_LAT              LATBbits.LATB4
#define MUX_EN_LAT              LATBbits.LATB5

#define MUX_A0_TRIS             TRISBbits.TRISB2
#define MUX_A1_TRIS             TRISBbits.TRISB3
#define MUX_A2_TRIS             TRISBbits.TRISB4
#define MUX_EN_TRIS             TRISBbits.TRISB5

#define MUX_A0_ANSEL            ANSELBbits.ANSB2
#define MUX_A1_ANSEL            ANSELBbits.ANSB3
#define MUX_A2_ANSEL            ANSELBbits.ANSB4
#define MUX_EN_ANSEL            ANSELBbits.ANSB5

#define MUX_DISABLE()           (MUX_EN_LAT = 0)
#define MUX_ENABLE()            (MUX_EN_LAT = 1)

/* Valid channel range for the ADG708 when addressed with three bits. */
#define MUX_CHANNEL_MIN         (1U)
#define MUX_CHANNEL_MAX         (8U)

/* Number of address bits required to cover all channels. */
#define MUX_ADDRESS_WIDTH       (3U)

/* Mask covering the valid address bits. */
#define MUX_ADDRESS_MASK        ((1U << MUX_ADDRESS_WIDTH) - 1U)

/* Mask for isolating a single bit from the address. */
#define MUX_SINGLE_BIT_MASK     (0x01U)

/* Bit positions within the address value. */
#define MUX_ADDRESS_BIT_A0      (0U)
#define MUX_ADDRESS_BIT_A1      (1U)
#define MUX_ADDRESS_BIT_A2      (2U)

/* -------------------------------------------------------------------------- */
/*                        Static helper declarations                          */
/* -------------------------------------------------------------------------- */

/**
 * @brief Write the three-bit address onto the PORTB pins.
 *
 * The function assumes that the enable signal is already low, preventing any
 * glitch on the output of the multiplexer while the address lines settle.
 */
static inline void mux_write_address(uint8_t address)
{
    uint8_t masked_address = address & MUX_ADDRESS_MASK;

    MUX_A0_LAT = (masked_address >> MUX_ADDRESS_BIT_A0) & MUX_SINGLE_BIT_MASK;
    MUX_A1_LAT = (masked_address >> MUX_ADDRESS_BIT_A1) & MUX_SINGLE_BIT_MASK;
    MUX_A2_LAT = (masked_address >> MUX_ADDRESS_BIT_A2) & MUX_SINGLE_BIT_MASK;
}

/* -------------------------------------------------------------------------- */
/*                             Public interface                               */
/* -------------------------------------------------------------------------- */

void mux_init(void)
{
    /* Ensure each control pin is configured for digital operation. */
    MUX_A0_ANSEL = 0;  /* RB2 as digital for A0. */
    MUX_A1_ANSEL = 0;  /* RB3 as digital for A1. */
    MUX_A2_ANSEL = 0;  /* RB4 as digital for A2. */
    MUX_EN_ANSEL = 0;  /* RB5 as digital for EN. */

    /* Default LAT values are prepared while the pins are still inputs to avoid glitches. */
    mux_write_address(0U);
    MUX_DISABLE();

    /* Finally, drive the pins as outputs with the predefined safe states. */
    MUX_A0_TRIS = 0;
    MUX_A1_TRIS = 0;
    MUX_A2_TRIS = 0;
    MUX_EN_TRIS = 0;
}

void mux_select(uint8_t channel)
{
    if ((channel < MUX_CHANNEL_MIN) || (channel > MUX_CHANNEL_MAX))
    {
        /* Ignore invalid channel requests to keep the previous selection. */
        return;
    }

    uint8_t address = (uint8_t)(channel - MUX_CHANNEL_MIN);

    MUX_DISABLE();
    mux_write_address(address);
    MUX_ENABLE();
}