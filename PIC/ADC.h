/* 
 * File: ADC.h  
 * Author: Minwoo Kim
 * Comments: 
 * Revision history: 10.29.25
 */
#ifndef ADC_H
#define ADC_H

#include <xc.h>
#include <stdint.h>
#include <stdbool.h>
#include "lmp91000.h"

//Choose between FVR or VDD for Vref+
#ifndef ADC_USE_FVR_REF
#define ADC_USE_FVR_REF   1      // 1: FVR(2.048V) , 0: VDD 
#endif

#ifndef ADC_VDD_MV
#define ADC_VDD_MV        3300   // When use VDD (mV)
#endif

// For FVR use 2.048V(recommended)
#ifndef ADC_FVR_MV
#define ADC_FVR_MV        2048
#endif

// ?? ??(??) ?? ??(us). ?? ????? ?? ??.
#ifndef ADC_TACQ_US
#define ADC_TACQ_US       10
#endif

/* ---- LMP91000 ?? ?? ??? ----
 * (????? ??? LMP91000_CA_*_SETTINGS? ??)
 */
float    ADC_Get_LMP_RTIA_Ohms(void);
float    ADC_Get_LMP_Vzero_mV(void);
uint16_t ADC_Get_LMP_Vref_mV(void);


/* ---- ?? ?? ---- */
float    ADC_VoutmV_To_Current_uA(uint16_t vout_mV, float vzero_mV, float rtia_ohms);
float    ADC_CodeToCurrent_uA(uint16_t code);
float    ADC_Read_RC7_Current_uA(void);


// RC7 config + ADC setting
void ADC_Init_RC7(void);

// Read 1 sample of 10 bit code from RC7
uint16_t ADC_Read_RC7_Code(void);

// Read 1 sample of 10 bit code from RA1 (used for OCP)
uint16_t ADC_Read_RA1_Code(void);

// Convert ADC 10bit code --> mV (Automatically calculate based on Vref+ setting)
uint16_t ADC_CodeToMilliVolts(uint16_t code);

//Convert ADC mV to uA

float    ADC_VoutmV_To_Current_uA(uint16_t vout_mV, float vzero_mV, float rtia_ohms);

/* ?? ??: LMP ??? ???? ??(ADC.c? ??) */
float    ADC_LMP_Read_RC7_Current_uA(void);
#endif /* ADC_H */
