SET TRANSACTION ISOLATION LEVEL READ UNCOMMITTED;
GO

DECLARE @START_DATE DATETIME, @END_DATE DATETIME;
SET @START_DATE = '2017-09-1';
SET @END_DATE = '2017-10-1';

DECLARE @AOM_CODE INT;
SET @AOM_CODE = 12;

-- table with EmployeePositionID of carers that are employed within the specified time window and support the specified aom
DECLARE @CARERS TABLE(EmployeePositionId INT);

INSERT INTO @CARERS
SELECT employee_position.EmployeePositionID as 'employee_position_id'
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
 WHERE (employee_position.StartDate <= @END_DATE AND (employee_position.EndDate IS NULL OR employee_position.EndDate > @END_DATE))
       AND (aom_struct_base.AomBaseID = @AOM_CODE OR aom_unit_base.AomBaseID = @AOM_CODE)
 ORDER BY employee_position_id

 -- find out exact time windows carers are available for work

 -- get standard schedules
DECLARE @SCHEDULES TABLE (EmployeePositionId INT, SchedulePatternId INT, StartingWeek INT, StartDate DATETIME, EndDate DATETIME)

INSERT INTO @SCHEDULES
SELECT employee_position.EmployeePositionID AS 'employee_position',
	work_schedule_pattern.SchedulePatternID AS 'schedule_pattern',
	work_schedule_pattern.StartingWeek AS 'starting_week',
 (SELECT MAX(v) FROM (VALUES (employee_position.StartDate), (work_schedule_pattern.StartDate), (@START_DATE)) as VALUE(v)) AS 'start_time',
 (SELECT MIN(v) FROM (VALUES (employee_position.EndDate), (work_schedule_pattern.EndDate), (@END_DATE)) AS VALUE(v)) as 'end_date'
  FROM @CARERS AS carers 
       INNER JOIN People.dbo.EmployeePosition AS employee_position
	   ON employee_position.EmployeePositionID = carers.EmployeePositionId
       INNER JOIN People.dbo.WorkSchedule AS work_schedule
       ON employee_position.PositionID = work_schedule.PositionID
       INNER JOIN People.dbo.SchedulePattern AS work_schedule_pattern
       ON work_schedule.SchedulePatternID = work_schedule_pattern.SchedulePatternID
 WHERE (work_schedule_pattern.StartDate <= @END_DATE AND (work_schedule_pattern.EndDate IS NULL OR work_schedule_pattern.EndDate > @START_DATE))
		 AND work_schedule_pattern.Isvalid = 1 AND work_schedule_pattern.IsVerified = 1 AND work_schedule.IsActive = 1

 -- get override schedules
DECLARE @OVERRIDE_SCHEDULES TABLE (EmployeePositionId INT, SchedulePatternId INT, StartingWeek INT, StartDate DATETIME, EndDate DATETIME)

INSERT INTO @OVERRIDE_SCHEDULES
SELECT employee_position.EmployeePositionID AS 'employee_position',
	override_schedule_pattern.SchedulePatternID AS 'schedule_pattern',
	override_schedule_pattern.StartingWeek AS 'starting_week',
 (SELECT MAX(v) FROM (VALUES (employee_position.StartDate), (override_schedule_pattern.StartDate), (@START_DATE)) as VALUE(v)) AS 'start_time',
 (SELECT MIN(v) FROM (VALUES (employee_position.EndDate), (override_schedule_pattern.EndDate), (@END_DATE)) AS VALUE(v)) as 'end_date'
  FROM @CARERS AS carers 
       INNER JOIN People.dbo.EmployeePosition AS employee_position
	   ON employee_position.EmployeePositionID = carers.EmployeePositionId
       INNER JOIN People.dbo.OverrideSchedule AS override_schedule
       ON employee_position.EmployeePositionID = override_schedule.EmployeePositionID
       INNER JOIN People.dbo.SchedulePattern AS override_schedule_pattern
       ON override_schedule.SchedulePatternID = override_schedule_pattern.SchedulePatternID
 WHERE (override_schedule_pattern.StartDate <= @END_DATE AND (override_schedule_pattern.EndDate IS NULL OR override_schedule_pattern.EndDate > @START_DATE))
		 AND override_schedule_pattern.Isvalid = 1 AND override_schedule_pattern.IsVerified = 1 AND override_schedule.IsActive = 1

