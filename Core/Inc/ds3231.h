/*
 * ds3231.h
 *
 *  Created on: __Oct__ 16, 2025
 *      Author: __Jagan__
 */

#ifndef INC_DS3231_H_
#define INC_DS3231_H_

#include "stm32wlxx_hal.h"  // For I2C_HandleTypeDef
#include <stdint.h>

/* DS3231 I2C Address (7-bit, shifted for HAL) */
#define DS3231_ADDR 0x68 << 1

/* DS3231 Time Structure (Decimal Format) */
typedef struct {
    uint8_t seconds;   // 0-59
    uint8_t minutes;   // 0-59
    uint8_t hours;     // 0-23 (24h mode)
    uint8_t weekday;   // 1-7 (1=Sunday)
    uint8_t date;      // 1-31
    uint8_t month;     // 1-12
    uint8_t year;      // 0-99 (2000 + year)
} DS3231_Time_t;

/* Function Prototypes */
void     ds3231_set_time(I2C_HandleTypeDef *hi2c, uint8_t sec, uint8_t min, uint8_t hour,
                         uint8_t weekday, uint8_t date, uint8_t month, uint8_t year);
uint32_t ds3231_read_time(I2C_HandleTypeDef *hi2c);
uint32_t ds3231_to_epoch(const DS3231_Time_t *time);
void     ds3231_from_epoch(uint32_t epoch, DS3231_Time_t *time);  /* NEW: epoch → DS3231_Time_t */
uint8_t  bcd_to_dec(uint8_t bcd);
uint8_t  dec_to_bcd(uint8_t dec);

#endif /* INC_DS3231_H_ */
