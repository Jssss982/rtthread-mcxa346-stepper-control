#ifndef TEST_RTDEVICE_H
#define TEST_RTDEVICE_H
#include "rtthread.h"
#define PIN_HIGH 1
#define PIN_LOW 0
#define PIN_MODE_OUTPUT 1
#define PIN_MODE_INPUT_PULLUP 2
#define PIN_IRQ_MODE_RISING 1
#define PIN_IRQ_MODE_RISING_FALLING 3
#define PIN_IRQ_ENABLE 1
#define PIN_IRQ_DISABLE 0
void rt_pin_mode(rt_base_t, int);
void rt_pin_write(rt_base_t, int);
int rt_pin_read(rt_base_t);
rt_err_t rt_pin_attach_irq(rt_base_t, int, void (*)(void *), void *);
rt_err_t rt_pin_detach_irq(rt_base_t);
rt_err_t rt_pin_irq_enable(rt_base_t, int);
#endif
