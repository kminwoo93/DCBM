#ifndef MEASUREMENT_H
#define MEASUREMENT_H

#include <stdint.h>
#include <stdbool.h>

typedef enum
{
    MEAS_ADC_SOURCE_RC7 = 0,
    MEAS_ADC_SOURCE_RA1 = 1
} measurement_adc_source_t;

/*
 * g_stop_requested ? global (NOT static) stop flag.
 * Set to true by delay_ms_interruptible when 0x00 arrives mid-measurement.
 * Also set by the 0x29 emergency stop handler in main.c.
 * main.c reads this after every run_measurement_loop call to immediately
 * stop the motor and skip remaining channels.
 *
 * MUST be declared without 'static' in measurement.c so main.c can see it
 * via this extern declaration.
 */
extern volatile bool g_stop_requested;

void measurement_set_adc_source(measurement_adc_source_t source);

/*
 * run_measurement_loop ? records ADC samples and streams them over UART.
 *
 * TotalSamplingNumber : number of samples to take
 * sampling_rate       : delay between samples in ms
 *
 * Checks g_stop_requested every 1ms inside the delay. Returns immediately
 * when a stop is detected. Sends "stopped\n" if stopped early, "END\n" if
 * completed normally.
 */
void run_measurement_loop(uint16_t TotalSamplingNumber, uint16_t sampling_rate);

#endif /* MEASUREMENT_H */
