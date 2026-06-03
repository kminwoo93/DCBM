
/* 
 * File: uart.h   
 * Author: Minwoo Kim
 * Comments:
 * Revision history: 
 */

#ifndef UART_H
#define UART_H

#include <xc.h>
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Initialise the hardware UART used for the ESP32 link.
 */
void uart_init(void);

/**
 * @brief Check if a character is waiting in the receive buffer.
 *
 * @return true when at least one character can be read with uart_read_byte().
 */
bool uart_rx_available(void);

/**
 * @brief Blocking read of a single received character.
 *
 * The function assumes uart_rx_available() returned true beforehand.
 */
char uart_read_byte(void);

/**
 * @brief Send a single byte out of the UART.
 */
void uart_write_byte(char data);

/**
 * @brief Send a NUL-terminated string out of the UART.
 */
void uart_write_string(const char *text);

/**
 * @brief Attempt to read a line (terminated by '\n') from the UART.
 *
 * Characters are accumulated until a line-feed is seen. Carriage returns are
 * discarded. When a full line is assembled the function copies the data to
 * @p buffer, appends a NUL terminator and returns true. The newline is not
 * stored in the buffer. If the line is longer than the provided buffer the
 * excess characters are discarded and the partial line is cleared.
 *
 * @param buffer       Destination buffer for the received line.
 * @param buffer_len   Size of @p buffer in bytes.
 *
 * @return true if a complete line was received and copied into @p buffer.
 */
bool uart_try_read_line(char *buffer, uint8_t buffer_len);

bool uart_try_read_byte(uint8_t *out);

// RX interrupt handler
void uart_rx_isr(void);

#endif /* UART_H */