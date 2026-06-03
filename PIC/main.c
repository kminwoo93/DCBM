/*
 * File:   main.c
 * Author: MinwooKim & Kiya (The Goat)
 *
 * @brief Demonstration application that toggles between channels on the ADG708.
 * 
 * Created on October 24, 2025, 5:08 PM Modified: 05/06/2026
 */
/* xc.h, stdint.h, stdio.h, stdbool.h, string.h are compiler/stdlib headers
 */

#include <xc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "board.h"
#include "i2c.h"
#include "lmp91000.h"
#include "mux.h"
#include "ADC.h"
#include "uart.h"
#include "measurement.h"
#include "motor.h"


/** Macros for the RC7 status indicator output. */
#define STATUS_LED_LAT   LATCbits.LATC7
#define STATUS_LED_TRIS  TRISCbits.TRISC7
#if defined(ANSELCbits)
#define STATUS_LED_ANSEL ANSELCbits.ANSC7
#endif

//Interrupt service define
void __interrupt() isr(void)
{
    // EUSART RX ????
    if (PIE1bits.RC1IE && PIR1bits.RC1IF)
    {
        uart_rx_isr();   // ?? ??? uart.c??
    }

    motor_isr();
}

static void status_led_init(void)
{
#if defined(STATUS_LED_ANSEL)
    STATUS_LED_ANSEL = 0U;
#endif

    STATUS_LED_LAT = 0U;
    STATUS_LED_TRIS = 0U;
}

static void status_led_set_high(void)
{
    STATUS_LED_LAT = 1U;
}

static void status_led_set_low(void)
{
    STATUS_LED_LAT = 0U;
}


