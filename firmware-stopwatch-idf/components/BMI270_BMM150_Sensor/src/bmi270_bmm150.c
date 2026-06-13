/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 lbuque <1102390310@qq.com>
 *
 * SPDX-License-Identifier: MIT
 */
#include "bmi270_bmm150.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "BMI270_BMM150";

int8_t configure_bmi2_sensor(struct bmi2_dev *dev)
{
    int8_t rslt;
    uint8_t sens_list[2] = { BMI2_ACCEL, BMI2_GYRO };

    struct bmi2_int_pin_config int_pin_cfg;
    int_pin_cfg.pin_type = BMI2_INT1;
    int_pin_cfg.int_latch = BMI2_INT_NON_LATCH;
    int_pin_cfg.pin_cfg[0].lvl = BMI2_INT_ACTIVE_HIGH;
    int_pin_cfg.pin_cfg[0].od = BMI2_INT_PUSH_PULL;
    int_pin_cfg.pin_cfg[0].output_en = BMI2_INT_OUTPUT_ENABLE;
    int_pin_cfg.pin_cfg[0].input_en = BMI2_INT_INPUT_DISABLE;

    struct bmi2_sens_config sens_cfg[2];
    sens_cfg[0].type = BMI2_ACCEL;
    sens_cfg[0].cfg.acc.bwp = BMI2_ACC_OSR2_AVG2;
    sens_cfg[0].cfg.acc.odr = BMI2_ACC_ODR_100HZ;
    sens_cfg[0].cfg.acc.filter_perf = BMI2_PERF_OPT_MODE;
    sens_cfg[0].cfg.acc.range = BMI2_ACC_RANGE_4G;
    sens_cfg[1].type = BMI2_GYRO;
    sens_cfg[1].cfg.gyr.filter_perf = BMI2_PERF_OPT_MODE;
    sens_cfg[1].cfg.gyr.bwp = BMI2_GYR_OSR2_MODE;
    sens_cfg[1].cfg.gyr.odr = BMI2_GYR_ODR_100HZ;
    sens_cfg[1].cfg.gyr.range = BMI2_GYR_RANGE_2000;
    sens_cfg[1].cfg.gyr.ois_range = BMI2_GYR_OIS_2000;

    rslt = bmi2_set_int_pin_config(&int_pin_cfg, dev);
    if (rslt != BMI2_OK)
        return rslt;

    rslt = bmi2_map_data_int(BMI2_DRDY_INT, BMI2_INT1, dev);
    if (rslt != BMI2_OK)
        return rslt;

    rslt = bmi2_set_sensor_config(sens_cfg, 2, dev);
    if (rslt != BMI2_OK)
        return rslt;

    rslt = bmi2_sensor_enable(sens_list, 2, dev);
    if (rslt != BMI2_OK)
        return rslt;

    return rslt;
}

int8_t configure_bmm150_sensor(struct bmm150_dev *dev)
{
    /* Status of api are returned to this variable. */
    int8_t rslt;
    struct bmm150_settings settings;

    /* Set powermode as normal mode */
    settings.pwr_mode = BMM150_POWERMODE_NORMAL;
    rslt = bmm150_set_op_mode(&settings, dev);

    if (rslt == BMM150_OK) {
        /* Setting the preset mode as Low power mode
         * i.e. data rate = 10Hz, XY-rep = 1, Z-rep = 2
         */
        settings.preset_mode = BMM150_PRESETMODE_REGULAR;
        //rslt = bmm150_set_presetmode(&settings, dev);

        if (rslt == BMM150_OK) {
            /* Map the data interrupt pin */
            settings.int_settings.drdy_pin_en = 0x01;
            //rslt = bmm150_set_sensor_settings(BMM150_SEL_DRDY_PIN_EN, &settings, dev);
        }
    }
    return rslt;
}

static int8_t bmi2_i2c_read(uint8_t reg_addr, uint8_t *reg_data, uint32_t len, void *intf_ptr)
{
    if ((reg_data == NULL) || (len == 0) || (len > 32)) {
        return -1;
    }

    struct dev_info* dev_info = (struct dev_info*)intf_ptr;

    i2c_bus_device_handle_t i2c_device = i2c_bus_device_create(dev_info->i2c_handle, dev_info->dev_addr, 0);
    esp_err_t ret = i2c_bus_read_bytes(i2c_device, reg_addr, len, reg_data);
    i2c_bus_device_delete(&i2c_device);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_bus_read_bytes failed: %d", ret);
        return -1;
    }
    return 0;
}

