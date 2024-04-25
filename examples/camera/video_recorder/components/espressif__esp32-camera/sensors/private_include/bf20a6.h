
#ifndef __BF20A6_H__
#define __BF20A6_H__

#include "C:\Espressif\frameworks\esp-idf-v5.0.1\components\esp32-camera-master\driver\include\sensor.h"

/**
 * @brief Detect sensor pid
 *
 * @param slv_addr SCCB address
 * @param id Detection result
 * @return
 *     0:       Can't detect this sensor
 *     Nonzero: This sensor has been detected
 */
int bf20a6_detect(int slv_addr, sensor_id_t *id);

/**
 * @brief initialize sensor function pointers
 *
 * @param sensor pointer of sensor
 * @return
 *      Always 0
 */
int bf20a6_init(sensor_t *sensor);

#endif // __BF20A6_H__
