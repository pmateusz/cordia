SET TRANSACTION ISOLATION LEVEL READ UNCOMMITTED;
GO

DECLARE @START_TIME DATETIME, @END_TIME DATETIME;
SET @START_TIME = '2017-09-1';
SET @END_TIME = '2017-10-1';

DECLARE @AOM_CODE INT;
SET @AOM_CODE = 1;

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
       AND (COALESCE(employee_position.EndDate, @END_TIME) > @START_TIME)
       AND (aom_unit_base.AomBaseID = @AOM_CODE OR aom_struct_base.AomBaseID = @AOM_CODE)
 ORDER BY employee_position_id;

SELECT leave.EmployeePositionId, leave.StartDate, leave.EndDate, leave.HalfDay, leave.HalfDayPeriod, leave_approval.ApprovalStatus
  FROM @CARERS AS carer
       INNER JOIN People.dbo.Leave leave
       ON leave.EmployeePositionID = carer.EmployeePositionId
       INNER JOIN People.dbo.LtLeaveApprovalType leave_approval
       ON leave.ApprovalStatus = leave_approval.ApprovalTypeID
       INNER JOIN People.dbo.LeaveType AS leave_type
       ON leave.LeaveTypeId = leave_type.LeaveTypeId
 WHERE leave_approval.ApprovalStatus NOT IN ('Cancelled', 'CancellationPending', 'Rejected')
       AND leave.StartDate <= @END_TIME
       AND leave.EndDate > @START_TIME
