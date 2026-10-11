#ifndef GOAL_H_
#define GOAL_H_

#include <stdbool.h>

/* Bring up the photoresistor ADC channel. Call before the first poll. */
int goal_service_init(void);

/* Re-arm edge detection. Call when a run starts so a marble already sitting
 * on the sensor from the previous run does not count. */
void goal_service_arm(void);

/*
 * Sample the photoresistor. Returns true exactly once, on the transition
 * into "covered" (marble over the sensor). Poll this while a run is active.
 */
bool goal_service_triggered(void);

#endif /* GOAL_H_ */
