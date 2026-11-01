import math
from pathlib import Path
import unittest
from protocol import parse_status, parse_config, MoveMeasurement, Endpoint, repeatability

def line(count=1021, target=1024, reached=1, active=0):
    return (f'msh >axis1: en=1 run={active} active={active} reached={reached} fault=none '
            f'count={count} target={target} err={target-count} speed=0Hz remain=0 '
            f'pulses=3968 enc_angle=89.74 target=90.00')

class ProtocolTests(unittest.TestCase):
    def test_duplicate_target_fields_use_integer_counts(self):
        s = parse_status(line(), 1)
        self.assertEqual(s.target, 1024)
        self.assertAlmostEqual(s.degrees(4096)[0], 89.736328125)
        self.assertAlmostEqual(s.degrees(4096)[2], 0.263671875)

    def test_reject_incomplete_and_inconsistent_status(self):
        self.assertIsNone(parse_status(line().split('pulses')[0], 1))
        self.assertIsNone(parse_status(line().replace('err=3', 'err=9'), 1))
        self.assertIsNone(parse_config('CPR=0 microstep=16 max=200Hz'))

    def test_ack_required_and_completion_counted_once(self):
        move = MoveMeasurement('stepper goto 90', 7, 0, 1, 4096)
        self.assertIsNone(move.observe(parse_status(line(), 1.1), None))
        self.assertEqual(move.errors, [])
        self.assertIsNone(move.observe(parse_status(line(0, reached=0, active=1), 1.2), 7))
        e = move.observe(parse_status(line(), 3), None)
        self.assertEqual(e.direction, 1)
        self.assertEqual(e.target_count, 1024)
        self.assertAlmostEqual(e.elapsed, 2)
        self.assertIsNone(move.observe(parse_status(line(), 4), None))
        self.assertEqual(len(move.errors), 2)
        self.assertAlmostEqual(move.rms, math.sqrt((90**2 + (3*360/4096)**2)/2))

    def test_already_at_target_completion(self):
        move = MoveMeasurement('stepper goto 0', 2, 0, 0, 4096)
        e = move.observe(parse_status(line(0, 0), 0.1), 2)
        self.assertEqual(e.direction, 0)
        self.assertEqual(repeatability([e, e]), [])

    def test_repeatability_separates_direction_target_and_cpr(self):
        def endpoint(count, direction=1, cpr=4096, target=1024):
            return Endpoint('stepper goto 90', target, count, target-count,
                            (target-count)*360/cpr, target*360/cpr, count*360/cpr,
                            direction, 2, cpr)
        rows = repeatability([endpoint(1021), endpoint(1023), endpoint(1026, -1),
                              endpoint(1024, cpr=4000), endpoint(511, target=512)])
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]['n'], 2)
        self.assertAlmostEqual(rows[0]['range_deg'], 2*360/4096)
        self.assertAlmostEqual(rows[0]['sample_std_deg'], math.sqrt(2)*360/4096)

    def test_parse_actual_recording(self):
        evidence = Path(__file__).resolve().parents[2] / 'output/evidence/video-demo-recording.log'
        if not evidence.exists():
            self.skipTest('Hardware log is not distributed in the source tree')
        samples = [parse_status(s, i) for i, s in enumerate(evidence.read_text(encoding='utf-8').splitlines())]
        samples = [s for s in samples if s]
        self.assertGreater(len(samples), 15)
        self.assertTrue(any(s.target == 1024 and s.count == 1021 and s.done for s in samples))
        self.assertEqual(samples[-1].en, 0)

if __name__ == '__main__':
    unittest.main(verbosity=2)