void main(void) {
    //Initialization starts here 
    mux_init();
    mux_select(1U);
//    status_led_init();
    i2c_init();
    ADC_Init_RC7();
    uart_init();
    motor_init();
    INTCONbits.PEIE = 1;   // Peripheral interrupt enable
    INTCONbits.GIE  = 1;   // Global interrupt enable
    //Initialization ends here 
    
    while (1)
    {
        
        uint8_t b;
        if (uart_try_read_byte(&b))
        {
            uart_write_string("READ\n");
            switch(b)
            {
                case 0x11:
                    mux_select(1);
                    uart_write_string("Ch.1 selected\n");
                    lmp91000_start_ca_measurement();               
                    measurement_set_adc_source(MEAS_ADC_SOURCE_RC7);
                    uart_write_string("BEGIN\n");
                    run_measurement_loop(600, 100); //60 sec ADC on
                    break;
                case 0x12:
                    mux_select(2);
                    uart_write_string("Ch.2 selected\n");
                    //lmp91000_configure_ca_3lead();
                    lmp91000_start_ca_measurement();
                    measurement_set_adc_source(MEAS_ADC_SOURCE_RC7);
                    uart_write_string("BEGIN\n");
                    run_measurement_loop(600, 100); //60 sec ADC on
                    break;
                case 0x13:
                    mux_select(3);
                    uart_write_string("Ch.3 selected\n");
                    //lmp91000_configure_ca_3lead();
                    lmp91000_start_ocp_measurement();
                    measurement_set_adc_source(MEAS_ADC_SOURCE_RA1);
                    uart_write_string("BEGIN\n");
                    run_measurement_loop(600, 100); //60 sec ADC on
                    break;
                case 0x14:
                    mux_select(4);
                    uart_write_string("Ch.4 selected\n");
                    //lmp91000_configure_ca_3lead();
                    lmp91000_start_ca_measurement();
                    measurement_set_adc_source(MEAS_ADC_SOURCE_RC7);
                    uart_write_string("BEGIN\n");
                    run_measurement_loop(600, 100); //60 sec ADC on
                    break;
                case 0x15:
                    mux_select(5);
                    uart_write_string("Ch.5 selected\n");
                    //lmp91000_configure_ca_3lead();
                    lmp91000_start_ca_measurement();
                    measurement_set_adc_source(MEAS_ADC_SOURCE_RC7);
                    uart_write_string("BEGIN\n");
                    run_measurement_loop(600, 100); //60 sec ADC on
                    break;
                case 0x16:
                    mux_select(6);
                    uart_write_string("Ch.6 selected\n");
                    //lmp91000_configure_ca_3lead();
                    lmp91000_start_ca_measurement();
                    measurement_set_adc_source(MEAS_ADC_SOURCE_RC7);
                    uart_write_string("BEGIN\n");
                    run_measurement_loop(600, 100); //60 sec ADC on
                    break;
                case 0x17:
                    mux_select(7);
                    uart_write_string("Ch.7 selected\n");
                    //lmp91000_configure_ca_3lead();
                    lmp91000_start_ocp_measurement();
                    measurement_set_adc_source(MEAS_ADC_SOURCE_RA1);
                    uart_write_string("BEGIN\n");
                    run_measurement_loop(600, 100); //60 sec ADC on
                    break;
                case 0x18:
                    mux_select(8);
                    uart_write_string("Ch.8 selected\n");
                    //lmp91000_configure_ca_3lead();
                    lmp91000_start_ca_measurement();
                    measurement_set_adc_source(MEAS_ADC_SOURCE_RC7);
                    uart_write_string("BEGIN\n");
                    run_measurement_loop(600, 100); //60 sec ADC on
                    break;
                    
                case 0x00:
                    uart_write_string("case:0x00\n");
                    break;

                case 0x29: //Used to stop any step instantly 
                    g_stop_requested = true;
                    motor_stop(); //Motor stops 
                    lmp91000_stop_measurement(); //Data stops 
                    uart_write_string("EMERGENCY_STOP\n"); //Output 
                    break;
                case 0x20: //500uL/min used for flushing 
                    motor_start_for_flow_ul_min(500.0f);
                    uart_write_string("Motor start\n");
                    break;
                case 0x21: //Used to stop the flushing step (NOTHING ELSE)
                    motor_stop();
                    uart_write_string("Motor stop\n");
                    break;

                /* 
                 * Calibration Commands are listed below: 
                 * We have two sets (P and B) 
                 * CH1  Glucose  Set-P   RC7 ADC   CA measurement
                 * CH2  Lactate  Set-P   RC7 ADC   CA measurement
                 * CH3  pH       Set-P   RA1 ADC   OCP measurement
                 * CH5  Glucose  Set-B   RC7 ADC   CA measurement
                 * CH6  Lactate  Set-B   RC7 ADC   CA measurement
                 * CH7  pH       Set-B   RA1 ADC   OCP measurement
                 * Each step sends text labels:
                 * "STEP_START\n"  recording started 
                 * "CH1\n"         channel 1 started 
                 * <ADC data>      ADC data output 
                 * "END\n"         Channel has finished 
                 * "STEP_END\n"    All Channels have finished 
                 */ 

                case 0x24: //Used for PBS slow (20uL/min)
                    motor_start_for_flow_ul_min(20.0f);
                    uart_write_string("PBS slow started\n");
                    break; //End 

                case 0x25: //Used to record the PBS data (All 6 channels) -Step 2
                    motor_start_for_flow_ul_min(20.0f); //Starts motor to 20uL/min 
                    uart_write_string("PBS_BG_START\n");

                    mux_select(1); //Starts channel 1 reading (60s)
                    lmp91000_start_ca_measurement();
                    measurement_set_adc_source(MEAS_ADC_SOURCE_RC7);
                    uart_write_string("CH1\n");
                    run_measurement_loop(600, 100); //60s
                    if (g_stop_requested) { 
                        motor_stop(); 
                        break; 
                    }

                    mux_select(2); //Starts channel 2 reading (60s)
                    lmp91000_start_ca_measurement();
                    measurement_set_adc_source(MEAS_ADC_SOURCE_RC7);
                    uart_write_string("CH2\n");
                    run_measurement_loop(600, 100); //60s
                    if (g_stop_requested) { 
                        motor_stop(); 
                        break; 
                    }

                    mux_select(3); //Starts channel 3 reading (60s)
                    lmp91000_start_ocp_measurement();
                    measurement_set_adc_source(MEAS_ADC_SOURCE_RA1);
                    uart_write_string("CH3\n");
                    run_measurement_loop(600, 100); //60s
                    if (g_stop_requested) { 
                        motor_stop(); 
                        break; 
                    }

                    mux_select(5); //Starts channel 5 reading (60s)
                    lmp91000_start_ca_measurement();
                    measurement_set_adc_source(MEAS_ADC_SOURCE_RC7);
                    uart_write_string("CH5\n");
                    run_measurement_loop(600, 100); //60s
                    if (g_stop_requested) { 
                        motor_stop(); 
                        break; 
                    }

                    mux_select(6); //Starts channel 6 reading (60s)
                    lmp91000_start_ca_measurement();
                    measurement_set_adc_source(MEAS_ADC_SOURCE_RC7);
                    uart_write_string("CH6\n");
                    run_measurement_loop(600, 100); //60s
                    if (g_stop_requested) { 
                        motor_stop(); 
                        break; 
                    }

                    mux_select(7); //Starts channel 7 reading (60s)
                    lmp91000_start_ocp_measurement();
                    measurement_set_adc_source(MEAS_ADC_SOURCE_RA1);
                    uart_write_string("CH7\n");
                    run_measurement_loop(600, 100); //60s
                    if (g_stop_requested) { 
                        motor_stop(); 
                        break; 
                    }

                    motor_stop(); //End of pbs background 
                    uart_write_string("PBS_BG_END\n");
                    break;

                case 0x26: //Trial 1 (Step 3)
                    motor_start_for_flow_ul_min(20.0f); //20uL/min input 
                    uart_write_string("TRIAL1_START\n");

                    mux_select(1); //Measures Channel 1 
                    lmp91000_start_ca_measurement();
                    measurement_set_adc_source(MEAS_ADC_SOURCE_RC7);
                    uart_write_string("CH1\n");
                    run_measurement_loop(600, 100); //60s
                    if (g_stop_requested) { 
                        motor_stop(); 
                        break; 
                    }

                    mux_select(2); //Measures Channel 2
                    lmp91000_start_ca_measurement();
                    measurement_set_adc_source(MEAS_ADC_SOURCE_RC7);
                    uart_write_string("CH2\n");
                    run_measurement_loop(600, 100); //60s
                    if (g_stop_requested) { 
                        motor_stop(); 
                        break; 
                    }

                    mux_select(3); //Measures Channel 3
                    lmp91000_start_ocp_measurement();
                    measurement_set_adc_source(MEAS_ADC_SOURCE_RA1);
                    uart_write_string("CH3\n");
                    run_measurement_loop(600, 100); //60s
                    if (g_stop_requested) { 
                        motor_stop(); 
                        break; 
                    }

                    mux_select(5); //Measures Channel 5
                    lmp91000_start_ca_measurement();
                    measurement_set_adc_source(MEAS_ADC_SOURCE_RC7);
                    uart_write_string("CH5\n");
                    run_measurement_loop(600, 100); //60s
                    if (g_stop_requested) { 
                        motor_stop(); 
                        break; 
                    }

                    mux_select(6); //Measures Channel 6
                    lmp91000_start_ca_measurement();
                    measurement_set_adc_source(MEAS_ADC_SOURCE_RC7);
                    uart_write_string("CH6\n");
                    run_measurement_loop(600, 100); //60s
                    if (g_stop_requested) { 
                        motor_stop(); 
                        break; 
                    }

                    mux_select(7); //Measures Channel 7
                    lmp91000_start_ocp_measurement();
                    measurement_set_adc_source(MEAS_ADC_SOURCE_RA1);
                    uart_write_string("CH7\n");
                    run_measurement_loop(600, 100); //60s
                    if (g_stop_requested) { 
                        motor_stop(); 
                        break; 
                    }

                    motor_stop(); //End of trial 1
                    uart_write_string("TRIAL1_END\n");
                    break;

                case 0x27: //Trial 2 (Records all 6 channels )
                    motor_start_for_flow_ul_min(20.0f); //20uL/min input 
                    uart_write_string("TRIAL2_START\n");

                    mux_select(1); //Measures Channel 1
                    lmp91000_start_ca_measurement();
                    measurement_set_adc_source(MEAS_ADC_SOURCE_RC7);
                    uart_write_string("CH1\n");
                    run_measurement_loop(600, 100); //60s
                    if (g_stop_requested) { motor_stop(); break; }

                    mux_select(2); //Measures Channel 2
                    lmp91000_start_ca_measurement();
                    measurement_set_adc_source(MEAS_ADC_SOURCE_RC7);
                    uart_write_string("CH2\n");
                    run_measurement_loop(600, 100); //60s
                    if (g_stop_requested) { motor_stop(); break; }

                    mux_select(3); //Measures Channel 3
                    lmp91000_start_ocp_measurement();
                    measurement_set_adc_source(MEAS_ADC_SOURCE_RA1);
                    uart_write_string("CH3\n");
                    run_measurement_loop(600, 100); //60s
                    if (g_stop_requested) { motor_stop(); break; }

                    mux_select(5); //Measures Channel 5
                    lmp91000_start_ca_measurement();
                    measurement_set_adc_source(MEAS_ADC_SOURCE_RC7);
                    uart_write_string("CH5\n");
                    run_measurement_loop(600, 100); //60s
                    if (g_stop_requested) { motor_stop(); break; }

                    mux_select(6); //Measures Channel 6
                    lmp91000_start_ca_measurement();
                    measurement_set_adc_source(MEAS_ADC_SOURCE_RC7);
                    uart_write_string("CH6\n");
                    run_measurement_loop(600, 100); //60s
                    if (g_stop_requested) { motor_stop(); break; }

                    mux_select(7); //Measures Channel 7
                    lmp91000_start_ocp_measurement();
                    measurement_set_adc_source(MEAS_ADC_SOURCE_RA1);
                    uart_write_string("CH7\n");
                    run_measurement_loop(600, 100); //60s
                    if (g_stop_requested) { motor_stop(); break; }

                    motor_stop(); //End of Trial 2 
                    uart_write_string("TRIAL2_END\n");
                    break;

                case 0x28: //Trial 3 (Records all 6 channels)
                    motor_start_for_flow_ul_min(20.0f); //20uL/min input 
                    uart_write_string("TRIAL3_START\n");

                    mux_select(1); //Measures Channel 1
                    lmp91000_start_ca_measurement();
                    measurement_set_adc_source(MEAS_ADC_SOURCE_RC7);
                    uart_write_string("CH1\n");
                    run_measurement_loop(600, 100); //60s
                    if (g_stop_requested) { motor_stop(); break; }

                    mux_select(2); //Measures Channel 2
                    lmp91000_start_ca_measurement();
                    measurement_set_adc_source(MEAS_ADC_SOURCE_RC7);
                    uart_write_string("CH2\n");
                    run_measurement_loop(600, 100); //60s
                    if (g_stop_requested) { motor_stop(); break; }

                    mux_select(3); //Measures Channel 3
                    lmp91000_start_ocp_measurement();
                    measurement_set_adc_source(MEAS_ADC_SOURCE_RA1);
                    uart_write_string("CH3\n");
                    run_measurement_loop(600, 100); //60s
                    if (g_stop_requested) { motor_stop(); break; }

                    mux_select(5); //Measures Channel 5
                    lmp91000_start_ca_measurement();
                    measurement_set_adc_source(MEAS_ADC_SOURCE_RC7);
                    uart_write_string("CH5\n");
                    run_measurement_loop(600, 100); //60s
                    if (g_stop_requested) { motor_stop(); break; }

                    mux_select(6); //Measures Channel 6
                    lmp91000_start_ca_measurement();
                    measurement_set_adc_source(MEAS_ADC_SOURCE_RC7);
                    uart_write_string("CH6\n");
                    run_measurement_loop(600, 100); //60s
                    if (g_stop_requested) { motor_stop(); break; }

                    mux_select(7); //Measures Channel 7
                    lmp91000_start_ocp_measurement();
                    measurement_set_adc_source(MEAS_ADC_SOURCE_RA1);
                    uart_write_string("CH7\n");
                    run_measurement_loop(600, 100); //60s
                    if (g_stop_requested) { motor_stop(); break; }

                    motor_stop(); //End of trial 3
                    uart_write_string("TRIAL3_END\n");
                    break;

                case 0x30: //Actual Sample Measurement (b2 = Ss - a * Cs)
                    motor_start_for_flow_ul_min(20.0f);  //20uL/min input
                    uart_write_string("SAMPLE_START\n");

                    mux_select(1); //Measures Channel 1
                    lmp91000_start_ca_measurement();
                    measurement_set_adc_source(MEAS_ADC_SOURCE_RC7);
                    uart_write_string("CH1\n");
                    run_measurement_loop(600, 100); //60s
                    if (g_stop_requested) { motor_stop(); break; }

                    mux_select(2); //Measures Channel 2
                    lmp91000_start_ca_measurement();
                    measurement_set_adc_source(MEAS_ADC_SOURCE_RC7);
                    uart_write_string("CH2\n");
                    run_measurement_loop(600, 100); //60s
                    if (g_stop_requested) { motor_stop(); break; }

                    mux_select(3); //Measures Channel 3
                    lmp91000_start_ocp_measurement();
                    measurement_set_adc_source(MEAS_ADC_SOURCE_RA1);
                    uart_write_string("CH3\n");
                    run_measurement_loop(600, 100); //60s
                    if (g_stop_requested) { motor_stop(); break; }

                    mux_select(5); //Measures Channel 5
                    lmp91000_start_ca_measurement();
                    measurement_set_adc_source(MEAS_ADC_SOURCE_RC7);
                    uart_write_string("CH5\n");
                    run_measurement_loop(600, 100); //60s
                    if (g_stop_requested) { motor_stop(); break; }

                    mux_select(6); //Measures Channel 6
                    lmp91000_start_ca_measurement();
                    measurement_set_adc_source(MEAS_ADC_SOURCE_RC7);
                    uart_write_string("CH6\n");
                    run_measurement_loop(600, 100); //60s
                    if (g_stop_requested) { motor_stop(); break; }

                    mux_select(7); //Measures Channel 7
                    lmp91000_start_ocp_measurement();
                    measurement_set_adc_source(MEAS_ADC_SOURCE_RA1);
                    uart_write_string("CH7\n");
                    run_measurement_loop(600, 100); //60s
                    if (g_stop_requested) { motor_stop(); break; }

                    motor_stop(); //Ends of actual sample measurement  
                    uart_write_string("SAMPLE_END\n");
                    break;
            }
        }
    }
}