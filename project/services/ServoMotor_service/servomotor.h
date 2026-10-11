#ifndef SERVOMOTOR_H
#define SERVOMOTOR_H

#include <zephyr/kernel.h>

#include "app_types.h"

#include <zephyr/drivers/pwm.h>

/**
 * Check that the Servo Motor is present and ready.
 *
 * Must be called before servomotor_service_start().
 *
 * @return 0 on success, negative errno otherwise.
 */
int servomotor_service_init(void);

/**
 * Start the rendering thread.
 *
 * The thread blocks on @p in and change the impulse for every sample it
 * receives, using the PWM driver.
 *
 * @param in queue delivering struct imu_sample items
 * @return 0 on success, negative errno otherwise.
 */
int servomotor_service_start(struct k_msgq *out);

/**
 * Set the active state of the servo motor service.
 *
 * @param on true to activate the service, false to deactivate it
 * @return void
 */
void servomotor_service_set_active(bool on);

/**
 * Set the tilt sensitivity level (SENS_MIN..SENS_MAX) and persist it.
 * Higher levels reach full servo deflection with a smaller tilt.
 *
 * @param level clamped into [SENS_MIN, SENS_MAX]
 */
void servomotor_service_set_sensitivity(uint8_t level);

/** @return the current tilt sensitivity level. */
uint8_t servomotor_service_get_sensitivity(void);

#endif // SERVOMOTOR_H