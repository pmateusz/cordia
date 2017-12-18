SET TRANSACTION ISOLATION LEVEL READ UNCOMMITTED;
GO

DECLARE @START_TIME DATETIME, @END_TIME DATETIME;
SET @START_TIME = '2017-09-1';
SET @END_TIME = '2017-10-1';

DECLARE @AOM_ID INT;
SET @AOM_ID = 637;

DECLARE @CARERS TABLE(CarerId INT, EmployeeId INT);

INSERT INTO @CARERS
SELECT carer_view.CarerId AS 'carer_id', carer_view.EmployeeId AS 'employee_id'
  FROM SparkCare.dbo.CarerView carer_view
 WHERE carer_view.HiredDate < @END_TIME
       AND (COALESCE(carer_view.TerminatedDate, @END_TIME) > @START_TIME)
       AND (carer_view.AomId = @AOM_ID)
 ORDER BY carer_id, employee_id;

DECLARE @TEMP_SCHEDULES TABLE (
	EmployeePositionId INT,
	SchedulePatternId INT,
	StartDate DATETIME,
	EndDate DATETIME,
	WorkWeek INT,
	Overtime INT,
	SchedulePatternId_Override INT,
	StartDate_Override DATETIME,
	EndDate_Override DATETIME,
	WorkWeek_Override INT);

INSERT INTO @TEMP_SCHEDULES
SELECT employee_position.EmployeePositionID AS 'employee_position',
	work_schedule_pattern.SchedulePatternID AS 'schedule_pattern',
	work_schedule_pattern.StartDate AS 'start_date',
	work_schedule_pattern.EndDate AS 'end_date',
	position.ContractedHours AS 'work_week',
	position.ContractedOvertimeHours AS 'work_week_overtime',
	override_schedule_pattern.SchedulePatternID AS 'schedule_pattern_override',
	override_schedule_pattern.StartDate AS 'start_date_override',
	override_schedule_pattern.EndDate AS 'end_date_override',
	override_schedule.OverrideHours AS 'work_week_overtime'
  FROM @CARERS AS carers 
       INNER JOIN People.dbo.EmployeePosition AS employee_position
	   ON employee_position.EmployeeID = carers.EmployeeId
	   INNER JOIN People.dbo.Position as position
	   ON employee_position.PositionID = position.PositionID
       LEFT OUTER JOIN People.dbo.WorkSchedule AS work_schedule
       ON employee_position.PositionID = work_schedule.PositionID
       LEFT OUTER JOIN People.dbo.SchedulePattern AS work_schedule_pattern
       ON work_schedule.SchedulePatternID = work_schedule_pattern.SchedulePatternID
	   LEFT OUTER JOIN People.dbo.OverrideSchedule AS override_schedule
	   ON employee_position.EmployeePositionID = override_schedule.EmployeePositionID
	   LEFT OUTER JOIN People.dbo.SchedulePattern AS override_schedule_pattern
       ON override_schedule.SchedulePatternID = override_schedule_pattern.SchedulePatternID
 WHERE work_schedule_pattern.SchedulePatternID IS NULL OR
  ((work_schedule_pattern.StartDate <= @END_TIME AND (work_schedule_pattern.EndDate IS NULL OR work_schedule_pattern.EndDate > @START_TIME))
	   AND work_schedule_pattern.Isvalid = 1 AND work_schedule_pattern.IsVerified = 1 /* AND work_schedule.IsActive = 1 -- NOTE we are interested in any schedule */)
	   AND override_schedule_pattern.SchedulePatternID IS NULL OR (
			work_schedule_pattern.StartDate <= override_schedule_pattern.StartDate
			AND (work_schedule_pattern.EndDate IS NULL OR override_schedule_pattern.StartDate <= work_schedule_pattern.EndDate)
			AND COALESCE(override_schedule_pattern.StartDate, @END_TIME) <= @END_TIME
			AND COALESCE(override_schedule_pattern.EndDate, @END_TIME) > @START_TIME
			AND (override_schedule_pattern.Isvalid = 1 AND override_schedule_pattern.IsVerified = 1 /* AND override_schedule_pattern.IsActive */));

DECLARE @SCHEDULES TABLE (
	EmployeePositionId INT,
	SchedulePatternId INT,
	WorkWeek INT,
	Overtime INT,
	StartDate DATETIME,
	EndDate DATETIME);

-- insert schedules that do not have overrides
-- insert schedules that precede their overrides
INSERT INTO @SCHEDULES
SELECT EmployeePositionId, SchedulePatternId, WorkWeek, Overtime,
	CASE
		WHEN StartDate < @START_TIME THEN
			@START_TIME
		ELSE
			StartDate
	END AS 'StartTime',
	CASE
		WHEN EndDate IS NULL OR EndDate > @END_TIME THEN
			@END_TIME
		ELSE
			EndDate
	END AS 'EndTime'
FROM @TEMP_SCHEDULES
WHERE SchedulePatternId_Override IS NULL OR (
	SchedulePatternId_Override IS NOT NULL
	AND StartDate <= StartDate_Override
	AND COALESCE(EndDate, StartDate_Override) < COALESCE(EndDate_Override, @END_TIME));

---- insert overrides
INSERT INTO @SCHEDULES
SELECT EmployeePositionId, SchedulePatternId_Override, WorkWeek_Override, Overtime,
	CASE
		WHEN StartDate_Override < @START_TIME THEN
			@START_TIME
		ELSE
			StartDate_Override
	END AS 'StartTime',
	CASE
		WHEN EndDate_Override IS NULL OR EndDate_Override > @END_TIME THEN
			@END_TIME
		ELSE
			EndDate_Override
	END AS 'EndTime'
FROM @TEMP_SCHEDULES
WHERE SchedulePatternId_Override IS NOT NULL AND StartDate_Override < COALESCE(EndDate_Override, @END_TIME);

---- insert schedule following override if necessary
INSERT INTO @SCHEDULES
SELECT EmployeePositionId, SchedulePatternId, WorkWeek, Overtime, COALESCE(EndDate_Override, @END_TIME) AS 'StartTime', @END_TIME as 'EndTime'
FROM @TEMP_SCHEDULES AS OUTER_SCHEDULES
WHERE (
	-- should the overriden schedule be padded to fill the missing time space?
	SELECT TOP(1) LOCAL_SCHEDULES.StartDate
		 FROM @TEMP_SCHEDULES AS LOCAL_SCHEDULES
		WHERE LOCAL_SCHEDULES.EmployeePositionId = OUTER_SCHEDULES.EmployeePositionId
			  AND OUTER_SCHEDULES.StartDate_Override IS NOT NULL
			  AND LOCAL_SCHEDULES.StartDate <= COALESCE(OUTER_SCHEDULES.EndDate_Override, @END_TIME)
			  AND COALESCE(LOCAL_SCHEDULES.EndDate, @END_TIME) <= @END_TIME
		 ORDER BY LOCAL_SCHEDULES.StartDate) > COALESCE(OUTER_SCHEDULES.EndDate_Override, @END_TIME);

DELETE FROM @SCHEDULES
WHERE DATEDIFF(minute, StartDate, EndDate) <= 0;

DELETE FROM @TEMP_SCHEDULES;

SELECT *
FROM @SCHEDULES
ORDER BY EmployeePositionId, StartDate;
