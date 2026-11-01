# 基于 RT-Thread 的编码器反馈步进电机定位系统

2026 RT-Thread 嵌入式大赛单轴演示工程，使用 FRDM-MCXA346、WHEELTEC D36A 和 MS42CG。已完成实机编码器反馈、10°/90°定位、反转 45°及返回软件零点。板上运行 RT-Thread 5.4.0；结束时停止并失能。

## 已实现功能与实测结果

- STEP/DIR/EN 控制、正反转、有限脉冲输出、停止与失能。
- AB 双边沿中断四倍频计数、角度换算、软件零点、绝对及相对角度定位。
- 定位采用按误差选择 50/100/200 Hz 的分段速度和最多 8 个脉冲的小段修正，不是 PID。
- 定位过程中检测无反馈和反向反馈，故障锁存并关闭 EN；这两种故障路径通过主机模拟验证，未进行实机拔线故障注入。
- FinSH/MSH 命令、独立控制线程、互斥锁；拒绝非法输入、越界值、NaN。

一轮实机串口结果（原始测试日志保留在开发工作区）：

| 命令 | 目标计数 | 到达计数 | 误差（目标－反馈） | 编码器换算角度 |
| --- | ---: | ---: | ---: | ---: |
| goto 10 | 114 | 111 | 3 | 9.76° |
| goto 90 | 1024 | 1021 | 3 | 89.74° |
| move -45 | 509 | 511 | -2 | 44.91° |
| goto 0 | 0 | 0 | 0 | 0.00° |

四次均为 reached=1、run=0、active=0、fault=none。move -45 从当时的实际反馈位置减去 512 计数，因此目标是 509（44.74°），不是固定绝对 45°。角度是编码器换算值；没有使用外部角度仪，不能将 3 计数容差（约 0.264°）称为绝对定位精度。用户确认此前开环测试电机实际转动。

## 接线与标定

调整接线或拨码前断开 USB 和电机电源；按接口编号和丝印识别，不凭线色。

| 信号 | 开发板接口 | GPIO / RT-Thread 编号 |
| --- | --- | --- |
| D36A ST1 | J3-15 | P3_0 / 96 |
| D36A DIR1 | J1-16 | P3_22 / 118 |
| D36A EN1 | J2-2 | P4_3 / 131，HIGH 使能 |
| D36A GND | J3-12 | GND |
| 编码器 VCC | J3-8 | 3.3V |
| 编码器 GND | J3-14 | GND |
| 编码器 A | J3-3 | P1_13 / 45 |
| 编码器 B | J1-6 | P3_31 / 127 |

D36A 使用独立电机电源，本次用户报告 11.5V；电机绕组接第 1 路，三方共地。编码器使用 3.3V，PWM/Z 不接；驱动器 5V/ADC 不接 MCU。用户已确认细分拨码 1、2、3 全 OFF，与本次 16 细分设置一致；电流档保持原硬件设置，没有实测或报告电流值。

配置为 200 整步/圈、16 细分，即 3200 STEP/圈。配套 MS42CG 手册写 4000 计数/圈，但这台实物标定 +3200 脉冲得到 +4089 计数、反向 3200 得到 -4098 计数变化，因此使用约 4096 计数/圈。更换电机后必须重新核对，不把本台参数推广到所有 MS42CG。

## 硬件与软件框架

```text
电脑 ── USB 串口 / MCU-Link ── FRDM-MCXA346（RT-Thread 5.4.0）
                                  │ STEP / DIR / EN
                                  ▼
                           D36A 驱动器 ── 两相步进电机
                                  ▲             │
                         独立电机电源       MS42CG 编码器
                                                │ A/B
                         GPIO 双边沿中断 ◀───────┘

FinSH/MSH 命令 ── 互斥锁 ── stepper 控制线程（2 ms）
                                │
                 目标计数－编码器计数 → 误差
                                │
                  方向 / 分段速度 / 小段脉冲
                                │
                   GPIO STEP 输出 → 反馈检查
                                │
                 到达停止 / 故障锁存并失能
```

模块：d36a_stepper.c 产生脉冲并累计实际输出上升沿；ms42cg_encoder.c 解码 AB；stepper_axis.c 执行定位和保护；stepper_shell.c 提供命令与线程。

## 使用

串口 115200、8N1、无流控，本机当前 COM5。保持电机空载。逐条执行，必须等 status 显示 reached=1、run=0、active=0、fault=none 后再进入下一段；相邻动作可停留 3 秒用于录像。

