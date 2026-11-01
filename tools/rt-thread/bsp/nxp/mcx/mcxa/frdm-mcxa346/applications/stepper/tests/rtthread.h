#ifndef TEST_RTTHREAD_H
#define TEST_RTTHREAD_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
typedef int rt_err_t; typedef int rt_bool_t; typedef intptr_t rt_base_t;
typedef int8_t rt_int8_t; typedef uint8_t rt_uint8_t;
typedef int32_t rt_int32_t; typedef uint32_t rt_uint32_t;
typedef int64_t rt_int64_t; typedef uint64_t rt_uint64_t;
typedef uint32_t rt_tick_t; typedef void *rt_thread_t;
struct rt_mutex { int unused; };
#define RT_NULL NULL
#define RT_TRUE 1
#define RT_FALSE 0
#define RT_EOK 0
#define RT_ERROR 1
#define RT_EINVAL 22
#define RT_ENOMEM 12
#define RT_WAITING_FOREVER -1
#define RT_IPC_FLAG_PRIO 1
#define RT_TICK_PER_SECOND 1000
#define RT_UNUSED(x) (void)(x)
#define rt_memset memset
#define rt_kprintf printf
#define INIT_APP_EXPORT(x)
#define FINSH_USING_MSH
rt_base_t rt_hw_interrupt_disable(void);
void rt_hw_interrupt_enable(rt_base_t level);
rt_tick_t rt_tick_get(void);
int rt_mutex_init(struct rt_mutex *, const char *, int);
int rt_mutex_take(struct rt_mutex *, int);
int rt_mutex_release(struct rt_mutex *);
rt_thread_t rt_thread_create(const char *, void (*)(void *), void *, int, int, int);
int rt_thread_startup(rt_thread_t);
void rt_thread_mdelay(int);
#endif
