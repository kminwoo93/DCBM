#ifndef LMP91000_H
#define LMP91000_H

#include <stdbool.h>
#include <stdint.h>

#define LMP91000_I2C_ADDRESS  (0x48U)

#define LMP91000_REG_STATUS   (0x00U)
#define LMP91000_REG_LOCK     (0x01U)
#define LMP91000_REG_TIACN    (0x10U)
#define LMP91000_REG_REFCN    (0x11U)
#define LMP91000_REG_MODECN   (0x12U)

/* TIACN register field definitions */
#define LMP91000_TIACN_TIA_GAIN_EXT   (0x00U)  // 000 (external)
#define LMP91000_TIACN_TIA_GAIN_2K75  (0x04U)  // 001
#define LMP91000_TIACN_TIA_GAIN_3K5   (0x08U)  // 010
#define LMP91000_TIACN_TIA_GAIN_7K    (0x0CU)  // 011
#define LMP91000_TIACN_TIA_GAIN_14K   (0x10U)  // 100
#define LMP91000_TIACN_TIA_GAIN_35K   (0x14U)  // 101
#define LMP91000_TIACN_TIA_GAIN_120K  (0x18U)  // 110
#define LMP91000_TIACN_TIA_GAIN_350K  (0x1CU)  // 111

#define LMP91000_TIACN_RLOAD_10_OHM   (0x00U)  // 00
#define LMP91000_TIACN_RLOAD_33_OHM   (0x01U)  // 01
#define LMP91000_TIACN_RLOAD_50_OHM   (0x02U)  // 10
#define LMP91000_TIACN_RLOAD_100_OHM  (0x03U)  // 11

/* REFCN register field definitions */
#define LMP91000_REFCN_REF_SOURCE_INT   (0x00U)  // bit7=0 (00000000)
#define LMP91000_REFCN_REF_SOURCE_EXT   (0x80U)  // bit7=1 (10000000)

#define LMP91000_REFCN_INT_Z_20_PCT     (0x00U)  // [6:5]=00 (00000000)
#define LMP91000_REFCN_INT_Z_50_PCT     (0x20U)  // [6:5]=01 (00100000)
#define LMP91000_REFCN_INT_Z_67_PCT     (0x40U)  // [6:5]=10 (01000000)
#define LMP91000_REFCN_INT_Z_BYPASS     (0x60U)  // [6:5]=11 (01100000)

#define LMP91000_REFCN_BIAS_POL_NEG     (0x00U)  // bit4=0 (00000000)
#define LMP91000_REFCN_BIAS_POL_POS     (0x10U)  // bit4=1 (00010000)

/* Valid BIAS codes ([3:0]) */
#define LMP91000_REFCN_BIAS_0_PCT       (0x00U) // 00000000
#define LMP91000_REFCN_BIAS_1_PCT       (0x01U) // 00000001
#define LMP91000_REFCN_BIAS_2_PCT       (0x02U) // 00000010
#define LMP91000_REFCN_BIAS_4_PCT       (0x03U)
#define LMP91000_REFCN_BIAS_6_PCT       (0x04U)
#define LMP91000_REFCN_BIAS_8_PCT       (0x05U)
#define LMP91000_REFCN_BIAS_10_PCT      (0x06U)
#define LMP91000_REFCN_BIAS_12_PCT      (0x07U)
#define LMP91000_REFCN_BIAS_14_PCT      (0x08U)
#define LMP91000_REFCN_BIAS_16_PCT      (0x09U)
#define LMP91000_REFCN_BIAS_18_PCT      (0x0AU)
#define LMP91000_REFCN_BIAS_20_PCT      (0x0BU)
#define LMP91000_REFCN_BIAS_22_PCT      (0x0CU)
#define LMP91000_REFCN_BIAS_24_PCT      (0x0DU) // (00001101)

/* MODECN register field definitions */
#define LMP91000_MODECN_SLEEP             (0x00U)
#define LMP91000_MODECN_2_LEAD            (0x01U)
#define LMP91000_MODECN_STANDBY           (0x02U)
#define LMP91000_MODECN_3_LEAD            (0x03U)
#define LMP91000_MODECN_TEMP_MEAS_TIA_OFF (0x06U)
#define LMP91000_MODECN_TEMP_MEAS_TIA_ON  (0x07U)

/* FET short is in MODECN bit7 */
#define LMP91000_MODECN_FET_SHORT         (0x80U)

#define LMP91000_LOCKED       (0x01U)
#define LMP91000_UNLOCKED     (0x00U)

#define LMP91000_CA_TIA_SETTINGS   (LMP91000_TIACN_TIA_GAIN_120K | LMP91000_TIACN_RLOAD_100_OHM)

#define LMP91000_CA_REF_SETTINGS   (LMP91000_REFCN_REF_SOURCE_INT |\
                                   LMP91000_REFCN_INT_Z_67_PCT   |\
                                   LMP91000_REFCN_BIAS_POL_NEG   |\
                                   LMP91000_REFCN_BIAS_2_PCT)
//0 -> 690, 2-> 688, 4-> 680~688, 8-> 675~687

// "quasi-OCP"?: ???? 0%, internal zero? ??? ??(67%) ??
#define LMP91000_OCP_REF_SETTINGS   (LMP91000_REFCN_REF_SOURCE_INT | \
                                    LMP91000_REFCN_INT_Z_67_PCT   | \
                                    LMP91000_REFCN_BIAS_POL_NEG   | \
                                    LMP91000_REFCN_BIAS_0_PCT)

// OCP ?? ??? TIA? ?? Standby? ?? (datasheet: Standby = TIA OFF, A1 ON) :contentReference[oaicite:0]{index=0}
#define LMP91000_OCP_MODE_SETTINGS  (LMP91000_MODECN_STANDBY)


bool lmp91000_unlock(void);
 
bool lmp91000_lock(void);

bool lmp91000_write_register(uint8_t reg, uint8_t value);

bool lmp91000_read_register(uint8_t reg, uint8_t *value);

bool lmp91000_configure(uint8_t tia_control, uint8_t ref_control, uint8_t mode_control);

bool lmp91000_read_status(uint8_t *status);

bool lmp91000_configure_ca_3lead(void);

bool lmp91000_start_ca_measurement(void);

bool lmp91000_start_ocp_measurement(void);

bool lmp91000_stop_measurement(void);

#endif /* LMP91000_H */