/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 lbuque <1102390310@qq.com>
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include "i2c_bus.h"
#include "bmi270.h"
#include "bmm150.h"

/**
 * @brief Sensor operating modes
 */
typedef enum {
    BOSCH_ACCELEROMETER_ONLY, /*!< Use only the accelerometer */
    BOSCH_MAGNETOMETER_ONLY,  /*!< Use only the magnetometer */
    BOSCH_ACCEL_AND_MAGN,     /*!< Use both the accelerometer and magnetometer */
} SensorMode_t;

/**
 * @brief I2C initialization parameters
 */
typedef struct {
    uint8_t i2c_addr;               /*!< I2C address of the BMI270 device (typically 0x68) */
    const uint8_t *config_file_ptr; /*!< Pointer to the configuration data buffer address */
    SensorMode_t mode;              /*!< Configuration to specify which sensors to initialize */
} bmi270_bmm150_config_t;

/**
 * @brief Device information structure
 */
struct dev_info {
    i2c_bus_handle_t i2c_handle; /*!< I2C bus handle */
    uint8_t dev_addr;            /*!< I2C device address */
};

/**
 * @brief BMI270_BMM150 sensor handle structure
 */
struct _bmi270_bmm150_handle_t {
    struct dev_info accel_gyro_dev_info; /*!< Accelerometer and gyroscope device information */
    struct dev_info mag_dev_info;        /*!< Magnetometer device information */
    struct bmi2_dev bmi2;                /*!< BMI2 device structure */
    struct bmm150_dev bmm1;              /*!< BMM150 device structure */
    uint16_t int_status;                 /*!< Interrupt status */
};

typedef struct _bmi270_bmm150_handle_t *bmi270_bmm150_handle_t;

/**
 * @brief Create and initialize the BMI270_BMM150 sensor.
 *
 * @param i2c_handle I2C bus handle.
 * @param handle_ret Output parameter to receive the sensor handle.
 * @param conf Configuration parameters for the sensor.
 *
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t bmi270_bmm150_sensor_create(i2c_bus_handle_t i2c_handle, bmi270_bmm150_handle_t *handle_ret, const bmi270_bmm150_config_t *conf);

/**
 * @brief Read acceleration data from the BMI270_BMM150 sensor.
 *
 * @param handle Sensor handle.
 * @param x Pointer to store the X-axis acceleration (in G).
 * @param y Pointer to store the Y-axis acceleration (in G).
 * @param z Pointer to store the Z-axis acceleration (in G).
 *
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t bmi270_bmm150_sensor_read_acceleration(bmi270_bmm150_handle_t handle, float *x, float *y, float *z);

/**
 * @brief Check if new acceleration data is available from the BMI270_BMM150 sensor.
 *
 * @param handle Sensor handle.
 * @param available Output parameter to receive the number of available samples in the FIFO.
 *
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t bmi270_bmm150_sensor_acceleration_available(bmi270_bmm150_handle_t handle, int *available);

/**
 * @brief Get the acceleration sample rate from the BMI270_BMM150 sensor.
 *
 * @param handle Sensor handle.
 * @param sample_rate_hz Output parameter to receive the sample rate in Hz.
 *
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t bmi270_bmm150_sensor_get_acceleration_sample_rate(bmi270_bmm150_handle_t handle, float *sample_rate_hz);

/**
 * @brief Read gyroscope data from the BMI270_BMM150 sensor.
 *
 * @param handle Sensor handle.
 * @param x Pointer to store the X-axis angular velocity (in degrees/second).
 * @param y Pointer to store the Y-axis angular velocity (in degrees/second).
 * @param z Pointer to store the Z-axis angular velocity (in degrees/second).
 *
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t bmi270_bmm150_sensor_read_gyroscope(bmi270_bmm150_handle_t handle, float *x, float *y, float *z);

/**
 * @brief Check if new gyroscope data is available from the BMI270_BMM150 sensor.
 *
 * @param handle Sensor handle.
 * @param available Output parameter to receive the number of available samples in the FIFO.
 *
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t bmi270_bmm150_sensor_gyroscope_available(bmi270_bmm150_handle_t handle, int *available);

/**
 * @brief Get the gyroscope sample rate from the BMI270_BMM150 sensor.
 *
 * @param handle Sensor handle.
 * @param sample_rate_hz Output parameter to receive the sample rate in Hz.
 *
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t bmi270_bmm150_sensor_gyroscope_sample_rate(bmi270_bmm150_handle_t handle, float *sample_rate_hz);

/**
 * @brief Read magnetic field data from the BMI270_BMM150 sensor.
 *
 * @param handle Sensor handle.
 * @param x Pointer to store the X-axis magnetic field (in micro Tesla).
 * @param y Pointer to store the Y-axis magnetic field (in micro Tesla).
 * @param z Pointer to store the Z-axis magnetic field (in micro Tesla).
 *
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t bmi270_bmm150_sensor_read_magnetic_field(bmi270_bmm150_handle_t handle, float *x, float *y, float *z);

/**
 * @brief Check if new magnetic field data is available from the BMI270_BMM150 sensor.
 *
 * @param handle Sensor handle.
 * @param available Output parameter to receive the number of available samples in the FIFO.
 *
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t bmi270_bmm150_sensor_magnetic_field_available(bmi270_bmm150_handle_t handle, int *available);

/**
 * @brief Get the magnetic field sample rate from the BMI270_BMM150 sensor.
 *
 * @param handle Sensor handle.
 * @param sample_rate_hz Output parameter to receive the sample rate in Hz.
 *
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t bmi270_bmm150_sensor_magnetic_field_sample_rate(bmi270_bmm150_handle_t handle, float *sample_rate_hz);

#ifdef __cplusplus
}
#endif // End of CPP guard
