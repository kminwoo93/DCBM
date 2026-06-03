#include "motor.h"

#include "xc.h"
#include "stdint.h"
#include "stdbool.h"
#include "board.h"

#define MOTOR_FULL_STEPS_PER_REV (200.0f)
/* TMC2209 standalone pin map (MS1/MS2): 00->8, 01->32, 10->64, 11->16 microsteps. */
#define MOTOR_MICROSTEP          (64.0f)
#define PUMP_UL_PER_REV          (52.5f)

#define MOTOR_MIN_STEP_HZ        (0.05f)
#define MOTOR_MAX_STEP_HZ        (2000.0f)
#define MOTOR_DEFAULT_FORWARD    (true)

#define MOTOR_DIR_LAT        LATCbits.LATC0
#define MOTOR_DIR_TRIS       TRISCbits.TRISC0
#define MOTOR_STEP_LAT       LATCbits.LATC1
#define MOTOR_STEP_TRIS      TRISCbits.TRISC1
#define MOTOR_DIR_ANSEL      ANSELCbits.ANSC0
#define MOTOR_STEP_ANSEL     ANSELCbits.ANSC1

static volatile bool g_motor_running = false;
static volatile bool g_step_pin_state = false;
static volatile uint16_t g_tmr1_reload = 0U;

static uint16_t motor_timer1_reload_from_interrupt_hz(float interrupt_hz)
{
    float tick_hz = ((float)BOARD_SYSTEM_CLOCK_HZ / 4.0f) / 8.0f;

    if (interrupt_hz < 0.1f)
    {
        interrupt_hz = 0.1f;
    }
    if (interrupt_hz > (tick_hz / 2.0f))
    {
        interrupt_hz = tick_hz / 2.0f;
    }

    float counts_f = tick_hz / interrupt_hz;

    if (counts_f < 1.0f)
    {
        counts_f = 1.0f;
    }
    if (counts_f > 65535.0f)
    {
        counts_f = 65535.0f;
    }

    return (uint16_t)(65536UL - (uint32_t)(counts_f + 0.5f));
}

void motor_init(void)
{
    MOTOR_DIR_ANSEL = 0U;
    MOTOR_STEP_ANSEL = 0U;

    MOTOR_DIR_LAT = 0U;
    MOTOR_STEP_LAT = 0U;

    MOTOR_DIR_TRIS = 0U;
    MOTOR_STEP_TRIS = 0U;

    motor_set_direction(MOTOR_DEFAULT_FORWARD);

    T1CLKbits.CS = 0b0001;

    T1CONbits.CKPS = 0b11;
    T1CONbits.nSYNC = 1U;
    T1CONbits.ON = 0U;

    g_tmr1_reload = motor_timer1_reload_from_interrupt_hz(2.0f);
    TMR1H = (uint8_t)(g_tmr1_reload >> 8);
    TMR1L = (uint8_t)(g_tmr1_reload & 0xFFU);

    PIR1bits.TMR1IF = 0U;
    PIE1bits.TMR1IE = 1U;

    T1CONbits.ON = 1U;
}

void motor_isr(void)
{
    if ((PIE1bits.TMR1IE != 0U) && (PIR1bits.TMR1IF != 0U))
    {
        PIR1bits.TMR1IF = 0U;

        TMR1H = (uint8_t)(g_tmr1_reload >> 8);
        TMR1L = (uint8_t)(g_tmr1_reload & 0xFFU);

        if (g_motor_running)
        {
            g_step_pin_state = !g_step_pin_state;
            MOTOR_STEP_LAT = (g_step_pin_state ? 1U : 0U);
        }
        else
        {
            g_step_pin_state = false;
            MOTOR_STEP_LAT = 0U;
        }
    }
}

void motor_set_direction(bool forward)
{
    MOTOR_DIR_LAT = (forward ? 1U : 0U);
}

void motor_start_with_step_hz(float step_hz)
{
    if (step_hz < MOTOR_MIN_STEP_HZ)
    {
        step_hz = MOTOR_MIN_STEP_HZ;
    }
    if (step_hz > MOTOR_MAX_STEP_HZ)
    {
        step_hz = MOTOR_MAX_STEP_HZ;
    }

    float interrupt_hz = step_hz * 2.0f;
    uint16_t reload = motor_timer1_reload_from_interrupt_hz(interrupt_hz);

    INTCONbits.GIE = 0U;
    g_tmr1_reload = reload;
    TMR1H = (uint8_t)(reload >> 8);
    TMR1L = (uint8_t)(reload & 0xFFU);
    g_step_pin_state = false;
    MOTOR_STEP_LAT = 0U;
    g_motor_running = true;
    INTCONbits.GIE = 1U;
}

void motor_start_for_flow_ul_min(float flow_ul_min)
{
    float motor_steps_per_rev_effective = MOTOR_FULL_STEPS_PER_REV * MOTOR_MICROSTEP;

    if (flow_ul_min < 0.0f)
    {
        flow_ul_min = 0.0f;
    }

    float rev_per_min = flow_ul_min / PUMP_UL_PER_REV;
    float steps_per_min = rev_per_min * motor_steps_per_rev_effective;
    float step_frequency_hz = steps_per_min / 60.0f;

    motor_start_with_step_hz(step_frequency_hz);
}

void motor_stop(void)
{
    INTCONbits.GIE = 0U;
    g_motor_running = false;
    g_step_pin_state = false;
    MOTOR_STEP_LAT = 0U;
    INTCONbits.GIE = 1U;
}