#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "sensor_mpu6050.h"

LOG_MODULE_REGISTER(sensor_mpu6050, CONFIG_LOG_DEFAULT_LEVEL);

#define SENSOR_THREAD_STACK_SIZE 1536
/* Sampling is the time-critical half, so it runs above the renderer. */
#define SENSOR_THREAD_PRIO	 K_PRIO_PREEMPT(5)
#define SAMPLE_INTERVAL		 K_MSEC(100)

static const struct device *const mpu6050 = DEVICE_DT_GET_ONE(invensense_mpu6050);

static K_THREAD_STACK_DEFINE(sensor_stack, SENSOR_THREAD_STACK_SIZE);
static struct k_thread sensor_thread;
static struct k_msgq *motor_q;

static int read_imu(struct imu_sample *sample)
{
	struct sensor_value accel[3];
	int rc = sensor_sample_fetch(mpu6050);

	if (rc == 0) {
		rc = sensor_channel_get(mpu6050, SENSOR_CHAN_ACCEL_XYZ, accel);
	}
	if (rc != 0) {
		return rc;
	}

	for (int i = 0; i < 3; i++) {
		sample->accel[i] = sensor_value_to_double(&accel[i]);
	}

	return 0;
}

static void publish_to(struct k_msgq *q, const struct imu_sample *sample)
{
    if (k_msgq_put(q, sample, K_NO_WAIT) == 0) {
        return;
    }
    struct imu_sample dropped;
    (void)k_msgq_get(q, &dropped, K_NO_WAIT);
    if (k_msgq_put(q, sample, K_NO_WAIT) != 0) {
        LOG_WRN("Dropped a sample on queue %p: consumer not keeping up", (void *)q);
    }
}
static void publish(const struct imu_sample *sample)
{
    publish_to(motor_q, sample);
}

static void sensor_thread_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (true) {
		struct imu_sample sample;
		int rc = read_imu(&sample);

		if (rc != 0) {
			LOG_ERR("Sample fetch/get failed: %d", rc);
		} else {
			LOG_DBG("accel %.2f %.2f %.2f m/s2", sample.accel[0], sample.accel[1], sample.accel[2]);
			publish(&sample);
		}

		k_sleep(SAMPLE_INTERVAL);
	}
}

int sensor_service_init(void)
{
	if (!device_is_ready(mpu6050)) {
		LOG_ERR("Sensor %s is not rea dy", mpu6050->name);
		return -ENODEV;
	}

	LOG_INF("Sensor %s ready", mpu6050->name);

	return 0;
}

int sensor_service_start(struct k_msgq *motor_out)
{
	if ( motor_out == NULL) {
		return -EINVAL;
	}

	motor_q = motor_out;

	k_thread_create(&sensor_thread, sensor_stack, K_THREAD_STACK_SIZEOF(sensor_stack),
			 sensor_thread_fn, NULL, NULL, NULL, SENSOR_THREAD_PRIO, 0, K_NO_WAIT);
	(void)k_thread_name_set(&sensor_thread, "sensor");

	return 0;
}