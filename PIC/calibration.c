#include "calibration.h"
#include "motor.h"
#include "mux.h"
#include "ADC.h"
#include "lmp91000.h"
#include "measurement.h"
#include "uart.h"
#include <xc.h>
#include <string.h>
#include <stdio.h>

/* Runs teh motor (100uL) */
static void run_motor_dispense(void)
{
    motor_start_for_flow_ul_min(CAL_FLOW_UL_MIN);
    uint32_t elapsed = 0UL;
    while (elapsed < CAL_MOTOR_RUN_MS)
    {
        __delay_ms(1000);
        elapsed += 1000UL;
    }

    motor_stop();

    /* Slight delay */
    __delay_ms(CAL_SETTLE_MS);
}

/* Takes ADC readings */
static void measure_channel(uint8_t ch_num, bool is_ocp, cal_measurement_t *out)
{
    char buf[32];
    uint32_t adc_sum = 0UL;

    mux_select(ch_num);
    __delay_ms(10);

    if (is_ocp)
    {
        lmp91000_start_ocp_measurement();
        measurement_set_adc_source(MEAS_ADC_SOURCE_RA1);
    }
    else
    {
        lmp91000_start_ca_measurement();
        measurement_set_adc_source(MEAS_ADC_SOURCE_RC7);
    }

    /* Delay */
    __delay_ms(50);

    /* Take multiple samples and average */
    for (uint8_t i = 0U; i < CAL_SAMPLES_PER_CH; i++)
    {
        if (is_ocp)
        {
            adc_sum += ADC_Read_RA1_Code();
        }
        else
        {
            adc_sum += ADC_Read_RC7_Code();
        }
        __delay_ms(CAL_SAMPLE_DELAY_MS);
    }

    out->adc_avg   = (uint16_t)(adc_sum / CAL_SAMPLES_PER_CH);
    out->voltage_mV = ADC_CodeToMilliVolts(out->adc_avg);
    float i_uA     = ADC_CodeToCurrent_uA(out->adc_avg);
    out->current_nA = (int32_t)(i_uA * 1000.0f);
    out->valid      = true;

    sprintf(buf, "[CAL] Ch%u ADC=%u %umV %ldnA\n",
            (unsigned)ch_num,
            (unsigned)out->adc_avg,
            (unsigned)out->voltage_mV,
            (long)out->current_nA);
    uart_write_string(buf);
}

/* Linear Regression */
static void linear_regression(const float *x, const float *y, uint8_t n, float *a, float *b, float *r_squared)
{
    float sum_x  = 0.0f;
    float sum_y  = 0.0f;
    float sum_xy = 0.0f;
    float sum_xx = 0.0f;
    float sum_yy = 0.0f;

    for (uint8_t i = 0U; i < n; i++)
    {
        sum_x  += x[i];
        sum_y  += y[i];
        sum_xy += x[i] * y[i];
        sum_xx += x[i] * x[i];
        sum_yy += y[i] * y[i];
    }

    float fn    = (float)n;
    float denom = (fn * sum_xx) - (sum_x * sum_x);

    if (denom == 0.0f)
    {
        *a = 0.0f;
        *b = sum_y / fn;
        *r_squared = 0.0f;
        return;
    }

    *a = ((fn * sum_xy) - (sum_x * sum_y)) / denom;
    *b = (sum_y - (*a * sum_x)) / fn;

    float ss_res = 0.0f;
    float ss_tot = 0.0f;
    float y_mean = sum_y / fn;
    for (uint8_t i = 0U; i < n; i++)
    {
        float y_pred = (*a * x[i]) + *b;
        float res    = y[i] - y_pred;
        ss_res += res * res;
        float tot = y[i] - y_mean;
        ss_tot += tot * tot;
    }

    *r_squared = (ss_tot > 0.0f) ? (1.0f - (ss_res / ss_tot)) : 1.0f;
}

void cal_init(cal_state_t *cal)
{
    memset(cal, 0, sizeof(cal_state_t));
}

