#include <zephyr/drivers/pwm.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/settings/settings.h>

#include "servomotor.h"

#define MOTOR_THREAD_STACK_SIZE 1536
#define MOTOR_THREAD_PRIO K_PRIO_PREEMPT(6)

static K_THREAD_STACK_DEFINE(motor_stack, MOTOR_THREAD_STACK_SIZE);
static struct k_thread motor_thread;
static struct k_msgq *input_q;

static atomic_t servo_active = ATOMIC_INIT(0);

/*
 * Tilt sensitivity as a level SENS_MIN..SENS_MAX. The motor thread turns it
 * into the acceleration (m/s^2) that maps to full servo deflection: a higher
 * level means a smaller ACCEL_MAX, so a gentler tilt drives the servo fully.
 * Stored as an atomic int (written by the game thread, read by the motor
 * thread) so there is no torn read of a multi-byte value.
 */
static atomic_t sens_level = ATOMIC_INIT(SENS_DEFAULT);

/* level 1..10 -> ACCEL_MAX 10.0 .. 1.0 */
static inline double accel_max_from_level(int level)
{
	return (double)(SENS_MAX + 1 - level);
}

LOG_MODULE_REGISTER(servomotor, CONFIG_LOG_DEFAULT_LEVEL);

static const struct pwm_dt_spec motor1 = PWM_DT_SPEC_GET(DT_NODELABEL(servo1));
static const struct pwm_dt_spec motor2 = PWM_DT_SPEC_GET(DT_NODELABEL(servo2));
static const uint32_t min_pulse1 = DT_PROP(DT_NODELABEL(servo1), min_pulse);
static const uint32_t max_pulse1 = DT_PROP(DT_NODELABEL(servo1), max_pulse);
static const uint32_t min_pulse2 = DT_PROP(DT_NODELABEL(servo2), min_pulse);
static const uint32_t max_pulse2 = DT_PROP(DT_NODELABEL(servo2), max_pulse);

/* Load the persisted sensitivity from the "cfg" settings subtree. */
static int cfg_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)
{
    if (!settings_name_steq(name, "sens", NULL)) {
        return -ENOENT;
    }

    uint8_t level = SENS_DEFAULT;
    ssize_t rc = read_cb(cb_arg, &level, sizeof(level));
    if (rc < 0) {
        return (int)rc;
    }

    if (level < SENS_MIN) {
        level = SENS_MIN;
    } else if (level > SENS_MAX) {
        level = SENS_MAX;
    }
    atomic_set(&sens_level, level);
    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(cfg, "cfg", NULL, cfg_set, NULL, NULL);

int servomotor_service_init(void)
{
    if (!device_is_ready(motor1.dev)) {
        LOG_ERR("PWM device %s is not ready", motor1.dev->name);
        return -ENODEV;
    }
    LOG_INF("PWM device %s is ready", motor1.dev->name);

    if (!device_is_ready(motor2.dev)) {
        LOG_ERR("PWM device %s is not ready", motor2.dev->name);
        return -ENODEV;
    }
    LOG_INF("PWM device %s is ready", motor2.dev->name);

    /* settings_subsys_init() is idempotent; score_service_init() may also
     * call it. Load our persisted sensitivity (no-op on first boot). */
    if (settings_subsys_init() == 0) {
        (void)settings_load_subtree("cfg");
    }
    LOG_INF("servo sensitivity level %ld", atomic_get(&sens_level));

    return 0;
}

static void motor_thread_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (true) {
      struct imu_sample sample;
      int ret;

      ret = k_msgq_get(input_q, &sample, K_FOREVER);
      if (ret != 0) {
            LOG_ERR("Failed to get sample from queue: %d", ret);
            continue;
      }
      if (!atomic_get(&servo_active)) {
          LOG_DBG("Servo motor service is inactive. Skipping sample.");
          continue;
      }
      double accel_max = accel_max_from_level((int)atomic_get(&sens_level));
      double normalized1 = (sample.accel[0] + accel_max) / (2.0 * accel_max);
      double normalized2 = (-sample.accel[1] + accel_max) / (2.0 * accel_max);
      if (normalized1 < 0.0) normalized1 = 0.0;
      if (normalized1 > 1.0) normalized1 = 1.0;
      if (normalized2 < 0.0) normalized2 = 0.0;
      if (normalized2 > 1.0) normalized2 = 1.0;

      uint32_t pulse1 = min_pulse1 + (uint32_t)(normalized1 * (max_pulse1 - min_pulse1));
      uint32_t pulse2 = min_pulse2 + (uint32_t)(normalized2 * (max_pulse2 - min_pulse2));

      ret = pwm_set_pulse_dt(&motor1, pulse1);
      if (ret != 0) {
            LOG_ERR("Failed to set servo 1 pulse width: %d", ret);
      } else {
            LOG_DBG("servo1 pulse %u ns (norm %.2f)", pulse1, normalized1);
      }
      ret = pwm_set_pulse_dt(&motor2, pulse2);
      if (ret != 0) {
            LOG_ERR("Failed to set servo 2 pulse width: %d", ret);
      } else {
            LOG_DBG("servo2 pulse %u ns (norm %.2f)", pulse2, normalized2);
      }
	}
}

int servomotor_service_start(struct k_msgq *in)
{
  if (in == NULL) {
		return -EINVAL;
	}

	input_q = in;

	k_thread_create(&motor_thread, motor_stack, K_THREAD_STACK_SIZEOF(motor_stack),
			motor_thread_fn, NULL, NULL, NULL, MOTOR_THREAD_PRIO, 0, K_NO_WAIT);
	(void)k_thread_name_set(&motor_thread, "motor");
    return 0;
}

void servomotor_service_set_active(bool on) {
    atomic_set(&servo_active, on ? 1 : 0);
    if (on) {
        LOG_INF("Servo motor service activated.");
    } else {
        LOG_INF("Servo motor service deactivated.");
        uint32_t pulse1 = min_pulse1 + (uint32_t)(0.5 * (max_pulse1 - min_pulse1));
        uint32_t pulse2 = min_pulse2 + (uint32_t)(0.5 * (max_pulse2 - min_pulse2));
        LOG_INF("Setting servo motors to neutral position: pulse1=%u, pulse2=%u", pulse1, pulse2);
        int ret = pwm_set_pulse_dt(&motor1, pulse1);
        if (ret != 0) {
            LOG_ERR("Failed to set servo 1 pulse width to neutral: %d", ret);
        }
        ret = pwm_set_pulse_dt(&motor2, pulse2);
        if (ret != 0) {
            LOG_ERR("Failed to set servo 2 pulse width to neutral: %d", ret);
        }
    }
}

void servomotor_service_set_sensitivity(uint8_t level)
{
    if (level < SENS_MIN) {
        level = SENS_MIN;
    } else if (level > SENS_MAX) {
        level = SENS_MAX;
    }
    atomic_set(&sens_level, level);

    int rc = settings_save_one("cfg/sens", &level, sizeof(level));
    if (rc) {
        LOG_ERR("failed to persist sensitivity: %d", rc);
    } else {
        LOG_INF("sensitivity level set to %u", level);
    }
}

uint8_t servomotor_service_get_sensitivity(void)
{
    return (uint8_t)atomic_get(&sens_level);
}