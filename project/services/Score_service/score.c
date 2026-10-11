#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include "score.h"

LOG_MODULE_REGISTER(score, CONFIG_LOG_DEFAULT_LEVEL);

static struct score_entry table[SCORE_MAX];
static size_t table_count;

/* ---- settings backend ---- */

static int scores_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	if (!settings_name_steq(name, "scores", NULL)) {
		return -ENOENT;
	}

	if (len > sizeof(table)) {
		len = sizeof(table);
	}

	ssize_t rc = read_cb(cb_arg, table, len);
	if (rc < 0) {
		LOG_ERR("scores read: %d", (int)rc);
		return (int)rc;
	}

	table_count = (size_t)rc / sizeof(struct score_entry);
	if (table_count > SCORE_MAX) {
		table_count = SCORE_MAX;
	}
	return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(maze, "maze", NULL, scores_set, NULL, NULL);

/* ---- public API ---- */

int score_service_init(void)
{
	int rc = settings_subsys_init();
	if (rc) {
		LOG_ERR("settings_subsys_init: %d", rc);
		return rc;
	}

	rc = settings_load_subtree("maze");
	if (rc) {
		/* first boot: nothing saved yet, not fatal */
		LOG_WRN("settings_load_subtree: %d", rc);
	}

	LOG_INF("scoreboard ready, %u entr%s", (unsigned int)table_count,
		table_count == 1 ? "y" : "ies");
	return 0;
}

bool score_service_qualifies(uint32_t time_ms)
{
	if (table_count < SCORE_MAX) {
		return true;
	}
	return time_ms < table[SCORE_MAX - 1].time_ms;
}

int score_service_save(const struct ui_model *m)
{
	if (!score_service_qualifies(m->result_ms)) {
		return -1;
	}

	struct score_entry e;
	memcpy(e.name, m->name, SCORE_NAME_LEN);
	e.name[SCORE_NAME_LEN] = '\0';
	e.time_ms = m->result_ms;

	/* insertion sort, ascending time */
	size_t pos = (table_count < SCORE_MAX) ? table_count : (SCORE_MAX - 1);
	if (table_count < SCORE_MAX) {
		table_count++;
	}
	while (pos > 0 && table[pos - 1].time_ms > e.time_ms) {
		table[pos] = table[pos - 1];
		pos--;
	}
	table[pos] = e;

	int rc = settings_save_one("maze/scores", table,
				  table_count * sizeof(struct score_entry));
	if (rc) {
		LOG_ERR("settings_save_one: %d", rc);
		return rc;
	}

	LOG_INF("saved %s %u ms at #%u", e.name, e.time_ms, (unsigned int)pos + 1);
	return (int)pos;
}

const struct score_entry *score_service_table(size_t *count)
{
	if (count) {
		*count = table_count;
	}
	return table;
}

int score_service_reset(void)
{
	memset(table, 0, sizeof(table));
	table_count = 0;

	int rc = settings_delete("maze/scores");
	if (rc) {
		LOG_ERR("settings_delete: %d", rc);
		return rc;
	}

	LOG_INF("scoreboard cleared");
	return 0;
}
