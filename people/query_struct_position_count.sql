USE People;
SET TRANSACTION ISOLATION LEVEL READ UNCOMMITTED;
GO

SELECT MIN(op_struct.OpStructName) AS struct_name,
       position.PositionName AS position_name, COUNT(position.PositionID) as position_count
  FROM dbo.OpStruct AS op_struct
       INNER JOIN dbo.Position AS position
       ON op_struct.OpStructID = position.OpStructID
       INNER JOIN dbo.EmployeePosition AS employee_position
       ON employee_position.PositionID = position.PositionID
       INNER JOIN dbo.CarePositions as care_position
       ON position.PositionName = care_position.PositionName
 WHERE employee_position.EndDate IS NULL
       OR GETDATE() < employee_position.EndDate
 GROUP BY op_struct.OpStructID, position.PositionName
