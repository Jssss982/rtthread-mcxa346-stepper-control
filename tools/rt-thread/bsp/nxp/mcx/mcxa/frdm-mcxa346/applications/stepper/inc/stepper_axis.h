#ifndef STEPPER_AXIS_H
#define STEPPER_AXIS_H

#include <rtthread.h>
#include "d36a_stepper.h"
#include "ms42cg_encoder.h"

typedef enum
{
    STEPPER_AXIS_FAULT_NONE = 0,
    STEPPER_AXIS_FAULT_NO_ENCODER,
    STEPPER_AXIS_FAULT_DIRECTION,
    STEPPER_AXIS_FAULT_DRIVER,
    STEPPER_AXIS_FAULT_NOT_READY
} stepper_axis_fault_t;

typedef struct
{
    rt_int32_t current_count;
    rt_int32_t target_count;
    rt_int32_t error_count;
    float current_angle_deg;
    float target_angle_deg;
    rt_bool_t active;
    rt_bool_t reached;
    rt_bool_t enabled;
    rt_bool_t running;
    rt_uint32_t speed_hz;
    rt_uint32_t remaining_steps;
    rt_uint32_t total_pulses;
    stepper_axis_fault_t fault;
} stepper_axis_snapshot_t;

typedef struct
{
    d36a_stepper_t driver;
    ms42cg_encoder_t encoder;

    rt_bool_t initialized;
    rt_bool_t closed_loop_active;
    rt_bool_t reached;
    stepper_axis_fault_t fault;

    rt_int32_t target_count;
    rt_uint8_t settle_ticks;

    rt_bool_t burst_active;
    rt_uint32_t burst_steps_remaining;
    rt_uint32_t burst_hz;
    rt_uint32_t burst_accum_milli;
    rt_int8_t burst_sign;

    rt_bool_t feedback_pending;
    rt_int8_t expected_sign;
    rt_uint32_t check_steps;
    rt_uint32_t reverse_steps;
    rt_int32_t check_start_count;
} stepper_axis_t;

rt_err_t stepper_axis_default_init(stepper_axis_t *axis);
rt_err_t stepper_axis_enable(stepper_axis_t *axis, rt_bool_t enable);
rt_err_t stepper_axis_set_speed(stepper_axis_t *axis, rt_int32_t signed_hz);
rt_err_t stepper_axis_steps(stepper_axis_t *axis, rt_int32_t signed_steps,
                           rt_uint32_t hz);
rt_err_t stepper_axis_goto_deg(stepper_axis_t *axis, float target_deg);
rt_err_t stepper_axis_move_deg(stepper_axis_t *axis, float delta_deg);
void stepper_axis_zero(stepper_axis_t *axis);
void stepper_axis_stop(stepper_axis_t *axis);
void stepper_axis_clear_fault(stepper_axis_t *axis);
void stepper_axis_control_tick(stepper_axis_t *axis, rt_uint32_t period_ms);
void stepper_axis_get_snapshot(stepper_axis_t *axis,
                               stepper_axis_snapshot_t *snapshot);
void stepper_axis_invert_driver_dir(stepper_axis_t *axis);
void stepper_axis_invert_encoder_dir(stepper_axis_t *axis);
const char *stepper_axis_fault_name(stepper_axis_fault_t fault);

#endif /* STEPPER_AXIS_H */
