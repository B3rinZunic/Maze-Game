#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>

#include "game.h"
#include "app_types.h"
#include "display_ssd1306.h"
#include "servomotor.h"
#include "score.h"
#include "goal.h"

LOG_MODULE_REGISTER(game, CONFIG_LOG_DEFAULT_LEVEL);

#define GAME_THREAD_STACK_SIZE 4096   /* state machine + ADC poll + CFB/I2C render */
#define GAME_THREAD_PRIO       K_PRIO_PREEMPT(7)   /* below sensor(5) and motor(6) */
#define TICK_MS                100                 /* on-screen clock refresh rate  */
#define LONG_PRESS_MS          500

/* GS_SCORES viewport height. Keep equal to the display's MAX_ROWS. */
#define SCORES_VISIBLE_ROWS    4

#define CODE_PREV  INPUT_BTN_0
#define CODE_NEXT  INPUT_BTN_1
#define CODE_OK    INPUT_BTN_2
#define CODE_GOAL  INPUT_BTN_3

K_MSGQ_DEFINE(game_event_msgq, sizeof(struct game_event), 10, 4);

/* ================= input subsystem -> game events ================= */
/*
 * gpio-keys emits value==1 on press and value==0 on release for each code.
 * For the three buttons we time press->release, so one button yields two
 * verbs (short = scroll, long = confirm/abort). The goal sensor only means
 * "the marble arrived", so it posts on the press edge.
 *
 * Runs in the input subsystem's thread (CONFIG_INPUT_MODE_THREAD), not an
 * ISR, so k_msgq_put()/k_uptime_get()/LOG are all fine here.
 */
static int64_t press_ts[3];   /* CODE_PREV/NEXT/OK, touched only by this thread */

static void on_input(struct input_event *evt, void *user_data)
{
	ARG_UNUSED(user_data);

	if (evt->type != INPUT_EV_KEY) {
		return;
	}

	struct game_event ge = { 0 };
	int idx;

	switch (evt->code) {
	case CODE_GOAL:
		if (evt->value) {
			ge.type = EVT_GOAL;
			(void)k_msgq_put(&game_event_msgq, &ge, K_NO_WAIT);
		}
		return;
	case CODE_PREV: idx = 0; break;
	case CODE_NEXT: idx = 1; break;
	case CODE_OK:   idx = 2; break;
	default:
		return;
	}

	if (evt->value) {                    
		press_ts[idx] = k_uptime_get();
		return;
	}

	ge.long_press = (k_uptime_get() - press_ts[idx]) >= LONG_PRESS_MS;
	ge.type = (idx == 0) ? EVT_BTN_PREV : (idx == 1) ? EVT_BTN_NEXT : EVT_BTN_OK;
	(void)k_msgq_put(&game_event_msgq, &ge, K_NO_WAIT);
}
INPUT_CALLBACK_DEFINE(NULL, on_input, NULL);

/* ================= state machine ================= */
static K_THREAD_STACK_DEFINE(game_stack, GAME_THREAD_STACK_SIZE);
static struct k_thread game_thread;

static char letter_next(char c) { return (c >= 'Z') ? 'A' : (char)(c + 1); }
static char letter_prev(char c) { return (c <= 'A') ? 'Z' : (char)(c - 1); }

/* Begin a run from the main menu. */
static void start_run(struct ui_model *m, int64_t *run_start)
{
	*run_start = k_uptime_get();
	m->elapsed_ms = 0;
	m->state = GS_PLAYING;
	goal_service_arm();
	servomotor_service_set_active(true);
}

/* End the current run: latch the time, park the servos, pick the next screen.
 * Shared by the goal poll and a (possible) digital EVT_GOAL. */
static void finish_run(struct ui_model *m, int64_t run_start)
{
	m->result_ms = (uint32_t)(k_uptime_get() - run_start);
	servomotor_service_set_active(false);

	if (score_service_qualifies(m->result_ms)) {
		strcpy(m->name, "AAA");
		m->cursor = 0;
		m->state = GS_ASK_SAVE;
	} else {
		m->rank = -1;
		m->state = GS_RESULT;
	}
}

/* Re-clamp the GS_SCORES viewport so list_sel stays on screen. */
static void scores_follow_cursor(struct ui_model *m)
{
	if (m->list_sel < m->scroll_top) {
		m->scroll_top = m->list_sel;
	} else if (m->list_sel >= m->scroll_top + SCORES_VISIBLE_ROWS) {
		m->scroll_top = (uint8_t)(m->list_sel - SCORES_VISIBLE_ROWS + 1);
	}
}