```text
stepper config
stepper en 1
stepper zero
stepper goto 10
stepper status
stepper goto 90
stepper status
stepper move -45
stepper status
stepper goto 0
stepper status
stepper stop
stepper en 0
```

也可使用 tools/demo-stepper.py --port COM5 自动执行上述演示；该脚本会立即使能并运动，仅在操作者准备好、手离开电机轴时运行。每段最多等待 30 秒，异常退出时尝试 stop 和 en 0。

开环有限运动示例：stepper steps 800 100；反向用 stepper steps -800 100。单次最多 3200 脉冲，完成后板上自行停止；pulses 是软件实际产生的上升沿总数，正反向均递增。

stop 停止脉冲但保留使能；en 0 停止并失能。开机默认失能。zero 只设置当前软件零点，不是寻找机械限位。故障后先排除原因，再 stepper fault clear。

## 编译与验证

基线 RT-Thread commit：cbc19778d0ae5ef2533a28c0f62c6a399858010a。BSP：bsp/nxp/mcx/mcxa/frdm-mcxa346；依赖 nxp-mcx-cmsis、nxp-mcx-series，源码包保留本次构建使用的快照。

本应用放入 BSP 的 applications/stepper。需要 RT_USING_PIN、RT_USING_MUTEX、FinSH/MSH、1000 Hz tick，不需要 PWM 外设。tools/prepare-bsp.py 修复本基线 GCC C 启动入口、FSymTab/VSymTab/自动初始化表保留和链接脚本重链接依赖。脚本可重复执行。

Windows：tools/build-stepper.ps1，默认使用 PATH 中的 Python，使用 -Python、-ArmBin 指定工具路径。其他电脑设置 RTT_EXEC_PATH、RTT_ROOT、RTT_CC=gcc，在 BSP 执行 scons -j4。固件输出到 output/firmware。上游链接脚本存在 LOAD 段 RWX 警告，当前未修改该布局。

主机测试：tools/test-stepper.ps1 -Gcc <gcc路径>。覆盖有限脉冲计数、正反转、模拟闭环、相对移动、模式切换、失能、无反馈/反向故障、IRQ 失败清理、AB 解码和非法输入；主机测试通过不代表实机故障测试通过。ARM GCC 14.3.1 构建通过，实机已验证启动、线程、Shell、编码器和上述定位动作。

## 边界

低速 GPIO 软件脉冲最高设定 200 Hz，线程调度及串口输出会影响实际速度，不承诺硬件定时精度。到达目标后停止修正，没有持续抗扰控制。没有机械回零、断电位置保持、加减速轨迹或负载精度测试。连续 speed 模式没有编码器失联自动停机，调试后须主动 stop/en 0。

## 来源

- [RT-Thread 开发板指南](https://www.rt-thread.org/document/site/#/rt-thread-version/rt-thread-standard/tutorial/quick-start/frdm_mcxa346/quick-start)
- [NXP UM12348](https://www.nxp.com/docs/en/user-manual/UM12348.pdf)，Rev 1.1 第 26 页图 13。
- 用户提供的 WHEELTEC MS42CG使用手册-20260714.pdf、步进电机驱动.pdf；仅本地参考，不随源码重新分发。

基于官方 BSP、NXP SDK 和工作区已有应用框架，使用 AI 辅助修改、测试、排障及文档整理；上游依赖保留各自版权和许可证。公开视频、源码地址、论坛发帖和作品登记须另行完成，实机调通本身不等于赛事提交完成。

## 桌面上位机

双击本目录“打开上位机.cmd”，默认连接 COM5（连接只读取状态）。使用 Python 3.9+、Tkinter、pyserial；其他电脑可执行 python host/app.py。界面显示目标/反馈角度、误差曲线、当前误差、单次运动采样 RMS 与峰值、每条命令的到位误差，以及同目标同指令方向的重复到位极差和样本标准差。

提供绝对角度、相对±10°、软件置零、回零点、2至10次0°↔90°往返、四段演示、停止失能和CSV导出。测试只有点击按钮才启动，序列结束自动停止并失能。样本保留最近12000条，图表显示最近30秒，导出位于 output/host-recordings。

测量口径和故障处理见 host/使用说明.txt。首次到位快照与随后静止时的读数可能不同；编码器反馈的离散程度不等于机械绝对精度。协议、命令/到位关联和GUI导出有7项自动化测试，COM5真实只读采集已验证，窗口已观察到一次90°命令及到位记录。
