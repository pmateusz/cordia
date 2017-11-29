"""Tests shift pattern"""

import datetime
import unittest

from rows.model.shift_pattern import ExecutableShiftPattern


class ShiftWeekFactoryTest(unittest.TestCase):
    """Tests shift week calculations"""

    def test_one_week_span(self):
        """Test shift week calculations for one week shifts"""

        # given
        reference = datetime.datetime(2017, 1, 29)

        # when
        week_fac = ExecutableShiftPattern.ShiftWeekFactory(reference.date(), 1, 1)

        # then
        self.assertEqual(week_fac(reference + datetime.timedelta(days=-6)), 1)
        self.assertEqual(week_fac(reference + datetime.timedelta(days=-7)), 1)
        self.assertEqual(week_fac(reference + datetime.timedelta(days=-8)), 1)
        self.assertEqual(week_fac(reference + datetime.timedelta(days=-13)), 1)
        self.assertEqual(week_fac(reference + datetime.timedelta(days=-14)), 1)
        self.assertEqual(week_fac(reference + datetime.timedelta(days=-15)), 1)
        self.assertEqual(week_fac(reference + datetime.timedelta(days=6)), 1)
        self.assertEqual(week_fac(reference + datetime.timedelta(days=7)), 1)
        self.assertEqual(week_fac(reference + datetime.timedelta(days=8)), 1)
        self.assertEqual(week_fac(reference + datetime.timedelta(days=13)), 1)
        self.assertEqual(week_fac(reference + datetime.timedelta(days=14)), 1)
        self.assertEqual(week_fac(reference + datetime.timedelta(days=15)), 1)

    def test_two_week_span(self):
        """Test shift week calculations for two week shifts"""

        # given
        reference = datetime.datetime(2017, 1, 29)

        # when
        first_week_fac = ExecutableShiftPattern.ShiftWeekFactory(reference.date(), 1, 2)

        # then
        self.assertEqual(first_week_fac(reference + datetime.timedelta(days=-1)), 2)
        self.assertEqual(first_week_fac(reference + datetime.timedelta(days=-6)), 2)
        self.assertEqual(first_week_fac(reference + datetime.timedelta(days=-7)), 2)
        self.assertEqual(first_week_fac(reference + datetime.timedelta(days=-8)), 1)
        self.assertEqual(first_week_fac(reference + datetime.timedelta(days=-13)), 1)
        self.assertEqual(first_week_fac(reference + datetime.timedelta(days=-14)), 1)
        self.assertEqual(first_week_fac(reference + datetime.timedelta(days=-15)), 2)
        self.assertEqual(first_week_fac(reference + datetime.timedelta(days=-20)), 2)
        self.assertEqual(first_week_fac(reference + datetime.timedelta(days=-21)), 2)
        self.assertEqual(first_week_fac(reference + datetime.timedelta(days=-22)), 1)
        self.assertEqual(first_week_fac(reference + datetime.timedelta(days=6)), 1)
        self.assertEqual(first_week_fac(reference + datetime.timedelta(days=7)), 2)
        self.assertEqual(first_week_fac(reference + datetime.timedelta(days=8)), 2)
        self.assertEqual(first_week_fac(reference + datetime.timedelta(days=13)), 2)
        self.assertEqual(first_week_fac(reference + datetime.timedelta(days=14)), 1)
        self.assertEqual(first_week_fac(reference + datetime.timedelta(days=15)), 1)
        self.assertEqual(first_week_fac(reference + datetime.timedelta(days=20)), 1)
        self.assertEqual(first_week_fac(reference + datetime.timedelta(days=21)), 2)
        self.assertEqual(first_week_fac(reference + datetime.timedelta(days=22)), 2)

        # when
        second_week_fac = ExecutableShiftPattern.ShiftWeekFactory(reference.date(), 2, 2)

        # then
        self.assertEqual(second_week_fac(reference + datetime.timedelta(days=-1)), 1)
        self.assertEqual(second_week_fac(reference + datetime.timedelta(days=-6)), 1)
        self.assertEqual(second_week_fac(reference + datetime.timedelta(days=-7)), 1)
        self.assertEqual(second_week_fac(reference + datetime.timedelta(days=-8)), 2)
        self.assertEqual(second_week_fac(reference + datetime.timedelta(days=-13)), 2)
        self.assertEqual(second_week_fac(reference + datetime.timedelta(days=-14)), 2)
        self.assertEqual(second_week_fac(reference + datetime.timedelta(days=-15)), 1)
        self.assertEqual(second_week_fac(reference + datetime.timedelta(days=-20)), 1)
        self.assertEqual(second_week_fac(reference + datetime.timedelta(days=-21)), 1)
        self.assertEqual(second_week_fac(reference + datetime.timedelta(days=-22)), 2)
        self.assertEqual(second_week_fac(reference + datetime.timedelta(days=6)), 2)
        self.assertEqual(second_week_fac(reference + datetime.timedelta(days=7)), 1)
        self.assertEqual(second_week_fac(reference + datetime.timedelta(days=8)), 1)
        self.assertEqual(second_week_fac(reference + datetime.timedelta(days=13)), 1)
        self.assertEqual(second_week_fac(reference + datetime.timedelta(days=14)), 2)
        self.assertEqual(second_week_fac(reference + datetime.timedelta(days=15)), 2)
        self.assertEqual(second_week_fac(reference + datetime.timedelta(days=20)), 2)
        self.assertEqual(second_week_fac(reference + datetime.timedelta(days=21)), 1)
        self.assertEqual(second_week_fac(reference + datetime.timedelta(days=22)), 1)


if __name__ == '__main__':
    unittest.main()
