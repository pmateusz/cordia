-- list carers supporting aom within specified time window

SET TRANSACTION ISOLATION LEVEL READ UNCOMMITTED;
GO

DECLARE @START_DATE DATETIME, @END_DATE DATETIME;
SET @START_DATE = '2017-10-31';
SET @END_DATE = '2017-11-30';

DECLARE @AOM_CODE INT;
SET @AOM_CODE = 12;

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
