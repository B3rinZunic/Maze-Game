#ifndef SENSOR_MPU6050_H_
#define SENSOR_MPU6050_H_

#include <zephyr/kernel.h>

#include "app_types.h"

/**
 * Check that the MPU6050 is present and ready.
 *
 * Must be called befor e sensor_service_start().
 *
 * @return 0 on success, negative errno otherwise.
 */
int sensor_service_init(void);

/**
 * Start the sampling thread.
 *
 * The thread polls the sensor and publishes each reading to @p out. If the
 * queue is full the oldest sample is dropped, so a slow consumer never stalls
 * the sampling cadence.
 *
 * @param out queue receiving struct imu_sample items
 * @return 0 on success, negative errno otherwise.
 */
int sensor_service_start(struct k_msgq *motor_out);

#endif /* SENSOR_MPU6050_H_ */
