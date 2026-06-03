#ifndef CALIBRATION_H
#define CALIBRATION_H

#include <stdint.h>
#include <stdbool.h>

/* Config */

/* Number of calibration points */
#define CAL_NUM_POINTS       3U

/* Testing what? (Glucose/Lactate) */
#define CAL_NUM_ANALYTES     2U   /* 0 is for Glucose, 1 is for Lactate, 2 is for both*/

/* Each has two sensors */
#define CAL_NUM_REPLICATES   2U

/* ACD Readings per channel */
#define CAL_SAMPLES_PER_CH   10U

/* Delay between samples in ms (For ADC above) */
#define CAL_SAMPLE_DELAY_MS  100U

/* Motor Speed per minute */
#define CAL_FLOW_UL_MIN      100.0f

/* Total motor run time */
#define CAL_MOTOR_RUN_MS     60000UL

/* Delay between each sample */
#define CAL_SETTLE_MS        2000UL

/* Channels (6) */
#define CH_PBS_A        1U   /* Set on PBS, Set A */
#define CH_GLUCOSE_A    2U   /* Set on Glucose, Set A */
#define CH_LACTATE_A    3U   /* Set on Lactate, Set A */
#define CH_PBS_B        4U   /* Set on PBS, Set A */
#define CH_GLUCOSE_B    5U   /* Set on Glucose, Set A */
#define CH_LACTATE_B    6U   /* Set on Lactate, Set A */

/* Stores the raw ADC average and the converted current in nA */
typedef struct
{
    uint16_t adc_avg;       /* Average raw ADC code */
    int32_t  current_nA;    /* Converted current in nanoamps */
    uint16_t voltage_mV;    /* Converted voltage in millivolts */
    bool     valid;         /* If everything worked */
} cal_measurement_t;

/* Gets the PBS baseline for all 6 channels */
typedef struct
{
    cal_measurement_t ch[6];   
    bool complete;             /* Baseline stat */
} cal_baseline_t;

/* Makes a point for each measurment */
typedef struct
{
    float             known_conc_mM;        /* Concentration in mM that is known */
    cal_measurement_t replicate_a;          /* A channel reading */
    cal_measurement_t replicate_b;          /* B channel reading */
    float             avg_current_nA;       /* Average of A and B after PBS subtraction */
    bool              valid;                /* True when done */
} cal_point_t;

/*
 * Linear regression 
 * y = ax + b
 *   y = measured current (nA)
 *   x = concentration (mM)
 *   a = slope (sensitivity, nA/mM)
 *   b = intercept (offset, nA)
 */
typedef struct
{
    float slope_a;          
    float intercept_b;      
    float r_squared;        
    bool  valid;           
} cal_regression_t;

/* Calibration for one set */
typedef struct
{
    cal_point_t      points[CAL_NUM_POINTS];  /* Get all 3 points */
    cal_regression_t regression;              /* Compute */
    bool             calibrated;              /* Return true when regression is done */
} cal_analyte_t;

/* ACTUAL sample measurement */
typedef struct
{
    float             known_conc_mM;     /* Known concentration */
    cal_measurement_t replicate_a;       /* A measurement */
    cal_measurement_t replicate_b;       /* B measurement */
    float             measured_conc_mM;  /* Concentration from regression */
    float             b_correction;     /* Correction for b */
    bool              valid;
} cal_blood_sample_t;

/* Save everything */
typedef struct
{
    cal_baseline_t    baseline;                      
    cal_analyte_t     analyte[CAL_NUM_ANALYTES];     
    cal_blood_sample_t blood[CAL_NUM_ANALYTES];      
} cal_state_t;

#define CAL_ANALYTE_GLUCOSE  0U
#define CAL_ANALYTE_LACTATE  1U
void cal_init(cal_state_t *cal);
bool cal_measure_pbs_baseline(cal_state_t *cal);
bool cal_measure_cal_point(cal_state_t *cal, uint8_t point_index, float glucose_conc_mM, float lactate_conc_mM);
bool cal_compute_regression(cal_state_t *cal);
bool cal_measure_blood_sample(cal_state_t *cal, uint8_t analyte_index, float known_conc_mM);
float cal_calculate_concentration(const cal_state_t *cal, uint8_t analyte_index, float current_nA);
void cal_print_summary(const cal_state_t *cal);

#endif 