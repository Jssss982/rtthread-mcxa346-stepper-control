#include "ms42cg_encoder.h"
#include <rtdevice.h>
#include "stepper_board.h"

#define MS42CG_AB_IRQ_MODE PIN_IRQ_MODE_RISING_FALLING

static const rt_int8_t qdec_table[16] =
{
    0, -1,  1,  0,
    1,  0,  0, -1,
   -1,  0,  0,  1,
    0,  1, -1,  0
};

static rt_bool_t ms42cg_pin_valid(rt_base_t pin)
{
    return pin != STEPPER_PIN_INVALID;
}

static rt_uint8_t ms42cg_read_ab(const ms42cg_encoder_t *encoder)
{
    rt_uint8_t a;
    rt_uint8_t b;

    a = (rt_pin_read(encoder->cfg.a_pin) != 0) ? 1U : 0U;
    b = (rt_pin_read(encoder->cfg.b_pin) != 0) ? 1U : 0U;

    return (rt_uint8_t)((a << 1U) | b);
}

static void ms42cg_ab_irq(void *args)
{
    ms42cg_encoder_t *encoder = (ms42cg_encoder_t *)args;
    rt_uint8_t now;
    rt_uint8_t index;
    rt_int8_t delta;

    if (encoder == RT_NULL)
    {
        return;
    }

    now = ms42cg_read_ab(encoder);
    index = (rt_uint8_t)((encoder->last_ab << 2U) | now);
    delta = qdec_table[index & 0x0FU];

    if (delta != 0)
    {
        encoder->raw_count += delta;
        encoder->last_delta = delta;
    }

    encoder->last_ab = now;
}

static void ms42cg_z_irq(void *args)
{
    ms42cg_encoder_t *encoder = (ms42cg_encoder_t *)args;
    rt_int8_t turn_sign;

    if (encoder == RT_NULL)
    {
        return;
    }

    turn_sign = (encoder->last_delta < 0) ? -1 : 1;
    encoder->z_count += turn_sign;
    encoder->count_at_last_z = ms42cg_encoder_get_count(encoder);
}

rt_err_t ms42cg_encoder_init(ms42cg_encoder_t *encoder,
                             const ms42cg_encoder_config_t *config)
{
    rt_err_t err;

    if ((encoder == RT_NULL) || (config == RT_NULL) ||
        (ms42cg_pin_valid(config->a_pin) == RT_FALSE) ||
        (ms42cg_pin_valid(config->b_pin) == RT_FALSE) ||
        (config->a_pin == config->b_pin) ||
        (config->z_pin == config->a_pin) || (config->z_pin == config->b_pin))
    {
        return -RT_EINVAL;
    }

    rt_memset(encoder, 0, sizeof(*encoder));
    encoder->cfg = *config;
    encoder->cfg.sign = (config->sign < 0) ? -1 : 1;
    if (encoder->cfg.counts_per_rev == 0U)
    {
        encoder->cfg.counts_per_rev = STEPPER_MS42CG_AB_CPR;
    }

    rt_pin_mode(encoder->cfg.a_pin, PIN_MODE_INPUT_PULLUP);
    rt_pin_mode(encoder->cfg.b_pin, PIN_MODE_INPUT_PULLUP);
    encoder->last_ab = ms42cg_read_ab(encoder);
    encoder->last_delta = 1;

    err = rt_pin_attach_irq(encoder->cfg.a_pin, MS42CG_AB_IRQ_MODE,
                            ms42cg_ab_irq, encoder);
    if (err != RT_EOK)
    {
        return err;
    }

    err = rt_pin_attach_irq(encoder->cfg.b_pin, MS42CG_AB_IRQ_MODE,
                            ms42cg_ab_irq, encoder);
    if (err != RT_EOK) goto fail_a;
    if (ms42cg_pin_valid(encoder->cfg.z_pin))
    {
        rt_pin_mode(encoder->cfg.z_pin, PIN_MODE_INPUT_PULLUP);
        err = rt_pin_attach_irq(encoder->cfg.z_pin, PIN_IRQ_MODE_RISING,
                               ms42cg_z_irq, encoder);
        if (err != RT_EOK) goto fail_b;
    }
    err = rt_pin_irq_enable(encoder->cfg.a_pin, PIN_IRQ_ENABLE);
    if (err != RT_EOK) goto fail_all;
    err = rt_pin_irq_enable(encoder->cfg.b_pin, PIN_IRQ_ENABLE);
    if (err != RT_EOK) goto fail_all;
    if (ms42cg_pin_valid(encoder->cfg.z_pin))
    {
        err = rt_pin_irq_enable(encoder->cfg.z_pin, PIN_IRQ_ENABLE);
        if (err != RT_EOK) goto fail_all;
    }

    ms42cg_encoder_zero(encoder);
    encoder->speed_last_tick = rt_tick_get();
    encoder->ready = RT_TRUE;

    return RT_EOK;
fail_all:
    /* Detach per-pin sources; disabling IRQ here would disable a shared
     * GPIO port vector on the MCXA BSP and affect other pins. */
    if (ms42cg_pin_valid(encoder->cfg.z_pin)) rt_pin_detach_irq(encoder->cfg.z_pin);
fail_b:
    rt_pin_detach_irq(encoder->cfg.b_pin);
fail_a:
    rt_pin_detach_irq(encoder->cfg.a_pin);
    return err;
}

