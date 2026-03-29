	/*
	 * ds3231.c
	 *
	 *  Created on: Oct 16, 2025
	 *      Author: Jagan
	 */

	#include "ds3231.h"
	#include <stdio.h>
	#include "main.h"  // For Error_Handler (CubeMX-generated)
	#include "utilities_conf.h"
	#include "sys_app.h"
       #include "i2c.h"

	/* BCD to Decimal Conversion */
	uint8_t bcd_to_dec(uint8_t bcd) {
		return ((bcd & 0x0F) + ((bcd >> 4) * 10));
	}

	/* Decimal to BCD Conversion */
	uint8_t dec_to_bcd(uint8_t dec) {
		return ((dec / 10 * 16) + (dec % 10));
	}

	/**
	 * @brief Set the time on DS3231 (initial write).
	 * @param hi2c: Pointer to I2C handle (e.g., &hi2c1).
	 * @param sec, min, hour: Time in decimal (24h mode).
	 * @param weekday: 1-7.
	 * @param date: 1-31.
	 * @param month: 1-12.
	 * @param year: 0-99 (2000 + year).
	 * @retval None (calls Error_Handler on failure).
	 */
	void ds3231_set_time(I2C_HandleTypeDef *hi2c, uint8_t sec, uint8_t min, uint8_t hour,
						 uint8_t weekday, uint8_t date, uint8_t month, uint8_t year) {
		uint8_t time_data[7] = {0};
		time_data[0] = dec_to_bcd(sec);    // Reg 0x00: Seconds
		time_data[1] = dec_to_bcd(min);    // 0x01: Minutes
		time_data[2] = dec_to_bcd(hour);  // 0x02: Hours (24h: bit6=0 auto)
		time_data[3] = dec_to_bcd(weekday); // 0x03: Day of Week
		time_data[4] = dec_to_bcd(date);   // 0x04: Date
		time_data[5] = dec_to_bcd(month);  // 0x05: Month (century bit=0)
		time_data[6] = dec_to_bcd(year);   // 0x06: Year

		if (HAL_I2C_Mem_Write(hi2c, DS3231_ADDR, 0x00, I2C_MEMADD_SIZE_8BIT, time_data, 7, HAL_MAX_DELAY) != HAL_OK) {
			// I2C Error: Check wiring, pull-ups, or call custom error
			Error_Handler();
		}

		// Optional debug print (requires UART init)
		APP_LOG(TS_ON, VLEVEL_M, "DS3231 set: %02d:%02d:%02d %02d/%02d/20%02d (wd %d)\r\n", hour, min, sec, month, date, year, weekday);
	}

	/**
	 * @brief Read current time from DS3231.
	 * @param hi2c: Pointer to I2C handle (e.g., &hi2c1).
	 * @param time: Pointer to DS3231_Time_t struct to fill.
	 * @retval None (calls Error_Handler on failure).
	 */

	uint32_t ds3231_read_time(I2C_HandleTypeDef *hi2c)
	{
		  MX_I2C2_Init();
	    static DS3231_Time_t time;
	    uint8_t raw_data[7] = {0};

	    if (HAL_I2C_Mem_Read(hi2c, DS3231_ADDR, 0x00,
	                          I2C_MEMADD_SIZE_8BIT,
	                          raw_data, 7, 500) != HAL_OK)
	    {
	        APP_LOG(TS_ON, VLEVEL_M, " I2C read failed!\r\n");
	        return 0;
	    }

	    time.seconds = bcd_to_dec(raw_data[0] & 0x7F);
	    time.minutes = bcd_to_dec(raw_data[1] & 0x7F);
	    time.hours   = bcd_to_dec(raw_data[2] & 0x3F);
	    time.weekday = bcd_to_dec(raw_data[3] & 0x07);
	    time.date    = bcd_to_dec(raw_data[4] & 0x3F);
	    time.month   = bcd_to_dec(raw_data[5] & 0x1F);
	    time.year    = bcd_to_dec(raw_data[6]);

	    return ds3231_to_epoch(&time);
	}


	/**
	 * @brief Convert DS3231 time to Unix epoch timestamp (seconds since 1970-01-01 00:00:00 UTC).
	 * @param time: Pointer to DS3231_Time_t struct.
	 * @return uint32_t epoch (valid up to 2038-01-19; use uint64_t for longer).
	 * @note Assumes 20xx year (2000 + year), 24h mode, no DST/timezone.
	 */
	uint32_t ds3231_to_epoch(const DS3231_Time_t *time) {
		// Days since 1970-01-01 (0-based)
		uint32_t days = 0;
		uint16_t full_year = 2000 + time->year;

		// Add days from full years 1970 to full_year-1
		for (uint16_t y = 1970; y < full_year; y++) {
			days += (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 366 : 365;
		}

		// Add days from Jan to month-1 in current year
		static const uint8_t month_days[13] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
		uint8_t leap_day = 0;
		if (time->month > 2) {
			if ((full_year % 4 == 0 && full_year % 100 != 0) || (full_year % 400 == 0)) {
				leap_day = 1;
			}
		}
		for (uint8_t m = 1; m < time->month; m++) {
			if (m == 2) {
				days += month_days[m] + leap_day;
			} else {
				days += month_days[m];
			}
		}

		// Add days in current month (0-based)
		days += time->date - 1;

		// Convert to seconds
		uint32_t epoch = days * 86400UL;  // Seconds in a day
		epoch += time->hours * 3600UL;
		epoch += time->minutes * 60UL;
		epoch += time->seconds;
		APP_LOG(TS_ON, VLEVEL_M, "DS3231 get: %d\r\n", epoch);

		return epoch;
	}


	/**
	 * @brief  Convert Unix epoch to DS3231_Time_t struct.
	 * @param  epoch: UTC seconds since 1970-01-01 00:00:00
	 * @param  time:  Pointer to DS3231_Time_t to fill
	 */
	void ds3231_from_epoch(uint32_t epoch, DS3231_Time_t *time)
	{
	    /* ---- Time of day ---- */
	    time->seconds = epoch % 60; epoch /= 60;
	    time->minutes = epoch % 60; epoch /= 60;
	    time->hours   = epoch % 24; epoch /= 24;

	    /* ---- Weekday (1970-01-01 was Thursday = day 4) ---- */
	    time->weekday = (uint8_t)((epoch + 4) % 7) + 1;  /* 1=Sunday */

	    /* ---- Date / Month / Year ---- */
	    uint16_t year = 1970;
	    while (1)
	    {
	        uint16_t days_in_year =
	            (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) ? 366 : 365;
	        if (epoch < days_in_year) break;
	        epoch -= days_in_year;
	        year++;
	    }

	    static const uint8_t month_days[13] =
	        {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

	    uint8_t leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) ? 1 : 0;

	    uint8_t month = 1;
	    while (1)
	    {
	        uint8_t dim = month_days[month] + (month == 2 ? leap : 0);
	        if (epoch < dim) break;
	        epoch -= dim;
	        month++;
	    }

	    time->date  = (uint8_t)(epoch + 1);    /* 1-based */
	    time->month = month;                    /* 1-12    */
	    time->year  = (uint8_t)(year - 2000);  /* 0-99    */
	}
