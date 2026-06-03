/*
 * File:   i2c.c
 * Author: MinwooKim
 *
 * Created on October 24, 2025, 11:53 AM
 */


#include "xc.h"
#include "board.h"
#include "i2c.h"

#if defined(ANSELCbits)
#ifndef I2C1_SCL_ANSEL
#define I2C1_SCL_ANSEL  ANSELCbits.ANSC3
#endif
#ifndef I2C1_SDA_ANSEL
#define I2C1_SDA_ANSEL  ANSELCbits.ANSC4
#endif
#endif

#ifndef I2C1_SCL_TRIS
#define I2C1_SCL_TRIS   TRISCbits.TRISC3
#endif

#ifndef I2C1_SDA_TRIS
#define I2C1_SDA_TRIS   TRISCbits.TRISC4
#endif

#ifndef I2C1_SCL_LAT
#define I2C1_SCL_LAT    LATCbits.LATC3
#endif

#ifndef I2C1_SDA_LAT
#define I2C1_SDA_LAT    LATCbits.LATC4
#endif

#ifndef I2C1_CLK_INPUT_PPS
#define I2C1_CLK_INPUT_PPS   0x13U
#endif

#ifndef I2C1_SDA_INPUT_PPS
#define I2C1_SDA_INPUT_PPS   0x14U
#endif

#ifndef I2C1_CLK_OUTPUT_PPS
#define I2C1_CLK_OUTPUT_PPS  0x07U
#endif

#ifndef I2C1_SDA_OUTPUT_PPS
#define I2C1_SDA_OUTPUT_PPS  0x08U
#endif

static inline void i2c_clear_interrupt_flag(void)
{
    PIR1bits.SSP1IF = 0U;
}

void i2c_idle_wait(void)
{
    while ((SSP1CON2 & 0x1FU) != 0U || SSP1STATbits.R_nW != 0U)
    {
        ;
    }

    i2c_clear_interrupt_flag();
}

void i2c_init(void)
{
//PPS Unlock sequence start-------------------
    INTCONbits.GIE = 0; //Suspend interrupts
    PPSLOCK = 0x55; //Required sequence
    PPSLOCK = 0xAA; //Required sequence
    PPSLOCKbits.PPSLOCKED = 0; //Clear PPSLOCKED bit
    INTCONbits.GIE = 1; //Restore interrupts
//PPS Unlock sequence end---------------------

//PPS configuration start---------------------
    SSP1CLKPPS = I2C1_CLK_INPUT_PPS;
    SSP1DATPPS = I2C1_SDA_INPUT_PPS;
    RC3PPS = I2C1_CLK_OUTPUT_PPS;   //RC3??? PPS???? SCL1? output?? ??
    RC4PPS = I2C1_SDA_OUTPUT_PPS;   //RC4??? PPS???? SDA1? output?? ??
//PPS configuration end-----------------------

//I2C1 Pin configuration start----------------
    ANSELCbits.ANSC3 = 0U;  //set as Digital
    ANSELCbits.ANSC4 = 0U;  //set as Digital
    I2C1_SCL_TRIS = 1U; //set as input
    I2C1_SDA_TRIS = 1U; //set as input
    I2C1_SCL_LAT = 1U;  //set as high(3.3V)
    I2C1_SDA_LAT = 1U;  //set as high(3.3V)
//I2C1 Pin configuration end------------------

//?? ???? ? ??? start-------------------
    SSP1CON1bits.SSPEN = 0;     // ?? OFF
    SSP1BUF;                    // BF ???? ?? ??(?? ?)
    i2c_clear_interrupt_flag(); //MSSP1 interrupt flag to 0
//?? ???? ? ??? end---------------------
    
//SSP1 Register Configuration start-----------
    SSP1ADD = (uint8_t)(((_XTAL_FREQ / (4UL * I2C_DEFAULT_BAUDRATE_HZ)) - 1UL) & 0xFFUL);   //=79
    
    SSP1STAT = 0b10000000U; // Slew rate disabled
    SSP1CON1 = 0b00101000U; // ?? ???, ?? ??
    SSP1CON2 = 0b00000000U; // Start, stop, restart, receive ?? ????
    SSP1CON3 = 0b00000000U;
            
    
    SSP1STAT = 0x80U;
    SSP1CON1 = 0x28U;
    SSP1CON2 = 0x00U;
    SSP1CON3 = 0x00U;
//SSP1 Register Configuration end-------------
    
//PPS lock sequence start -------------------
    INTCONbits.GIE = 0; //Suspend interrupts
    PPSLOCK = 0x55; //Required sequence
    PPSLOCK = 0xAA; //Required sequence
    PPSLOCKbits.PPSLOCKED = 1; //Set PPSLOCKED bit
    INTCONbits.GIE = 1; //Restore interrupts
//PPS lock sequence end ----------------------

}



