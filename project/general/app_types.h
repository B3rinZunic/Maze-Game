#ifndef APP_TYPES_H_
#define APP_TYPES_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Raw IMU reading, shared sensor -> servo */
struct imu_sample {
	double accel[3];
};

/* High-score table */
#define SCORE_NAME_LEN 3
#define SCORE_MAX      5

struct score_entry {
	char     name[SCORE_NAME_LEN + 1];
	uint32_t time_ms;
};

/* Servo tilt sensitivity, chosen in Settings. Higher level = a smaller tilt
 * reaches full servo deflection. Owned and persisted by the servo service. */
#define SENS_MIN     1
#define SENS_MAX     10
#define SENS_DEFAULT 5

/* The screen being shown == the game's current state. */
enum game_state {
	GS_HOME,       
	GS_SCORES,     
	GS_SETTINGS,    
	GS_CONFIRM,     
	GS_PLAYING,
	GS_ASK_SAVE,
	GS_NAME_ENTRY,
	GS_RESULT,
};

/* Rows of the GS_HOME menu, in display order. */
enum menu_item {
	MENU_START,
	MENU_RESULTS,
	MENU_SETTINGS,
	MENU_COUNT,
};

/* Rows of the GS_SETTINGS sub-menu, in display order. */
enum settings_item {
	SET_SENSITIVITY,
	SET_RESET,
	SET_BACK,
	SET_COUNT,
};

/* Verbs the game understands, produced by the input layer. */
enum game_evt_type {
	EVT_BTN_PREV,
	EVT_BTN_NEXT,
	EVT_BTN_OK,
	EVT_GOAL,
};

struct game_event {
	enum game_evt_type type;
	bool long_press;  
};

/* Model for the UI state */
struct ui_model {
	enum game_state state;

	uint8_t  menu_sel;                   /* GS_HOME: highlighted enum menu_item */

	uint8_t  set_sel;                    /* GS_SETTINGS: highlighted enum settings_item */
	bool     set_editing;                /* GS_SETTINGS: adjusting the sensitivity value */
	uint8_t  sensitivity;                /* GS_SETTINGS: servo level for display, 1..10  */

	uint8_t  list_sel;                   /* GS_SCORES: cursor, 0 = Back, 1..N = score */
	uint8_t  scroll_top;                 /* GS_SCORES: first visible list index */

	uint32_t elapsed_ms;                 /* GS_PLAYING: live run time         */
	uint32_t result_ms;                  /* GS_ASK_SAVE/NAME/RESULT: final run */

	char     name[SCORE_NAME_LEN + 1];   /* GS_NAME_ENTRY: chars being edited  */
	uint8_t  cursor;                     /* GS_NAME_ENTRY: active index 0..2   */

	int8_t   rank;                       /* GS_RESULT: 1-based place, or -1    */
	const struct score_entry *scores;    /* GS_RESULT/GS_SCORES: table snapshot */
	size_t   score_count;
};

#endif /* APP_TYPES_H_ */
