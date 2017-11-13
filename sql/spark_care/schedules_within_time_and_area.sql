SET TRANSACTION ISOLATION LEVEL READ UNCOMMITTED;
GO

DECLARE @START_TIME DATETIME, @END_TIME DATETIME;
SET @START_TIME = '2017-09-1';
SET @END_TIME = '2017-10-1';

DECLARE @AOM_CODE INT;
SET @AOM_CODE = 4;

-- table with EmployeePositionID of carers that are employed within the specified time window who supported the specified aom
DECLARE @CARERS TABLE(EmployeePositionId INT);

INSERT INTO @CARERS
SELECT employee_position.EmployeePositionID AS 'employee_position_id'
  FROM People.dbo.EmployeePosition AS employee_position
       INNER JOIN People.dbo.Position AS position
       ON employee_position.PositionID = position.PositionID
       INNER JOIN People.dbo.CarePositions AS care_position
       ON position.PositionName = care_position.PositionName
       INNER JOIN SparkCare.dbo.AomOpStruct AS aom_struct
       ON position.OpStructID = aom_struct.OpStructID
       INNER JOIN SparkCare.dbo.AomOrgUnit as aom_unit
       ON position.OrgUnitID = aom_unit.OrgUnitId
       INNER JOIN SparkCare.dbo.AomBase AS aom_struct_base
       ON aom_struct_base.AomID = aom_struct.AomId
       INNER JOIN SparkCare.dbo.AomBase AS aom_unit_base
       ON aom_unit_base.AomID = aom_unit.AomID
 WHERE employee_position.StartDate < @END_TIME
       AND (employee_position.EndDate IS NULL or employee_position.EndDate > @START_TIME)
	   AND (aom_unit_base.AomBaseID = @AOM_CODE OR aom_struct_base.AomBaseID = @AOM_CODE)
 ORDER BY employee_position_id

 DECLARE @TEMP_SCHEDULES TABLE (EmployeePositionId INT, SchedulePatternId INT, StartDate DATETIME, EndDate DATETIME, SchedulePatternId_Override INT, StartDate_Override DATETIME, EndTime_Override DATETIME)
 --INSERT INTO @TEMP_SCHEDULES
 SELECT employee_position.EmployeePositionID AS 'employee_position',
	work_schedule_pattern.SchedulePatternID AS 'schedule_pattern',
	work_schedule_pattern.StartDate,
	work_schedule_pattern.EndDate,
	override_schedule_pattern.SchedulePatternID AS 'override_schedule_pattern',
	override_schedule_pattern.StartDate,
	override_schedule_pattern.EndDate
  FROM @CARERS AS carers 
       INNER JOIN People.dbo.EmployeePosition AS employee_position
	   ON employee_position.EmployeePositionID = carers.EmployeePositionId
       INNER JOIN People.dbo.WorkSchedule AS work_schedule
       ON employee_position.PositionID = work_schedule.PositionID
       INNER JOIN People.dbo.SchedulePattern AS work_schedule_pattern
       ON work_schedule.SchedulePatternID = work_schedule_pattern.SchedulePatternID
	   LEFT OUTER JOIN People.dbo.OverrideSchedule AS override_schedule
	   ON employee_position.EmployeePositionID = override_schedule.EmployeePositionID
	   LEFT OUTER JOIN People.dbo.SchedulePattern AS override_schedule_pattern
       ON override_schedule.SchedulePatternID = override_schedule_pattern.SchedulePatternID
 WHERE (work_schedule_pattern.StartDate <= @END_TIME AND (work_schedule_pattern.EndDate IS NULL OR work_schedule_pattern.EndDate > @START_TIME))
	   AND work_schedule_pattern.Isvalid = 1 AND work_schedule_pattern.IsVerified = 1
	   -- AND work_schedule.IsActive = 1 -- NOTE we are interested in any schedule
	   AND override_schedule_pattern.SchedulePatternID IS NULL OR (
			work_schedule_pattern.StartDate <= override_schedule_pattern.StartDate
			--AND override_schedule_pattern.StartDate < work_schedule_pattern.EndDate
			AND override_schedule_pattern.StartDate <= @END_TIME
			AND (override_schedule_pattern.EndDate IS NULL OR override_schedule_pattern.EndDate > @START_TIME)
			AND (override_schedule_pattern.Isvalid = 1 AND override_schedule_pattern.IsVerified = 1)
			-- AND override_schedule_pattern.IsActive
		)

-- there are 4 scenarios how schedule patterns can may overlap:
-- a) equal -> replace reference schedule id
-- b) a full inclusion -> split a time interval into 3 periods A-B-A
-- c) no relation -> do nothing
-- d) share common beginning -> split a time interval into 2 periods B-A
-- e) share common end -> split a time interval into 2 periods A-B

SELECT *
FROM @TEMP_SCHEDULES

DECLARE override_schedule_cursor CURSOR READ_ONLY FORWARD_ONLY FOR
SELECT employee_position.EmployeePositionID AS 'employee_position',
	override_schedule_pattern.SchedulePatternID AS 'schedule_pattern',
 (SELECT MAX(v) FROM (VALUES (employee_position.StartDate), (override_schedule_pattern.StartDate), (@START_TIME)) as VALUE(v)) AS 'start_time',
 (SELECT MIN(v) FROM (VALUES (employee_position.EndDate), (override_schedule_pattern.EndDate), (@END_TIME)) AS VALUE(v)) AS 'end_date'
  FROM @CARERS AS carers 
       INNER JOIN People.dbo.EmployeePosition AS employee_position
	   ON employee_position.EmployeePositionID = carers.EmployeePositionId
       INNER JOIN People.dbo.OverrideSchedule AS override_schedule
       ON employee_position.EmployeePositionID = override_schedule.EmployeePositionID
       INNER JOIN People.dbo.SchedulePattern AS override_schedule_pattern
       ON override_schedule.SchedulePatternID = override_schedule_pattern.SchedulePatternID
 WHERE (override_schedule_pattern.StartDate <= @END_TIME AND (override_schedule_pattern.EndDate IS NULL OR override_schedule_pattern.EndDate > @START_TIME))
	   AND override_schedule_pattern.Isvalid = 1 AND override_schedule_pattern.IsVerified = 1
	   -- AND override_schedule.IsActive = 1

OPEN override_schedule_cursor



CLOSE override_schedule_cursor
DEALLOCATE override_schedule_cursor
 /*
 -- find out exact time windows carers are available for work

 -- get standard schedules
DECLARE @SCHEDULES TABLE (EmployeePositionId INT, SchedulePatternId INT, StartingWeek INT, StartDate DATETIME, EndDate DATETIME)

 -- get override schedules
DECLARE @OVERRIDE_SCHEDULES TABLE (EmployeePositionId INT, SchedulePatternId INT, StartingWeek INT, StartDate DATETIME, EndDate DATETIME)



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
  */
