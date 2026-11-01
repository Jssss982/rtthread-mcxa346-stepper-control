#ifndef D36A_STEPPER_H
#define D36A_STEPPER_H

#include <rtthread.h>
#include <rtdevice.h>

typedef struct
{
    const char *name;
    rt_base_t step_pin;
    rt_base_t dir_pin;
    rt_base_t en_pin;
    rt_uint8_t en_active_level;
    rt_uint8_t positive_dir_level;
    rt_uint32_t min_hz;
    rt_uint32_t max_hz;
} d36a_stepper_config_t;

typedef struct
{
    d36a_stepper_config_t cfg;
    rt_uint32_t phase_milli;
    rt_bool_t pulse_high;
    rt_uint32_t total_pulses;
    rt_bool_t enabled;
    rt_bool_t output_active;
    rt_int8_t direction_sign;
    rt_uint32_t current_hz;
} d36a_stepper_t;

rt_err_t d36a_stepper_init(d36a_stepper_t *motor,
                           const d36a_stepper_config_t *config);
rt_err_t d36a_stepper_enable(d36a_stepper_t *motor, rt_bool_t enable);
rt_err_t d36a_stepper_set_direction(d36a_stepper_t *motor, rt_int8_t sign);
rt_err_t d36a_stepper_set_speed(d36a_stepper_t *motor, rt_int32_t signed_hz);
rt_err_t d36a_stepper_stop(d36a_stepper_t *motor);
/* Called by the control thread. Returns actual STEP rising edges (0 or 1).
 * A late call never emits a catch-up burst. High/low each last >= one tick. */
rt_uint32_t d36a_stepper_poll(d36a_stepper_t *motor, rt_uint32_t period_ms);

rt_bool_t d36a_stepper_is_enabled(const d36a_stepper_t *motor);
rt_bool_t d36a_stepper_is_running(const d36a_stepper_t *motor);
rt_uint32_t d36a_stepper_get_speed_hz(const d36a_stepper_t *motor);
rt_int8_t d36a_stepper_get_direction(const d36a_stepper_t *motor);
void d36a_stepper_invert_positive_dir(d36a_stepper_t *motor);

#endif /* D36A_STEPPER_H */
