USE People;
SET TRANSACTION ISOLATION LEVEL READ UNCOMMITTED;
GO

SELECT schedule_pattern.SchedulePatternId,
       week_slot.WeekSlotId,
       interval_slot.Day, interval_slot.StartTime, interval_slot.EndTime,
       interval_type.Type
  FROM dbo.SchedulePattern AS schedule_pattern
       INNER JOIN dbo.WeekSlot AS week_slot
       ON week_slot.SchedulePatternID = schedule_pattern.SchedulePatternID
       INNER JOIN dbo.IntervalSlot AS interval_slot
       ON interval_slot.WeekSlotId = week_slot.WeekSlotID
       INNER JOIN dbo.IntervalType AS interval_type
       ON interval_type.IntervalTypeID = interval_slot.Type
 ORDER BY schedule_pattern.SchedulePatternID, week_slot.WeekSlotID, interval_slot.Day, interval_slot.StartTime
