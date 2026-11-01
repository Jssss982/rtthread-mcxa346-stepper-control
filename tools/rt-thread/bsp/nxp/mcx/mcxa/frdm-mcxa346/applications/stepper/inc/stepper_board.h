#ifndef STEPPER_BOARD_H
#define STEPPER_BOARD_H

#include <rtthread.h>

#define STEPPER_PIN_INVALID              ((rt_base_t)-1)

/* NXP UM12348 Rev 1.1, figure 13. RT-Thread pin = port * 32 + bit.
 * Low-speed GPIO pulse backend: no FlexPWM pin mux required. */
#define STEPPER_AXIS1_STEP_PIN           (3 * 32 + 0)  /* J3-15 */
#ifndef STEPPER_AXIS1_STEP_PWM_DEV
#define STEPPER_AXIS1_STEP_PWM_DEV       "pwm0"
#endif

#ifndef STEPPER_AXIS1_STEP_PWM_CHANNEL
#define STEPPER_AXIS1_STEP_PWM_CHANNEL   1
#endif

#ifndef STEPPER_AXIS1_DIR_PIN
#define STEPPER_AXIS1_DIR_PIN            (3 * 32 + 22) /* J1-16 */
#endif

#ifndef STEPPER_AXIS1_EN_PIN
#define STEPPER_AXIS1_EN_PIN             (4 * 32 + 3)  /* J2-2 */
#endif

#ifndef STEPPER_AXIS1_EN_ACTIVE_LEVEL
#define STEPPER_AXIS1_EN_ACTIVE_LEVEL    PIN_HIGH
#endif

#ifndef STEPPER_AXIS1_POSITIVE_DIR_LEVEL
#define STEPPER_AXIS1_POSITIVE_DIR_LEVEL PIN_HIGH
#endif

#ifndef STEPPER_AXIS1_ENC_A_PIN
#define STEPPER_AXIS1_ENC_A_PIN          (1 * 32 + 13) /* J3-3 */
#endif

#ifndef STEPPER_AXIS1_ENC_B_PIN
#define STEPPER_AXIS1_ENC_B_PIN          (3 * 32 + 31) /* J1-6 */
#endif

#ifndef STEPPER_AXIS1_ENC_Z_PIN
#define STEPPER_AXIS1_ENC_Z_PIN          STEPPER_PIN_INVALID
#endif

#ifndef STEPPER_AXIS1_ENCODER_SIGN
#define STEPPER_AXIS1_ENCODER_SIGN       1
#endif

#define STEPPER_MOTOR_FULL_STEPS_PER_REV 200U
#define STEPPER_D36A_MICROSTEP           16U
#define STEPPER_PULSES_PER_REV           (STEPPER_MOTOR_FULL_STEPS_PER_REV * STEPPER_D36A_MICROSTEP)

/*
 * This physical motor was calibrated at about 4096 counts per turn:
 * +3200 STEP -> +4089 counts, -3200 STEP -> -4098 counts (16 microsteps).
 * The supplied manual says 4000; verify each motor rather than assuming.
 */
#define STEPPER_MS42CG_AB_CPR            4096U

#define STEPPER_CONTROL_PERIOD_MS        2U
#define STEPPER_MIN_STEP_HZ              1U
#define STEPPER_MAX_STEP_HZ              200U

#define STEPPER_CL_TOLERANCE_COUNTS      3U
#define STEPPER_CL_SETTLE_TICKS          3U
#define STEPPER_CL_MAX_BURST_STEPS       ((8U * STEPPER_D36A_MICROSTEP) / 16U)
#define STEPPER_CL_MOVE_CONFIRM_COUNTS   2
#define STEPPER_CL_NO_ENCODER_STEP_LIMIT 64U
#define STEPPER_CL_REVERSE_STEP_LIMIT    64U
#define STEPPER_CL_REVERSE_COUNTS        3

#define STEPPER_APP_THREAD_STACK         2048
#define STEPPER_APP_THREAD_PRIORITY      20
#define STEPPER_APP_THREAD_TICK          10

#endif /* STEPPER_BOARD_H */
