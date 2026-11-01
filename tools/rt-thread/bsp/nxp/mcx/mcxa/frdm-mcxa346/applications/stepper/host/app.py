"""RT-Thread stepper desktop monitor. Opening/connecting never starts motion."""
import argparse
import csv
import json
import math
import queue
import time
from collections import deque
from dataclasses import asdict
from datetime import datetime
from pathlib import Path
import tkinter as tk
from tkinter import ttk, messagebox

try:
    from serial.tools import list_ports
    from serial_link import SerialLink
except ImportError:
    root = tk.Tk(); root.withdraw()
    messagebox.showerror('缺少依赖', '请在运行本程序的 Python 环境执行：python -m pip install pyserial')
    raise
from protocol import MoveMeasurement, repeatability, endpoint_dict

BG = '#F2F5FA'
INK = '#16243B'
MUTED = '#63748B'
TEAL = '#008F83'
BLUE = '#3267D6'
ORANGE = '#D07713'
RED = '#BD3444'
SIDE = '#142238'
FONT = 'Microsoft YaHei UI'
ROOT = Path(__file__).resolve().parents[2]
EXPORTS = ROOT / 'output' / 'host-recordings'

class Plot(tk.Canvas):
    def __init__(self, parent, error=False):
        super().__init__(parent, bg='white', highlightthickness=0, height=220)
        self.error = error
        self.bind('<Configure>', lambda e: self.draw(*self.data) if hasattr(self, 'data') else None)

    def draw(self, samples, now, cpr):
        self.data = samples, now, cpr
        self.delete('all')
        w, h = self.winfo_width(), self.winfo_height()
        if w < 100 or h < 100:
            return
        left, top, right, bottom = 58, 39, w - 20, h - 32
        self.create_text(18, 16, text='跟踪误差  / °' if self.error else '目标与反馈  / °',
                         anchor='w', fill=INK, font=(FONT, 11, 'bold'))
        if not self.error:
            self.create_text(w - 20, 16, text='━ 反馈   ┄ 目标', anchor='e', fill=TEAL, font=(FONT, 9))
        else:
            self.create_text(w - 20, 16, text='目标 − 反馈', anchor='e', fill=ORANGE, font=(FONT, 9))
        recent = [s for s in samples if now - 30 <= s.t <= now]
        values = []
        for s in recent:
            a, target, err = s.degrees(cpr or 4096)
            values.extend([err] if self.error else [a, target])
        if self.error:
            extent = max(1.0, max(map(abs, values), default=0) * 1.12)
            lo, hi = -extent, extent
        else:
            lo, hi = min(values, default=0), max(values, default=90)
            pad = max(2.0, (hi - lo) * 0.12)
            lo, hi = lo - pad, hi + pad
        def y(v): return bottom - (v - lo) / (hi - lo) * (bottom - top)
        def x(t): return left + (t - (now - 30)) / 30 * (right - left)
        if self.error and cpr:
            tol = 3 * 360 / cpr
            self.create_rectangle(left, y(tol), right, y(-tol), fill='#E4F5EE', outline='')
        for i in range(5):
            v = lo + (hi - lo) * i / 4
            yy = y(v)
            self.create_line(left, yy, right, yy, fill='#E9EEF5')
            self.create_text(left - 8, yy, text=f'{v:.2f}', anchor='e', fill=MUTED, font=(FONT, 8))
        for sec in (30, 20, 10, 0):
            xx = x(now - sec)
            self.create_text(xx, bottom + 17, text=f'-{sec}s' if sec else '现在', fill=MUTED, font=(FONT, 8))
        if not recent or not cpr:
            self.create_text((left + right) / 2, (top + bottom) / 2,
                             text='等待开发板实时数据', fill=MUTED, font=(FONT, 12))
            return
        indexes = (2,) if self.error else (1, 0)
        for idx in indexes:
            segment = []
            prev = None
            color = ORANGE if self.error else (BLUE if idx == 1 else TEAL)
            def flush():
                if len(segment) >= 4:
                    self.create_line(*segment, fill=color, width=2,
                                     dash=(5, 4) if idx == 1 else ())
            for s in recent:
                if prev is not None and s.t - prev > 1.5:
                    flush(); segment = []
                segment.extend((x(s.t), y(s.degrees(cpr)[idx])))
                prev = s.t
            flush()
            if segment:
                xx, yy = segment[-2:]
                self.create_oval(xx - 3, yy - 3, xx + 3, yy + 3, fill=color, outline='')

