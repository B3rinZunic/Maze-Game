#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/display/cfb.h>
#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <stdio.h>

#include "display_ssd1306.h"

LOG_MODULE_REGISTER(display_ssd1306, CONFIG_LOG_DEFAULT_LEVEL);

/* Longest line we build plus the terminator. */
#define LINE_BUF_LEN 20
/* 128x64 with the smallest CFB font (10x16) is 4 rows.
 * Keep in sync with SCORES_VISIBLE_ROWS in game.c. */
#define MAX_ROWS     4
/* The CFB default font set never holds more than this many entries. */
#define MAX_FONTS    8

static const struct device *const display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

static uint8_t text_rows;
static uint8_t font_height;

/* The CFB state is a single shared resource; serialise every draw through it. */
static K_MUTEX_DEFINE(draw_lock);

/*
 * Pick the smallest of the built-in fonts so that as many rows as possible fit
 * on the panel. The default CFB font set is 10x16, 15x24 and 20x32, which gives
 * four rows on a 128x64 panel.
 */
static int select_smallest_font(void)
{
	uint8_t best_idx = 0;
	uint8_t best_height = UINT8_MAX;
	uint8_t width;
	uint8_t height;
	bool found = false;

	for (uint8_t idx = 0; idx < MAX_FONTS; idx++) {
		if (cfb_get_font_size(display_dev, idx, &width, &height) != 0) {
			break;
		}

		if (height < best_height) {
			best_height = height;
			best_idx = idx;
			found = true;
		}
	}

	if (!found) {
		LOG_ERR("No CFB fonts available");
		return -ENOENT;
	}

	font_height = best_height;

	return cfb_framebuffer_set_font(display_dev, best_idx);
}

int display_service_init(void)
{
	int ret;

	if (!device_is_ready(display_dev)) {
		LOG_ERR("Display %s is not ready", display_dev->name);
		return -ENODEV;
	}

	if (display_set_pixel_format(display_dev, PIXEL_FORMAT_MONO10) != 0 &&
	    display_set_pixel_format(display_dev, PIXEL_FORMAT_MONO01) != 0) {
		LOG_ERR("Failed to set a monochrome pixel format");
		return -ENOTSUP;
	}

	ret = cfb_framebuffer_init(display_dev);
	if (ret < 0) {
		LOG_ERR("Framebuffer init failed: %d", ret);
		return ret;
	}

	ret = select_smallest_font();
	if (ret < 0) {
		return ret;
	}

	ret = cfb_framebuffer_clear(display_dev, true);
	if (ret < 0) {
		LOG_ERR("Framebuffer clear failed: %d", ret);
		return ret;
	}

	ret = display_blanking_off(display_dev);
	if (ret < 0 && ret != -ENOSYS) {
		LOG_ERR("display_blanking_off failed: %d", ret);
		return ret;
	}

	text_rows = cfb_get_display_parameter(display_dev, CFB_DISPLAY_ROWS);
	if (text_rows > MAX_ROWS) {
		text_rows = MAX_ROWS;
	}

	LOG_INF("Display %s ready: %ux%u, %u text rows", display_dev->name,
		cfb_get_display_parameter(display_dev, CFB_DISPLAY_WIDTH),
		cfb_get_display_parameter(display_dev, CFB_DISPLAY_HEIGHT), text_rows);

	return 0;
}

/* Draw up to text_rows strings and push the framebuffer to the panel. */
static int draw_lines(const char *const lines[MAX_ROWS])
{
	int ret;

	k_mutex_lock(&draw_lock, K_FOREVER);

	ret = cfb_framebuffer_clear(display_dev, false);
	if (ret < 0) {
		LOG_ERR("Framebuffer clear failed: %d", ret);
		goto out;
	}

	for (size_t row = 0; row < text_rows; row++) {
		if (lines[row] == NULL) {
			continue;
		}

		ret = cfb_print(display_dev, lines[row], 0, (int16_t)(row * font_height));
		if (ret < 0) {
			LOG_ERR("cfb_print row %u failed: %d", (unsigned int)row, ret);
			goto out;
		}
	}

	ret = cfb_framebuffer_finalize(display_dev);
	if (ret < 0) {
		LOG_ERR("Framebuffer finalize failed: %d", ret);
	}

out:
	k_mutex_unlock(&draw_lock);
	return ret;
}

/* Format milliseconds as "S.CCs" (centiseconds) without pulling in %f. */
static void fmt_time(char *buf, size_t len, uint32_t ms)
{
	snprintf(buf, len, "%u.%02us", ms / 1000U, (ms % 1000U) / 10U);
}

/*
 * Each screen is a pure function of the model. Buffers are static because the
 * only caller is the single game thread — no re-entrancy.
 */

static void render_home(const struct ui_model *m)
{
	static const char *const labels[MENU_COUNT] = {
		[MENU_START]    = "Start Game",
		[MENU_RESULTS]  = "Results",
		[MENU_SETTINGS] = "Settings",
	};
	static char buf[MENU_COUNT][LINE_BUF_LEN];
	const char *lines[MAX_ROWS] = { "MAZE GAME", buf[0], buf[1], buf[2] };

	for (int i = 0; i < MENU_COUNT; i++) {
		snprintf(buf[i], LINE_BUF_LEN, "%s%s",
			 (i == m->menu_sel) ? "> " : "  ", labels[i]);
	}
	draw_lines(lines);
}

