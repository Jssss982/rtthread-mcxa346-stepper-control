#include "d36a_stepper.h"
#include "stepper_board.h"

/* Deliberately limited to 200 Hz for the low-speed demonstration.
 * Every rising edge is counted; timing follows RTOS scheduling, not a
 * hardware pulse timer. Never claim precision velocity or high-speed motion. */
static rt_uint8_t opposite(rt_uint8_t level)
{
    return level == PIN_HIGH ? PIN_LOW : PIN_HIGH;
}

rt_err_t d36a_stepper_init(d36a_stepper_t *motor,
                         const d36a_stepper_config_t *config)
{
    if (!motor || !config || config->step_pin < 0 || config->dir_pin < 0 ||
        config->en_pin < 0 || config->step_pin == config->dir_pin ||
        config->step_pin == config->en_pin || config->dir_pin == config->en_pin ||
        config->min_hz == 0 || config->max_hz < config->min_hz ||
        config->max_hz > STEPPER_MAX_STEP_HZ)
        return -RT_EINVAL;
    rt_memset(motor, 0, sizeof(*motor));
    motor->cfg = *config;
    /* Gate the driver first, then configure the pulse output. */
    rt_pin_mode(config->en_pin, PIN_MODE_OUTPUT);
    rt_pin_write(config->en_pin, opposite(config->en_active_level));
    rt_pin_mode(config->step_pin, PIN_MODE_OUTPUT);
    rt_pin_write(config->step_pin, PIN_LOW);
    rt_pin_mode(config->dir_pin, PIN_MODE_OUTPUT);
    motor->direction_sign = 1;
    rt_pin_write(config->dir_pin, config->positive_dir_level);
    return RT_EOK;
}

rt_err_t d36a_stepper_stop(d36a_stepper_t *motor)
{
    if (!motor) return -RT_EINVAL;
    if (motor->cfg.step_pin >= 0) rt_pin_write(motor->cfg.step_pin, PIN_LOW);
    motor->pulse_high = RT_FALSE;
    motor->output_active = RT_FALSE;
    motor->current_hz = 0;
    motor->phase_milli = 0;
    return RT_EOK;
}

rt_err_t d36a_stepper_enable(d36a_stepper_t *motor, rt_bool_t enable)
{
    if (!motor) return -RT_EINVAL;
    if (!enable) d36a_stepper_stop(motor);
    rt_pin_write(motor->cfg.en_pin, enable ? motor->cfg.en_active_level :
                 opposite(motor->cfg.en_active_level));
    motor->enabled = enable ? RT_TRUE : RT_FALSE;
    return RT_EOK;
}

rt_err_t d36a_stepper_set_direction(d36a_stepper_t *motor, rt_int8_t sign)
{
    if (!motor) return -RT_EINVAL;
    /* Stop before DIR changes; the next pulse waits a complete period. */
    sign = sign < 0 ? -1 : 1;
    if (motor->direction_sign != sign) d36a_stepper_stop(motor);
    motor->direction_sign = sign;
    rt_pin_write(motor->cfg.dir_pin, sign > 0 ? motor->cfg.positive_dir_level :
                 opposite(motor->cfg.positive_dir_level));
    return RT_EOK;
}

rt_err_t d36a_stepper_set_speed(d36a_stepper_t *motor, rt_int32_t signed_hz)
{
    rt_uint32_t hz;
    if (!motor) return -RT_EINVAL;
    if (signed_hz == 0) return d36a_stepper_stop(motor);
    hz = signed_hz < 0 ? (rt_uint32_t)(-(signed_hz + 1)) + 1U :
                        (rt_uint32_t)signed_hz;
    if (hz < motor->cfg.min_hz || hz > motor->cfg.max_hz) return -RT_EINVAL;
    d36a_stepper_stop(motor);
    d36a_stepper_set_direction(motor, signed_hz < 0 ? -1 : 1);
    d36a_stepper_enable(motor, RT_TRUE);
    motor->current_hz = hz;
    motor->output_active = RT_TRUE;
    return RT_EOK;
}

rt_uint32_t d36a_stepper_poll(d36a_stepper_t *motor, rt_uint32_t period_ms)
{
    rt_uint32_t elapsed;
    if (!motor || !motor->enabled || !motor->output_active || period_ms == 0)
        return 0;
    /* Clamp accounting after a scheduler delay; never catch up with a train
     * of pulses that the rotor could not follow. */
    elapsed = period_ms > STEPPER_CONTROL_PERIOD_MS ?
              STEPPER_CONTROL_PERIOD_MS : period_ms;
    motor->phase_milli += motor->current_hz * elapsed;
    if (motor->pulse_high)
    {
        rt_pin_write(motor->cfg.step_pin, PIN_LOW);
        motor->pulse_high = RT_FALSE;
        return 0;
    }
    if (motor->phase_milli < 1000U) return 0;
    motor->phase_milli -= 1000U;
    rt_pin_write(motor->cfg.step_pin, PIN_HIGH);
    motor->pulse_high = RT_TRUE;
    motor->total_pulses++;
    return 1;
}

rt_bool_t d36a_stepper_is_enabled(const d36a_stepper_t *m)
{ return m ? m->enabled : RT_FALSE; }
rt_bool_t d36a_stepper_is_running(const d36a_stepper_t *m)
{ return m ? m->output_active : RT_FALSE; }
rt_uint32_t d36a_stepper_get_speed_hz(const d36a_stepper_t *m)
{ return m ? m->current_hz : 0; }
rt_int8_t d36a_stepper_get_direction(const d36a_stepper_t *m)
{ return m ? m->direction_sign : 0; }
void d36a_stepper_invert_positive_dir(d36a_stepper_t *m)
{
    if (!m) return;
    d36a_stepper_stop(m);
    m->cfg.positive_dir_level = opposite(m->cfg.positive_dir_level);
    d36a_stepper_set_direction(m, m->direction_sign);
}
