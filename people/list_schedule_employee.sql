USE People;
SET TRANSACTION ISOLATION LEVEL READ UNCOMMITTED;
GO

SELECT schedule_pattern.SchedulePatternID AS schedule_id, schedule_pattern.StartDate AS start_date, schedule_pattern.EndDate AS end_date, schedule_pattern.StartingWeek AS starting_week,
       employee_position.EmployeePositionID AS employee_position,
       work_schedule.EndDate AS work_schedule_end_date, override_schedule.EndDate AS override_end_date,
       availability_schedule.EndDate as availability_end_date
  FROM dbo.SchedulePattern AS schedule_pattern
       INNER JOIN dbo.WorkSchedule AS work_schedule
       ON work_schedule.SchedulePatternID = schedule_pattern.SchedulePatternID
       INNER JOIN dbo.EmployeePosition AS employee_position
       ON employee_position.PositionID = work_schedule.PositionID
       INNER JOIN dbo.Position AS position
       ON position.PositionID = employee_position.PositionID
       INNER JOIN dbo.CarePositions AS care_position
       ON position.PositionName = care_position.PositionName
       LEFT OUTER JOIN dbo.OverrideSchedule AS override_schedule
       ON employee_position.EmployeePositionID = override_schedule.EmployeePositionID
       INNER JOIN dbo.ltOverrideReason AS override_reason
       ON override_reason.Reason = override_schedule.Reason
       LEFT OUTER JOIN dbo.AvailabilitySchedule AS availability_schedule
       ON availability_schedule.SchedulePatternID = schedule_pattern.SchedulePatternID
 WHERE (employee_position.EndDate IS NULL OR GETDATE() < employee_position.EndDate)
       AND (schedule_pattern.Isvalid = 1 AND schedule_pattern.IsVerified = 1)
       AND (work_schedule.IsActive = 1 AND work_schedule.EndDate IS NULL OR GETDATE() < work_schedule.EndDate)
       AND (override_schedule.SchedulePatternID IS NULL OR (override_schedule.SchedulePatternID IS NOT NULL AND override_schedule.EndDate IS NULL OR GETDATE() < override_schedule.EndDate))
