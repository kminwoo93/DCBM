/*
 * File:   ADC.c
 * Author: MinwooKim
 *
 * Created on October 29, 2025, 11:11 AM
 */


#include "xc.h"
#include "ADC.h"
#include "board.h"
#include "lmp91000.h"

/* ---- TIACN/REFCN ?? ??? ---- */
#define LMP91000_TIACN_TIA_GAIN_MASK   (0x1Cu)   /* [4:2] */
#define LMP91000_TIACN_RLOAD_MASK      (0x03u)   /* [1:0] */
#define LMP91000_REFCN_IZ_MASK         (0x60u)   /* [6:5] */
#define LMP91000_REFCN_BIAS_POL_MASK   (0x10u)   /* [4] */
#define LMP91000_REFCN_BIAS_MAG_MASK   (0x0Fu)   /* [3:0] */
#define LMP91000_REFCN_REF_SRC_MASK    (0x80u)   /* [7] */
/* ---- ?? ?? ?? ???? ----
 * board.h? VDD(mV)? ???? ??? ?? ???.
 * ?) #define BOARD_VDD_MV 3300
 */
#ifndef BOARD_VDD_MV
#define BOARD_VDD_MV 3300
#endif

/* ====== LMP ?? ?? (????? ??? ??) ====== */
float ADC_Get_LMP_RTIA_Ohms(void)
{
    uint8_t g = (uint8_t)(LMP91000_CA_TIA_SETTINGS & LMP91000_TIACN_TIA_GAIN_MASK);
    switch (g) {
        case LMP91000_TIACN_TIA_GAIN_EXT:  return 0.0f;       /* ?? RTIA: ? ?? */
        case LMP91000_TIACN_TIA_GAIN_2K75: return 2750.0f;
        case LMP91000_TIACN_TIA_GAIN_3K5:  return 3500.0f;
        case LMP91000_TIACN_TIA_GAIN_7K:   return 7000.0f;
        case LMP91000_TIACN_TIA_GAIN_14K:  return 14000.0f;
        case LMP91000_TIACN_TIA_GAIN_35K:  return 35000.0f;
        case LMP91000_TIACN_TIA_GAIN_120K: return 120000.0f;
        case LMP91000_TIACN_TIA_GAIN_350K: return 350000.0f;
        default: return 35000.0f;
    }
}

uint16_t ADC_Get_LMP_Vref_mV(void)
{
    /* REFCN bit7: 1=EXT, 0=INT */
    bool ext = ((LMP91000_CA_REF_SETTINGS & LMP91000_REFCN_REF_SRC_MASK) != 0);
    if (ext) {
        /* ?? ?? ??: ??? ?? ?? */
        return 2500; /* ?: 2.500V */
    } else {
        /* ?? ??: LMP ?? ??? VDD?? ? ?? VDD? ?? */
        return (uint16_t)BOARD_VDD_MV;
    }
}

static float LMP_GetIZ_Fraction(void)
{
    uint8_t iz = (uint8_t)(LMP91000_CA_REF_SETTINGS & LMP91000_REFCN_IZ_MASK);
    switch (iz) {
        case LMP91000_REFCN_INT_Z_20_PCT: return 0.20f;
        case LMP91000_REFCN_INT_Z_50_PCT: return 0.50f;
        case LMP91000_REFCN_INT_Z_67_PCT: return 0.67f;
        default: /* BYPASS ? */          return 0.50f;
    }
}

float ADC_Get_LMP_Vzero_mV(void)
{
    float iz = LMP_GetIZ_Fraction();
    float vref_mV = (float)ADC_Get_LMP_Vref_mV();
    return vref_mV * iz; /* mV */
}


/* ---- ?? ??: ? ?? µA ?? ---- */
float ADC_LMP_Read_RC7_Current_uA(void)
{
    uint16_t code    = ADC_Read_RC7_Code();
    uint16_t vout_mV = ADC_CodeToMilliVolts(code);

    float iz        = LMP_GetIZ_Fraction();
    float vref_mV   = (float)ADC_Get_LMP_Vref_mV();
    float vzero_mV  = vref_mV * iz;
    float rtia_ohms = ADC_Get_LMP_RTIA_Ohms();

    return ADC_VoutmV_To_Current_uA(vout_mV, vzero_mV, rtia_ohms);
}

static inline void adc_acquire_delay_us(uint16_t us)
{
    while (us--) { __delay_us(1); }
}

