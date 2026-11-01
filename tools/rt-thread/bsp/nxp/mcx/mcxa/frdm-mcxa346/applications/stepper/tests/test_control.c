#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "stepper_axis.h"
#include "stepper_board.h"
/* Include shell for direct regression of its command parser. */
#include "../src/stepper_shell.c"
static int pins[160], modes[160], edges, fail_attach=-1, fail_enable=-1;
static void (*handlers[160])(void *);
static void *args[160];
static unsigned tick;
rt_base_t rt_hw_interrupt_disable(void) { return 0; }
void rt_hw_interrupt_enable(rt_base_t x) { (void)x; }
rt_tick_t rt_tick_get(void) { return tick; }
void rt_pin_mode(rt_base_t p,int m) { assert(p>=0 && p<160);modes[p]=m; }
void rt_pin_write(rt_base_t p,int v)
{
    assert(p>=0 && p<160);
    if (p==STEPPER_AXIS1_STEP_PIN && v && !pins[p]) ++edges;
    pins[p]=v;
}
int rt_pin_read(rt_base_t p) { return pins[p]; }
int rt_pin_attach_irq(rt_base_t p,int mode,void (*h)(void *),void *a)
{ (void)mode;if (p==fail_attach)return -RT_ERROR;handlers[p]=h;args[p]=a;return 0; }
int rt_pin_detach_irq(rt_base_t p) { handlers[p]=NULL;return 0; }
int rt_pin_irq_enable(rt_base_t p,int e) { (void)e;return p==fail_enable?-RT_ERROR:0; }
int rt_mutex_init(struct rt_mutex *m,const char *s,int i) { (void)m;(void)s;(void)i;return 0; }
int rt_mutex_take(struct rt_mutex *m,int i) { (void)m;(void)i;return 0; }
int rt_mutex_release(struct rt_mutex *m) { (void)m;return 0; }
rt_thread_t rt_thread_create(const char *s,void (*f)(void *),void *p,int a,int b,int c)
{ (void)s;(void)f;(void)p;(void)a;(void)b;(void)c;return (void*)1; }
int rt_thread_startup(rt_thread_t t) { (void)t;return 0; }
void rt_thread_mdelay(int n) { tick+=(unsigned)n; }
static void reset(stepper_axis_t *a)
{
    memset(pins,0,sizeof pins);memset(handlers,0,sizeof handlers);
    edges=0;fail_attach=fail_enable=-1;tick=0;
    assert(stepper_axis_default_init(a)==0);
    assert(!a->driver.enabled && pins[STEPPER_AXIS1_EN_PIN]==0);
}
static void simulate(stepper_axis_t *a,int loops,int feedback)
{
    int fraction=0;
    for(int i=0;i<loops;i++)
    {
        int before=edges;
        stepper_axis_control_tick(a,2);tick+=2;
        if(feedback && edges!=before)
        {
            fraction+=STEPPER_MS42CG_AB_CPR;
            a->encoder.raw_count+=(fraction/3200)*a->driver.direction_sign*feedback;
            fraction%=3200;
        }
    }
}
int main(void)
{
    stepper_axis_t a;
    reset(&a);
    assert(stepper_axis_set_speed(&a,200)==0);
    simulate(&a,500,0);assert(edges==200);
    assert(stepper_axis_set_speed(&a,201)==-RT_EINVAL);
    assert(a.driver.current_hz==200);
    stepper_axis_stop(&a);simulate(&a,50,0);assert(edges==200 && !pins[STEPPER_AXIS1_STEP_PIN]);
    reset(&a);assert(stepper_axis_steps(&a,800,100)==0);simulate(&a,6000,0);
    assert(edges==800 && a.driver.total_pulses==800 && a.reached && !a.driver.output_active);
    assert(stepper_axis_steps(&a,-800,100)==0);simulate(&a,6000,0);
    assert(edges==1600 && a.driver.direction_sign==-1 && a.reached);
    assert(stepper_axis_steps(&a,0,100)!=0 && stepper_axis_steps(&a,3201,100)!=0);
    assert(stepper_axis_steps(&a,10,0)!=0 && stepper_axis_steps(&a,10,201)!=0);
    reset(&a);assert(stepper_axis_set_speed(&a,-100)==0);simulate(&a,500,0);assert(edges==100 && a.driver.direction_sign==-1);
    reset(&a);stepper_axis_goto_deg(&a,90);simulate(&a,15000,1);
    assert(a.reached && !a.closed_loop_active && a.fault==STEPPER_AXIS_FAULT_NONE);
    assert(abs(ms42cg_encoder_get_count(&a.encoder)-(int)(STEPPER_MS42CG_AB_CPR/4))<=3);
    stepper_axis_move_deg(&a,-45);simulate(&a,10000,1);
    assert(a.reached && abs(ms42cg_encoder_get_count(&a.encoder)-(int)(STEPPER_MS42CG_AB_CPR/8))<=6);
    reset(&a);stepper_axis_goto_deg(&a,90);simulate(&a,5000,0);
    assert(a.fault==STEPPER_AXIS_FAULT_NO_ENCODER && !a.driver.enabled);
    stepper_axis_zero(&a);assert(a.fault==STEPPER_AXIS_FAULT_NO_ENCODER);
    assert(stepper_axis_enable(&a,1)!=0);
    stepper_axis_clear_fault(&a);assert(!a.driver.output_active && !a.closed_loop_active);
    reset(&a);stepper_axis_goto_deg(&a,90);simulate(&a,5000,-1);
    assert(a.fault==STEPPER_AXIS_FAULT_DIRECTION && !a.driver.enabled);
    reset(&a);stepper_axis_set_speed(&a,200);simulate(&a,20,0);
    stepper_axis_goto_deg(&a,0);simulate(&a,20,0);assert(a.reached && !a.driver.output_active);
    reset(&a);stepper_axis_goto_deg(&a,90);simulate(&a,20,1);
    stepper_axis_enable(&a,0);int saved=edges;simulate(&a,100,1);assert(edges==saved);
    reset(&a);assert(stepper_axis_goto_deg(&a,NAN)!=0);assert(stepper_axis_goto_deg(&a,INFINITY)!=0);
    /* A complete forward Gray-code cycle yields four counts. */
    reset(&a);int states[4]={2,3,1,0};int prev=0;
    for(int i=0;i<4;i++)
    {
        int now=states[i];pins[STEPPER_AXIS1_ENC_A_PIN]=(now>>1)&1;pins[STEPPER_AXIS1_ENC_B_PIN]=now&1;
        int pin=((prev^now)&2)?STEPPER_AXIS1_ENC_A_PIN:STEPPER_AXIS1_ENC_B_PIN;
        handlers[pin](args[pin]);prev=now;
    }
    assert(ms42cg_encoder_get_count(&a.encoder)==4);
    reset(&a);fail_attach=STEPPER_AXIS1_ENC_B_PIN;
    assert(stepper_axis_default_init(&a)!=0 && !handlers[STEPPER_AXIS1_ENC_A_PIN]);
    reset(&a);fail_enable=STEPPER_AXIS1_ENC_B_PIN;
    assert(stepper_axis_default_init(&a)!=0 && !handlers[STEPPER_AXIS1_ENC_A_PIN] && !handlers[STEPPER_AXIS1_ENC_B_PIN]);
    reset(&g_axis1);g_ready=1;
    char *bad[]={"stepper","speed","junk"};assert(stepper_cmd(3,bad)!=0);
    char *nan[]={"stepper","goto","nan"};assert(stepper_cmd(3,nan)!=0);
    char *big[]={"stepper","speed","99999999999999999999"};assert(stepper_cmd(3,big)!=0);
    char *bad_en[]={"stepper","en","2"};assert(stepper_cmd(3,bad_en)!=0);
    assert(!g_axis1.driver.enabled);
    puts("PASS: pulse count, direction, positioning, relative move, mode switch, disable, faults, IRQ cleanup, quadrature, invalid input");
    return 0;
}
