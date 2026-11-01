#include "stepper_axis.h"
#include "stepper_board.h"

static rt_uint32_t axis_abs_i32(rt_int32_t value)
{
    return (value >= 0) ?
           (rt_uint32_t)value :
           ((rt_uint32_t)(-(value + 1)) + 1U);
}

static rt_int32_t axis_angle_to_count(float angle_deg)
{
    float scaled = angle_deg * (float)STEPPER_MS42CG_AB_CPR / 360.0f;

    if (scaled >= 0.0f)
    {
        return (rt_int32_t)(scaled + 0.5f);
    }

    return (rt_int32_t)(scaled - 0.5f);
}

static float axis_count_to_angle(rt_int32_t count)
{
    return ((float)count * 360.0f) / (float)STEPPER_MS42CG_AB_CPR;
}

static rt_uint32_t axis_select_frequency(rt_uint32_t error_counts)
{
    if (error_counts > 50U) return 200U;
    if (error_counts > 15U) return 100U;
    return 50U;
}

static rt_uint32_t axis_error_to_steps(rt_uint32_t error_counts)
{
    rt_uint64_t numerator;
    rt_uint32_t steps;
    rt_uint32_t max_burst = STEPPER_CL_MAX_BURST_STEPS;

    numerator = (rt_uint64_t)error_counts * (rt_uint64_t)STEPPER_PULSES_PER_REV;
    steps = (rt_uint32_t)((numerator + STEPPER_MS42CG_AB_CPR - 1U) /
                          STEPPER_MS42CG_AB_CPR);

    if (steps == 0U)
    {
        steps = 1U;
    }
    if (max_burst == 0U)
    {
        max_burst = 1U;
    }
    if (steps > max_burst)
    {
        steps = max_burst;
    }

    return steps;
}

static void axis_set_fault(stepper_axis_t *axis, stepper_axis_fault_t fault)
{
    axis->fault = fault;
    axis->closed_loop_active = RT_FALSE;
    axis->reached = RT_FALSE;
    axis->feedback_pending = RT_FALSE;
    axis->check_steps = 0U;
    axis->reverse_steps = 0U;
    axis->burst_active = RT_FALSE;
    axis->burst_steps_remaining = 0U;
    if (axis->initialized) d36a_stepper_enable(&axis->driver, RT_FALSE);
}

static rt_err_t axis_start_burst(stepper_axis_t *axis,
                                 rt_uint32_t steps,
                                 rt_uint32_t hz,
                                 rt_int8_t sign)
{
    rt_err_t err;
    rt_int32_t signed_hz;

    if ((steps == 0U) || (hz == 0U))
    {
        return RT_EOK;
    }

    signed_hz = (sign < 0) ? -(rt_int32_t)hz : (rt_int32_t)hz;
    err = d36a_stepper_set_speed(&axis->driver, signed_hz);
    if (err != RT_EOK)
    {
        axis_set_fault(axis, STEPPER_AXIS_FAULT_DRIVER);
        return err;
    }

    axis->burst_active = RT_TRUE;
    axis->burst_steps_remaining = steps;
    axis->burst_hz = hz;
    axis->burst_sign = (sign < 0) ? -1 : 1;
    axis->burst_accum_milli = 0U;

    return RT_EOK;
}

static void axis_update_burst(stepper_axis_t *axis, rt_uint32_t period_ms)
{
    rt_uint32_t emitted;

    if (axis->burst_active == RT_FALSE)
    {
        return;
    }

    if (axis->burst_steps_remaining == 0U)
    {
        axis->burst_active = RT_FALSE;
        d36a_stepper_stop(&axis->driver);
        if (!axis->closed_loop_active) axis->reached = RT_TRUE;
        return;
    }
    emitted = d36a_stepper_poll(&axis->driver, period_ms);
    if (emitted == 0U)
    {
        return;
    }

    if (emitted >= axis->burst_steps_remaining)
    {
        axis->burst_steps_remaining = 0U;
        return;
    }

    axis->burst_steps_remaining -= emitted;
}

