#ifndef SCORE_H_
#define SCORE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app_types.h"

/* Bring up the settings/NVS backend and load the saved table. */
int score_service_init(void);

/* True if @time_ms would land inside the top SCORE_MAX. */
bool score_service_qualifies(uint32_t time_ms);

/*
 * Insert {m->name, m->result_ms} into the table and persist it.
 * Returns the 0-based rank it landed at (0 = best), or -1 if it did not
 * place or the save failed.
 */
int score_service_save(const struct ui_model *m);

/* Read-only view of the current table, best first, for the results screen. */
const struct score_entry *score_service_table(size_t *count);

/* Wipe the high-score table from RAM and flash. Returns 0 on success. */
int score_service_reset(void);

#endif /* SCORE_H_ */
