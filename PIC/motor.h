#ifndef MOTOR_H
#define MOTOR_H

#include <stdbool.h>

void motor_init(void);
void motor_isr(void);
void motor_set_direction(bool forward);
void motor_start_with_step_hz(float step_hz);
void motor_start_for_flow_ul_min(float flow_ul_min);
void motor_stop(void);

#endif /* MOTOR_H */
