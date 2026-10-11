#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "app_types.h"
#include "display_ssd1306.h"
#include "sensor_mpu6050.h"
#include "servomotor.h"
#include "score.h"
#include "goal.h"
#include "game.h"

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

/* Sensor -> servo. A couple of samples of slack so a slow servo write never
 * stalls sampling. The game service does not sit on this path; it only gates
 * the servo with servomotor_service_set_active(). */
K_MSGQ_DEFINE(motor_msgq, sizeof(struct imu_sample), 4, 4);

int main(void)
{
	int ret;

	ret = sensor_service_init();
	if (ret < 0) {
		LOG_ERR("Sensor init failed: %d", ret);
		return ret;
	}

	ret = display_service_init();
	if (ret < 0) {
		LOG_ERR("Display init failed: %d", ret);
		return ret;
	}

	ret = servomotor_service_init();
	if (ret < 0) {
		LOG_ERR("Servo Motor init failed: %d", ret);
		return ret;
	}

	ret = score_service_init();
	if (ret < 0) {
		LOG_ERR("Score init failed: %d", ret);
		return ret;
	}

	/* Non-fatal: without the goal sensor a run can still be ended with a
	 * long press on OK, so let the rest of the game come up. */
	ret = goal_service_init();
	if (ret < 0) {
		LOG_WRN("Goal sensor init failed: %d (long-press OK to end a run)", ret);
	}

	ret = sensor_service_start(&motor_msgq);
	if (ret < 0) {
		LOG_ERR("Sensor service start failed: %d", ret);
		return ret;
	}

	ret = servomotor_service_start(&motor_msgq);
	if (ret < 0) {
		LOG_ERR("Servo Motor service start failed: %d", ret);
		return ret;
	}

	ret = game_service_start();
	if (ret < 0) {
		LOG_ERR("Game service start failed: %d", ret);
		return ret;
	}

	LOG_INF("Maze game running");
	return 0;
}
