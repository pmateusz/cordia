DECLARE @SCHEDULE_PATTERN_ID INT;
SET @SCHEDULE_PATTERN_ID = 20190;

SELECT schedule_pattern.SchedulePatternID, week_slot.WeekNumber, interval_slot.Day, CONVERT(TIME, interval_slot.StartTime) AS 'StartTime', CONVERT(TIME, interval_slot.EndTime) AS 'EndTime', interval_type.Type
  FROM People.dbo.SchedulePattern schedule_pattern
       INNER JOIN People.dbo.WeekSlot week_slot
       ON week_slot.SchedulePatternID = schedule_pattern.SchedulePatternID
       INNER JOIN People.dbo.IntervalSlot interval_slot
       ON interval_slot.WeekSlotID = week_slot.WeekSlotID
       LEFT OUTER JOIN People.dbo.IntervalType AS interval_type
       ON interval_slot.Type = interval_type.IntervalTypeID
 WHERE schedule_pattern.SchedulePatternID = @SCHEDULE_PATTERN_ID
ORDER BY schedule_pattern.SchedulePatternId, interval_slot.Day, interval_slot.StartTime
