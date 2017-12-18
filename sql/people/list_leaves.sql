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

SELECT carer.EmployeeId, carer.CarerId, leave.EmployeePositionId, leave.StartDate, leave.EndDate, leave.HalfDay, leave.HalfDayPeriod, leave_approval.ApprovalStatus
  FROM @CARERS AS carer
	   INNER JOIN People.dbo.EmployeePosition employee_position
	   ON carer.EmployeeId = employee_position.EmployeeID
       INNER JOIN People.dbo.Leave leave
       ON leave.EmployeePositionID = employee_position.EmployeePositionID
       INNER JOIN People.dbo.LtLeaveApprovalType leave_approval
       ON leave.ApprovalStatus = leave_approval.ApprovalTypeID
       INNER JOIN People.dbo.LeaveType AS leave_type
       ON leave.LeaveTypeId = leave_type.LeaveTypeId
 WHERE leave_approval.ApprovalStatus NOT IN ('Cancelled', 'CancellationPending', 'Rejected')
       AND leave.StartDate <= @END_TIME
       AND leave.EndDate > @START_TIME