int8_t bmi2_i2c_write(uint8_t reg_addr, const uint8_t *reg_data, uint32_t len, void *intf_ptr)
{
    if ((reg_data == NULL) || (len == 0) || (len > 32)) {
        return -1;
    }

    struct dev_info* dev_info = (struct dev_info*)intf_ptr;

    i2c_bus_device_handle_t i2c_device = i2c_bus_device_create(dev_info->i2c_handle, dev_info->dev_addr, 0);
    esp_err_t ret = i2c_bus_write_bytes(i2c_device, reg_addr, len, reg_data);
    i2c_bus_device_delete(&i2c_device);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "reg_addr: 0x%02X", reg_addr);
        ESP_LOGE(TAG, "i2c_bus_write_bytes failed: %d", ret);
        return -1;
    }
    return 0;
}

#define NOP()         asm volatile("nop")

static void bmi2_delay_us(uint32_t us, void *intf_ptr)
{
    uint64_t m = (uint64_t)esp_timer_get_time();
    if (us) {
        uint64_t e = (m + us);
        if (m > e) {  //overflow
            while ((uint64_t)esp_timer_get_time() > e) {
                NOP();
            }
        }
        while ((uint64_t)esp_timer_get_time() < e) {
            NOP();
        }
  }
}

static int8_t aux_i2c_read(uint8_t reg_addr, uint8_t *reg_data, uint32_t length, void *intf_ptr) {
    struct bmi2_dev *dev = (struct bmi2_dev *)intf_ptr;
    return bmi2_read_aux_man_mode(reg_addr, reg_data, length, dev);
}

static int8_t aux_i2c_write(uint8_t reg_addr, const uint8_t *reg_data, uint32_t length, void *intf_ptr) {
    struct bmi2_dev *dev = (struct bmi2_dev *)intf_ptr;
    return bmi2_write_aux_man_mode(reg_addr, reg_data, length, dev);
}