void ADC_Init_RC7(void)
{
    /* RC7 pin to analog input */
    TRISCbits.TRISC7 = 1;         // input
    ANSELCbits.ANSC7 = 1;         // analog
    
#if ADC_USE_FVR_REF
    /* Use FVR 2.048V  */
    FVRCONbits.FVREN = 1;
    FVRCONbits.ADFVR = 0b11;      // 2x = 4.096V
    while (!FVRCONbits.FVRRDY) { ; }
#endif
    
    /* ADC Configuration */
    // Result format: Right, CLK: ADCRC(internal), (+)reference: VDD or FVR
    ADCON1=0x00U;
    ADCON1bits.FM = 1; //Right-justified
    ADCON1bits.CS = 0b111;  //ADCRC
#if ADC_USE_FVR_REF
    ADCON1bits.PREF = 0b11; // VREF+ = FVR
#else
    ADCON1bits.PREF = 0b00; // VREF+ = VDD
#endif
    //Channel select : RC7
    ADCON0 = 0x00;
    ADCON0bits.CHS = 0b010111;  //CHS = RC7
    ADCON0bits.ON=1;
}

uint16_t ADC_Read_RC7_Code(void)
{
    /* Re-select channel every time (guards against accidental CHS changes elsewhere) */
    ADCON0bits.CHS = 0b010111;  // RC7

    /* Acquisition: give the S/H cap enough time to settle.
     * NOTE: LMP91000 VOUT can be relatively high impedance, so use a longer TACQ. */
    adc_acquire_delay_us(20);

    /* Dummy conversion (recommended after channel change / first sample) */
    ADCON0bits.GO = 1;
    while (ADCON0bits.GO) { ; }
    (void)((((uint16_t)ADRESH) << 8) | ADRESL);

    /* Real conversion */
    adc_acquire_delay_us(20);
    ADCON0bits.GO = 1;
    while (ADCON0bits.GO) { ; }

    return ((((uint16_t)ADRESH) << 8) | ADRESL); // 10-bit (Right-justified)
}

uint16_t ADC_Read_RA1_Code(void)
{
    /* Ensure RA1 is configured as analog input for OCP path */
    TRISAbits.TRISA1 = 1;
    ANSELAbits.ANSA1 = 1;

    /* Re-select channel every time (guards against accidental CHS changes elsewhere) */
    ADCON0bits.CHS = 0b000001;  // RA1

    /* Acquisition: give the S/H cap enough time to settle. */
    adc_acquire_delay_us(20);

    /* Dummy conversion (recommended after channel change / first sample) */
    ADCON0bits.GO = 1;
    while (ADCON0bits.GO) { ; }
    (void)((((uint16_t)ADRESH) << 8) | ADRESL);

    /* Real conversion */
    adc_acquire_delay_us(20);
    ADCON0bits.GO = 1;
    while (ADCON0bits.GO) { ; }

    return ((((uint16_t)ADRESH) << 8) | ADRESL); // 10-bit (Right-justified)
}



uint16_t ADC_CodeToMilliVolts(uint16_t code)
{
#if ADC_USE_FVR_REF
    const uint32_t vref_mV = (uint32_t)ADC_FVR_MV;
#else
    const uint32_t vref_mV = (uint32_t)ADC_VDD_MV;
#endif
    // 10-bit: code/1023 * Vref (???)
    return (uint16_t)((code * vref_mV + 511U) / 1023U);
}
//* ====== ?? ?? ====== */
float ADC_VoutmV_To_Current_uA(uint16_t vout_mV, float vzero_mV, float rtia_ohms)
{
    /* I(µA) = (Vout - Vzero)/RTIA × 1e6 (?, V? volt ??)
       vout_mV? vzero_mV? mV??? 1000?? ?? volt? ?? */
    float dV_mV = (float)vout_mV - vzero_mV;
    return (dV_mV / 1000.0f) / rtia_ohms * 1.0e6f;
}

float ADC_CodeToCurrent_uA(uint16_t code)
{
    const float vref_mV   = 2048.0f;     // FVR ???? (mV)
    const float vzero_mV  = 1650.0f;     // LMP91000 INT_Z=50% ?? (mV)
    const float rtia_ohms = 350000.0f;   // 350 k?

    // 1) ADC ?? ? ?? ??(mV)
    float vout_mV = ((float)code * vref_mV + 511.0f) / 1023.0f;

    // 2) WE ?? (µA) = (Vout - Vzero) / RTIA × 1e3
    float iwe_uA = (vout_mV - vzero_mV) / rtia_ohms * 1000.0f;

    return iwe_uA;
}

float ADC_Read_RC7_Current_uA(void)
{
    uint16_t code = ADC_Read_RC7_Code();
    return ADC_CodeToCurrent_uA(code);
}