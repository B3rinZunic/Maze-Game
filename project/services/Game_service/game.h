#ifndef GAME_H_
#define GAME_H_

/*
 * Spawn the state-machine thread. Buttons and the goal sensor arrive through
 * the input subsystem, frames go straight to display_service_render(), so
 * there is nothing to wire in here.
 */
int game_service_start(void);

#endif /* GAME_H_ */