static void axis_check_feedback(stepper_axis_t *axis, rt_int32_t current_count)
{
    rt_int32_t movement;

    if (axis->feedback_pending == RT_FALSE)
    {
        return;
    }

    axis->feedback_pending = RT_FALSE;
    movement = current_count - axis->check_start_count;

    if (((axis->expected_sign > 0) &&
         (movement >= STEPPER_CL_MOVE_CONFIRM_COUNTS)) ||
        ((axis->expected_sign < 0) &&
         (movement <= -STEPPER_CL_MOVE_CONFIRM_COUNTS)))
    {
        axis->check_steps = 0U;
        axis->reverse_steps = 0U;
        return;
    }

    if (((axis->expected_sign > 0) &&
         (movement <= -STEPPER_CL_REVERSE_COUNTS)) ||
        ((axis->expected_sign < 0) &&
         (movement >= STEPPER_CL_REVERSE_COUNTS)))
    {
        axis->reverse_steps += axis->check_steps;
        axis->check_steps = 0U;
        axis->expected_sign = 0;

        if (axis->reverse_steps >= STEPPER_CL_REVERSE_STEP_LIMIT)
        {
            axis_set_fault(axis, STEPPER_AXIS_FAULT_DIRECTION);
        }
        return;
    }

    if (axis->check_steps >= STEPPER_CL_NO_ENCODER_STEP_LIMIT)
    {
        axis_set_fault(axis, STEPPER_AXIS_FAULT_NO_ENCODER);
    }
}

static void axis_process_closed_loop(stepper_axis_t *axis)
{
    rt_int32_t current_count;
    rt_int32_t error;
    rt_uint32_t error_abs;
    rt_uint32_t steps;
    rt_uint32_t frequency;
    rt_int8_t sign;

    if ((axis->initialized == RT_FALSE) ||
        (axis->fault != STEPPER_AXIS_FAULT_NONE) ||
        (axis->burst_active == RT_TRUE))
    {
        return;
    }

    current_count = ms42cg_encoder_get_count(&axis->encoder);
    axis_check_feedback(axis, current_count);

    if ((axis->closed_loop_active == RT_FALSE) ||
        (axis->fault != STEPPER_AXIS_FAULT_NONE))
    {
        return;
    }

    error = axis->target_count - current_count;
    error_abs = axis_abs_i32(error);

    if (error_abs <= STEPPER_CL_TOLERANCE_COUNTS)
    {
        if (axis->settle_ticks < STEPPER_CL_SETTLE_TICKS)
        {
            axis->settle_ticks++;
        }
        if (axis->settle_ticks >= STEPPER_CL_SETTLE_TICKS)
        {
            axis->reached = RT_TRUE;
            axis->closed_loop_active = RT_FALSE;
        }
        return;
    }

    axis->settle_ticks = 0U;
    axis->reached = RT_FALSE;
    steps = axis_error_to_steps(error_abs);
    frequency = axis_select_frequency(error_abs);
    sign = (error > 0) ? 1 : -1;

    if (axis_start_burst(axis, steps, frequency, sign) != RT_EOK)
    {
        return;
    }

    if ((axis->check_steps == 0U) || (axis->expected_sign != sign))
    {
        axis->check_start_count = current_count;
        axis->check_steps = 0U;
        axis->expected_sign = sign;
    }

    axis->check_steps += steps;
    axis->feedback_pending = RT_TRUE;
}

rt_err_t stepper_axis_default_init(stepper_axis_t *axis)
{
    d36a_stepper_config_t driver_cfg;
    ms42cg_encoder_config_t encoder_cfg;
    rt_err_t err;

    if (axis == RT_NULL)
    {
        return -RT_EINVAL;
    }

    rt_memset(axis, 0, sizeof(*axis));

    driver_cfg.name = "axis1";
    driver_cfg.step_pin = STEPPER_AXIS1_STEP_PIN;
    driver_cfg.dir_pin = STEPPER_AXIS1_DIR_PIN;
    driver_cfg.en_pin = STEPPER_AXIS1_EN_PIN;
    driver_cfg.en_active_level = STEPPER_AXIS1_EN_ACTIVE_LEVEL;
    driver_cfg.positive_dir_level = STEPPER_AXIS1_POSITIVE_DIR_LEVEL;
    driver_cfg.min_hz = STEPPER_MIN_STEP_HZ;
    driver_cfg.max_hz = STEPPER_MAX_STEP_HZ;

    encoder_cfg.name = "axis1";
    encoder_cfg.a_pin = STEPPER_AXIS1_ENC_A_PIN;
    encoder_cfg.b_pin = STEPPER_AXIS1_ENC_B_PIN;
    encoder_cfg.z_pin = STEPPER_AXIS1_ENC_Z_PIN;
    encoder_cfg.counts_per_rev = STEPPER_MS42CG_AB_CPR;
    encoder_cfg.sign = STEPPER_AXIS1_ENCODER_SIGN;

    err = d36a_stepper_init(&axis->driver, &driver_cfg);
    if (err != RT_EOK)
    {
        axis_set_fault(axis, STEPPER_AXIS_FAULT_NOT_READY);
        return err;
    }

    err = ms42cg_encoder_init(&axis->encoder, &encoder_cfg);
    if (err != RT_EOK)
    {
        axis_set_fault(axis, STEPPER_AXIS_FAULT_NOT_READY);
        return err;
    }

    axis->target_count = 0;
    axis->closed_loop_active = RT_FALSE;
    axis->reached = RT_TRUE;
    axis->fault = STEPPER_AXIS_FAULT_NONE;
    axis->initialized = RT_TRUE;

    return RT_EOK;
}

