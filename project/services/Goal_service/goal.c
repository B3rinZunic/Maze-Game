#include <zephyr/drivers/adc.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "goal.h"

LOG_MODULE_REGISTER(goal, CONFIG_LOG_DEFAULT_LEVEL);

/*
 * Photoresistor in a voltage divider on ADC1_IN0 (PA0 / Arduino A0). The
 * /zephyr,user node in the board overlay names the channel.
 *
 * The marble stopping over the goal shadows the LDR, so the reading DROPS.
 * Absolute thresholds are useless here: a value that means "covered" in a dim
 * room means "uncovered" in a bright one. So detection is RELATIVE:
 *
 *   - a slow running average (EMA) of the uncovered reading is the live
 *     ambient "baseline" — it drifts with the sun/room light on its own;
 *   - the goal fires when the reading falls below GOAL_DROP_PCT % of that
 *     baseline (a proportional dip, so it scales with brightness);
 *   - once covered the baseline is frozen (the marble must not poison it) and
 *     it re-arms only after the reading climbs back over GOAL_CLEAR_PCT %.
 *
 * Noise is handled by averaging each poll and requiring several dips in a row.
 * This assumes covered => LOWER counts (matches the current wiring). If your
 * divider is the other way round, invert the compares.
 */
static const struct adc_dt_spec ldr = ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 0);

#define GOAL_SAMPLES_PER_READ  8      /* ADC conversions averaged per poll        */
#define GOAL_CONFIRM_SAMPLES   3      /* consecutive dips before firing           */

#define GOAL_DROP_PCT          60     /* fire when reading < baseline * this %    */
#define GOAL_CLEAR_PCT         80     /* re-arm when reading >= baseline * this %  */
#define GOAL_BASELINE_ALPHA    16     /* EMA: baseline moves ~1/ALPHA toward now   */

/*
 * A real LDR divider never pins to a rail: there is always series resistance,
 * so a healthy reading sits well inside [GOAL_FAULT_LOW, GOAL_FAULT_HIGH].
 * Outside that band means the sensor is disconnected / shorted — never a goal.
 */
#define GOAL_FAULT_LOW         32
#define GOAL_FAULT_HIGH        4063

static bool    covered;
static uint8_t below_streak;
static bool    fault_logged;
static int32_t baseline;        /* live ambient (uncovered) average, in counts */

int goal_service_init(void)
{
	if (!adc_is_ready_dt(&ldr)) {
		LOG_ERR("ADC %s not ready", ldr.dev->name);
		return -ENODEV;
	}

	int rc = adc_channel_setup_dt(&ldr);
	if (rc) {
		LOG_ERR("adc_channel_setup_dt: %d", rc);
		return rc;
	}

	LOG_INF("goal LDR ready on %s channel %u", ldr.dev->name, ldr.channel_id);
	return 0;
}

/* Average of GOAL_SAMPLES_PER_READ conversions, or -1 on ADC error. */
static int ldr_counts(void)
{
	int16_t buf[GOAL_SAMPLES_PER_READ];
	/* extra_samplings takes all GOAL_SAMPLES_PER_READ conversions in a single
	 * adc_read_dt() (1 + extra), instead of one driver call per sample. */
	const struct adc_sequence_options opts = {
		.extra_samplings = GOAL_SAMPLES_PER_READ - 1,
		.interval_us = 0,
	};
	struct adc_sequence seq = {
		.options = &opts,
		.buffer = buf,
		.buffer_size = sizeof(buf),
	};
	int32_t sum = 0;

	(void)adc_sequence_init_dt(&ldr, &seq);

	if (adc_read_dt(&ldr, &seq) != 0) {
		return -1;
	}

	for (int i = 0; i < GOAL_SAMPLES_PER_READ; i++) {
		sum += buf[i];
	}

	return (int)(sum / GOAL_SAMPLES_PER_READ);
}

void goal_service_arm(void)
{
	covered = false;
	below_streak = 0;
	fault_logged = false;

	/* Seed the baseline from the current (uncovered) light level so the very
	 * first run adapts to whatever the room is right now. Runs at run start,
	 * when the sensor is expected to be clear. */
	int c = ldr_counts();
	baseline = (c > 0) ? c : 2048;
	LOG_INF("goal armed, ambient baseline %d counts", baseline);
}

bool goal_service_triggered(void)
{
	int c = ldr_counts();
	if (c < 0) {
		return false;
	}

	/* Tuning aid: raise the "goal" log level to DBG to see this. */
	LOG_DBG("LDR %d counts, baseline %d (streak %u)", c, baseline, below_streak);

	/* Sensor unplugged / shorted: rail-pinned reading. Never a real goal. */
	if (c < GOAL_FAULT_LOW || c > GOAL_FAULT_HIGH) {
		if (!fault_logged) {
			LOG_WRN("LDR reads %d (rail-pinned) - check the PA0 divider wiring", c);
			fault_logged = true;
		}
		below_streak = 0;
		return false;
	}

	int32_t drop_level  = (baseline * GOAL_DROP_PCT) / 100;
	int32_t clear_level = (baseline * GOAL_CLEAR_PCT) / 100;

	/* Already covered: frozen baseline; wait for a clean uncover to re-arm. */
	if (covered) {
		if (c >= clear_level) {
			covered = false;
		}
		return false;
	}

	if (c <= drop_level) {
		/* A proportional dip below ambient — candidate goal. */
		if (++below_streak >= GOAL_CONFIRM_SAMPLES) {
			covered = true;
			below_streak = 0;
			LOG_INF("goal reached (LDR %d < %d, baseline %d)",
				c, drop_level, baseline);
			return true;
		}
	} else {
		below_streak = 0;
		/* Clearly uncovered: let the baseline track slow ambient drift. */
		baseline += (c - baseline) / GOAL_BASELINE_ALPHA;
	}

	return false;
}
