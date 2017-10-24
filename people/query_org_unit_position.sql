USE People;
SET TRANSACTION ISOLATION LEVEL READ UNCOMMITTED;
GO

SELECT MIN(org_unit.OrgUnitName) AS org_name,
       position.PositionName AS position_name, COUNT(position.PositionID) as position_count
  FROM dbo.OrgUnit AS org_unit
       INNER JOIN dbo.Position AS position
       ON org_unit.OrgUnitID = position.OrgUnitID
       INNER JOIN dbo.EmployeePosition AS employee_position
       ON employee_position.PositionID = position.PositionID
       INNER JOIN dbo.CarePositions as care_position
       ON position.PositionName = care_position.PositionName
 WHERE employee_position.EndDate IS NULL
       OR GETDATE() < employee_position.EndDate
 GROUP BY org_unit.OrgUnitID, position.PositionName;
