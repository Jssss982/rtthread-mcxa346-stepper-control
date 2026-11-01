#ifndef MS42CG_ENCODER_H
#define MS42CG_ENCODER_H

#include <rtthread.h>

typedef struct
{
    const char *name;
    rt_base_t a_pin;
    rt_base_t b_pin;
    rt_base_t z_pin;
    rt_uint32_t counts_per_rev;
    rt_int8_t sign;
} ms42cg_encoder_config_t;

typedef struct
{
    ms42cg_encoder_config_t cfg;
    volatile rt_int32_t raw_count;
    volatile rt_int32_t zero_count;
    volatile rt_int32_t z_count;
    volatile rt_int32_t count_at_last_z;
    volatile rt_uint8_t last_ab;
    volatile rt_int8_t last_delta;
    rt_int32_t speed_last_count;
    rt_tick_t speed_last_tick;
    rt_bool_t ready;
} ms42cg_encoder_t;

rt_err_t ms42cg_encoder_init(ms42cg_encoder_t *encoder,
                             const ms42cg_encoder_config_t *config);
void ms42cg_encoder_zero(ms42cg_encoder_t *encoder);
void ms42cg_encoder_set_sign(ms42cg_encoder_t *encoder, rt_int8_t sign);
void ms42cg_encoder_invert_sign(ms42cg_encoder_t *encoder);

rt_int32_t ms42cg_encoder_get_count(const ms42cg_encoder_t *encoder);
rt_int32_t ms42cg_encoder_get_z_count(const ms42cg_encoder_t *encoder);
rt_int32_t ms42cg_encoder_get_count_at_last_z(const ms42cg_encoder_t *encoder);
float ms42cg_encoder_get_angle_deg(const ms42cg_encoder_t *encoder);
float ms42cg_encoder_get_speed_dps(ms42cg_encoder_t *encoder);
rt_bool_t ms42cg_encoder_is_ready(const ms42cg_encoder_t *encoder);

#endif /* MS42CG_ENCODER_H */