void print_rslt(int8_t rslt)
{
    switch (rslt) {
        case BMI2_OK: return; /* Do nothing */ break;
        case BMI2_E_NULL_PTR:
            ESP_LOGE(TAG, "Error [%d] : Null pointer\n", rslt);
        break;

        case BMI2_E_COM_FAIL:
            ESP_LOGE(TAG, "Error [%d] : Communication failure\n", rslt);
        break;

        case BMI2_E_DEV_NOT_FOUND:
            ESP_LOGE(TAG, "Error [%d] : Device not found\n", rslt);
        break;

        case BMI2_E_OUT_OF_RANGE:
            ESP_LOGE(TAG, "Error [%d] : Out of range\n", rslt);
        break;

        case BMI2_E_ACC_INVALID_CFG:
            ESP_LOGE(TAG, "Error [%d] : Invalid accel configuration\n", rslt);
        break;

        case BMI2_E_GYRO_INVALID_CFG:
            ESP_LOGE(TAG, "Error [%d] : Invalid gyro configuration\n", rslt);
        break;

        case BMI2_E_ACC_GYR_INVALID_CFG:
            ESP_LOGE(TAG, "Error [%d] : Invalid accel/gyro configuration\n", rslt);
        break;

        case BMI2_E_INVALID_SENSOR:
            ESP_LOGE(TAG, "Error [%d] : Invalid sensor\n", rslt);
        break;

        case BMI2_E_CONFIG_LOAD:
            ESP_LOGE(TAG, "Error [%d] : Configuration loading error\n", rslt);
        break;

        case BMI2_E_INVALID_PAGE:
            ESP_LOGE(TAG, "Error [%d] : Invalid page\n", rslt);
        break;

        case BMI2_E_INVALID_FEAT_BIT:
            ESP_LOGE(TAG, "Error [%d] : Invalid feature bit\n", rslt);
        break;

        case BMI2_E_INVALID_INT_PIN:
            ESP_LOGE(TAG, "Error [%d] : Invalid interrupt pin\n", rslt);
        break;

        case BMI2_E_SET_APS_FAIL:
            ESP_LOGE(TAG, "Error [%d] : Setting advanced power mode failed\n", rslt);
        break;

        case BMI2_E_AUX_INVALID_CFG:
            ESP_LOGE(TAG, "Error [%d] : Invalid auxiliary configuration\n", rslt);
        break;

        case BMI2_E_AUX_BUSY:
            ESP_LOGE(TAG, "Error [%d] : Auxiliary busy\n", rslt);
        break;

        case BMI2_E_SELF_TEST_FAIL:
            ESP_LOGE(TAG, "Error [%d] : Self test failed\n", rslt);
        break;

        case BMI2_E_REMAP_ERROR:
            ESP_LOGE(TAG, "Error [%d] : Remapping error\n", rslt);
        break;

        case BMI2_E_GYR_USER_GAIN_UPD_FAIL:
            ESP_LOGE(TAG, "Error [%d] : Gyro user gain update failed\n", rslt);
        break;

        case BMI2_E_SELF_TEST_NOT_DONE:
            ESP_LOGE(TAG, "Error [%d] : Self test not done\n", rslt);
        break;

        case BMI2_E_INVALID_INPUT:
            ESP_LOGE(TAG, "Error [%d] : Invalid input\n", rslt);
        break;

        case BMI2_E_INVALID_STATUS:
            ESP_LOGE(TAG, "Error [%d] : Invalid status\n", rslt);
        break;

        case BMI2_E_CRT_ERROR:
            ESP_LOGE(TAG, "Error [%d] : CRT error\n", rslt);
        break;

        case BMI2_E_ST_ALREADY_RUNNING:
            ESP_LOGE(TAG, "Error [%d] : Self test already running\n", rslt);
        break;

        case BMI2_E_CRT_READY_FOR_DL_FAIL_ABORT:
            ESP_LOGE(TAG, "Error [%d] : CRT ready for DL fail abort\n", rslt);
        break;

        case BMI2_E_DL_ERROR:
            ESP_LOGE(TAG, "Error [%d] : DL error\n", rslt);
        break;

        case BMI2_E_PRECON_ERROR:
            ESP_LOGE(TAG, "Error [%d] : PRECON error\n", rslt);
        break;

        case BMI2_E_ABORT_ERROR:
            ESP_LOGE(TAG, "Error [%d] : Abort error\n", rslt);
        break;

        case BMI2_E_GYRO_SELF_TEST_ERROR:
            ESP_LOGE(TAG, "Error [%d] : Gyro self test error\n", rslt);
        break;

        case BMI2_E_GYRO_SELF_TEST_TIMEOUT:
            ESP_LOGE(TAG, "Error [%d] : Gyro self test timeout\n", rslt);
        break;

        case BMI2_E_WRITE_CYCLE_ONGOING:
            ESP_LOGE(TAG, "Error [%d] : Write cycle ongoing\n", rslt);
        break;

        case BMI2_E_WRITE_CYCLE_TIMEOUT:
            ESP_LOGE(TAG, "Error [%d] : Write cycle timeout\n", rslt);
        break;

        case BMI2_E_ST_NOT_RUNING:
            ESP_LOGE(TAG, "Error [%d] : Self test not running\n", rslt);
        break;

        case BMI2_E_DATA_RDY_INT_FAILED:
            ESP_LOGE(TAG, "Error [%d] : Data ready interrupt failed\n", rslt);
        break;

        case BMI2_E_INVALID_FOC_POSITION:
            ESP_LOGE(TAG, "Error [%d] : Invalid FOC position\n", rslt);
        break;

        default:
            ESP_LOGE(TAG, "Error [%d] : Unknown error code\n", rslt);
        break;
  }
}

