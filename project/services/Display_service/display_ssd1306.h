#ifndef DISPLAY_SSD1306_H_
#define DISPLAY_SSD1306_H_

#include <stddef.h>

#include <zephyr/kernel.h>

#include "app_types.h"

/**
 * Bring up the display selected by the "zephyr,display" chosen node.
 *
 * Must be called before the first display_service_render().
 *
 * @return 0 on success, negative errno otherwise.
 */
int display_service_init(void);

/**
 * Draw the screen described by @p m. Pure function of the model: it decides
 * nothing, it only renders. Safe to call only from a single thread.
 * 
 * @return 0 on success, negative errno otherwise.
 */
int display_service_render(const struct ui_model *m);

#endif /* DISPLAY_SSD1306_H_ */