-- why only a few carers have shift patterns?

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
       INNER JOIN People.dbo.EmployeePositionShiftPattern employee_position_shift_pattern
       ON employee_position_shift_pattern.EmployeePositionId = employee_position.EmployeePositionId
 WHERE (employee_position.EndDate IS NULL OR employee_position.EndDate > GETDATE())
