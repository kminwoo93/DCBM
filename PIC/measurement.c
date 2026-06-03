/*
 * File:   measurement.c
 * Author: Minwoo Kim & Kiya (The GOAT)
 */

#include "measurement.h"
#include "board.h"
#include "uart.h"
#include "ADC.h"
#include "lmp91000.h"
#include <stdio.h>
#include <stdbool.h>

static measurement_adc_source_t g_measurement_adc_source = MEAS_ADC_SOURCE_RC7;

/*
 * g_stop_requested ? NOT static so main.c can read it via extern.
 * Set to true when 0x00 arrives mid-measurement.
 * Cleared to false at the start of every new run_measurement_loop call.
 */
volatile bool g_stop_requested = false;

void measurement_set_adc_source(measurement_adc_source_t source)
{
    g_measurement_adc_source = source;
}

/*
 * delay_ms_interruptible
 *
 * Waits up to `ms` milliseconds but polls uart_try_read_byte every 1ms.
 * If 0x00 is received at any point, sets g_stop_requested = true and
 * returns immediately ? making the stop happen within 1ms of arriving.
 */
static void delay_ms_interruptible(uint16_t ms)
{
    uint8_t b = 0;
    for (uint16_t i = 0; i < ms; i++)
    {
        __delay_ms(1);

        if (uart_try_read_byte(&b) && b == 0x00)
        {
            g_stop_requested = true;
            return;
        }
    }
}

/*
 * run_measurement_loop
 *
 * Records TotalSamplingNumber ADC samples at sampling_rate ms intervals.
 * Streams each sample as "time_ms,ADC\n" over UART.
 *
 * Stop behaviour:
 *   - Checks g_stop_requested at the TOP of every loop iteration
 *   - Checks inside the delay every 1ms via delay_ms_interruptible
 *   - Returns immediately (no END sent) if stopped
 *   - Sends "END\n" only on normal completion
 */
void run_measurement_loop(uint16_t TotalSamplingNumber, uint16_t sampling_rate)
{
    uint8_t b = 0;

    /* Clear stop flag ? fresh start for each channel */
    g_stop_requested = false;

    for (uint16_t second = 0; second < TotalSamplingNumber; ++second)
    {
        /* Check for stop byte that arrived between iterations */
        if (uart_try_read_byte(&b) && b == 0x00)
            g_stop_requested = true;

        /* Exit immediately if stop was requested */
        if (g_stop_requested)
        {
            lmp91000_stop_measurement();
            uart_write_string("stopped\n");
            return;
        }

        /* Read ADC */
        uint32_t time_ms = (uint32_t)second * (uint32_t)sampling_rate;
        uint16_t ADCread = (g_measurement_adc_source == MEAS_ADC_SOURCE_RA1)
            ? ADC_Read_RA1_Code()
            : ADC_Read_RC7_Code();

        /* Stream sample over UART */
        char line[32];
        int n = snprintf(line, sizeof(line), "%lu,%u\n",
                         (unsigned long)time_ms,
                         (unsigned)ADCread);
        for (int i = 0; i < n; ++i)
            uart_write_byte((char)line[i]);

        /* Interruptible delay ? returns early on 0x00 */
        delay_ms_interruptible(sampling_rate);

        /* Check again after delay */
        if (g_stop_requested)
        {
            lmp91000_stop_measurement();
            uart_write_string("stopped\n");
            return;
        }
    }

    /* Normal completion */
    uart_write_string("END\n");
    lmp91000_stop_measurement();
}