bool cal_measure_pbs_baseline(cal_state_t *cal)
{
    /* Dispense 100uL of PBS */
    run_motor_dispense();
    
    /*
     * Measure each channel:
     * Ch1 = PBS_A    (CA)
     * Ch2 = Glucose_A (CA)
     * Ch3 = Lactate_A (OCP)
     * Ch4 = PBS_B    (CA)
     * Ch5 = Glucose_B (CA)
     * Ch6 = Lactate_B (CA)
     */
    
    measure_channel(CH_PBS_A,     false, &cal->baseline.ch[0]);
    measure_channel(CH_GLUCOSE_A, false, &cal->baseline.ch[1]);
    measure_channel(CH_LACTATE_A, true,  &cal->baseline.ch[2]); 
    measure_channel(CH_PBS_B,     false, &cal->baseline.ch[3]);
    measure_channel(CH_GLUCOSE_B, false, &cal->baseline.ch[4]);
    measure_channel(CH_LACTATE_B, false, &cal->baseline.ch[5]);

    cal->baseline.complete = true;
    return true;
}

bool cal_measure_cal_point(cal_state_t *cal, uint8_t point_index, float glucose_conc_mM, float lactate_conc_mM)
{
    if (point_index >= CAL_NUM_POINTS)
    {
        return false;
    }

    if (!cal->baseline.complete)
    {
        return false;
    }

    run_motor_dispense();

    /* Measure Glucose channels */
    cal_point_t *glu = &cal->analyte[CAL_ANALYTE_GLUCOSE].points[point_index];
    glu->known_conc_mM = glucose_conc_mM;

    measure_channel(CH_GLUCOSE_A, false, &glu->replicate_a);
    measure_channel(CH_GLUCOSE_B, false, &glu->replicate_b);

    int32_t glu_a_net = glu->replicate_a.current_nA
                        - cal->baseline.ch[1].current_nA; 
    int32_t glu_b_net = glu->replicate_b.current_nA
                        - cal->baseline.ch[4].current_nA; 

    glu->avg_current_nA = (float)(glu_a_net + glu_b_net) / 2.0f;
    glu->valid = true;

    cal_point_t *lac = &cal->analyte[CAL_ANALYTE_LACTATE].points[point_index];
    lac->known_conc_mM = lactate_conc_mM;

    measure_channel(CH_LACTATE_A, true,  &lac->replicate_a); /* OCP */
    measure_channel(CH_LACTATE_B, false, &lac->replicate_b); /* CA */

    int32_t lac_a_net = lac->replicate_a.current_nA
                        - cal->baseline.ch[2].current_nA; /* Ch3 baseline */
    int32_t lac_b_net = lac->replicate_b.current_nA
                        - cal->baseline.ch[5].current_nA; /* Ch6 baseline */

    lac->avg_current_nA = (float)(lac_a_net + lac_b_net) / 2.0f;
    lac->valid = true;

    return true;
}

bool cal_compute_regression(cal_state_t *cal)
{

    for (uint8_t a = 0U; a < CAL_NUM_ANALYTES; a++)
    {
        /* Check all 3 calibration points are valid */
        for (uint8_t p = 0U; p < CAL_NUM_POINTS; p++)
        {
            if (!cal->analyte[a].points[p].valid)
            {
                return false;
            }
        }

        float x[CAL_NUM_POINTS];
        float y[CAL_NUM_POINTS];

        for (uint8_t p = 0U; p < CAL_NUM_POINTS; p++)
        {
            x[p] = cal->analyte[a].points[p].known_conc_mM;
            y[p] = cal->analyte[a].points[p].avg_current_nA;
        }

        float slope, intercept, r2;
        linear_regression(x, y, CAL_NUM_POINTS, &slope, &intercept, &r2);

        cal->analyte[a].regression.slope_a     = slope;
        cal->analyte[a].regression.intercept_b = intercept;
        cal->analyte[a].regression.r_squared   = r2;
        cal->analyte[a].regression.valid       = true;
        cal->analyte[a].calibrated             = true;

        const char *name = (a == CAL_ANALYTE_GLUCOSE) ? "Glucose" : "Lactate";
    }

    return true;
}

