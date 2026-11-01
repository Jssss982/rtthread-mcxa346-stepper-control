"""GUI model/export integration, with no serial port and no motor motion."""
import csv
import json
from pathlib import Path
import tempfile
import time
import tkinter as tk
import unittest
import app
from protocol import MoveMeasurement, parse_status
from test_protocol import line

class AppTests(unittest.TestCase):
    def test_measurement_table_and_export(self):
        window = tk.Tk(); window.withdraw()
        old_exports = app.EXPORTS
        try:
            monitor = app.Monitor(window)
            monitor.cpr = 4096
            monitor.connected = True
            t = time.monotonic()
            monitor.move = MoveMeasurement('stepper goto 90', 7, 0, t, 4096)
            monitor.last_move = monitor.move
            monitor.handle('sample', (parse_status(line(0, reached=0, active=1), t+.1), 7))
            monitor.handle('sample', (parse_status(line(), t+2), None))
            monitor.handle('sample', (parse_status(line(), t+2.3), None))
            self.assertEqual(len(monitor.endpoints), 1)
            self.assertEqual(len(monitor.table.get_children()), 1)
            self.assertEqual(monitor.metrics['end'].get(), '+0.264°')
            with tempfile.TemporaryDirectory() as folder:
                app.EXPORTS = Path(folder)
                monitor.export()
                with next(Path(folder).glob('*_samples.csv')).open(encoding='utf-8-sig') as f:
                    rows = list(csv.DictReader(f))
                self.assertEqual(len(rows), 3)
                self.assertEqual(rows[-1]['target_count'], '1024')
                self.assertAlmostEqual(float(rows[-1]['error_deg']), 0.263671875)
                with next(Path(folder).glob('*_endpoints.csv')).open(encoding='utf-8-sig') as f:
                    endpoints = list(csv.DictReader(f))
                self.assertEqual(len(endpoints), 1)
                report = json.loads(next(Path(folder).glob('*_summary.json')).read_text(encoding='utf-8'))
                self.assertEqual(report['endpoint_count'], 1)
            monitor.clear()
            self.assertEqual(len(monitor.samples), 0)
            self.assertEqual(len(monitor.endpoints), 0)
        finally:
            app.EXPORTS = old_exports
            window.destroy()

if __name__ == '__main__':
    unittest.main(verbosity=2)
