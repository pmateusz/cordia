-- some carers belong to more than one AOM

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
 WHERE aom_unit.AomId != aom_struct.AomID
       AND (employee_position.EndDate IS NULL OR employee_position.EndDate > GETDATE())
