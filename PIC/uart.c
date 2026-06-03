/*
 * File:   uart.c
 * Author: MinwooKim
 *
 * Created on November 10, 2025, 9:59 AM
 */

#include "uart.h"

#include <xc.h>
#include "board.h" 
#include <pic16f15256.h>

//volatile variables for interrupt
volatile uint8_t g_uart_rx_byte = 0;
volatile bool    g_uart_rx_ready = false;

#ifndef UART_BAUDRATE
#define UART_BAUDRATE (115200UL)
#endif

#ifndef UART_RX_BUFFER_LENGTH
#define UART_RX_BUFFER_LENGTH (32U)
#endif

/* Provide device compatibility wrappers so the driver works with both the
 * legacy EUSART register names (TXSTA/RCSTA/BAUDCON/SPBRG) and the newer
 * numbered variants used on recent PIC16 devices. */

  #define UART_TXSTA   TX1STAbits
  #define UART_RCSTA   RC1STAbits
  #define UART_BAUDCON BAUD1CONbits
  #define UART_SPBRGL  SP1BRGL
  #define UART_SPBRGH  SP1BRGH
  #define UART_TXREG   TX1REG
  #define UART_RCREG   RC1REG


#define UART_TX_FLAG    PIR1bits.TX1IF
#define UART_RX_FLAG    PIR1bits.RC1IF


static uint8_t receive_buffer[UART_RX_BUFFER_LENGTH];
static uint8_t receive_index = 0U;


static uint16_t uart_calculate_brg_value(void)
{
    const uint32_t divisor = 4UL * UART_BAUDRATE;
    uint32_t value = (BOARD_SYSTEM_CLOCK_HZ + (divisor / 2UL)) / divisor;
    if (value > 0U)
    {
        value -= 1U;
    }
    return (uint16_t)value;
}

static void uart_handle_overrun(void)
{
    if (UART_RCSTA.OERR)
    {
        UART_RCSTA.CREN = 0U;
        UART_RCSTA.CREN = 1U;
        receive_index = 0U;
    }
}

void uart_init(void)
{
//PPS Unlock sequence start-------------------
    INTCONbits.GIE = 0; //Suspend interrupts
    PPSLOCK = 0x55; //Required sequence
    PPSLOCK = 0xAA; //Required sequence
    PPSLOCKbits.PPSLOCKED = 0; //Clear PPSLOCKED bit
    INTCONbits.GIE = 1; //Restore interrupts
//PPS Unlock sequence end---------------------
    
    RX1PPS = 0x15;   // RC5 -> RX1 (PORTC(010), PIN5(101) => 0x15)
    RC6PPS = 0x05;   // RC6 -> TX1 (TX1 function code = 0x05)
    
//PPS lock sequence start -------------------
    INTCONbits.GIE = 0; //Suspend interrupts
    PPSLOCK = 0x55; //Required sequence
    PPSLOCK = 0xAA; //Required sequence
    PPSLOCKbits.PPSLOCKED = 1; //Set PPSLOCKED bit
    INTCONbits.GIE = 1; //Restore interrupts
//PPS lock sequence end ----------------------

    TRISCbits.TRISC5 = 1U; /* C5 as input for RX */
    TRISCbits.TRISC6 = 0U; /* C6 as output for TX */
    ANSELCbits.ANSC5 = 0U;
    ANSELCbits.ANSC6 = 0U;


    uint16_t brg_value;                 // 
    brg_value = uart_calculate_brg_value();  //

    UART_TXSTA.SYNC = 0U;  /* Asynchronous mode */
    UART_TXSTA.BRGH = 1U;  /* High-speed baud */
    UART_BAUDCON.BRG16 = 1U; /* 16-bit BRG */

    UART_SPBRGH = (uint8_t)(brg_value >> 8);
    UART_SPBRGL = (uint8_t)(brg_value & 0xFFU);

    UART_RCSTA.SPEN = 1U;  /* Enable serial port */
    UART_RCSTA.CREN = 1U;  /* Enable continuous receive */
    UART_TXSTA.TXEN = 1U;  /* Enable transmitter */
    
    PIE1bits.RC1IE = 1;    // EUSART Receive Interrupt Enable

    receive_index = 0U;
}

bool uart_rx_available(void)
{
    uart_handle_overrun();
    return (UART_RX_FLAG != 0U);
}

char uart_read_byte(void)
{
    while (!uart_rx_available())
    {
        ;
    }
    return (char)UART_RCREG;
}

void uart_write_byte(char data)
{
    while (UART_TX_FLAG == 0U)
    {
        ;
    }
    UART_TXREG = (uint8_t)data;
}

void uart_write_string(const char *text)
{
    while ((text != NULL) && (*text != '\0'))
    {
        uart_write_byte(*text++);
    }
}

bool uart_try_read_line(char *buffer, uint8_t buffer_len)
{
    if ((buffer == NULL) || (buffer_len == 0U))
    {
        return false;
    }

    uart_handle_overrun();

    while (uart_rx_available())
    {
        char ch = (char)UART_RCREG;

        if (ch == '\r')
        {
            continue;
        }

        if (ch == '\n')
        {
            if (receive_index < buffer_len)
            {
                receive_buffer[receive_index] = '\0';
                for (uint8_t i = 0U; i <= receive_index; ++i)
                {
                    buffer[i] = (char)receive_buffer[i];
                }
                receive_index = 0U;
                return true;
            }
            receive_index = 0U;
            continue;
        }

        if (receive_index < (UART_RX_BUFFER_LENGTH - 1U))
        {
            receive_buffer[receive_index++] = (uint8_t)ch;
        }
        else
        {
            receive_index = 0U;
        }
    }

    (void)buffer;
    (void)buffer_len;
    return false;
}
//Read RC1REG directly
//bool uart_try_read_byte(uint8_t* out)
//{
//    // ?? ?? ?? ??(OERR)
//    if (UART_RCSTA.OERR) {
//        UART_RCSTA.CREN = 0;  // clear overrun by toggling CREN
//        UART_RCSTA.CREN = 1;
//    }
//    // ???? ??(FERR)? RC1REG ?? ??? ???
//    if (UART_RCSTA.FERR) {
//        volatile uint8_t dump = UART_RCREG;
//        (void)dump;
//        return false;
//    }
//    if (UART_RX_FLAG) {                 // PIR1bits.RC1IF
//        *out = UART_RCREG;              // RC1REG?? 1??? ??
//        return true;
//    }
//    return false;
//}

//Rx interrupt handler
void uart_rx_isr(void)
{
    uart_write_byte('0');
    uart_write_byte('\n');
    // Must read RCREG to clear the flag
    uint8_t c = RC1REG;

    // Overrun error clear
    uart_handle_overrun();

    g_uart_rx_byte  = c;
    g_uart_rx_ready = true;
}

//To use with the interrupt handler
bool uart_try_read_byte(uint8_t *b)
{

    if (g_uart_rx_ready)
    {
        g_uart_rx_ready = false;
        *b = g_uart_rx_byte;
        return true;
    }
    return false;
}
