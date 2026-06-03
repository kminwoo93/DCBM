#include "i2c.h"
#include "lmp91000.h"
#include <xc.h>
#include "board.h"
/* TIACN: TIA ??/?? */

/* MODECN: 3-?? ?? + (??)FET ? */
#define LMP91000_CA_MODE_SETTINGS            (LMP91000_MODECN_3_LEAD)
#define LMP91000_CA_MODE_SETTINGS_WITH_FET   (LMP91000_MODECN_3_LEAD | \
                                              LMP91000_MODECN_FET_SHORT)


bool lmp91000_unlock(void)
{
    return i2c_write_register(LMP91000_I2C_ADDRESS, LMP91000_REG_LOCK, LMP91000_UNLOCKED);
}

bool lmp91000_lock(void)
{
    return i2c_write_register(LMP91000_I2C_ADDRESS, LMP91000_REG_LOCK, LMP91000_LOCKED);
}

bool lmp91000_write_register(uint8_t reg, uint8_t value)
{
    return i2c_write_register(LMP91000_I2C_ADDRESS, reg, value);
}

bool lmp91000_read_register(uint8_t reg, uint8_t *value)
{
    return i2c_read_register(LMP91000_I2C_ADDRESS, reg, value);
}

bool lmp91000_configure(uint8_t tia_control, uint8_t ref_control, uint8_t mode_control)
{
    if (!lmp91000_unlock())
    {
        return false;
    }

    if (!lmp91000_write_register(LMP91000_REG_TIACN, tia_control))
    {
        return false;
    }

    if (!lmp91000_write_register(LMP91000_REG_REFCN, ref_control))
    {
        return false;
    }

    if (!lmp91000_write_register(LMP91000_REG_MODECN, mode_control))
    {
        return false;
    }

    return lmp91000_lock();
}

bool lmp91000_read_status(uint8_t *status)
{
    return lmp91000_read_register(LMP91000_REG_STATUS, status);
}

bool lmp91000_configure_ca_3lead(void)
{
    if (!lmp91000_unlock())
    {
        return false;
    }

    if (!lmp91000_write_register(LMP91000_REG_TIACN, LMP91000_CA_TIA_SETTINGS))
    {
        return false;
    }

    if (!lmp91000_write_register(LMP91000_REG_REFCN, LMP91000_CA_REF_SETTINGS))
    {
        return false;
    }

    if (!lmp91000_write_register(LMP91000_REG_MODECN, LMP91000_MODECN_STANDBY))
    {
        return false;
    }

    return lmp91000_lock();
}

bool lmp91000_start_ca_measurement(void)
{
    if (!lmp91000_unlock())
    {
        return false;
    }
    __delay_ms(5);
    //TIA gain, TIA Rload setting
    if (!lmp91000_write_register(LMP91000_REG_TIACN, LMP91000_CA_TIA_SETTINGS))
    {
        return false;
    }
    __delay_ms(5);
    //Ref internal, Bias %, Polarity setting
    if (!lmp91000_write_register(LMP91000_REG_REFCN, LMP91000_CA_REF_SETTINGS))
    {
        return false;
    }
    __delay_ms(5);
    //Mode setting
    if (!lmp91000_write_register(LMP91000_REG_MODECN, LMP91000_MODECN_3_LEAD))
    {
        return false;
    }
    __delay_ms(5);

    return lmp91000_lock();
}
bool lmp91000_start_ocp_measurement(void)
{
    if (!lmp91000_unlock())
    {
        return false;
    }
    __delay_ms(5);
    //TIA gain, TIA Rload setting
    if (!lmp91000_write_register(LMP91000_REG_TIACN, LMP91000_CA_TIA_SETTINGS))
    {
        return false;
    }
    // Bias = 0% ? ??? "?????" ??? ??
    if (!lmp91000_write_register(LMP91000_REG_REFCN, LMP91000_OCP_REF_SETTINGS)) return false;
    __delay_ms(5);

    // Deep sleep (MODE=000) + FET_SHORT OFF(bit7=0)
    if (!lmp91000_write_register(LMP91000_REG_MODECN, LMP91000_OCP_MODE_SETTINGS)) return false;
    __delay_ms(5);

    return lmp91000_lock();
}
bool lmp91000_stop_measurement(void)
{
    if (!lmp91000_unlock())
    {
        return false;
    }

    if (!lmp91000_write_register(LMP91000_REG_MODECN, LMP91000_CA_MODE_SETTINGS_WITH_FET))
    {
        return false;
    }

    if (!lmp91000_write_register(LMP91000_REG_MODECN, LMP91000_MODECN_SLEEP))
    {
        return false;
    }

    return lmp91000_lock();
}