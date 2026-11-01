# RT-Thread 步进电机闭环定位与误差观测

2026 RT-Thread 嵌入式大赛项目。**FRDM-MCXA346 + WHEELTEC D36A + MS42CG**，包含运行于 RT-Thread 5.4.0 的单片机程序，以及 Python/Tkinter 串口上位机。

已在实物上完成编码器反馈、10°/90°定位、相对反转45°和返回软件零点；上位机能显示目标/反馈/误差曲线并导出 CSV。该项目用于低速空载演示，软件脉冲最高设定200Hz。

## 目录

| 内容 | 位置 |
| --- | --- |
| 单片机应用与接线说明 | [frdm-mcxa346-stepper-rtthread](frdm-mcxa346-stepper-rtthread) |
| 单片机 C 源码 | [src](frdm-mcxa346-stepper-rtthread/src) |
| 引脚及控制参数 | [stepper_board.h](frdm-mcxa346-stepper-rtthread/inc/stepper_board.h) |
| 上位机检测工具 | [host](frdm-mcxa346-stepper-rtthread/host) |
| 上位机操作与指标定义 | [使用说明](frdm-mcxa346-stepper-rtthread/host/使用说明.txt) |
| 构建、测试和移植脚本 | [tools](tools) |
| 构建所需 RT-Thread / NXP 源码快照 | [tools/rt-thread](tools/rt-thread) |
| 依赖和输入源码包校验值 | [dependency-lock.json](dependency-lock.json) |

## 上位机

需要 Python 3.9+（含 Tkinter）与 pyserial。在仓库根目录执行：

```powershell
python -m pip install -r frdm-mcxa346-stepper-rtthread/host/requirements.txt
python frdm-mcxa346-stepper-rtthread/host/app.py --port COM5 --connect
```

也可双击应用目录的“打开上位机.cmd”。默认115200、8N1；串口可在界面选择。**连接只读取状态，点击运动按钮才启动电机。** 同一串口不要同时打开其他串口助手。

- 目标角度、编码器角度、实时跟踪误差曲线。
- 单次运动的采样 RMS、采样峰值、首次到位误差和到位记录。
- 绝对定位、相对±10°、软件置零、回零点、四段演示。
- 0°↔90°往返2至10次，按相同目标/指令方向分组显示反馈极差和样本标准差。
- CSV/JSON导出到 `output/host-recordings`；Esc请求停止并失能。

1计数约0.087891°，软件到位容差为±3计数、约±0.264°。这些是编码器量化与控制阈值，**不能等同于实际机械精度**。串口约3–4Hz，采样RMS和峰值可能漏掉快速变化；外部角度精度需要独立仪器验证。

## 系统框架

```mermaid
flowchart LR
    PC[Python 上位机 / 串口命令] -->|USB 串口| MCU[FRDM-MCXA346 / RT-Thread]
    MCU -->|STEP / DIR / EN| D[D36A]
    D --> M[MS42CG 步进电机]
    M -->|A / B 编码器反馈| MCU
    MCU -->|状态 / 计数 / 误差| PC
```

RT-Thread 使用 FinSH/MSH、2ms控制线程、互斥锁和GPIO双边沿中断。闭环按误差选择50/100/200Hz，采用最多8脉冲的小段修正；到位后停止修正，没有持续抗扰控制。定位过程提供无反馈、反向反馈故障锁存与失能。

接线：ST1→J3-15，DIR1→J1-16，EN1→J2-2；编码器A→J3-3、B→J1-6、VCC→J3-8的3.3V。驱动与开发板共地；完整GND接线见应用README。D36A独立电源，本次演示约11.5V。断电调整接线和拨码，测试时保持空载。

当前配置为16细分、3200 STEP/圈。本台实物标定约4096编码器计数/圈，与配套手册4000计数/圈存在差异，更换电机后必须复核。

## 构建固件

仓库包含构建所需的源码快照，无需另行下载整套RT-Thread。基线提交为 `cbc19778d0ae5ef2533a28c0f62c6a399858010a`。

安装 Python、SCons 和 Arm GNU 工具链，在 PowerShell 中执行：

```powershell
python -m pip install scons
.\tools\build-stepper.ps1 -Python python -ArmBin 'C:\Toolchains\arm-gnu-toolchain\bin'
```

请将ArmBin替换为实际目录。输出在 `output/firmware`。脚本会复制维护中的应用并应用已验证的GCC启动入口、注册表保留和链接依赖修复。没有自动烧录或自动启动电机。

已验证工具链：Arm GNU14.3.1、SCons4.11.1。上游链接脚本仍有LOAD段RWX警告，当前未改变其布局。

## 测试与实物结果

```powershell
# 安装桌面 Python 的 Tkinter；Windows 官方 Python 默认提供。
python -m unittest discover -s frdm-mcxa346-stepper-rtthread/host -p 'test_*.py' -v
.\tools\test-stepper.ps1 -Gcc gcc
```

上位机测试覆盖目标字段解析、命令与到位关联、重复统计和GUI/CSV导出；GUI测试需要可用的图形桌面。开发时有7项通过，其中读取本地实物日志的用例在公开仓库没有该日志时会跳过。C主机测试覆盖脉冲计数、模拟闭环、模式切换、失能、反馈故障、IRQ清理和非法输入。

实物四段演示的一轮串口结果：

| 命令 | 目标计数 | 到位计数 | 误差计数 | 反馈角度 |
| --- | ---: | ---: | ---: | ---: |
| goto 10 | 114 | 111 | 3 | 9.76° |
| goto 90 | 1024 | 1021 | 3 | 89.74° |
| move -45 | 509 | 511 | -2 | 44.91° |
| goto 0 | 0 | 0 | 0 | 0.00° |

四段均报告到位、无故障，演示结束停止并失能。相对移动以执行时的反馈位置为起点，因此第三段目标不是绝对45°。故障路径有主机模拟测试，未进行实机拔线故障注入、负载精度或长期可靠性验证。

软件零点不是机械回零；断电后需重新设定。USB断线后上位机不能保证停止命令送达，必要时关闭实体驱动电源。

## 来源和许可

项目应用、上位机及项目脚本以Apache-2.0发布；第三方源码保留原版权和各自许可证，详见 [THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt)。参考RT-Thread官方BSP、NXP资料和WHEELTEC配套说明。

厂商教学视频、手册、原板固件备份和本机凭据不在此仓库中。