bool cal_measure_blood_sample(cal_state_t *cal,
                              uint8_t      analyte_index,
                              float        known_conc_mM)
{
    if (analyte_index >= CAL_NUM_ANALYTES)
    {
        return false;
    }

    if (!cal->analyte[analyte_index].calibrated)
    {
        return false;
    }

    const char *name = (analyte_index == CAL_ANALYTE_GLUCOSE) ? "Glucose" : "Lactate";

    run_motor_dispense();

    cal_blood_sample_t *blood = &cal->blood[analyte_index];
    blood->known_conc_mM = known_conc_mM;

    /* Measure the appropriate channels */
    if (analyte_index == CAL_ANALYTE_GLUCOSE)
    {
        measure_channel(CH_GLUCOSE_A, false, &blood->replicate_a);
        measure_channel(CH_GLUCOSE_B, false, &blood->replicate_b);
    }
    else
    {
        measure_channel(CH_LACTATE_A, true,  &blood->replicate_a);
        measure_channel(CH_LACTATE_B, false, &blood->replicate_b);
    }

    /* Average the two replicates */
    float avg_current = (float)(blood->replicate_a.current_nA
                               + blood->replicate_b.current_nA) / 2.0f;

    /* Subtract PBS baseline */
    float baseline_current;
    if (analyte_index == CAL_ANALYTE_GLUCOSE)
    {
        baseline_current = (float)(cal->baseline.ch[1].current_nA
                                  + cal->baseline.ch[4].current_nA) / 2.0f;
    }
    else
    {
        baseline_current = (float)(cal->baseline.ch[2].current_nA
                                  + cal->baseline.ch[5].current_nA) / 2.0f;
    }
    float net_current = avg_current - baseline_current;

    /*
     * Calculate what the regression predicts for this current:
     * x = (y - b) / a
     */
    float a = cal->analyte[analyte_index].regression.slope_a;
    float b = cal->analyte[analyte_index].regression.intercept_b;

    blood->measured_conc_mM = (a != 0.0f) ? ((net_current - b) / a) : 0.0f;

    float b_corrected = net_current - (a * known_conc_mM);
    blood->b_correction = b_corrected - b;


    cal->analyte[analyte_index].regression.intercept_b = b_corrected;

    blood->valid = true;
    return true;
}

float cal_calculate_concentration(const cal_state_t *cal,
                                  uint8_t            analyte_index,
                                  float              current_nA)
{
    if (analyte_index >= CAL_NUM_ANALYTES)       return -1.0f;
    if (!cal->analyte[analyte_index].calibrated) return -1.0f;

    float a = cal->analyte[analyte_index].regression.slope_a;
    float b = cal->analyte[analyte_index].regression.intercept_b;

    if (a == 0.0f) return -1.0f;

    /* x = (y - b) / a */
    return (current_nA - b) / a;
}

void cal_print_summary(const cal_state_t *cal)
{

    /* PBS baseline */
    for (uint8_t i = 0U; i < 6U; i++)
    {
        sprintf(buf, "  Ch%u: %ldnA %umV\n",
                (unsigned)(i + 1U),
                (long)cal->baseline.ch[i].current_nA,
                (unsigned)cal->baseline.ch[i].voltage_mV);
    }

    /* Analytes */
    const char *names[CAL_NUM_ANALYTES] = {"Glucose", "Lactate"};
    for (uint8_t a = 0U; a < CAL_NUM_ANALYTES; a++)
    {
        for (uint8_t p = 0U; p < CAL_NUM_POINTS; p++)
        {
            sprintf(buf, "  Cal%u: conc=",  (unsigned)(p + 1U));
        }
        if (cal->analyte[a].regression.valid)
        {
        }
    }

}