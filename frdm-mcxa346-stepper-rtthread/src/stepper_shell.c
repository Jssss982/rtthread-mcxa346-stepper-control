#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <rtthread.h>
#ifdef FINSH_USING_MSH
#include <finsh.h>
#endif
#include "stepper_axis.h"
#include "stepper_board.h"

static stepper_axis_t g_axis1;
static struct rt_mutex g_lock;
static rt_bool_t g_ready;

static void print_deg(const char *label, float v)
{
    rt_int32_t n = (rt_int32_t)(v * 100.0f + (v < 0 ? -0.5f : 0.5f));
    rt_uint32_t a = n < 0 ? (rt_uint32_t)(-(n + 1)) + 1U : (rt_uint32_t)n;
    rt_kprintf("%s%s%u.%02u", label, n < 0 ? "-" : "", a / 100U, a % 100U);
}

static void print_status(void)
{
    stepper_axis_snapshot_t s;
    stepper_axis_get_snapshot(&g_axis1, &s);
    rt_kprintf("axis1: en=%d run=%d active=%d reached=%d fault=%s count=%d target=%d err=%d speed=%uHz remain=%u ",
        s.enabled, s.running, s.active, s.reached, stepper_axis_fault_name(s.fault),
        s.current_count, s.target_count, s.error_count, s.speed_hz, s.remaining_steps);
    rt_kprintf("pulses=%u ", s.total_pulses);
    print_deg("enc_angle=", s.current_angle_deg);
    print_deg(" target=", s.target_angle_deg);
    rt_kprintf("\n");
}

static void print_help(void)
{
    rt_kprintf("stepper status | config | en <0|1> | speed <-200..200> | stop\n");
    rt_kprintf("stepper zero | goto <deg> | move <deg> | fault clear\n");
    rt_kprintf("stepper steps <-3200..3200, nonzero> <1..200 Hz> (open loop)\n");
    rt_kprintf("stepper dir invert | enc invert\n");
    rt_kprintf("Low-speed GPIO pulses; zero is a relative reference, not homing.\n");
}

static void stepper_thread_entry(void *parameter)
{
    RT_UNUSED(parameter);
    while (1)
    {
        rt_mutex_take(&g_lock, RT_WAITING_FOREVER);
        stepper_axis_control_tick(&g_axis1, STEPPER_CONTROL_PERIOD_MS);
        rt_mutex_release(&g_lock);
        rt_thread_mdelay(STEPPER_CONTROL_PERIOD_MS);
    }
}

int stepper_app_init(void)
{
    rt_thread_t thread;
    rt_err_t err;
    err = rt_mutex_init(&g_lock, "axislock", RT_IPC_FLAG_PRIO);
    if (err != RT_EOK) return err;
    err = stepper_axis_default_init(&g_axis1);
    if (err != RT_EOK)
    {
        rt_kprintf("stepper: init failed (%d)\n", err);
        return err;
    }
    thread = rt_thread_create("stepper", stepper_thread_entry, RT_NULL,
        STEPPER_APP_THREAD_STACK, STEPPER_APP_THREAD_PRIORITY, STEPPER_APP_THREAD_TICK);
    if (thread == RT_NULL) return -RT_ENOMEM;
    err = rt_thread_startup(thread);
    if (err != RT_EOK) return err;
    g_ready = RT_TRUE;
    rt_kprintf("stepper: ready; GPIO pulse backend, 200Hz max, %u encoder counts/rev; EN=0\n", STEPPER_MS42CG_AB_CPR);
    return RT_EOK;
}
INIT_APP_EXPORT(stepper_app_init);

#ifdef FINSH_USING_MSH
static rt_bool_t parse_int(const char *s, long min, long max, long *value)
{
    char *end;
    long n;
    errno = 0;
    n = strtol(s, &end, 10);
    if (s == end || *end || errno == ERANGE || n < min || n > max) return RT_FALSE;
    *value = n;
    return RT_TRUE;
}

static rt_bool_t parse_deg(const char *s, float *value)
{
    char *end;
    double n;
    errno = 0;
    n = strtod(s, &end);
    if (s == end || *end || errno == ERANGE || n != n || n < -100000 || n > 100000)
        return RT_FALSE;
    *value = (float)n;
    return RT_TRUE;
}

static int command_locked(int argc, char **argv)
{
    long n;
    long hz;
    float deg;
    rt_err_t err = RT_EOK;
    if (argc == 2 && strcmp(argv[1], "status") == 0) { print_status(); return 0; }
    if (argc == 2 && strcmp(argv[1], "config") == 0)
    {
        rt_kprintf("STEP=J3-15/P3_0 DIR=J1-16/P3_22 EN=J2-2/P4_3 active-high\n");
        rt_kprintf("A=J3-3/P1_13 B=J1-6/P3_31 CPR=%u microstep=%u max=%uHz\n",
            STEPPER_MS42CG_AB_CPR, STEPPER_D36A_MICROSTEP, STEPPER_MAX_STEP_HZ);
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "stop") == 0) stepper_axis_stop(&g_axis1);
    else if (argc == 2 && strcmp(argv[1], "zero") == 0) stepper_axis_zero(&g_axis1);
    else if (argc == 3 && strcmp(argv[1], "en") == 0 && parse_int(argv[2], 0, 1, &n))
        err = stepper_axis_enable(&g_axis1, (rt_bool_t)n);
    else if (argc == 3 && strcmp(argv[1], "speed") == 0 && parse_int(argv[2], -200, 200, &n))
        err = stepper_axis_set_speed(&g_axis1, (rt_int32_t)n);
    else if (argc == 4 && strcmp(argv[1], "steps") == 0 &&
             parse_int(argv[2], -3200, 3200, &n) && n != 0 &&
             parse_int(argv[3], 1, 200, &hz))
        err = stepper_axis_steps(&g_axis1, (rt_int32_t)n, (rt_uint32_t)hz);
    else if (argc == 3 && strcmp(argv[1], "goto") == 0 && parse_deg(argv[2], &deg))
        err = stepper_axis_goto_deg(&g_axis1, deg);
    else if (argc == 3 && strcmp(argv[1], "move") == 0 && parse_deg(argv[2], &deg))
        err = stepper_axis_move_deg(&g_axis1, deg);
    else if (argc == 3 && strcmp(argv[1], "fault") == 0 && strcmp(argv[2], "clear") == 0)
        stepper_axis_clear_fault(&g_axis1);
    else if (argc == 3 && strcmp(argv[1], "dir") == 0 && strcmp(argv[2], "invert") == 0)
        stepper_axis_invert_driver_dir(&g_axis1);
    else if (argc == 3 && strcmp(argv[1], "enc") == 0 && strcmp(argv[2], "invert") == 0)
        stepper_axis_invert_encoder_dir(&g_axis1);
    else { print_help(); return -RT_EINVAL; }
    if (err != RT_EOK) rt_kprintf("stepper: command rejected (%d)\n", err);
    print_status();
    return err;
}

static int stepper_cmd(int argc, char **argv)
{
    int result;
    if (!g_ready) { rt_kprintf("stepper: not ready\n"); return -RT_ERROR; }
    rt_mutex_take(&g_lock, RT_WAITING_FOREVER);
    result = command_locked(argc, argv);
    rt_mutex_release(&g_lock);
    return result;
}
MSH_CMD_EXPORT_ALIAS(stepper_cmd, stepper, D36A MS42CG stepper control);
#endif
