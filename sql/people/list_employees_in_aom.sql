-- list carers supporting aom within specified time window

SET TRANSACTION ISOLATION LEVEL READ UNCOMMITTED;
GO

DECLARE @START_TIME DATETIME, @END_TIME DATETIME;
SET @START_TIME = '2017-10-31';
SET @END_TIME = '2017-11-30';

DECLARE @AOM_CODE INT;
SET @AOM_CODE = 1;

SELECT employee_position.EmployeePositionID AS 'employee_position_id',
	employee_position.StartDate AS 'start_time',
	employee_position.EndDate AS 'end_time',
	aom_struct_base.AomBaseID AS 'struct_aom',
	aom_unit_base.AomBaseID AS 'unit_aom'
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
 ORDER BY employee_position_id, start_time