/*
-- verify how many carers are skipped
SELECT *
  FROM People.dbo.EmployeePosition employee_position
 WHERE employee_position.EmployeePositionID IN (
    SELECT carer.EmployeePositionId
	  FROM @CARERS AS carer
	       LEFT OUTER JOIN @SCHEDULES as schedule
	       ON schedule.EmployeePositionId = carer.EmployeePositionId
	       LEFT OUTER JOIN @OVERRIDE_SCHEDULES as override_schedule
	       ON override_schedule.EmployeePositionId = carer.EmployeePositionId
	 WHERE schedule.SchedulePatternId IS NULL AND override_schedule.SchedulePatternId IS NULL)
*/

/*
-- exclude leave
SELECT *
  FROM @CARERS AS carer
       INNER JOIN People.dbo.Leave leave
       ON leave.EmployeePositionID = carer.EmployeePositionId
       INNER JOIN People.dbo.LtLeaveApprovalType leave_approval
       ON leave.ApprovalStatus = leave_approval.ApprovalTypeID
       INNER JOIN People.dbo.LeaveType AS leave_type
       ON leave.LeaveTypeId = leave_type.LeaveTypeId
WHERE (leave_approval.ApprovalStatus != 'Cancelled' AND leave_approval.ApprovalStatus != 'CancellationPending')
AND leave.StartDate <= @END_DATE AND leave.EndDate > @START_DATE
*/


SELECT *
  FROM People.dbo.SchedulePattern schedule_pattern
      INNER JOIN People.dbo.WeekSlot week_slot
      ON week_slot.SchedulePatternID = schedule_pattern.SchedulePatternID
      INNER JOIN People.dbo.IntervalSlot interval_slot
      ON interval_slot.WeekSlotID = week_slot.WeekSlotID
      LEFT OUTER JOIN People.dbo.IntervalType AS interval_type
      ON interval_slot.Type = interval_type.IntervalTypeID
 WHERE interval_slot.StartTime < @END_DATE
	   AND interval_slot.EndTime > @START_DATE
	   AND schedule_pattern.SchedulePatternID in (
		 SELECT SchedulePatternId
		FROM @SCHEDULES)
ORDER BY schedule_pattern.SchedulePatternId, interval_slot.Day

SELECT *
  FROM People.dbo.SchedulePattern schedule_pattern
      INNER JOIN People.dbo.WeekSlot week_slot
      ON week_slot.SchedulePatternID = schedule_pattern.SchedulePatternID
      INNER JOIN People.dbo.IntervalSlot interval_slot
      ON interval_slot.WeekSlotID = week_slot.WeekSlotID
      LEFT OUTER JOIN People.dbo.IntervalType AS interval_type
      ON interval_slot.Type = interval_type.IntervalTypeID
 WHERE interval_slot.StartTime < @END_DATE
	   AND interval_slot.EndTime > @START_DATE
	   AND schedule_pattern.SchedulePatternID in (
		 SELECT SchedulePatternId
		FROM @SCHEDULES)
ORDER BY schedule_pattern.SchedulePatternId, interval_slot.Day

SELECT *
  FROM @CARERS as carers
  INNER JOIN People.dbo.OvertimeInterval as overtime_interval
  ON overtime_interval.EmployeePositionId = carers.EmployeePositionId
WHERE overtime_interval.StartDateTime < @END_DATE
  AND overtime_interval.EndDateTime > @START_DATE
