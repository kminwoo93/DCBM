/*
 * File:   i2c.h
 * Author: MinwooKim
 *
 * Created on October 24, 2025, 11:53 AM
 */

#ifndef I2C_H
#define I2C_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Default I2C clock speed in Hertz. */
#ifndef I2C_DEFAULT_BAUDRATE_HZ
#define I2C_DEFAULT_BAUDRATE_HZ   (100000UL)
#endif

/** I2C transfer directions. */
typedef enum
{
    I2C_DIRECTION_WRITE = 0U,
    I2C_DIRECTION_READ = 1U
} i2c_transfer_direction_t;

void i2c_init(void);

bool i2c_start(uint8_t address, i2c_transfer_direction_t direction);


bool i2c_restart(uint8_t address, i2c_transfer_direction_t direction);

bool i2c_write(uint8_t data);

uint8_t i2c_read(bool ack);

void i2c_stop(void);

bool i2c_write_register(uint8_t address, uint8_t reg, uint8_t value);

bool i2c_read_register(uint8_t address, uint8_t reg, uint8_t *value);

bool i2c_read_registers(uint8_t address, uint8_t reg, uint8_t *buffer, size_t length);

bool i2c_read_register_diag(uint8_t address, uint8_t reg,
                            uint8_t *value, uint8_t *phase_out);

#endif /* I2C_H */