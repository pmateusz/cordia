USE People;
SET TRANSACTION ISOLATION LEVEL READ UNCOMMITTED;
GO

SELECT *
  FROM dbo.ShiftPattern AS shift_pattern
       INNER JOIN dbo.Week AS week
       ON week.ShiftPatternID = shift_pattern.ShiftPatternID
       INNER JOIN dbo.Day AS day
       ON day.WeekID = week.WeekID
       INNER JOIN dbo.Interval AS interval
       ON interval.DayID = day.DayID
       INNER JOIN dbo.IntervalType AS interval_type
       ON interval_type.IntervalTypeID = interval.IntervalTypeID
       LEFT OUTER JOIN dbo.OvertimeShiftPattern AS overtime_shift_pattern
       ON overtime_shift_pattern.ShiftPatternID = shift_pattern.ShiftPatternID
       LEFT JOIN dbo.PositionShiftPattern AS position_shift_pattern
       ON position_shift_pattern.ShiftPatternID = shift_pattern.ShiftPatternID
       LEFT JOIN dbo.TemplateShiftPattern AS template_shift_pattern
       ON template_shift_pattern.ShiftPatternID = shift_pattern.ShiftPatternID