rt_err_t stepper_axis_enable(stepper_axis_t *axis, rt_bool_t enable)
{
    if ((axis == RT_NULL) || (axis->initialized == RT_FALSE))
    {
        return -RT_EINVAL;
    }

    if (enable && axis->fault != STEPPER_AXIS_FAULT_NONE) return -RT_ERROR;
    if (enable == RT_FALSE)
    {
        stepper_axis_stop(axis);
    }

    return d36a_stepper_enable(&axis->driver, enable);
}

rt_err_t stepper_axis_set_speed(stepper_axis_t *axis, rt_int32_t signed_hz)
{
    if (signed_hz > (rt_int32_t)STEPPER_MAX_STEP_HZ ||
        signed_hz < -(rt_int32_t)STEPPER_MAX_STEP_HZ) return -RT_EINVAL;
    if ((axis == RT_NULL) || (axis->initialized == RT_FALSE))
    {
        return -RT_EINVAL;
    }
    if (axis->fault != STEPPER_AXIS_FAULT_NONE)
    {
        return -RT_ERROR;
    }

    axis->closed_loop_active = RT_FALSE;
    axis->reached = RT_FALSE;
    axis->burst_active = RT_FALSE;
    axis->burst_steps_remaining = 0U;
    axis->feedback_pending = RT_FALSE;
    axis->check_steps = 0U;

    return d36a_stepper_set_speed(&axis->driver, signed_hz);
}

rt_err_t stepper_axis_goto_deg(stepper_axis_t *axis, float target_deg)
{
    if ((axis == RT_NULL) || (axis->initialized == RT_FALSE))
    {
        return -RT_EINVAL;
    }
    if (axis->fault != STEPPER_AXIS_FAULT_NONE)
    {
        return -RT_ERROR;
    }

    if ((target_deg != target_deg) ||
        (target_deg > 100000.0f) ||
        (target_deg < -100000.0f))
    {
        return -RT_EINVAL;
    }

    stepper_axis_stop(axis);
    axis->target_count = axis_angle_to_count(target_deg);
    axis->closed_loop_active = RT_TRUE;
    axis->reached = RT_FALSE;
    axis->settle_ticks = 0U;
    axis->feedback_pending = RT_FALSE;
    axis->check_steps = 0U;
    axis->reverse_steps = 0U;
    axis->expected_sign = 0;

    return d36a_stepper_enable(&axis->driver, RT_TRUE);
}

rt_err_t stepper_axis_steps(stepper_axis_t *axis, rt_int32_t signed_steps,
                           rt_uint32_t hz)
{
    if (!axis || !axis->initialized || signed_steps == 0 ||
        signed_steps > 3200 || signed_steps < -3200 ||
        hz < STEPPER_MIN_STEP_HZ || hz > STEPPER_MAX_STEP_HZ)
        return -RT_EINVAL;
    if (axis->fault != STEPPER_AXIS_FAULT_NONE) return -RT_ERROR;
    stepper_axis_stop(axis);
    return axis_start_burst(axis, axis_abs_i32(signed_steps), hz,
                            signed_steps < 0 ? -1 : 1);
}

rt_err_t stepper_axis_move_deg(stepper_axis_t *axis, float delta_deg)
{
    float current;

    if ((axis == RT_NULL) || (axis->initialized == RT_FALSE))
    {
        return -RT_EINVAL;
    }

    current = ms42cg_encoder_get_angle_deg(&axis->encoder);
    return stepper_axis_goto_deg(axis, current + delta_deg);
}

