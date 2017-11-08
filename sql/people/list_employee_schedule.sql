SET TRANSACTION ISOLATION LEVEL READ UNCOMMITTED;

SELECT *
  FROM People.dbo.EmployeePosition AS employee_position
       INNER JOIN People.dbo.Position AS position
       ON employee_position.PositionID = position.PositionID
       INNER JOIN People.dbo.CarePositions AS care_position
       ON position.PositionName = care_position.PositionName
       LEFT OUTER JOIN SparkCare.dbo.AomOpStruct AS aom_struct
       ON position.OpStructID = aom_struct.OpStructID
       LEFT OUTER JOIN SparkCare.dbo.AomOrgUnit as aom_unit
       ON position.OrgUnitID = aom_unit.OrgUnitId
       INNER JOIN SparkCare.dbo.AomBase AS aom_struct_base
       ON aom_struct_base.AomID = aom_struct.AomId
       INNER JOIN SparkCare.dbo.AomBase AS aom_unit_base
       ON aom_unit_base.AomID = aom_unit.AomID
	LEFT OUTER JOIN People.dbo.WorkSchedule AS work_schedule
       ON employee_position.PositionID = work_schedule.PositionID
	LEFT OUTER JOIN dbo.SchedulePattern AS work_schedule_pattern
       ON work_schedule.SchedulePatternID = work_schedule_pattern.SchedulePatternID
	LEFT OUTER JOIN dbo.OverrideSchedule AS override_schedule
	ON employee_position.EmployeePositionID = override_schedule.EmployeePositionID
	LEFT OUTER JOIN dbo.SchedulePattern AS override_schedule_pattern
	ON override_schedule.SchedulePatternID = override_schedule_pattern.SchedulePatternID
	LEFT OUTER JOIN dbo.ltOverrideReason AS override_reason
       ON override_reason.Reason = override_schedule.Reason
 WHERE (employee_position.EndDate IS NULL OR employee_position.EndDate > GETDATE())
		AND ((work_schedule.IsActive = 1 AND (work_schedule.EndDate IS NULL OR work_schedule.EndDate > GETDATE()) AND work_schedule_pattern.Isvalid = 1 AND work_schedule_pattern.IsVerified = 1)
	       OR (override_schedule.IsActive = 1 AND (override_schedule.EndDate IS NULL OR override_schedule.EndDate > GETDATE()) AND override_schedule_pattern.Isvalid = 1 AND override_schedule_pattern.IsVerified = 1))
