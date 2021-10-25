/*******************************************************************************
  BMI160 Sensor Driver Interface Source File

  Company:
    Microchip Technology Inc.

  File Name:
    bmi160_sensor.c

  Summary:
    This file implements the simplified sensor API for configuring the BMI160 IMU sensor

  Description:
    None
 *******************************************************************************/

/*******************************************************************************
* Copyright (C) 2020 Microchip Technology Inc. and its subsidiaries.
*
* Subject to your compliance with these terms, you may use Microchip software
* and any derivatives exclusively with Microchip products. It is your
* responsibility to comply with third party license terms applicable to your
* use of third party software (including open source software) that may
* accompany Microchip software.
*
* THIS SOFTWARE IS SUPPLIED BY MICROCHIP "AS IS". NO WARRANTIES, WHETHER
* EXPRESS, IMPLIED OR STATUTORY, APPLY TO THIS SOFTWARE, INCLUDING ANY IMPLIED
* WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY, AND FITNESS FOR A
* PARTICULAR PURPOSE.
*
* IN NO EVENT WILL MICROCHIP BE LIABLE FOR ANY INDIRECT, SPECIAL, PUNITIVE,
* INCIDENTAL OR CONSEQUENTIAL LOSS, DAMAGE, COST OR EXPENSE OF ANY KIND
* WHATSOEVER RELATED TO THE SOFTWARE, HOWEVER CAUSED, EVEN IF MICROCHIP HAS
* BEEN ADVISED OF THE POSSIBILITY OR THE DAMAGES ARE FORESEEABLE. TO THE
* FULLEST EXTENT ALLOWED BY LAW, MICROCHIP'S TOTAL LIABILITY ON ALL CLAIMS IN
* ANY WAY RELATED TO THIS SOFTWARE WILL NOT EXCEED THE AMOUNT OF FEES, IF ANY,
* THAT YOU HAVE PAID DIRECTLY TO MICROCHIP FOR THIS SOFTWARE.
 *******************************************************************************/

#include <stdint.h>
#include <string.h>
#include "sensor.h"
#include "bmi160.h"
// *****************************************************************************
// *****************************************************************************
// Section: Platform specific includes
// *****************************************************************************
// *****************************************************************************
#include "mcc_generated_files/mcc.h"

// *****************************************************************************
// *****************************************************************************
// Section: Macros for setting sample rate and sensor range
// *****************************************************************************
// *****************************************************************************
// Macro function to get the proper Macro defines corresponding to SNSR_SAMPLE_RATE
#define __SNSRSAMPLERATEMACRO(x, y) BMI160_ ## x ## _ODR_ ## y ## HZ
#define _SNSRSAMPLERATEEXPR(x, y) __SNSRSAMPLERATEMACRO(x, y)
#define _GET_IMU_SAMPLE_RATE_MACRO(x) _SNSRSAMPLERATEEXPR(x, SNSR_SAMPLE_RATE)

#define __SNSRACCELRANGEMACRO(x) BMI160_ACCEL_RANGE_ ## x ## G
#define _SNSRACCELRANGEEXPR(x) __SNSRACCELRANGEMACRO(x)
#define _GET_IMU_ACCEL_RANGE_MACRO() _SNSRACCELRANGEEXPR(SNSR_ACCEL_RANGE)

#define __SNSRGYRORANGEMACRO(x) BMI160_GYRO_RANGE_ ## x ## _DPS
#define _SNSRGYRORANGEEXPR(x) __SNSRGYRORANGEMACRO(x)
#define _GET_IMU_GYRO_RANGE_MACRO() _SNSRGYRORANGEEXPR(SNSR_GYRO_RANGE)

// *****************************************************************************
// *****************************************************************************
// Section: Serial comms implementation
// *****************************************************************************
// *****************************************************************************
typedef struct
{
    size_t len;
    void *data;
}buf_t;

static twi0_operations_t read_complete_handler(void *ptr)
{
    I2C0_SetBuffer(((buf_t *)ptr)->data,((buf_t*)ptr)->len);
    I2C0_SetDataCompleteCallback(I2C0_SetReturnStopCallback, NULL);
    return I2C0_RESTART_READ;
}

static int8_t bmi160_i2c_read (uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, uint16_t len) {
    twi0_error_t ret;
    buf_t    readbuffer;
    
    readbuffer.data = data;
    readbuffer.len = len;
    
    while((ret = I2C0_Open(dev_addr)) == I2C0_BUSY); // sit here until we get the bus..
    I2C0_SetDataCompleteCallback(read_complete_handler, &readbuffer);
    I2C0_SetAddressNackCallback(I2C0_SetRestartWriteCallback, NULL);
    I2C0_SetBuffer(&reg_addr, 1);
    I2C0_MasterOperation(0);
    while((ret = I2C0_Close()) == I2C0_BUSY); // sit here until finished.

    if (ret != I2C0_NOERR) {
        return BMI160_E_COM_FAIL;
    }
    
    return BMI160_OK;
}

static int8_t bmi160_i2c_write (uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, uint16_t len) {
    twi0_error_t ret;
    static uint8_t buff [SNSR_COM_BUF_SIZE];
    
    if (len + 1 > SNSR_COM_BUF_SIZE)
        return BMI160_E_COM_FAIL;
    
    buff[0] = reg_addr;
    for (int i = 0; i < len; i++) {
        buff[i+1] = data[i];
    }
    
    while((ret = I2C0_Open(dev_addr)) == I2C0_BUSY); // sit here until we get the bus..
    I2C0_SetAddressNackCallback(I2C0_SetRestartWriteCallback, NULL); //NACK polling?
    I2C0_SetBuffer(buff, len+1);
    I2C0_MasterOperation(0);
    while((ret = I2C0_Close()) == I2C0_BUSY); // sit here until finished.
    
    if (ret != I2C0_NOERR) {
        return BMI160_E_COM_FAIL;
    }

    return BMI160_OK;
}