void stepper_axis_zero(stepper_axis_t *axis)
{
    if ((axis == RT_NULL) || (axis->initialized == RT_FALSE))
    {
        return;
    }

    stepper_axis_stop(axis);
    ms42cg_encoder_zero(&axis->encoder);
    axis->target_count = 0;
    axis->closed_loop_active = RT_FALSE;
    axis->reached = RT_TRUE;
    axis->settle_ticks = 0U;
    axis->feedback_pending = RT_FALSE;
    axis->check_steps = 0U;
    axis->reverse_steps = 0U;
    axis->check_start_count = 0;
}

void stepper_axis_stop(stepper_axis_t *axis)
{
    if (axis == RT_NULL)
    {
        return;
    }

    if (axis->initialized) d36a_stepper_stop(&axis->driver);
    axis->closed_loop_active = RT_FALSE;
    axis->reached = RT_FALSE;
    axis->burst_active = RT_FALSE;
    axis->burst_steps_remaining = 0U;
    axis->feedback_pending = RT_FALSE;
    axis->check_steps = 0U;
}

void stepper_axis_clear_fault(stepper_axis_t *axis)
{
    if ((axis == RT_NULL) || (axis->initialized == RT_FALSE))
    {
        return;
    }

    d36a_stepper_stop(&axis->driver);
    axis->target_count = ms42cg_encoder_get_count(&axis->encoder);
    axis->closed_loop_active = RT_FALSE;
    axis->reached = RT_TRUE;
    axis->fault = STEPPER_AXIS_FAULT_NONE;
    axis->settle_ticks = 0U;
    axis->burst_active = RT_FALSE;
    axis->burst_steps_remaining = 0U;
    axis->feedback_pending = RT_FALSE;
    axis->check_steps = 0U;
    axis->reverse_steps = 0U;
    axis->check_start_count = axis->target_count;
}

void stepper_axis_control_tick(stepper_axis_t *axis, rt_uint32_t period_ms)
{
    if ((axis == RT_NULL) || (period_ms == 0U))
    {
        return;
    }

    if (!axis->initialized || axis->fault != STEPPER_AXIS_FAULT_NONE) return;
    if (axis->burst_active) axis_update_burst(axis, period_ms);
    else if (!axis->closed_loop_active) d36a_stepper_poll(&axis->driver, period_ms);
    axis_process_closed_loop(axis);
}

void stepper_axis_get_snapshot(stepper_axis_t *axis,
                               stepper_axis_snapshot_t *snapshot)
{
    if ((axis == RT_NULL) || (snapshot == RT_NULL))
    {
        return;
    }

    rt_memset(snapshot, 0, sizeof(*snapshot));
    snapshot->current_count = ms42cg_encoder_get_count(&axis->encoder);
    snapshot->target_count = axis->target_count;
    snapshot->error_count = snapshot->target_count - snapshot->current_count;
    snapshot->current_angle_deg = axis_count_to_angle(snapshot->current_count);
    snapshot->target_angle_deg = axis_count_to_angle(snapshot->target_count);
    snapshot->active = axis->closed_loop_active;
    snapshot->reached = axis->reached;
    snapshot->enabled = d36a_stepper_is_enabled(&axis->driver);
    snapshot->running = d36a_stepper_is_running(&axis->driver);
    snapshot->speed_hz = d36a_stepper_get_speed_hz(&axis->driver);
    snapshot->remaining_steps = axis->burst_steps_remaining;
    snapshot->total_pulses = axis->driver.total_pulses;
    snapshot->fault = axis->fault;
}

void stepper_axis_invert_driver_dir(stepper_axis_t *axis)
{
    if ((axis == RT_NULL) || (axis->initialized == RT_FALSE))
    {
        return;
    }

    stepper_axis_stop(axis);
    d36a_stepper_invert_positive_dir(&axis->driver);
}

void stepper_axis_invert_encoder_dir(stepper_axis_t *axis)
{
    if ((axis == RT_NULL) || (axis->initialized == RT_FALSE))
    {
        return;
    }

    stepper_axis_stop(axis);
    ms42cg_encoder_invert_sign(&axis->encoder);
}

const char *stepper_axis_fault_name(stepper_axis_fault_t fault)
{
    switch (fault)
    {
    case STEPPER_AXIS_FAULT_NONE:
        return "none";
    case STEPPER_AXIS_FAULT_NO_ENCODER:
        return "no_encoder";
    case STEPPER_AXIS_FAULT_DIRECTION:
        return "direction";
    case STEPPER_AXIS_FAULT_DRIVER:
        return "driver";
    case STEPPER_AXIS_FAULT_NOT_READY:
        return "not_ready";
    default:
        return "unknown";
    }
}