esp_err_t bmi270_bmm150_sensor_create(i2c_bus_handle_t i2c_handle, bmi270_bmm150_handle_t *handle_ret, const bmi270_bmm150_config_t *conf)
{
    if (i2c_handle == NULL || handle_ret == NULL || conf == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    bmi270_bmm150_handle_t handle = calloc(1, sizeof(struct _bmi270_bmm150_handle_t));
    if (handle == NULL) {
        return ESP_ERR_NO_MEM;
    }
    handle->int_status = 0;

    handle->bmi2.chip_id = conf->i2c_addr;
    handle->bmi2.read = bmi2_i2c_read;
    handle->bmi2.write = bmi2_i2c_write;
    handle->bmi2.delay_us = bmi2_delay_us;
    handle->bmi2.intf = BMI2_I2C_INTF;
    handle->bmi2.intf_ptr = &handle->accel_gyro_dev_info;
    handle->bmi2.read_write_len = 30; // Limitation of the Wire library
    handle->bmi2.config_file_ptr = conf->config_file_ptr; // Use the default BMI270 config file

    handle->accel_gyro_dev_info.i2c_handle = i2c_handle;
    handle->accel_gyro_dev_info.dev_addr = handle->bmi2.chip_id;

    int8_t result = 0;

    if (conf->mode != BOSCH_MAGNETOMETER_ONLY) {
        result |= bmi270_init(&handle->bmi2);
        print_rslt(result);

        result |= configure_bmi2_sensor(&handle->bmi2);
        print_rslt(result);
    }

    /* Pull-up resistor 2k is set to the trim regiter */
    uint8_t regdata = BMI2_ASDA_PUPSEL_2K;
    result          = bmi2_set_regs(BMI2_AUX_IF_TRIM, &regdata, 1, &handle->bmi2);
    print_rslt(result);

    struct bmi2_sens_config config;
    config.type = BMI2_AUX;

    /* Get default configurations for the type of feature selected. */
    result = bmi270_get_sensor_config(&config, 1, &handle->bmi2);
    print_rslt(result);

    /* Rewrite */
    config.cfg.aux.odr             = BMI2_AUX_ODR_100HZ;
    config.cfg.aux.aux_en          = BMI2_ENABLE;
    config.cfg.aux.i2c_device_addr = BMM150_DEFAULT_I2C_ADDRESS;
    config.cfg.aux.fcu_write_en    = BMI2_ENABLE;
    config.cfg.aux.man_rd_burst    = BMI2_AUX_READ_LEN_3;
    config.cfg.aux.read_addr       = BMM150_REG_DATA_X_LSB;
    config.cfg.aux.manual_en       = BMI2_ENABLE;

    /* Set back */
    result = bmi270_set_sensor_config(&config, 1, &handle->bmi2);
    print_rslt(result);

    /* BMM150 init */
    handle->bmm1.read     = aux_i2c_read;
    handle->bmm1.write    = aux_i2c_write;
    handle->bmm1.delay_us = bmi2_delay_us;
    handle->bmm1.intf     = BMM150_I2C_INTF;
    handle->bmm1.intf_ptr = &handle->bmi2;

    if(conf->mode != BOSCH_ACCELEROMETER_ONLY) {
        result |= bmm150_init(&handle->bmm1);
        print_rslt(result);

        result = configure_bmm150_sensor(&handle->bmm1);
        print_rslt(result);
    }

    *handle_ret = handle;
    return ESP_OK;
}

// default range is +-4G, so conversion factor is (((1 << 15)/4.0f))
#define INT16_to_G   (8192.0f)

esp_err_t bmi270_bmm150_sensor_read_acceleration(bmi270_bmm150_handle_t handle, float *x, float *y, float *z)
{
    struct bmi2_sens_data sensor_data;
    int8_t ret = bmi2_get_sensor_data(&sensor_data, &handle->bmi2);

    *x = sensor_data.acc.x / INT16_to_G;
    *y = sensor_data.acc.y / INT16_to_G;
    *z = sensor_data.acc.z / INT16_to_G;
    if (ret != BMI2_OK) {
        return ESP_FAIL;
    }
    return ESP_OK;
}


esp_err_t bmi270_bmm150_sensor_acceleration_available(bmi270_bmm150_handle_t handle, int *available)
{
    uint16_t status;
    int8_t ret = bmi2_get_int_status(&status, &handle->bmi2);
    if (ret != BMI2_OK) {
        return ESP_FAIL;
    }
    *available = ((status | handle->int_status) & BMI2_ACC_DRDY_INT_MASK);
    handle->int_status = status;
    handle->int_status &= ~BMI2_ACC_DRDY_INT_MASK;
    return ESP_OK;
}

esp_err_t bmi270_bmm150_sensor_get_acceleration_sample_rate(bmi270_bmm150_handle_t handle, float *sample_rate_hz)
{
    struct bmi2_sens_config sens_cfg;
    sens_cfg.type = BMI2_ACCEL;
    int8_t ret = bmi2_get_sensor_config(&sens_cfg, 1, &handle->bmi2);
    if (ret != BMI2_OK) {
        return ESP_FAIL;
    }
    *sample_rate_hz = (1 << sens_cfg.cfg.acc.odr) * 0.39;
    return ESP_OK;
}

// default range is +-2000dps, so conversion factor is (((1 << 15)/4.0f))
#define INT16_to_DPS   (16.384f)

// Gyroscope
esp_err_t bmi270_bmm150_sensor_read_gyroscope(bmi270_bmm150_handle_t handle, float *x, float *y, float *z)
{
    struct bmi2_sens_data sensor_data;
    int8_t ret = bmi2_get_sensor_data(&sensor_data, &handle->bmi2);
    if (ret != BMI2_OK) {
        return ESP_FAIL;
    }
    *x = sensor_data.gyr.x / INT16_to_DPS;
    *y = sensor_data.gyr.y / INT16_to_DPS;
    *z = sensor_data.gyr.z / INT16_to_DPS;
    return ESP_OK;
}


esp_err_t bmi270_bmm150_sensor_gyroscope_available(bmi270_bmm150_handle_t handle, int *available)
{
    uint16_t status;
    int8_t ret = bmi2_get_int_status(&status, &handle->bmi2);
    if (ret != BMI2_OK) {
        return ESP_FAIL;
    }
    *available = ((status | handle->int_status) & BMI2_GYR_DRDY_INT_MASK);
    handle->int_status = status;
    handle->int_status &= ~BMI2_GYR_DRDY_INT_MASK;
    return ESP_OK;
}

esp_err_t bmi270_bmm150_sensor_gyroscope_sample_rate(bmi270_bmm150_handle_t handle, float *sample_rate_hz)
{
    struct bmi2_sens_config sens_cfg;
    sens_cfg.type = BMI2_GYRO;
    int8_t ret = bmi2_get_sensor_config(&sens_cfg, 1, &handle->bmi2);
    if (ret != BMI2_OK) {
        return ESP_FAIL;
    }
    *sample_rate_hz = (1 << sens_cfg.cfg.gyr.odr) * 0.39;
    return ESP_OK;
}

// Magnetometer
esp_err_t bmi270_bmm150_sensor_read_magnetic_field(bmi270_bmm150_handle_t handle, float *x, float *y, float *z)
{
    struct bmm150_mag_data mag_data;
    int8_t ret = bmm150_read_mag_data(&mag_data, &handle->bmm1);
    *x = mag_data.x;
    *y = mag_data.y;
    *z = mag_data.z;

    return ret == BMM150_OK ? ESP_OK : ESP_FAIL;
}

esp_err_t bmi270_bmm150_sensor_magnetic_field_available(bmi270_bmm150_handle_t handle, int *available)
{
    int8_t ret = bmm150_get_interrupt_status(&handle->bmm1);
    *available = handle->bmm1.int_status & BMM150_INT_ASSERTED_DRDY;

    return ret == BMM150_OK ? ESP_OK : ESP_FAIL;
}

esp_err_t bmi270_bmm150_sensor_magnetic_field_sample_rate(bmi270_bmm150_handle_t handle, float *sample_rate_hz)
{
    struct bmm150_settings settings;
    int8_t ret = bmm150_get_sensor_settings(&settings, &handle->bmm1);
    switch (settings.data_rate) {
        case BMM150_DATA_RATE_10HZ:
            *sample_rate_hz = 10;
        break;

        case BMM150_DATA_RATE_02HZ:
            *sample_rate_hz = 2;
        break;

        case BMM150_DATA_RATE_06HZ:
            *sample_rate_hz = 6;
        break;

        case BMM150_DATA_RATE_08HZ:
            *sample_rate_hz = 8;
        break;

        case BMM150_DATA_RATE_15HZ:
            *sample_rate_hz = 15;
        break;

        case BMM150_DATA_RATE_20HZ:
            *sample_rate_hz = 20;
        break;

        case BMM150_DATA_RATE_25HZ:
            *sample_rate_hz = 25;
        break;

        case BMM150_DATA_RATE_30HZ:
            *sample_rate_hz = 30;
        break;
    }

    return ret == BMM150_OK ? ESP_OK : ESP_FAIL;
}