void ms42cg_encoder_zero(ms42cg_encoder_t *encoder)
{
    rt_base_t level;

    if (encoder == RT_NULL)
    {
        return;
    }

    level = rt_hw_interrupt_disable();
    encoder->zero_count = encoder->raw_count;
    encoder->z_count = 0;
    encoder->count_at_last_z = 0;
    encoder->speed_last_count = 0;
    rt_hw_interrupt_enable(level);
}

void ms42cg_encoder_set_sign(ms42cg_encoder_t *encoder, rt_int8_t sign)
{
    if (encoder == RT_NULL)
    {
        return;
    }

    encoder->cfg.sign = (sign < 0) ? -1 : 1;
}

void ms42cg_encoder_invert_sign(ms42cg_encoder_t *encoder)
{
    if (encoder == RT_NULL)
    {
        return;
    }

    encoder->cfg.sign = (encoder->cfg.sign < 0) ? 1 : -1;
}

rt_int32_t ms42cg_encoder_get_count(const ms42cg_encoder_t *encoder)
{
    rt_base_t level;
    rt_int32_t count;

    if (encoder == RT_NULL)
    {
        return 0;
    }

    level = rt_hw_interrupt_disable();
    count = encoder->raw_count - encoder->zero_count;
    rt_hw_interrupt_enable(level);

    return (encoder->cfg.sign < 0) ? -count : count;
}

rt_int32_t ms42cg_encoder_get_z_count(const ms42cg_encoder_t *encoder)
{
    if (encoder == RT_NULL)
    {
        return 0;
    }

    return (encoder->cfg.sign < 0) ? -encoder->z_count : encoder->z_count;
}

rt_int32_t ms42cg_encoder_get_count_at_last_z(const ms42cg_encoder_t *encoder)
{
    return (encoder != RT_NULL) ? encoder->count_at_last_z : 0;
}

float ms42cg_encoder_get_angle_deg(const ms42cg_encoder_t *encoder)
{
    if ((encoder == RT_NULL) || (encoder->cfg.counts_per_rev == 0U))
    {
        return 0.0f;
    }

    return ((float)ms42cg_encoder_get_count(encoder) * 360.0f) /
           (float)encoder->cfg.counts_per_rev;
}

float ms42cg_encoder_get_speed_dps(ms42cg_encoder_t *encoder)
{
    rt_tick_t now;
    rt_tick_t elapsed;
    rt_int32_t count;
    rt_int32_t delta;
    float elapsed_s;

    if ((encoder == RT_NULL) || (encoder->cfg.counts_per_rev == 0U))
    {
        return 0.0f;
    }

    now = rt_tick_get();
    elapsed = now - encoder->speed_last_tick;
    if (elapsed == 0)
    {
        return 0.0f;
    }

    count = ms42cg_encoder_get_count(encoder);
    delta = count - encoder->speed_last_count;
    encoder->speed_last_count = count;
    encoder->speed_last_tick = now;

    elapsed_s = (float)elapsed / (float)RT_TICK_PER_SECOND;
    return ((float)delta * 360.0f) /
           ((float)encoder->cfg.counts_per_rev * elapsed_s);
}

rt_bool_t ms42cg_encoder_is_ready(const ms42cg_encoder_t *encoder)
{
    return (encoder != RT_NULL) ? encoder->ready : RT_FALSE;
}