bool i2c_start(uint8_t address, i2c_transfer_direction_t direction)
{
    uint8_t control_byte = (uint8_t)((address << 1U) | (uint8_t)direction);

    i2c_idle_wait();
    SSP1CON2bits.SEN = 1U;
    while (SSP1CON2bits.SEN != 0U)
    {
        ;
    }
    i2c_clear_interrupt_flag(); //MSSP1 interrupt flag to 0

    if (!i2c_write(control_byte))
    {
        i2c_stop();
        return false;
    }

    return true;
}

bool i2c_restart(uint8_t address, i2c_transfer_direction_t direction)
{
    uint8_t control_byte = (uint8_t)((address << 1U) | (uint8_t)direction);

    i2c_idle_wait();
    SSP1CON2bits.RSEN = 1U;
    while (SSP1CON2bits.RSEN != 0U)
    {
        ;
    }
    i2c_clear_interrupt_flag(); //MSSP1 interrupt flag to 0

    if (!i2c_write(control_byte))
    {
        i2c_stop();
        return false;
    }

    return true;
}

bool i2c_write(uint8_t data)
{
    i2c_idle_wait();
    SSP1BUF = data;
    while (SSP1STATbits.BF != 0U)
    {
        ;
    }
    i2c_clear_interrupt_flag(); //MSSP1 interrupt flag to 0
    i2c_idle_wait();
    return (SSP1CON2bits.ACKSTAT == 0U);
}

uint8_t i2c_read(bool ack)
{
    uint8_t data;

    i2c_idle_wait();
    SSP1CON2bits.RCEN = 1U;
    while (SSP1STATbits.BF == 0U)
    {
        ;
    }
    i2c_clear_interrupt_flag(); //MSSP1 interrupt flag to 0
    data = SSP1BUF;
    i2c_idle_wait();

    SSP1CON2bits.ACKDT = (ack ? 0U : 1U);
    SSP1CON2bits.ACKEN = 1U;
    while (SSP1CON2bits.ACKEN != 0U)
    {
        ;
    }
    i2c_clear_interrupt_flag(); //MSSP1 interrupt flag to 0

    return data;
}

void i2c_stop(void)
{
    i2c_idle_wait();
    SSP1CON2bits.PEN = 1U;
    while (SSP1CON2bits.PEN != 0U)
    {
        ;
    }
    i2c_clear_interrupt_flag(); //MSSP1 interrupt flag to 0
}

bool i2c_write_register(uint8_t address, uint8_t reg, uint8_t value)
{
    if (!i2c_start(address, I2C_DIRECTION_WRITE))
    {
        return false;
    }

    if (!i2c_write(reg) || !i2c_write(value))
    {
        i2c_stop();
        return false;
    }

    i2c_stop();
    return true;
}

bool i2c_read_register(uint8_t address, uint8_t reg, uint8_t *value)
{
    if (value == NULL)
    {
        return false;
    }

    if (!i2c_start(address, I2C_DIRECTION_WRITE))
    {
        return false;
    }

    if (!i2c_write(reg))
    {
        i2c_stop();
        return false;
    }

    if (!i2c_restart(address, I2C_DIRECTION_READ))
    {
        i2c_stop();
        return false;
    }

    *value = i2c_read(false);
    i2c_stop();
    return true;
}

bool i2c_read_registers(uint8_t address, uint8_t reg, uint8_t *buffer, size_t length)
{
    size_t index;

    if (buffer == NULL || length == 0U)
    {
        return false;
    }

    if (!i2c_start(address, I2C_DIRECTION_WRITE))
    {
        return false;
    }

    if (!i2c_write(reg))
    {
        i2c_stop();
        return false;
    }

    if (!i2c_restart(address, I2C_DIRECTION_READ))
    {
        i2c_stop();
        return false;
    }

    for (index = 0U; index < length; ++index)
    {
        bool ack = (index + 1U) < length;
        buffer[index] = i2c_read(ack);
    }

    i2c_stop();
    return true;
}