static void render_scores(const struct ui_model *m)
{
	static char buf[MAX_ROWS][LINE_BUF_LEN];
	const char *lines[MAX_ROWS];

	for (uint8_t r = 0; r < MAX_ROWS; r++) {
		size_t i = (size_t)m->scroll_top + r;   /* logical row: 0 = Back */
		const char *mark = (i == m->list_sel) ? "> " : "  ";

		if (i == 0) {
			snprintf(buf[r], LINE_BUF_LEN, "%s< Back", mark);
			lines[r] = buf[r];
		} else if (m->scores != NULL && i - 1 < m->score_count) {
			char t[LINE_BUF_LEN];
			const struct score_entry *e = &m->scores[i - 1];

			fmt_time(t, sizeof(t), e->time_ms);
			snprintf(buf[r], LINE_BUF_LEN, "%s%s %s", mark, e->name, t);
			lines[r] = buf[r];
		} else if (i == 1 && m->score_count == 0) {
			snprintf(buf[r], LINE_BUF_LEN, "  (no scores)");
			lines[r] = buf[r];
		} else {
			lines[r] = NULL;
		}
	}
	draw_lines(lines);
}

static void render_settings(const struct ui_model *m)
{
	static char buf[SET_COUNT][LINE_BUF_LEN];
	const char *lines[MAX_ROWS] = { "SETTINGS", buf[0], buf[1], buf[2] };

	for (int i = 0; i < SET_COUNT; i++) {
		const char *mark = (i == m->set_sel) ? "> " : "  ";

		switch (i) {
		case SET_SENSITIVITY:
			if (m->set_sel == SET_SENSITIVITY && m->set_editing) {
				snprintf(buf[i], LINE_BUF_LEN, "> Sens <%u>", m->sensitivity);
			} else {
				snprintf(buf[i], LINE_BUF_LEN, "%sSens: %u", mark, m->sensitivity);
			}
			break;
		case SET_RESET:
			snprintf(buf[i], LINE_BUF_LEN, "%sReset scores", mark);
			break;
		case SET_BACK:
			snprintf(buf[i], LINE_BUF_LEN, "%sBack", mark);
			break;
		}
	}
	draw_lines(lines);
}

static void render_confirm(const struct ui_model *m)
{
	ARG_UNUSED(m);
	static const char *const lines[MAX_ROWS] = {
		"Reset scores?", "OK = yes", "PREV/NEXT = no", NULL,
	};
	draw_lines(lines);
}

static void render_playing(const struct ui_model *m)
{
	static char buf[LINE_BUF_LEN];
	const char *lines[MAX_ROWS] = { "TIME", buf, "tilt to roll", NULL };

	fmt_time(buf, sizeof(buf), m->elapsed_ms);
	draw_lines(lines);
}

static void render_ask_save(const struct ui_model *m)
{
	static char buf[LINE_BUF_LEN];
	const char *lines[MAX_ROWS] = { "TOP SCORE!", buf, "OK=save", "PREV/NEXT=skip" };

	fmt_time(buf, sizeof(buf), m->result_ms);
	draw_lines(lines);
}

static void render_name_entry(const struct ui_model *m)
{
	static char buf[LINE_BUF_LEN];
	const char *lines[MAX_ROWS] = { "ENTER NAME", buf, "PREV/NEXT  OK", NULL };

	/* wrap the active character in >x< so it is obvious which one edits */
	int p = 0;
	for (uint8_t i = 0; i < SCORE_NAME_LEN && p < (int)sizeof(buf) - 4; i++) {
		char l = (i == m->cursor) ? '>' : ' ';
		char r = (i == m->cursor) ? '<' : ' ';
		buf[p++] = l;
		buf[p++] = m->name[i];
		buf[p++] = r;
	}
	buf[p] = '\0';
	draw_lines(lines);
}

static void render_result(const struct ui_model *m)
{
	static char time_buf[LINE_BUF_LEN];
	static char rank_buf[LINE_BUF_LEN];
	static char best_buf[LINE_BUF_LEN];
	const char *lines[MAX_ROWS] = { time_buf, rank_buf, best_buf, "any key = home" };

	fmt_time(time_buf, sizeof(time_buf), m->result_ms);

	if (m->rank > 0) {
		snprintf(rank_buf, sizeof(rank_buf), "NEW #%d", m->rank);
	} else {
		snprintf(rank_buf, sizeof(rank_buf), "not placed");
	}

	if (m->scores != NULL && m->score_count > 0) {
		char t[LINE_BUF_LEN];

		fmt_time(t, sizeof(t), m->scores[0].time_ms);
		snprintf(best_buf, sizeof(best_buf), "best %s %s", m->scores[0].name, t);
	} else {
		best_buf[0] = '\0';
	}

	draw_lines(lines);
}

int display_service_render(const struct ui_model *m)
{
	if (m == NULL) {
		return -EINVAL;
	}

	switch (m->state) {
	case GS_HOME:       render_home(m);       break;
	case GS_SCORES:     render_scores(m);     break;
	case GS_SETTINGS:   render_settings(m);   break;
	case GS_CONFIRM:    render_confirm(m);    break;
	case GS_PLAYING:    render_playing(m);    break;
	case GS_ASK_SAVE:   render_ask_save(m);   break;
	case GS_NAME_ENTRY: render_name_entry(m); break;
	case GS_RESULT:     render_result(m);     break;
	default:
		LOG_ERR("Unknown game state: %d", m->state);
		return -EINVAL;
	}

	return 0;
}