// *****************************************************************************
// *****************************************************************************
// Section: Platform generic sensor implementation functions
// *****************************************************************************
// *****************************************************************************
int bmi160_sensor_read(struct sensor_device_t *sensor, snsr_data_t *ptr)
{
    /* Read bmi160 sensor data */
    struct bmi160_sensor_data accel;
    struct bmi160_sensor_data gyro;
    int status;
    
    status = bmi160_get_sensor_data(BMI160_ACCEL_SEL | BMI160_GYRO_SEL, &accel, &gyro, &sensor->device);
    if (status != BMI160_OK)
        return status;
    
    /* Convert sensor data to buffer type and write to buffer */
#if SNSR_USE_ACCEL
    *ptr++ = (snsr_data_t) accel.x;
    *ptr++ = (snsr_data_t) accel.y;
    *ptr++ = (snsr_data_t) accel.z;
#endif
#if SNSR_USE_GYRO
    *ptr++ = (snsr_data_t) gyro.x;
    *ptr++ = (snsr_data_t) gyro.y;
    *ptr++ = (snsr_data_t) gyro.z;
#endif
    
    return status;
}

int bmi160_sensor_init(struct sensor_device_t *sensor) {
    sensor->status = BMI160_OK;
    
    /* Initialize BMI160 */
    sensor->device.id = BMI160_I2C_ADDR;
    sensor->device.interface = BMI160_I2C_INTF;
    sensor->device.read = bmi160_i2c_read;
    sensor->device.write = bmi160_i2c_write;
    sensor->device.delay_ms = snsr_sleep_ms;
    
    sensor->status = bmi160_init(&sensor->device);
    
    return sensor->status;
}

int bmi160_sensor_set_config(struct sensor_device_t *sensor) {    
    if (sensor->status != BMI160_OK)
        return sensor->status;
            
    /* Select the Output data rate, range of accelerometer sensor */
    sensor->device.accel_cfg.odr = _GET_IMU_SAMPLE_RATE_MACRO(ACCEL); //BMI160_ACCEL_ODR_100HZ;
    sensor->device.accel_cfg.range = _GET_IMU_ACCEL_RANGE_MACRO();//BMI160_ACCEL_RANGE_2G;
    sensor->device.accel_cfg.bw = BMI160_ACCEL_BW_NORMAL_AVG4;
    
    /* Select the power mode of accelerometer sensor */
    sensor->device.accel_cfg.power = BMI160_ACCEL_NORMAL_MODE;
    
    /* Select the Output data rate, range of Gyroscope sensor */
    sensor->device.gyro_cfg.odr = _GET_IMU_SAMPLE_RATE_MACRO(GYRO); //BMI160_GYRO_ODR_100HZ;
    sensor->device.gyro_cfg.range = _GET_IMU_GYRO_RANGE_MACRO(); //BMI160_GYRO_RANGE_2000_DPS;
    sensor->device.gyro_cfg.bw = BMI160_GYRO_BW_NORMAL_MODE;

    /* Select the power mode of Gyroscope sensor */
    sensor->device.gyro_cfg.power = BMI160_GYRO_NORMAL_MODE;

    /* Set the sensor configuration */
    if ((sensor->status = bmi160_set_sens_conf(&sensor->device)) != BMI160_OK)
        return sensor->status;
    
//    /* Set up fast offset compensation */
//    struct bmi160_foc_conf foc_conf;
//    struct bmi160_offsets offsets;
//    
//    foc_conf.acc_off_en = BMI160_ENABLE;
//    foc_conf.foc_acc_x  = BMI160_FOC_ACCEL_0G;
//    foc_conf.foc_acc_y  = BMI160_FOC_ACCEL_0G;
//    foc_conf.foc_acc_z  = BMI160_FOC_ACCEL_POSITIVE_G;
//    foc_conf.gyro_off_en = BMI160_ENABLE;
//    foc_conf.foc_gyr_en = BMI160_ENABLE;
//
//    sensor->status = bmi160_start_foc(&foc_conf, &offsets, &sensor->device);
    
    /* Configure sensor interrupt */
    struct bmi160_int_settg int_config;
    
    /* Select the Interrupt channel/pin */
    int_config.int_channel = BMI160_INT_CHANNEL_1;// Interrupt channel/pin 1

    /* Select the Interrupt type */
    int_config.int_type = BMI160_ACC_GYRO_DATA_RDY_INT;// Choosing data ready interrupt
    
    /* Select the interrupt channel/pin settings */
    int_config.int_pin_settg.output_en = BMI160_ENABLE;// Enabling interrupt pins to act as output pin
    int_config.int_pin_settg.output_mode = BMI160_DISABLE;// Choosing push-pull mode for interrupt pin
    int_config.int_pin_settg.output_type = BMI160_ENABLE;// Choosing active high output
    int_config.int_pin_settg.edge_ctrl = BMI160_ENABLE;// Choosing edge triggered output
    int_config.int_pin_settg.input_en = BMI160_DISABLE;// Disabling interrupt pin to act as input
    int_config.int_pin_settg.latch_dur = BMI160_LATCH_DUR_NONE;// non-latched output
            
    /* Set the data ready interrupt */
    if ((sensor->status = bmi160_set_int_config(&int_config, &sensor->device)) != BMI160_OK)
        return sensor->status;
    
    return sensor->status;
}