class Monitor:
    def __init__(self, root, port='COM5', auto_connect=False):
        self.root = root
        self.root.title('RT-Thread 步进定位观测台 · FRDM-MCXA346')
        self.root.geometry('1320x860')
        self.root.minsize(1140, 800)
        self.root.configure(bg=BG)
        self.events = queue.Queue()
        self.link = None
        self.connected = False
        self.closing = False
        self.cpr = None
        self.last = None
        self.samples = deque(maxlen=12000)
        self.endpoints = []
        self.move = None
        self.last_move = None
        self.token = 0
        self.plan = deque()
        self.plan_running = False
        self.plan_due = None
        self.session_start = time.monotonic()
        self.wall_start = datetime.now().astimezone().isoformat()
        self.port_var = tk.StringVar(value=port)
        self.angle_var = tk.StringVar(value='90')
        self.cycles_var = tk.StringVar(value='3')
        self.note = tk.StringVar(value='连接后只读取状态。点击运动按钮才会使能电机。')
        self.config_text = tk.StringVar(value='等待读取固件配置')
        self.repeat_text = tk.StringVar(value='同一目标、同一指令方向至少 2 次到位后显示离散程度。')
        self.metrics = {k: tk.StringVar(value='—') for k in ['target', 'actual', 'error', 'state', 'rms', 'peak', 'end', 'n']}
        self.control_buttons = []
        self._layout()
        self.refresh_ports()
        self.root.protocol('WM_DELETE_WINDOW', self.close)
        self.root.bind('<Escape>', lambda e: self.halt('已请求停止并失能'))
        self.root.after(50, self.tick)
        if auto_connect:
            self.root.after(300, self.connect)

    def label(self, parent, text='', **kwargs):
        return tk.Label(parent, text=text, font=(FONT, 10), bg=parent.cget('bg'), fg=INK, **kwargs)

    def button(self, parent, text, command, color=BLUE, track=False):
        b = tk.Button(parent, text=text, command=command, bg=color, fg='white',
                      activebackground=color, activeforeground='white', relief='flat',
                      cursor='hand2', font=(FONT, 10), padx=12, pady=7,
                      disabledforeground='#9DAABC', borderwidth=0)
        if track:
            self.control_buttons.append(b)
        return b

    def _layout(self):
        style = ttk.Style()
        style.theme_use('clam')
        style.configure('Treeview', font=(FONT, 9), rowheight=25, background='white',
                        fieldbackground='white', foreground=INK, borderwidth=0)
        style.configure('Treeview.Heading', font=(FONT, 9, 'bold'), background='#EDF2F8', foreground=MUTED)
        style.map('Treeview', background=[('selected', '#E5EFFD')], foreground=[('selected', INK)])
        side = tk.Frame(self.root, bg=SIDE, width=242)
        side.pack(side='left', fill='y'); side.pack_propagate(False)
        tk.Label(side, text='RT-THREAD', bg=SIDE, fg='#55D9CB', font=(FONT, 17, 'bold')).pack(anchor='w', padx=22, pady=(25, 2))
        tk.Label(side, text='步进定位观测台', bg=SIDE, fg='white', font=(FONT, 15, 'bold')).pack(anchor='w', padx=22)
        tk.Label(side, text='FRDM-MCXA346  ×  D36A', bg=SIDE, fg='#AAB9CD', font=(FONT, 9)).pack(anchor='w', padx=22, pady=(5, 22))
        def caption(text):
            tk.Label(side, text=text, bg=SIDE, fg='#AAB9CD', font=(FONT, 9)).pack(anchor='w', padx=22, pady=(12, 6))
        caption('01  串口连接 · 115200 / 8N1')
        row = tk.Frame(side, bg=SIDE); row.pack(fill='x', padx=22)
        self.port_box = ttk.Combobox(row, textvariable=self.port_var, width=13, state='readonly')
        self.port_box.pack(side='left', fill='x', expand=True)
        self.button(row, '刷新', self.refresh_ports, '#31445F').pack(side='right', padx=(6, 0))
        self.connect_button = self.button(side, '连接开发板', self.connect)
        self.connect_button.pack(fill='x', padx=22, pady=(9, 0))
        self.connection_label = tk.Label(side, text='● 未连接', bg=SIDE, fg='#AAB9CD', font=(FONT, 10))
        self.connection_label.pack(anchor='w', padx=22, pady=(8, 0))
        caption('02  角度控制 · 空载演示')
        row = tk.Frame(side, bg=SIDE); row.pack(fill='x', padx=22)
        tk.Entry(row, textvariable=self.angle_var, font=(FONT, 14), width=8, relief='flat').pack(side='left', fill='x', expand=True)
        tk.Label(row, text=' °', bg=SIDE, fg='white', font=(FONT, 12)).pack(side='left')
        self.button(side, '转到该角度', self.goto_input, track=True).pack(fill='x', padx=22, pady=(8, 0))
        row = tk.Frame(side, bg=SIDE); row.pack(fill='x', padx=22, pady=(8, 0))
        self.button(row, '相对 −10°', lambda: self.start_move('stepper move -10'), '#31445F', True).pack(side='left', expand=True, fill='x', padx=(0, 3))
        self.button(row, '相对 +10°', lambda: self.start_move('stepper move 10'), '#31445F', True).pack(side='left', expand=True, fill='x', padx=(3, 0))
        row = tk.Frame(side, bg=SIDE); row.pack(fill='x', padx=22, pady=(8, 0))
        self.button(row, '当前位置置零', lambda: self.simple('stepper zero'), '#31445F', True).pack(side='left', expand=True, fill='x', padx=(0, 3))
        self.button(row, '回零点', lambda: self.start_move('stepper goto 0'), '#31445F', True).pack(side='left', expand=True, fill='x', padx=(3, 0))
        caption('03  重复到位观测')
        row = tk.Frame(side, bg=SIDE); row.pack(fill='x', padx=22)
        tk.Label(row, text='0° ↔ 90°   往返次数', bg=SIDE, fg='white', font=(FONT, 9)).pack(side='left')
        ttk.Spinbox(row, from_=2, to=10, textvariable=self.cycles_var, width=3).pack(side='right')
        self.button(side, '开始往返测试', self.start_repeat, TEAL, True).pack(fill='x', padx=22, pady=(8, 0))
        self.button(side, '演示 10° / 90° / −45° / 0°', self.start_demo, '#31445F', True).pack(fill='x', padx=22, pady=(8, 0))
        self.button(side, '清除故障', lambda: self.simple('stepper fault clear'), '#31445F', True).pack(fill='x', padx=22, pady=(8, 0))
        self.button(side, '停止并失能  [Esc]', lambda: self.halt('已请求停止并失能'), RED).pack(fill='x', padx=22, pady=(18, 8))
        main = tk.Frame(self.root, bg=BG); main.pack(fill='both', expand=True, padx=22, pady=20)
        head = tk.Frame(main, bg=BG); head.pack(fill='x')
        title = tk.Label(head, text='实时反馈与定位误差', font=(FONT, 20, 'bold'), bg=BG, fg=INK)
        title.pack(side='left')
        self.button(head, '导出 CSV', self.export, TEAL).pack(side='right')
        self.button(head, '清空统计', self.clear, '#63748B').pack(side='right', padx=8)
        tk.Label(main, textvariable=self.config_text, bg=BG, fg=MUTED, font=(FONT, 9), anchor='w').pack(fill='x', pady=(7, 12))
        cards = tk.Frame(main, bg=BG); cards.pack(fill='x')
        for i, (key, name, color) in enumerate([('target', '目标角度', BLUE), ('actual', '编码器角度', TEAL), ('error', '当前跟踪误差', ORANGE), ('state', '驱动状态', INK)]):
            box = tk.Frame(cards, bg='white', padx=14, pady=10)
            box.grid(row=0, column=i, sticky='ew', padx=(0 if i == 0 else 10, 0))
            cards.columnconfigure(i, weight=1, uniform='cards')
            tk.Label(box, text=name, bg='white', fg=MUTED, font=(FONT, 9), anchor='w').pack(fill='x')
            tk.Label(box, textvariable=self.metrics[key], bg='white', fg=color, font=(FONT, 22, 'bold'), anchor='w').pack(fill='x', pady=(5, 0))
        plots = tk.Frame(main, bg=BG); plots.pack(fill='both', expand=True, pady=(12, 0))
        self.angle_plot = Plot(plots); self.error_plot = Plot(plots, error=True)
        self.angle_plot.pack(side='left', fill='both', expand=True, padx=(0, 6))
        self.error_plot.pack(side='left', fill='both', expand=True, padx=(6, 0))
        stats = tk.Frame(main, bg=BG); stats.pack(fill='x', pady=10)
        for key, title in [('rms', '本次运动采样 RMS'), ('peak', '本次采样峰值 |误差|'), ('end', '末次到位误差'), ('n', '有效到位次数')]:
            box = tk.Frame(stats, bg=BG); box.pack(side='left', expand=True, fill='x')
            tk.Label(box, text=title, bg=BG, fg=MUTED, font=(FONT, 9)).pack(anchor='w')
            tk.Label(box, textvariable=self.metrics[key], bg=BG, fg=INK, font=(FONT, 16, 'bold')).pack(anchor='w')
        section = tk.Frame(main, bg='white', padx=12, pady=8); section.pack(fill='x')
        tk.Label(section, text='到位记录  ·  每条运动命令只计一次', bg='white', fg=INK, font=(FONT, 10, 'bold')).pack(anchor='w', pady=(0, 6))
        columns = ('index', 'command', 'target', 'actual', 'error', 'time')
        self.table = ttk.Treeview(section, columns=columns, show='headings', height=4)
        for col, title, width in zip(columns, ['#', '命令', '目标 / °', '反馈 / °', '误差 / °', '观测耗时 / s'], [35, 150, 95, 95, 95, 100]):
            self.table.heading(col, text=title); self.table.column(col, width=width, anchor='center')
        self.table.pack(fill='x')
        tk.Label(section, textvariable=self.repeat_text, bg='white', fg=TEAL, font=(FONT, 9), justify='left', anchor='w', wraplength=900).pack(fill='x', pady=(6, 0))
        tk.Label(main, text='误差来自编码器计数；绿色带为 ±3 计数容差。重复离散程度与运动 RMS 均不等于实际机械精度。', bg=BG, fg=MUTED, anchor='w', font=(FONT, 9)).pack(fill='x', pady=(8, 0))
        tk.Label(main, textvariable=self.note, bg=BG, fg=BLUE, anchor='w', font=(FONT, 9), wraplength=950, justify='left').pack(fill='x', pady=(6, 6))
        self.log = tk.Text(main, height=4, bg='#E8EDF5', fg='#43536C', relief='flat', font=('Consolas', 8), state='disabled')
        self.log.pack(fill='x')

    def add_log(self, text):
        self.log.configure(state='normal')
        self.log.insert('end', text + '\n')
        if int(self.log.index('end-1c').split('.')[0]) > 250:
            self.log.delete('1.0', '80.0')
        self.log.see('end'); self.log.configure(state='disabled')

    def refresh_ports(self):
        names = [p.device for p in list_ports.comports()]
        self.port_box.configure(values=names)

    def reset_measurements(self):
        self.samples.clear(); self.endpoints.clear()
        self.last_move = None
        self.session_start = time.monotonic()
        self.wall_start = datetime.now().astimezone().isoformat()
        self.table.delete(*self.table.get_children())
        for k in ['rms', 'peak', 'end', 'n']:
            self.metrics[k].set('—')
        self.repeat_text.set('同一目标、同一指令方向至少 2 次到位后显示离散程度。')

    def connect(self):
        if self.link:
            self.halt('正在断开，尝试停止并失能…')
            self.link.close()
            return
        port = self.port_var.get().strip()
        if not port or port not in [p.device for p in list_ports.comports()]:
            self.note.set('所选串口不存在，请连接开发板后刷新并选择正确串口。'); return
        self.reset_measurements(); self.cpr = None; self.last = None
        self.link = SerialLink(port, self.events)
        self.connect_button.configure(text='正在连接…', state='disabled')
        self.link.start()

    def fresh(self):
        return bool(self.connected and self.cpr and self.last and time.monotonic() - self.last.t < 1.5)

    def simple(self, command):
        if self.fresh() and not self.move and not self.plan_running:
            if command == 'stepper zero':
                self.reset_measurements()  # Never mix measurements from different origins.
            self.link.send(command)
            self.note.set('已发送：' + command)

    def goto_input(self):
        try:
            value = float(self.angle_var.get())
            if not math.isfinite(value) or abs(value) > 360:
                raise ValueError()
        except ValueError:
            self.note.set('请输入 -360 到 360 之间的有限角度。'); return
        self.start_move(f'stepper goto {value:g}')

    def start_move(self, command):
        if not self.fresh() or self.move:
            return False
        if self.last.fault != 'none':
            self.note.set('驱动报告故障，请排除接线问题后清除故障。'); return False
        # Keep this demonstration within one revolution from the software origin.
        value = float(command.split()[-1])
        target = value if ' goto ' in command else self.last.count * 360 / self.cpr + value
        if abs(target) > 360:
            self.note.set('本上位机演示范围限制为软件零点 ±360°。'); return False
        self.token += 1
        self.move = MoveMeasurement(command, self.token, self.last.count, time.monotonic(), self.cpr)
        self.last_move = self.move
        self.link.send(command, self.token)
        self.note.set('执行中：' + command + '；Esc 停止并失能。')
        return True

    def start_repeat(self):
        try:
            n = int(self.cycles_var.get())
            if not 2 <= n <= 10:
                raise ValueError()
        except ValueError:
            self.note.set('往返次数请输入 2 到 10 的整数。'); return
        self.begin_plan(['stepper goto 0'] + ['stepper goto 90', 'stepper goto 0'] * n)

    def start_demo(self):
        self.begin_plan(['stepper goto 10', 'stepper goto 90', 'stepper move -45', 'stepper goto 0'])

    def begin_plan(self, commands):
        if not self.fresh() or self.move or self.plan_running:
            return
        self.plan = deque(commands)
        self.plan_running = True
        self.plan_due = time.monotonic()

    def halt(self, reason):
        self.plan.clear(); self.plan_running = False; self.plan_due = None; self.move = None
        if self.link:
            self.link.stop_motion()
        self.note.set(reason)

    def clear(self):
        if self.move or self.plan_running:
            self.note.set('请先停止运动，再清空统计。'); return
        self.reset_measurements()
        self.note.set('已清空曲线与统计，开发板位置未改变。')

    def complete(self, e):
        self.endpoints.append(e)
        self.table.insert('', 'end', values=(len(self.endpoints), e.command.removeprefix('stepper '),
                          f'{e.target_deg:.3f}', f'{e.actual_deg:.3f}', f'{e.error_deg:+.3f}', f'{e.elapsed:.2f}'))
        children = self.table.get_children()
        if len(children) > 100:
            self.table.delete(children[0])
        self.table.see(self.table.get_children()[-1])
        self.metrics['end'].set(f'{e.error_deg:+.3f}°')
        self.metrics['n'].set(str(len(self.endpoints)))
        groups = repeatability(self.endpoints)
        if groups:
            self.repeat_text.set('同向重复到位：' + '；'.join(
                f"{g['target_deg']:.1f}° {'正向' if g['direction'] > 0 else '反向'} n={g['n']}，极差 {g['range_deg']:.3f}°，样本 σ {g['sample_std_deg']:.3f}°"
                for g in groups[-3:]))
        self.note.set(f'已到位：反馈 {e.actual_deg:.3f}°，误差 {e.error_deg:+.3f}°。')
        self.move = None
        if self.plan_running:
            self.plan_due = time.monotonic() + 1.5

    def handle(self, kind, data):
        if kind == 'connected':
            self.connected = True
            self.connect_button.configure(text='断开连接', state='normal')
            self.note.set('已连接，只读取数据；点击运动按钮才会启动电机。')
        elif kind == 'config':
            if self.cpr and self.cpr != data[0]:
                self.halt('编码器配置变化，已停止并清空统计。'); self.reset_measurements()
            self.cpr = data[0]
            self.config_text.set(f'实时串口 · {self.port_var.get()} · {data[1]} 细分 · 1 计数 = {360/data[0]:.5f}° · 容差 ±{1080/data[0]:.3f}° · 约 3–4 次/秒 · 最近 30 秒')
        elif kind == 'sample':
            s, token = data
            self.last = s
            if self.cpr:
                self.samples.append(s)
            if s.fault != 'none':
                self.halt('板上故障：' + s.fault)
            elif self.move:
                e = self.move.observe(s, token)
                if e:
                    self.complete(e)
        elif kind in ('error', 'rejected'):
            self.halt('通信/命令异常：' + data)
            self.add_log('! ' + data)
        elif kind == 'disconnected':
            self.connected = False; self.link = None
            self.plan.clear(); self.plan_running = False; self.move = None
            self.connect_button.configure(text='连接开发板', state='normal')
            self.config_text.set('未连接 · 曲线和统计保留，可导出；重新连接将开始新会话。')
        elif kind in ('tx', 'rx'):
            if kind == 'rx' and 'stepper: ready;' in data:
                self.halt('开发板已重启，请重新连接并设置软件零点。')
                self.cpr = None
            if kind == 'rx' or data != 'stepper status':
                self.add_log(('> ' if kind == 'tx' else '') + data)

    def tick(self):
        try:
            while True:
                self.handle(*self.events.get_nowait())
        except queue.Empty:
            pass
        now = time.monotonic()
        if self.move and now - self.move.started > 30:
            self.halt('本段超过 30 秒，已请求停止并失能。')
        if self.move and not self.fresh():
            self.halt('状态超过 1.5 秒未更新，已请求停止；请检查 USB。')
        if self.plan_running and not self.move and self.plan_due and now >= self.plan_due:
            if self.plan:
                if not self.start_move(self.plan.popleft()):
                    self.halt('测试已停止：当前状态不允许继续运动。')
                self.plan_due = None
            else:
                self.halt('全部动作完成，已请求停止并失能；可导出 CSV。')
        fresh = self.fresh()
        self.connection_label.configure(text='● 实时连接' if fresh else ('● 等待有效数据' if self.connected else '● 未连接'), fg='#55D9CB' if fresh else '#E9B664')
        idle = fresh and not self.move and not self.plan_running and not self.last.run and not self.last.active
        for b in self.control_buttons:
            b.configure(state='normal' if idle else 'disabled')
        self.port_box.configure(state='disabled' if self.link else 'readonly')
        if self.last and self.cpr:
            a, target, err = self.last.degrees(self.cpr)
            self.metrics['target'].set(f'{target:.3f}°')
            self.metrics['actual'].set(f'{a:.3f}°')
            self.metrics['error'].set(f'{err:+.3f}°')
            status = '数据过期' if not fresh else ('故障' if self.last.fault != 'none' else ('定位中' if self.last.active or self.last.run else ('保持使能' if self.last.en else '已失能')))
            self.metrics['state'].set(status)
        if self.last_move and self.last_move.errors:
            self.metrics['rms'].set(f'{self.last_move.rms:.3f}°')
            self.metrics['peak'].set(f'{self.last_move.peak:.3f}°')
        self.angle_plot.draw(self.samples, now, self.cpr)
        self.error_plot.draw(self.samples, now, self.cpr)
        if self.closing and self.link is None:
            self.root.destroy(); return
        self.root.after(100, self.tick)

    def export(self):
        if not self.samples or not self.cpr:
            self.note.set('还没有可导出的有效编码器数据。'); return
        EXPORTS.mkdir(parents=True, exist_ok=True)
        prefix = EXPORTS / datetime.now().strftime('%Y%m%d_%H%M%S_%f')
        with Path(str(prefix) + '_samples.csv').open('w', encoding='utf-8-sig', newline='') as f:
            fields = ['elapsed_s', 'encoder_deg', 'target_deg', 'error_deg', 'count', 'target_count', 'error_count', 'en', 'run', 'active', 'reached', 'fault', 'speed_hz', 'remaining_steps', 'total_pulses', 'cpr']
            writer = csv.writer(f); writer.writerow(fields)
            for s in self.samples:
                a, target, err = s.degrees(self.cpr)
                writer.writerow([s.t-self.session_start, a, target, err, s.count, s.target, s.err, s.en, s.run, s.active, s.reached, s.fault, s.speed, s.remain, s.pulses, self.cpr])
        with Path(str(prefix) + '_endpoints.csv').open('w', encoding='utf-8-sig', newline='') as f:
            fields = list(EndpointFields)
            writer = csv.DictWriter(f, fieldnames=fields); writer.writeheader()
            writer.writerows(endpoint_dict(e) for e in self.endpoints)
        report = dict(mode='live_serial', session_start=self.wall_start, port=self.port_var.get(),
                      cpr=self.cpr, sample_count=len(self.samples), endpoint_count=len(self.endpoints),
                      retention='最新12000条串口样本；非等间隔采样',
                      repeatability=repeatability(self.endpoints),
                      definitions=dict(error='target_count - encoder_count; degrees = counts * 360 / CPR',
                                       completion='one reached=1/run=0/active=0/fault=none snapshot per acknowledged command',
                                       repeatability='same target + commanded displacement direction + CPR; sample standard deviation (n-1); displacements <=3 counts excluded',
                                       limitation='Encoder-derived observations, not independently measured mechanical accuracy. USB sampling may miss peak error.'))
        Path(str(prefix) + '_summary.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
        self.note.set('已导出：' + str(prefix) + '_samples.csv（另含到位 CSV 和统计 JSON）')
        self.add_log('EXPORT ' + str(prefix))

    def close(self):
        self.closing = True
        self.halt('正在停止并断开串口…')
        if self.link:
            self.link.close()
        else:
            self.root.destroy()

EndpointFields = ('command', 'target_count', 'count', 'error_count', 'error_deg', 'target_deg', 'actual_deg', 'direction', 'elapsed', 'cpr')

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--port', default='COM5')
    parser.add_argument('--connect', action='store_true', help='Connect read-only; never starts motion')
    args = parser.parse_args()
    window = tk.Tk()
    Monitor(window, args.port, args.connect)
    window.mainloop()