static void game_thread_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	struct ui_model model = {
		.state    = GS_HOME,
		.menu_sel = MENU_START,
		.name     = "AAA",
		.rank     = -1,
	};
	int64_t run_start = 0;
	bool dirty = true;                              

	while (true) {
		struct game_event ev;
		bool have_ev = k_msgq_get(&game_event_msgq, &ev, K_MSEC(TICK_MS)) == 0;

		if (have_ev) {
			switch (model.state) {
			case GS_HOME:
				if (ev.type == EVT_BTN_PREV) {
					model.menu_sel = (model.menu_sel + MENU_COUNT - 1) % MENU_COUNT;
					dirty = true;
				} else if (ev.type == EVT_BTN_NEXT) {
					model.menu_sel = (model.menu_sel + 1) % MENU_COUNT;
					dirty = true;
				} else if (ev.type == EVT_BTN_OK) {
					switch (model.menu_sel) {
					case MENU_START:
						start_run(&model, &run_start);
						break;
					case MENU_RESULTS:
						model.list_sel = 0;
						model.scroll_top = 0;
						model.state = GS_SCORES;
						break;
					case MENU_SETTINGS:
						model.set_sel = SET_SENSITIVITY;
						model.set_editing = false;
						model.sensitivity = servomotor_service_get_sensitivity();
						model.state = GS_SETTINGS;
						break;
					default:
						break;
					}
					dirty = true;
				}
				break;

			case GS_SCORES:
				if (ev.type == EVT_BTN_PREV) {
					if (model.list_sel > 0) {
						model.list_sel--;
						scores_follow_cursor(&model);
						dirty = true;
					}
				} else if (ev.type == EVT_BTN_NEXT) {
					if (model.list_sel < model.score_count) {
						model.list_sel++;
						scores_follow_cursor(&model);
						dirty = true;
					}
				} else if (ev.type == EVT_BTN_OK) {
					if (model.list_sel == 0) {          /* "< Back" row */
						model.menu_sel = MENU_RESULTS;
						model.state = GS_HOME;
						dirty = true;
					}
					/* OK on a score row: nothing yet */
				}
				break;

			case GS_SETTINGS:
				if (model.set_editing) {
					/* editing the sensitivity value */
					if (ev.type == EVT_BTN_PREV && model.sensitivity > SENS_MIN) {
						model.sensitivity--;
						servomotor_service_set_sensitivity(model.sensitivity);
						dirty = true;
					} else if (ev.type == EVT_BTN_NEXT && model.sensitivity < SENS_MAX) {
						model.sensitivity++;
						servomotor_service_set_sensitivity(model.sensitivity);
						dirty = true;
					} else if (ev.type == EVT_BTN_OK) {
						model.set_editing = false;   /* done editing */
						dirty = true;
					}
				} else {
					/* navigating the settings sub-menu */
					if (ev.type == EVT_BTN_PREV) {
						model.set_sel = (model.set_sel + SET_COUNT - 1) % SET_COUNT;
						dirty = true;
					} else if (ev.type == EVT_BTN_NEXT) {
						model.set_sel = (model.set_sel + 1) % SET_COUNT;
						dirty = true;
					} else if (ev.type == EVT_BTN_OK) {
						switch (model.set_sel) {
						case SET_SENSITIVITY:
							model.set_editing = true;
							break;
						case SET_RESET:
							model.state = GS_CONFIRM;
							break;
						case SET_BACK:
							model.menu_sel = MENU_SETTINGS;
							model.state = GS_HOME;
							break;
						default:
							break;
						}
						dirty = true;
					}
				}
				break;

			case GS_CONFIRM:
				if (ev.type == EVT_BTN_OK) {                /* yes, wipe scores */
					score_service_reset();
					model.set_sel = SET_RESET;
					model.state = GS_SETTINGS;
					dirty = true;
				} else if (ev.type == EVT_BTN_PREV || ev.type == EVT_BTN_NEXT) {
					model.set_sel = SET_RESET;         /* no, back to settings */
					model.state = GS_SETTINGS;
					dirty = true;
				}
				break;

			case GS_PLAYING:
				if (ev.type == EVT_GOAL) {
					finish_run(&model, run_start);
					dirty = true;
				} else if (ev.type == EVT_BTN_OK && ev.long_press) {
					servomotor_service_set_active(false);
					model.menu_sel = MENU_START;
					model.state = GS_HOME;          /* abort the run */
					dirty = true;
				}
				break;

			case GS_ASK_SAVE:
				if (ev.type == EVT_BTN_OK) {            /* yes, enter a name */
					model.state = GS_NAME_ENTRY;
					dirty = true;
				} else if (ev.type == EVT_BTN_PREV || ev.type == EVT_BTN_NEXT) {
					model.rank = -1;               /* no, skip saving */
					model.state = GS_RESULT;
					dirty = true;
				}
				break;

			case GS_NAME_ENTRY:
				if (ev.type == EVT_BTN_PREV) {
					model.name[model.cursor] = letter_prev(model.name[model.cursor]);
					dirty = true;
				} else if (ev.type == EVT_BTN_NEXT) {
					model.name[model.cursor] = letter_next(model.name[model.cursor]);
					dirty = true;
				} else if (ev.type == EVT_BTN_OK) {
					if (++model.cursor >= SCORE_NAME_LEN) {
						int r = score_service_save(&model);
						model.rank = (r >= 0) ? (int8_t)(r + 1) : -1;
						model.state = GS_RESULT;
					}
					dirty = true;
				}
				break;

			case GS_RESULT:
				model.menu_sel = MENU_START;
				model.rank = -1;
				model.state = GS_HOME;                 /* any button -> menu */
				dirty = true;
				break;
			}
		}

		/* While playing, the clock advances and the goal is polled even
		 * when no button event arrived (that is what the TICK_MS timeout
		 * on k_msgq_get is for). */
		if (model.state == GS_PLAYING) {
			model.elapsed_ms = (uint32_t)(k_uptime_get() - run_start);
			if (goal_service_triggered()) {
				finish_run(&model, run_start);
			}
			dirty = true;
		}

		if (dirty) {
			model.scores = score_service_table(&model.score_count);
			(void)display_service_render(&model);
			dirty = false;
		}
	}
}

int game_service_start(void)
{
	k_thread_create(&game_thread, game_stack, K_THREAD_STACK_SIZEOF(game_stack),
			game_thread_fn, NULL, NULL, NULL, GAME_THREAD_PRIO, 0, K_NO_WAIT);
	(void)k_thread_name_set(&game_thread, "game");
	return 0;
}